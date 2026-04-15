
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "net.h"

int readPacketHeader(int fd, struct packet_header *ph);
int server_accept();
size_t parsePacketHeader(unsigned char *buff, struct packet_header *ph);
size_t parseMessageHeader(unsigned char *buff, struct message *msg);
int readPacket(int fd);

// -----

ssize_t readEntirePayload(int fd, uint8_t **buff_out) {

  uint8_t *payload_buff = {0};
  ssize_t total_payload_size = 0;

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

  *buff_out = payload_buff;

  return total_payload_size;
}

int readPacketHeader(int fd, struct packet_header *ph) {

  // Peeking in recv
  unsigned char header_buff_peek[MAX_PKT_HEADER_SIZE] = {0};
  ssize_t header_peek_n =
      recv(fd, header_buff_peek, MAX_PKT_HEADER_SIZE, MSG_PEEK);

  if (header_peek_n <= 0) {
    return header_peek_n;
  }

  // Parsing the packet header

  size_t pkt_header_size = parsePacketHeader(header_buff_peek, ph);

  // Consuming the header from the recv
  unsigned char header_buff_consume[pkt_header_size];
  ssize_t n_consumed = recv(fd, header_buff_consume, pkt_header_size, 0);

  if (n_consumed != pkt_header_size) {
    return -2;
  }

  return pkt_header_size;
}

/// @brief read recieved packet and enqueue incoming packet into
/// 'incMessageToHandle'
// @return false (0) if no update
int readPacket(int fd) {

  // Read-in and parsing header
  struct packet_header packet_header;
  ssize_t packet_header_size = readPacketHeader(fd, &packet_header);

  if (packet_header_size == 0) {
    printf("connexion close from client, fd : '%d'\n", fd);
    // deleting it from pollfds
    pollfds.items[index_of_poll(fd)].fd = -1;
    return 0;
  }

  if (packet_header_size < 0) {
    fprintf(stderr, "impossible de recv sur le fd '%d'\n", fd);
    perror("");
    return -1;
  }

  // Printing stuff

  printf("pkt_header_size %ld :\t", packet_header_size);
  printf("cmd_cnt = %d, flags = %d, peer_id = %d \n",
         packet_header.CommandCount, packet_header.flags, packet_header.PeerID);

  // Si pas de message à traiter, on return
  if (packet_header.CommandCount == 0) {

    // Flushing the payload if exist
    unsigned char payload_buff[1024];
    while (recv(fd, payload_buff, sizeof(payload_buff), MSG_DONTWAIT) > 0)
      ;
    return 1;
  }

  unsigned char *payload_buff = NULL;
  ssize_t total_payload_size = readEntirePayload(fd, &payload_buff);

  if (total_payload_size <= 0) {
    return -2;
  }

  ssize_t message_offset_payload = 0;

  for (int i = 0; i < packet_header.CommandCount &&
                  message_offset_payload < total_payload_size;
       i++) {

    struct message msg = {0};
    size_t msg_header_size = parseMessageHeader(payload_buff, &msg);
    message_offset_payload += msg_header_size;
    printf("msg_header_size %ld :\t", msg_header_size);
    printf("command %d, flags %d, channel_id %d, seq_number %d\n", msg.Command,
           msg.flags, msg.ChannelID, msg.seq_number);

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

    cb_enqueue(recvMessageBuff, msg);
  }

  if (message_offset_payload != total_payload_size) {
    fprintf(stderr, "message_offset_payload %ld != %ld total_payload_size\n",
            message_offset_payload, total_payload_size);
  }

  free(payload_buff);

  return 0;
}

/// @return true (non-zero) if the fd is triggered on 'pollin'
int server_accept() {

  if (pollfds.items[0].revents == 0) {
    return 0;
  }

  if ((pollfds.items[0].revents & POLLIN) == 0) {
    fprintf(stderr,
            "le ServFd à été trigger par poll(), mais n'est pas en POLLIN\n"
            "\trevent = %d\n",
            pollfds.items[0].revents);
    return -1;
  }

  pollfds.items[0].revents = 0;

  int client_fd = accept(pollfds.items[0].fd, NULL, NULL);
  if (client_fd == SOCK_ERR) {
    perror("error while accepting new client");
    return -2;
  }

  printf("\nnew connection, fd : '%d'\n", client_fd);

  struct client c = {.peerId = client_fd}; // TODO : Générer un vrai id
  da_append(da_client, c);

  struct pollfd pfd = {.fd = client_fd, .events = POLLIN};
  da_append(pollfds, pfd);

  readPacket(client_fd);

  return 0;
}

void read_and_enqueue_message() {

  for (size_t i = 0; i < pollfds.count; i++) {
    if (pollfds.items[i].revents & POLLIN) {
      printf("\nnew message from '%d'\n", pollfds.items[i].fd);
      readPacket(pollfds.items[i].fd);
    }
  }
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

ssize_t index_of_client(int id) {

  for (size_t i = 0; i < da_client.count; i++) {
    if (da_client.items[i].peerId == id) {
      return i;
    }
  }

  return -1;
}

ssize_t index_of_poll(int fd) {

  for (size_t i = 0; i < pollfds.count; i++) {
    if (pollfds.items[i].fd == fd) {
      return i;
    }
  }

  return -1;
}
