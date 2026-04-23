
#include <netinet/in.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"

struct da_int {
  int *items;

  size_t count;
  size_t capacity;
};

// struct da_client G_da_client = {0};

int main(int argc, char **argv) {

  struct sockaddr_in addr = {
      .sin_family = AF_INET,                     /* AF_INET */
      .sin_port = htons(PORT),                   /* Port number */
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK), /* IPv4 address */
  };

  char *msg1_payload = "coucou les harribo";
  int msg1_payload_size = strlen(msg1_payload);

  const int total_packet_size =
      sizeof(struct NNet_packet_header_base_raw) + sizeof(struct NNet_packet_header_opt_time_raw) +
      sizeof(struct NNet_message_base_raw) + sizeof(struct NNet_message_send_raw) + msg1_payload_size;

  uint8_t *send_buff = malloc(total_packet_size);
  size_t buff_it = 0;

  struct NNet_packet_header_base_raw *ph = (struct NNet_packet_header_base_raw *)send_buff;

  ph->PeerIDFlags = htons(1);
  ph->PeerIDFlags |= PACKET_FLAG_SENT_TIME;
  ph->CommandCount = ntohs(1);
  buff_it += sizeof(*ph);

  ph->opt_timeSpent->time = 100;
  buff_it += sizeof(*ph->opt_timeSpent);

  // Message 1
  // -- command & flags

  struct NNet_message_base_raw *msg1 = (struct NNet_message_base_raw *)(send_buff + buff_it);

  msg1->CommandFlags = SEND_RELIABLE | MESSAGE_FLAG_ACKNOWLEDGE;
  msg1->ChannelID = 22;
  msg1->ReliableSeqNumber = htons(2200);
  buff_it += sizeof(*msg1);

  struct NNet_message_send_raw *msg1_part2 = (struct NNet_message_send_raw *)(msg1->message_part2);
  msg1_part2->DataLength = htonl(msg1_payload_size);
  memcpy(msg1_part2->payload, msg1_payload, msg1_payload_size);

  buff_it += sizeof(*msg1_part2) + msg1_payload_size;

  const int nb = 2;
  struct da_int fds = {0};

  for (size_t i = 0; i < nb; i++) {
    int f = socket(AF_INET, SOCK_STREAM, 0);
    da_append(fds, f);
  }

  alignas(struct NNet_packet_header_base_raw) uint8_t recvbuff[MAX_PKT_SIZE];
  for (size_t i = 0; i < nb; i++) {

    if (connect(fds.items[i], (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
      perror("impossible to connect the socket");
      return -1;
    }

    ssize_t n = send(fds.items[i], send_buff, buff_it, 0);

    n = recv(fds.items[i], recvbuff, MAX_PKT_SIZE, 0);
    if (n > 0) {

        struct NNet_packet_header_base_raw *ph = (struct NNet_packet_header_base_raw *)recvbuff;

      printf("recv: \n\tpeerID: %d\n\tflag: %d\n\tcommandCount: %d\n",
             ntohs( ph->PeerIDFlags & (~PACKET_FLAG_MASK) ),
             ph->PeerIDFlags & (PACKET_FLAG_MASK),
             ntohs(ph->CommandCount));
    }
  }

  printf("%d request sended\n", nb);

  free(send_buff);

  return 0;
}
