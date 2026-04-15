#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"

struct da_pollfd {
  struct pollfd *items;
  size_t capacity;
  size_t count;
};

int handleRecvMessage();
int setThisFuckinkSocket();
// bloquante
void functionOfPollHandling(struct da_pollfd *pollfds);

int servFd = -1;

// --------------------------------------------------
// MAIN Function
// --------------------------------------------------

int main(int argc, char **argv) {

  // -- Set-up socket
  int servFd = setThisFuckinkSocket();

  printf("listening on port '%d'\n", PORT);

  // -- setup befor infinite loop
  struct da_pollfd pollfds = {0};

  da_append(pollfds, (struct pollfd){.fd = servFd});

  // -- start infinite loop
  while (1) {

    // -----------------------
    // Traitement message reçu
    // -----------------------
    functionOfPollHandling(&pollfds);

    checkIncommingPacketFromFD(&da_client);

    // TODO on traitera par batch plus tard, atm, on traite tant qu'il y a à
    // traiter
    while (handleRecvMessage())
      ;

    // // -----------------------
    // // Traitement message à envoyer
    // // -----------------------
    // if (sendMessageBuff.count >= 1) {
    //   // TODO traiter un message de `incMessageToHandle`
    //   struct send_mes *msg = getSendMessage();
    //   if (msg != NULL) {
    //     handleSendMessage(msg);
    //   }
    // }
  }

  return 0;
}

// --------------------

void functionOfPollHandling(struct da_pollfd *pollfds) {

  if (poll(pollfds->items, pollfds->count, -1) == -1) {
    perror("error whil poll-ing");
  }

  if (pollfds->items[0].revents & POLLIN) {
    int client_fd = accept(servFd, NULL, NULL);

    if (client_fd == SOCK_ERR) {
      perror("error while accepting new client");
    } else {
      // else, we append the fd to the dynamic array to poll it
      da_append(da_client, (struct client){.fd = client_fd});
    }
  }

  for (size_t i = 1; i < pollfds->count; i++) {

    if (pollfds->items[i].revents & POLLIN) {
      printf("socket '%d' pollin", pollfds->items[i].fd);
    }
  }
}

int handleRecvMessage() {

  struct message *msg = getRecvMessage();
  if (msg == NULL) {
    return 0;
  }

  printf("%s\n", __FUNCTION__);

  switch (msg->Command) {
  case ACKNOWLEDGE:
    assert(1 && "Not implemented yet");
    // TODO : faire une liste de packet devant etre ack
    // lorsque l'on recoit un ack, on cherche dans la liste, et on la tej de la
    // liste
    break;

  case CONNECT: {
    assert(1 && "CONNECT : Not implemented yet");

    // TODO mettre dans le buff "outMessagebuff"
  } break;

  case DISCONNECT:
  case PING:
    assert(1 && "DISCONNECT & PING : Not implemented yet");
    break;

  case SEND_RELIABLE:
  case SEND_UNRELIABLE:
  case SEND_UNSEQUENCED:
    printf("traitement d'un msg ayant pour command 'SEND_RELIABLE', "
           "'SEND_UNRELIABLE' ou 'SEND_FRAGMENT'\n ");
    msg->isDisable = 1;
    assert(1 && "SEND_XXX : Not implemented yet");

    break;

  case SEND_FRAGMENT:
  case SEND_UNRELIABLE_FRAGMENT:
    // TODO : faudra memcpy dans un cache le temps d'assembler tout les
    // fragments
    assert(1 && "FRAGMENT & UNRELIABLE_FRAGMENT : Not implemented yet");
    break;

  case BANDWIDTH_LIMIT:
  case THROTTLE_CONFIGURE:
    assert(1 && "BANDWIDTH_LIMIT & THROTTLE_CONFIGURE : Not implemented");

  default:
  case VERIFY_CONNECT:
    break;
  };

  if (msg->payload != NULL) {
    free(msg->payload);
    msg->payload = NULL;
  }

  return 1;
}

int setThisFuckinkSocket() {
  int serv_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (serv_fd == SOCK_ERR) {
    perror("impossible to create socket");
    return SOCK_ERR;
  }

  setsockopt(serv_fd, SOL_SOCKET, SO_REUSEPORT, &((int){1}), sizeof(int));

  const struct sockaddr_in addr = {
      .sin_family = AF_INET,         /* Famille d'adresses : AF_INET */
      .sin_port = htons(2202),       /* Port dans l'ordre des octets réseau */
      .sin_addr.s_addr = INADDR_ANY, /* Adresse Internet */
  };

  if (bind(serv_fd, (const struct sockaddr *)&addr, sizeof(addr)) == SOCK_ERR) {
    perror("impossible to bind socket");
    return SOCK_ERR;
  }

  if (listen(serv_fd, BACKLOG) == SOCK_ERR) {
    perror("impossible to listen the socket");
    return SOCK_ERR;
  }

  return serv_fd;
}

// -----

// TODO : utiliser les struct "_raw" pour parser de maniere plus lisible avant
// de balancer ca dans ph
size_t parsePacketHeader(unsigned char *buff, struct packet_header *ph) {

  size_t offset_readed = 0;

  uint16_t *peer_and_flags_networked = (uint16_t *)(buff + offset_readed);
  offset_readed += sizeof(uint16_t);

  ph->flags = (*peer_and_flags_networked & PACKET_FLAG_MASK);

  ph->PeerID = ntohs(*peer_and_flags_networked & (PACKET_FLAG_MASK ^ 0xFFFF));

  uint16_t *command_count_networked = (uint16_t *)(buff + offset_readed);
  offset_readed += sizeof(uint16_t);

  ph->CommandCount = ntohs(*command_count_networked);

  if (ph->flags & PACKET_FLAG_SENT_TIME) {
    uint16_t *timestamp = (uint16_t *)(buff + offset_readed);
    offset_readed += sizeof(*timestamp);
    ph->SentTime = ntohs(*timestamp);
  }

  return offset_readed;
}

// TODO : utiliser les struct "_raw" pour parser de maniere plus lisible avant
// de balancer ca dans msg
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

  printf("command %d, flags %d, channel_id %d, seq_number %d\n", msg->Command,
         msg->flags, msg->ChannelID, msg->seq_number);
  switch (msg->Command) {

  case ACKNOWLEDGE: {

    uint16_t *timestamp = (uint16_t *)(buff + offset_readed);
    offset_readed += sizeof(*timestamp);
    msg->ReceivedSentTime = ntohs(*timestamp);
  } break;

  case CONNECT:
  case VERIFY_CONNECT:
  case SEND_RELIABLE:
  case SEND_UNRELIABLE:
  case SEND_UNSEQUENCED: {
    uint32_t *data_length_networked = (uint32_t *)(buff + offset_readed);
    offset_readed += sizeof(*data_length_networked);
    msg->DataLength = ntohl(*data_length_networked);

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
int handleIncommingPacket(struct client client) {
  printf("\nmessage reçu from fd : '%d'\n", client.fd);

  // Peeking in recv
  unsigned char header_buff_peek[MAX_PKT_HEADER_SIZE] = {0};
  recv(client.fd, header_buff_peek, MAX_PKT_HEADER_SIZE, MSG_PEEK);

  // Parsing the packet header
  struct packet_header packet_header = {0};

  size_t pkt_header_size = parsePacketHeader(header_buff_peek, &packet_header);
  printf("pkt_header_size %ld\n", pkt_header_size);
  printf("cmd_cnt = %d, flags = %d, peer_id = %d \n",
         packet_header.CommandCount, packet_header.flags, packet_header.PeerID);
  // sentTime :(

  packet_header.client = client;

  // Consuming the header from the recv
  unsigned char header_buff_consume[pkt_header_size];
  recv(client.fd, header_buff_consume, pkt_header_size, 0);

  // If command count is 0, we're flushing the rest of the recv, and returning
  if (packet_header.CommandCount == 0) {
    // Pas de message à traiter
    // Header avec un payload vide

    // Flushing the payload if exist
    unsigned char payload_buff[2048];
    //
    while (recv(client.fd, payload_buff, sizeof(payload_buff), MSG_DONTWAIT) >
           0)
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
          recv(client.fd, payload_buff + total_payload_size, alloc_per_loop, 0);

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

  printf("enque-ing message\n");

  ssize_t message_offset_payload = 0;

  for (int i = 0; i < packet_header.CommandCount &&
                  message_offset_payload < total_payload_size;
       i++) {

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
      printf("en théorie, le payload du messag est de : %ld\n", rest_payload);
      assert(1 && "FRAGMENTED ARE NOT IMPLEMENTED YET");
    } break;
    default:
      fprintf(stderr, "Error '%s' : UNREACHABLE POINT HIT\n", __FUNCTION__);
      break;
    }

    da_append(recvMessageBuff, msg);
  }

  if (message_offset_payload != total_payload_size) {
    fprintf(stderr, "message_offset_payload %ld != %ld total_payload_size\n",
            message_offset_payload, total_payload_size);
  }

  free(payload_buff);

  return 0;
}

void checkIncommingPacketFromFD(struct da_client *da) {

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

    handleIncommingPacket(da->items[i]);
  }
}

struct message *getRecvMessage() {
  if (recvMessageBuff.count <= 0)
    return NULL;

  struct message *msg = NULL;

  for (size_t i = 0; i < recvMessageBuff.count && msg == NULL; i++) {
    if (!recvMessageBuff.items[i].isDisable) {
      msg = (recvMessageBuff.items + i);
    }
  }
  return msg;
}
