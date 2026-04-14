#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <netinet/in.h>
#include <netinet/ip.h> /* Surensemble des précédents */
#include <string.h>
#include <sys/socket.h>

// --------------------------------------------------
// DEFINE ZONE
// --------------------------------------------------
#define SOCK_ERR (-1)
#define POLLING_ERROR (-1)
#define PORT 2202
#define BACKLOG 512
#define MAX_PKT_HEADER_SIZE (64)

#define PKT_PEER_MASK (0x3FFF)

enum flag {
  MESSAGE_FLAG_ACKNOWLEDGE = (1 << 7),
  MESSAGE_FLAG_UNSEQUENCED = (1 << 6),
  MESSAGE_FLAG_MASK = MESSAGE_FLAG_ACKNOWLEDGE | MESSAGE_FLAG_UNSEQUENCED,

  HEADER_FLAG_COMPRESSED = (1 << 6), // si commpressé
  HEADER_FLAG_SENT_TIME = (1 << 7),  // Si on envoit le timestamp
  HEADER_FLAG_OFFSET = (8),
  HEADER_FLAG_MASK = (HEADER_FLAG_COMPRESSED | HEADER_FLAG_SENT_TIME)
                     << HEADER_FLAG_OFFSET,

};

enum message_command {
  ACKNOWLEDGE = 0x01,
  CONNECT = 0x02,
  VERIFY_CONNECT = 0x03,
  DISCONNECT = 0x04,
  PING = 0x05,
  SEND_RELIABLE = 0x06,
  SEND_UNRELIABLE = 0x07,
  SEND_FRAGMENT = 0x08,
  SEND_UNSEQUENCED = 0x09,
  BANDWIDTH_LIMIT = 0x0A,
  THROTTLE_CONFIGURE = 0x0B,
  SEND_UNRELIABLE_FRAGMENT = 0x0C,
};

#define da_append(da, item)                                                    \
  do {                                                                         \
    if (da.count >= da.capacity) {                                             \
      if (da.capacity == 0)                                                    \
        da.capacity = 256;                                                     \
      else                                                                     \
        da.capacity *= 2;                                                      \
      da.items = realloc(da.items, da.capacity * sizeof(*da.items));           \
    }                                                                          \
    da.items[da.count++] = item;                                               \
  } while (0)

// --------------------------------------------------
// Structure
// --------------------------------------------------

struct client {
  int fd;
};

// -- Dynamic arrays

struct da_client {
  struct client *items;
  size_t count;
  size_t capacity;
};

struct packet_header {
  uint16_t peer_id;
  uint16_t commandCount;
  uint8_t flags;
  uint8_t sentTime[6];
};

struct message {
  struct packet_header packet_header;
  uint8_t command;
  uint8_t channel_id;
  uint16_t seq_number;
  uint8_t flags;
  char isDisable;

  // --

  struct {
    uint8_t ReceivedSentTime[6];
    uint32_t data_length;
    uint32_t FragmentCount;
    uint32_t FragmentNumber;
    uint32_t TotalLength;
    uint32_t FragmentOffset;

    uint8_t *payload;
  };
};

struct da_message {
  struct message *items;
  size_t count;
  size_t capacity;
};

// --------------------------------------------------
// Global variable
// --------------------------------------------------

struct da_message incMessageToHandle;

struct da_message outMessageQueueToHandle;

// struct da_xxx waitingAck;

// --------------------------------------------------
// Function
// --------------------------------------------------

size_t parsePacketHeader(unsigned char *buff, struct packet_header *ph) {

  size_t offset_readed = 0;

  uint16_t *peer_and_flags_networked = (uint16_t *)(buff + offset_readed);
  offset_readed += sizeof(uint16_t);

  ph->flags =
      (*peer_and_flags_networked & HEADER_FLAG_MASK) >> HEADER_FLAG_OFFSET;

  ph->peer_id = ntohs(*peer_and_flags_networked & (HEADER_FLAG_MASK ^ 0xFFFF));

  uint16_t *command_count_networked = (uint16_t *)(buff + offset_readed);
  offset_readed += sizeof(uint16_t);

  ph->commandCount = ntohs(*command_count_networked);

  if (ph->flags & HEADER_FLAG_SENT_TIME) {
    const uint8_t timestamp_size = 6;
    memcpy(ph->sentTime, buff + offset_readed, timestamp_size);
    offset_readed += timestamp_size;
  }

  return offset_readed;
}

size_t parseMessageHeader(unsigned char *buff, struct message *msg) {
  size_t offset_readed = 0;

  // -- command & flags
  uint8_t *command_and_flags = (uint8_t *)(buff + offset_readed);
  offset_readed += sizeof(uint8_t);

  msg->flags = (*command_and_flags & MESSAGE_FLAG_MASK);
  msg->command = (*command_and_flags & (MESSAGE_FLAG_MASK ^ 0xFFFF));

  // -- channel_id
  uint8_t *channel_id = (uint8_t *)(buff + offset_readed);
  offset_readed += sizeof(uint8_t);

  msg->channel_id = (*channel_id);

  // -- seq_number
  uint16_t *seq_number_networked = (uint16_t *)(buff + offset_readed);
  offset_readed += sizeof(uint16_t);

  msg->seq_number = ntohs(*seq_number_networked);

  switch (msg->command) {

  case ACKNOWLEDGE:
  case CONNECT:
  case VERIFY_CONNECT:
  case DISCONNECT:
  case PING:
  case SEND_RELIABLE:
  case SEND_UNRELIABLE:
  case SEND_FRAGMENT:
  case SEND_UNSEQUENCED:
  case SEND_UNRELIABLE_FRAGMENT:
    break;

  default:
    break;
  };

  return offset_readed;
}

/// @brief read recieved packet and enqueue incoming packet into
/// 'incMessageToHandle'
int handleIncommingPacket(int fd) {
  printf("message reçu from fd : '%d'\n", fd);

  unsigned char header_buff_peek[MAX_PKT_HEADER_SIZE] = {0};
  recv(fd, header_buff_peek, MAX_PKT_HEADER_SIZE, MSG_PEEK);

  struct packet_header packet_header = {0};

  printf("parsing packet header...\n");
  size_t header_size = parsePacketHeader(header_buff_peek, &packet_header);

  printf("cmd_cnt = %d, flags = %d, peer_id = %d \n",
         packet_header.commandCount, packet_header.flags,
         packet_header.peer_id);
  // sentTime :(

  unsigned char header_buff_consume[header_size];
  recv(fd, header_buff_consume, header_size, 0);

  unsigned char payload_buff[2048];

  if (packet_header.commandCount == 0) {
    // Pas de message à traiter
    // Header avec un payload vide

    // Flushing the payload if exist
    while (recv(fd, payload_buff, sizeof(payload_buff), MSG_DONTWAIT) > 0)
      ;
    return 0;
  }

  size_t payload_len_recv = recv(fd, payload_buff, 2048, 0);

  if (payload_len_recv == 2048) {
    fprintf(stderr, "j'crois j'ai merdé sur le payload\n");
    return -1;
  }

  printf("enque-ing message\n");

  for (int i = 0; i < packet_header.commandCount; i++) {
    struct message msg = {0};

    parseMessageHeader(payload_buff, &msg);

    switch (msg.command) {
      switch (msg) {}
    }
  }
}

void checkIncommingPacket(struct da_client *da) {
  for (int i = 0; i < da->count; i++) {

    if (da->items[i].fd == -1) {
      continue;
    }

    char buff_peek = {0};
    size_t n = recv(da->items[i].fd, &buff_peek, 1, MSG_PEEK);

    if (n == SOCK_ERR) {
      if (errno != EWOULDBLOCK)
        perror("error while recv-ing socket");

      continue;
    }

    if (n == 0) {
      printf("connexion close from client, fd : '%d'\n", da->items[i].fd);
      da->items[i].fd = -1;
      continue;
    }

    handleIncommingPacket(da->items[i].fd);
  }
}

int main(int argc, char **argv) {

  int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (fd == SOCK_ERR) {
    perror("impossible to create socket");
    return SOCK_ERR;
  }

  setsockopt(fd, SOL_SOCKET, SOL_SOCKET, &((int){1}), sizeof(int));

  const struct sockaddr_in addr = {
      .sin_family = AF_INET,         /* Famille d'adresses : AF_INET */
      .sin_port = htons(2202),       /* Port dans l'ordre des octets réseau */
      .sin_addr.s_addr = INADDR_ANY, /* Adresse Internet */
  };

  if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) == SOCK_ERR) {
    perror("impossible to bind socket");
    return SOCK_ERR;
  }

  if (listen(fd, BACKLOG) == SOCK_ERR) {
    perror("impossible to listen the socket");
    return SOCK_ERR;
  }

  struct da_client da_client = {0};

  printf("listening on port '%d'\n", PORT);

  while (1) {

    if (incMessageToHandle.count >= 1) {
      // TODO Envoyer un message de `incMessageToHandle`
    }

    // -----------------------
    // Traitement nouveau client
    // -----------------------
    int client_fd = accept(fd, NULL, NULL);

    if (client_fd == SOCK_ERR) {
      if (errno != EWOULDBLOCK)
        // If this is not a EWOULDBLOCK, this is a real error
        perror("error while accepting new client");
    } else {
      // else, we append the fd to the dynamic array to poll it
      da_append(da_client, (struct client){.fd = client_fd});
    }

    // -----------------------
    // Traitement message reçu
    // -----------------------
    checkIncommingPacket(&da_client);
  }

  return 0;
}
