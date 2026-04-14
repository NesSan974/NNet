#include <asm-generic/errno.h>
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

  PACKET_FLAG_COMPRESSED = (1 << 6), // si commpressé
  PACKET_FLAG_SENT_TIME = (1 << 7),  // Si on envoit le timestamp
  PACKET_FLAG_OFFSET = (8),
  PACKET_FLAG_MASK = (PACKET_FLAG_COMPRESSED | PACKET_FLAG_SENT_TIME)
                     << PACKET_FLAG_OFFSET,

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
  uint16_t PeerID;
  uint16_t CommandCount;
  uint8_t flags;
  uint8_t SentTime[6];
};

struct message {
  struct packet_header packet_header;
  uint8_t Command;
  uint8_t ChannelID;
  union {
    uint16_t seq_number;
    uint16_t UnreliableSeqNumber;
    uint16_t ReliableSeqNumber;
  };
  uint8_t flags;
  char isDisable;

  // --

  uint16_t StartSeq;

  uint32_t DataLength;
  uint32_t FragmentCount;
  uint32_t FragmentNumber;
  uint32_t TotalLength;
  uint32_t FragmentOffset;
  uint8_t *payload;

  uint8_t ReceivedSentTime[6];
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
      (*peer_and_flags_networked & PACKET_FLAG_MASK) >> PACKET_FLAG_OFFSET;

  ph->PeerID = ntohs(*peer_and_flags_networked & (PACKET_FLAG_MASK ^ 0xFFFF));

  uint16_t *command_count_networked = (uint16_t *)(buff + offset_readed);
  offset_readed += sizeof(uint16_t);

  ph->CommandCount = ntohs(*command_count_networked);

  if (ph->flags & PACKET_FLAG_SENT_TIME) {
    const uint8_t timestamp_size = 6;
    memcpy(ph->SentTime, buff + offset_readed, timestamp_size);
    offset_readed += timestamp_size;
  }

  return offset_readed;
}

size_t parseMessageHeader(unsigned char *buff, struct message *msg) {
  size_t offset_readed = 0;

  // -- command & flags
  uint8_t *command_and_flags = (uint8_t *)(buff + offset_readed);
  offset_readed += sizeof(*command_and_flags);

  msg->flags = (*command_and_flags & MESSAGE_FLAG_MASK);
  msg->Command = (*command_and_flags & (MESSAGE_FLAG_MASK ^ 0xFFFF));

  // -- channel_id
  uint8_t *channel_id = (uint8_t *)(buff + offset_readed);
  offset_readed += sizeof(*channel_id);

  msg->ChannelID = (*channel_id);

  // -- seq_number
  if (msg->Command != SEND_UNSEQUENCED &&
      (msg->flags & MESSAGE_FLAG_UNSEQUENCED) == 0) {
    // Si command n'est pas 'SEND_UNSEQUENCED' ET qu'il n'y a le flag
    // 'MESSAGE_FLAG_UNSEQUENCED'
    // alors, il y a un numero de sequence

    uint16_t *seq_number_networked = (uint16_t *)(buff + offset_readed);
    offset_readed += sizeof(*seq_number_networked);

    msg->seq_number = ntohs(*seq_number_networked);
  }


  printf("command %d, flags %d, channel_id %d, seq_number %d\n", msg->Command, msg->flags, msg->ChannelID, msg->seq_number);
  switch (msg->Command) {

  case ACKNOWLEDGE:

    memcpy(msg->ReceivedSentTime, buff + offset_readed,
           sizeof(msg->ReceivedSentTime));
    offset_readed += sizeof(msg->ReceivedSentTime);
    break;

  case CONNECT:
  case VERIFY_CONNECT:
  case SEND_RELIABLE:
  case SEND_UNRELIABLE:
  case SEND_UNSEQUENCED: {
    uint32_t *data_length_networked = (uint32_t *)(buff + offset_readed);
    offset_readed += sizeof(*data_length_networked);
    msg->DataLength = ntohs(*data_length_networked);

  } break;

  case SEND_UNRELIABLE_FRAGMENT:
  case SEND_FRAGMENT: {
    uint16_t *start_seq_networked = (uint16_t *)(buff + offset_readed);
    offset_readed += sizeof(*start_seq_networked);
    msg->StartSeq = ntohs(*start_seq_networked);

    uint32_t *fragment_count_networked = (uint32_t *)(buff + offset_readed);
    offset_readed += sizeof(*fragment_count_networked);
    msg->FragmentCount = ntohl(*fragment_count_networked);

    uint32_t *fragment_number_networked = (uint32_t *)(buff + offset_readed);
    offset_readed += sizeof(*fragment_number_networked);
    msg->FragmentNumber = ntohl(*fragment_number_networked);

    uint32_t *total_length_networked = (uint32_t *)(buff + offset_readed);
    offset_readed += sizeof(*total_length_networked);
    msg->TotalLength = ntohl(*total_length_networked);

    uint32_t *fragment_offset_networked = (uint32_t *)(buff + offset_readed);
    offset_readed += sizeof(*fragment_offset_networked);
    msg->FragmentOffset = ntohl(*fragment_offset_networked);

  } break;

  case PING:
  case DISCONNECT:
    break;

  default:
    fprintf(stderr, "Error '%s' : UNREACHABLE POINT HIT\n", __FUNCTION__);
    break;
  };

  return offset_readed;
}

/// @brief read recieved packet and enqueue incoming packet into
/// 'incMessageToHandle'
int handleIncommingPacket(int fd) {
  printf("\nmessage reçu from fd : '%d'\n", fd);

  // Peeking in recv
  unsigned char header_buff_peek[MAX_PKT_HEADER_SIZE] = {0};
  recv(fd, header_buff_peek, MAX_PKT_HEADER_SIZE, MSG_PEEK);

  // Parsing the packet header
  struct packet_header packet_header = {0};
  size_t pkt_header_size = parsePacketHeader(header_buff_peek, &packet_header);
  printf("pkt_header_size %ld\n", pkt_header_size);
  printf("cmd_cnt = %d, flags = %d, peer_id = %d \n",
         packet_header.CommandCount, packet_header.flags, packet_header.PeerID);
  // sentTime :(

  // Consuming the header from the recv
  unsigned char header_buff_consume[pkt_header_size];
  recv(fd, header_buff_consume, pkt_header_size, 0);

  // If command count is 0, we're flushing the rest of the recv, and returning
  if (packet_header.CommandCount == 0) {
    // Pas de message à traiter
    // Header avec un payload vide

    // Flushing the payload if exist
    unsigned char payload_buff[2048];
    //
    while (recv(fd, payload_buff, sizeof(payload_buff), MSG_DONTWAIT) > 0)
      ;
    return 0;
  }

  // Reading all the payload (liste of message)

  unsigned char *payload_buff = {0};
  size_t total_payload_size = 0;

  {
    ssize_t recv_return_value = 0;
    size_t multiplicator = 1;

    const size_t alloc_per_loop = 1024;

    // On incrémente de 1024 en 1024 jusqu'a avoir la tout le payload

    do {

      payload_buff = realloc(payload_buff, alloc_per_loop * multiplicator);

      recv_return_value =
          recv(fd, payload_buff + total_payload_size, alloc_per_loop, 0);

      total_payload_size += recv_return_value;
      multiplicator++;
    } while (recv_return_value == alloc_per_loop && multiplicator <= 100);

    // If we go over 100Ko, we're not reading this sheisse
    if (multiplicator > 100) {
      fprintf(
          stderr,
          "%s : Error while allocating memory for payload, payload >= 100Ko\n",
          __FUNCTION__);
      free(payload_buff);

      // TODO : flush the recv

      return -2;
    }

    // Check there is a problem
    if (recv_return_value == -1) {
      total_payload_size++;
      if (errno != EWOULDBLOCK) {
        perror("Error while recv-ing payload");
        free(payload_buff);
        return -2;
      }
    }
  }

  printf("\nenque-ing message\n");

  ssize_t message_offset_payload = 0;

  for (int i = 0; i < packet_header.CommandCount; i++) {
    struct message msg = {0};
    size_t msg_header_size = parseMessageHeader(payload_buff, &msg);
    message_offset_payload += msg_header_size;
    printf("msg_header_size : %ld\n", msg_header_size);

    switch (msg.Command) {

    case CONNECT:
    case VERIFY_CONNECT:
    case SEND_RELIABLE:
    case SEND_UNRELIABLE:
    case SEND_UNSEQUENCED: {
        printf("datalen : %d\n", msg.DataLength);
      msg.payload = malloc(msg.DataLength);
      memcpy(msg.payload, payload_buff + message_offset_payload,
             msg.DataLength);
      message_offset_payload += msg.DataLength;

    } break;

    case SEND_UNRELIABLE_FRAGMENT:
    case SEND_FRAGMENT: {
      size_t rest_payload = total_payload_size - message_offset_payload;
      message_offset_payload += rest_payload;
      printf("en théorie, le payload du messag eest de : %ld\n", rest_payload);
      assert(1 && "FRAGMENTED ARE NOT IMPLEMENTED YET");
    } break;
    default:
      fprintf(stderr, "Error '%s' : UNREACHABLE POINT HIT\n", __FUNCTION__);
      break;
    }

    da_append(incMessageToHandle, msg);
  }



  if (message_offset_payload != total_payload_size) {
    fprintf(stderr, "message_offset_payload %ld != %ld total_payload_size\n",
            message_offset_payload, total_payload_size);
  }

  free(payload_buff);

  return 0;
}

void checkIncommingPacket(struct da_client *da) {
  for (int i = 0; i < da->count; i++) {

    if (da->items[i].fd == -1) {
      continue;
    }

    // Checking if there is atleast one byte
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
