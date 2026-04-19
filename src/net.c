
#include <asm-generic/errno.h>
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

struct cb_message G_recvMessageBuff[NB_PRIORITY];
struct cb_message G_sendMessageBuff[NB_PRIORITY];

struct da_client G_da_client = {0};

int readPacket(int fd);

ssize_t readPacketHeader(int fd, struct packet_header *ph_out);
size_t parsePacketHeader(unsigned char *buff, struct packet_header *ph_out);

size_t parseMessage(unsigned char *buff, size_t buff_size,
                    struct message *msg_out);
size_t parseMessageHeader(unsigned char *buff, size_t buff_size,
                          struct message *msg_out);
void handleSendBuff();

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

size_t parseMessage(unsigned char *buff, size_t buff_size,
                    struct message *msg_out) {

  size_t header_size = parseMessageHeader(buff, buff_size, msg_out);

  if (header_size == 0) {
    return 0;
  }

  printf("msg_header_size %ld :\t", header_size);
  printf("command %d, flags %d, channel_id %d, seq_number %d\n",
         msg_out->Command, msg_out->flags, msg_out->ChannelID,
         msg_out->seq_number);

  size_t message_payload_size = 0;

  switch (msg_out->Command) {

  case CONNECT:
  case VERIFY_CONNECT:
  case SEND_RELIABLE:
  case SEND_UNRELIABLE:
  case SEND_UNSEQUENCED: {
    printf("datalen : %d\n", msg_out->DataLength);

    msg_out->payload = malloc(msg_out->DataLength);
    memcpy(msg_out->payload, buff + header_size, msg_out->DataLength);
    message_payload_size += msg_out->DataLength;
  } break;

  case SEND_UNRELIABLE_FRAGMENT:
  case SEND_FRAGMENT: {
    size_t rest_payload = buff_size - header_size;
    message_payload_size += rest_payload;
    printf("Le payload du message est de : '%ld' octet\n", rest_payload);
    assert(1 && "FRAGMENTED ARE NOT IMPLEMENTED YET");
  } break;
  default:
    fprintf(stderr, "Error '%s' : UNREACHABLE POINT HIT\n", __FUNCTION__);
    break;
  }

  return header_size + message_payload_size;
}

ssize_t readPacketHeader(int fd, struct packet_header *ph) {

  // Peeking in recv
  unsigned char recv_buff[MAX_PKT_HEADER_SIZE + 1] = {0};
  ssize_t header_peeked_n =
      recv(fd, recv_buff, MAX_PKT_HEADER_SIZE + 1, MSG_PEEK);

  if (header_peeked_n <= 0) {
    return header_peeked_n;
  }

  if (header_peeked_n < MIN_PKT_HEADER_SIZE) {
    return -2;
  }

  // Parsing the packet header
  size_t pkt_header_size = parsePacketHeader(recv_buff, ph);

  if (pkt_header_size > MAX_PKT_HEADER_SIZE) {
    return -3;
  }

  // Consuming the header from the recv
  ssize_t n_consumed = recv(fd, recv_buff, pkt_header_size, 0);

  if (n_consumed < pkt_header_size) {
    return -4;
  }

  return pkt_header_size;
}

/// @brief read the recieved packet, and enqueue message into
// 'incMessageToHandle'
// @param fd fd to recv
// @return false (0) if no update
int readPacket(int fd) {

  // Read-in and parsing header
  struct packet_header packet_header;
  ssize_t packet_header_size = readPacketHeader(fd, &packet_header);

  // NOTE / TODO : tcp related
  if (packet_header_size == 0) {
    printf("connexion close from client, fd : '%d'\n", fd);
    // deleting it from pollfds
    G_pollfds.items[index_of_poll(fd)].fd = -1;
    return 0;
  }

  if (packet_header_size < 0) {

    // TODO : faire une fonction "print error" ou un truc comme ca
    switch (packet_header_size) {
    case -1:
      fprintf(stderr, "error while recv-ing the packet header on the fd '%d'\n",
              fd);
      perror("");
      break;
    case -2:
      fprintf(stderr, "Unexpected end of the packet header, the packet is too "
                      "small (lt 4)\n");
      break;
    case -3:
      fprintf(stderr,
              "Error while parsing the packet header, parsed too much\n");
      break;
    default:
      fprintf(stderr, "Error '%s' : UNREACHABLE POINT HIT\n", __FUNCTION__);
      break;
    }

    return -1;
  }

  // Printing stuff

  printf("pkt_header_size %ld :\t", packet_header_size);
  printf("cmd_cnt = %d, flags = %d, peer_id = %d \n",
         packet_header.CommandCount, packet_header.flags, packet_header.PeerID);

  unsigned char *payload_buff = NULL;
  ssize_t total_payload_size = readEntirePayload(fd, &payload_buff);

  // Si pas de message à traiter, on return
  if (packet_header.CommandCount == 0) {
    return 1;
  }

  if (total_payload_size <= 0) {
    return -2;
  }

  ssize_t read_payload = 0;

  for (int i = 0;
       i < packet_header.CommandCount && read_payload < total_payload_size;
       i++) {

    // Parse message

    if (total_payload_size + read_payload < MIN_MSG_HEADER_SIZE) {
      assert(1 &&
             "Garbage payload, can't parse message header, payload too small");
    }

    struct message msg = {0};
    size_t parse_size =
        parseMessage(payload_buff + read_payload, total_payload_size, &msg);

    if (parse_size < 0) {
      assert(1 && "impossible to read or parse the message");
    }

    read_payload += parse_size;

    cb_enqueue(G_recvMessageBuff[(msg.ChannelID & CHANNEL_FLAG_MASK) >>
                                 CHANNEL_FLAG_OFFSET],
               msg);
  }

  if (read_payload != total_payload_size) {
    fprintf(stderr, "message_offset_payload %ld != %ld total_payload_size\n",
            read_payload, total_payload_size);
  }

  free(payload_buff);

  return 0;
}

void net_handle_io() {

  for (size_t i = 0; i < G_pollfds.count; i++) {
    if (G_pollfds.items[i].revents & POLLIN) {
      printf("\nnew message from '%d'\n", G_pollfds.items[i].fd);
      readPacket(G_pollfds.items[i].fd);
    }
  }
}

size_t parseMessageHeader(unsigned char *message_buff, size_t buff_size,
                          struct message *msg_out) {

  if (buff_size < sizeof(struct message_base_raw)) {
    return 0;
  }

  size_t actual_header_size = 0;

  uint8_t internal_buff[MAX_MSG_HEADER_SIZE];
  struct message_base_raw *read_msg = (struct message_base_raw *)internal_buff;

  memcpy(read_msg, message_buff, sizeof(*read_msg));
  actual_header_size += sizeof(*read_msg);

  // -- command & flags
  msg_out->flags = (read_msg->CommandFlags & MESSAGE_FLAG_MASK);
  msg_out->Command = (read_msg->CommandFlags & (MESSAGE_FLAG_MASK ^ 0xFF));

  // -- channel_id
  msg_out->priority = read_msg->ChannelIDFlags & CHANNEL_FLAG_MASK;
  msg_out->ChannelID = read_msg->ChannelIDFlags & (CHANNEL_FLAG_MASK ^ 0xFFFF);

  // -- seq_number
  if (msg_out->Command != SEND_UNSEQUENCED &&
      (msg_out->flags & MESSAGE_FLAG_UNSEQUENCED) == 0) {
    // Si command n'est pas 'SEND_UNSEQUENCED' ET qu'il n'y a le flag
    // 'MESSAGE_FLAG_UNSEQUENCED'
    // alors, il y a un numero de sequence

    // TODO memcpy quand on aura dispatch le seq number
    msg_out->seq_number = ntohs(read_msg->seq_number);
  }

  switch (msg_out->Command) {

  case ACKNOWLEDGE: {

    struct message_ack_raw *read_msg_ack;

    if (buff_size < actual_header_size + sizeof(*read_msg_ack)) {
      actual_header_size = 0;
      break;
    }

    memcpy(read_msg->message_part2, message_buff + actual_header_size,
           sizeof(*read_msg_ack));

    read_msg_ack = (struct message_ack_raw *)(read_msg->message_part2);
    actual_header_size += sizeof(*read_msg_ack);

    msg_out->ReceivedSentTime = ntohs(read_msg_ack->ReceivedSentTime);
  } break;

  case CONNECT:
  case VERIFY_CONNECT:
    assert(0 && "impossible to parse 'CONNECT' and 'VERIFY_CONNECT',"
                "not implemented yet");
    break;

  case SEND_RELIABLE:
  case SEND_UNRELIABLE:
  case SEND_UNSEQUENCED: {

    struct message_send_raw *read_msg_send;

    if (buff_size < actual_header_size + sizeof(*read_msg_send)) {
      actual_header_size = 0;
      break;
    }

    memcpy(read_msg->message_part2, message_buff + actual_header_size,
           sizeof(*read_msg_send));

    read_msg_send = (struct message_send_raw *)(read_msg->message_part2);
    actual_header_size += sizeof(*read_msg_send);

    msg_out->DataLength = ntohl(read_msg_send->DataLength);
  } break;

  case SEND_UNRELIABLE_FRAGMENT:
  case SEND_FRAGMENT: {

    struct message_fragment_raw *read_msg_frgm;
    if (buff_size < actual_header_size + sizeof(struct message_fragment_raw)) {
      actual_header_size = 0;
      break;
    }

    memcpy(read_msg->message_part2, message_buff + actual_header_size,
           sizeof(*read_msg_frgm));

    read_msg_frgm = (struct message_fragment_raw *)(read_msg->message_part2);
    actual_header_size += sizeof(*read_msg_frgm);

    msg_out->StartSeq = ntohs(read_msg_frgm->StartSeq);
    msg_out->FragmentCount = ntohl(read_msg_frgm->FragmentCount);
    msg_out->FragmentNumber = ntohl(read_msg_frgm->FragmentNumber);
    msg_out->TotalLength = ntohl(read_msg_frgm->TotalLength);
    msg_out->FragmentOffset = ntohl(read_msg_frgm->FragmentOffset);

  } break;

  case PING:
  case DISCONNECT:
    break;

  default:
    fprintf(stderr, "Error '%s' : UNREACHABLE POINT HIT\n", __FUNCTION__);
    break;
  };

  return actual_header_size;
}

size_t parsePacketHeader(unsigned char *packet_buff,
                         struct packet_header *ph_out) {

  size_t offset_readed = 0;

  uint8_t internal_buff[MAX_PKT_HEADER_SIZE];
  struct packet_header_base_raw *read_msg =
      (struct packet_header_base_raw *)internal_buff;

  memcpy(read_msg, packet_buff, sizeof(*read_msg));
  offset_readed += sizeof(*read_msg);

  ph_out->flags = (read_msg->PeerIDFlags & PACKET_FLAG_MASK);
  ph_out->PeerID = ntohs(read_msg->PeerIDFlags & (PACKET_FLAG_MASK ^ 0xFFFF));
  ph_out->CommandCount = ntohs(read_msg->CommandCount);

  if (ph_out->flags & PACKET_FLAG_SENT_TIME) {
    memcpy(read_msg->opt_timeSpent, packet_buff + offset_readed,
           sizeof(*read_msg->opt_timeSpent));
    ph_out->SentTime = ntohs(read_msg->opt_timeSpent->time);
    offset_readed += sizeof(*read_msg->opt_timeSpent);
  }

  return offset_readed;
}

size_t getMessageSize(struct message *msg, size_t payload_actual_size) {
  size_t msg_size = sizeof(struct message_base_raw);

  switch (msg->Command) {
  case ACKNOWLEDGE:
    msg_size += sizeof(struct message_ack_raw);
    break;

  case CONNECT:
  case VERIFY_CONNECT:
  case DISCONNECT:
  case PING:
    assert(0 && "getMessageSize - CONNECT, VERIFY_CONNECT, DISCONNECT, PING : "
                "Not implemented yet");
    break;

  case SEND_RELIABLE:
  case SEND_UNRELIABLE:
  case SEND_UNSEQUENCED:
    msg_size += sizeof(struct message_send_raw) + msg->DataLength;
    break;

  case SEND_FRAGMENT:
  case SEND_UNRELIABLE_FRAGMENT:

    assert(0 &&
           "getMessageSize - SEND_FRAGMENT, SEND_UNRELIABLE_FRAGMENT : Not "
           "implemented yet");

    // msg_size += sizeof(struct message_fragment_raw) + ???
    break;

  case BANDWIDTH_LIMIT:
  case THROTTLE_CONFIGURE:
    assert(0 && "getMessageSize - BANDWIDTH_LIMIT, THROTTLE_CONFIGURE : Not "
                "implemented");
    break;
  };

  return msg_size;
}

void handleSendBuff() {

  int shouldStop = 0;

  // atm on envois tout le temps le opt sentTime
  const size_t packet_header_size = sizeof(struct packet_header_base_raw) +
                                    sizeof(struct packet_header_opt_time_raw);

  while (!shouldStop) {

    uint8_t *packet_buffer = malloc(MAX_PKT_SIZE);
    int pkt_payload_actual_size = 0;

    /*
           faire un offset d'au moins MAX_HEADER_SIZE ou en vrai de 8, pour etre
           sur que le reste des structs soit alligné en mémoire
    */
    for (size_t i = 0; i < NB_PRIORITY; i++) {

      while (G_sendMessageBuff[i].count > 0) {

        struct message msg_peek = {0};
        cb_peek(G_sendMessageBuff[i], msg_peek);

        size_t msg_size = getMessageSize(&msg_peek, pkt_payload_actual_size);

        if (packet_header_size + pkt_payload_actual_size + msg_size >=
            MAX_PKT_SIZE) {
          assert(1 && "not implemented the sending yet");

          // Si c'est un send > MAX_PKT_SIZE - les headers
          // créé les fragments
          // remplacer le send actuel par un fragment
          // ajouter le fragment dans le packet_buffer
          // ajouter le reste des fragment par la head ?
          break;
        }

        struct message msg;
        cb_dequeue(G_sendMessageBuff[i], msg);

        size_t offset = packet_header_size + pkt_payload_actual_size;
        addMessageToRaw(packet_buffer + offset, &msg);

        pkt_payload_actual_size += msg_size;
      }

      if (packet_header_size + pkt_payload_actual_size >= MAX_PKT_SIZE) {
        // send le packet
        printf("on send un paquet \n");
        free(packet_buffer);
        break;
      }
      if (i == NB_PRIORITY - 1) {
        shouldStop = 1;
        if (pkt_payload_actual_size > 0) {
          printf("on send un paquet \n");
        }
        free(packet_buffer);
      }
    }
  }
}

ssize_t index_of_client(int id) {

  for (size_t i = 0; i < G_da_client.count; i++) {
    if (G_da_client.items[i].peerId == id) {
      return i;
    }
  }

  return -1;
}

ssize_t index_of_poll(int fd) {

  for (size_t i = 0; i < G_pollfds.count; i++) {
    if (G_pollfds.items[i].fd == fd) {
      return i;
    }
  }

  return -1;
}

void initMessageBuffer() {
  for (size_t i = 0; i < NB_PRIORITY; i++) {
    cb_init(G_recvMessageBuff[i], 1024 * 500);
    cb_init(G_sendMessageBuff[i], 1024 * 500);
  }
}

void freeMessageBuffer() {
  for (size_t i = 0; i < NB_PRIORITY; i++) {
    free(G_recvMessageBuff[i].items);
    G_recvMessageBuff[i].count = 0;
    G_recvMessageBuff[i].capacity = 0;
    G_recvMessageBuff->items = NULL;

    free(G_sendMessageBuff[i].items);
    G_sendMessageBuff[i].count = 0;
    G_sendMessageBuff[i].capacity = 0;
    G_sendMessageBuff->items = NULL;
  }
}
