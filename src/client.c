
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"


// struct da_client G_da_client = {0};
struct da_pollfd G_pollfds = {0};

int main(int argc, char **argv) {

  int fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in addr = {
      .sin_family = AF_INET,                     /* AF_INET */
      .sin_port = htons(PORT),                   /* Port number */
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK), /* IPv4 address */
  };

  if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("impossible to connect the socket");
    return -1;
  }

  char *msg1_payload = "coucou les harribo";
  int msg1_payload_size = strlen(msg1_payload);

  const int total_packet_size = sizeof(struct packet_header_base_raw) +
                                sizeof(struct packet_header_opt_time_raw) +
                                sizeof(struct message_base_raw) +
                                sizeof(struct message_send_raw) +
                                msg1_payload_size;

  uint8_t *send_buff = malloc(total_packet_size);
  size_t buff_it = 0;

  struct packet_header_base_raw *ph =
      (struct packet_header_base_raw *)send_buff;

  ph->PeerIDFlags = htons(1);
  ph->PeerIDFlags |= PACKET_FLAG_SENT_TIME;
  ph->CommandCount = ntohs(1);
  buff_it += sizeof(*ph);

  ph->opt_timeSpent->time = 100;
  buff_it += sizeof(*ph->opt_timeSpent);

  // Message 1
  // -- command & flags

  struct message_base_raw *msg1 =
      (struct message_base_raw *)(send_buff + buff_it);

  msg1->CommandFlags = SEND_RELIABLE | MESSAGE_FLAG_ACKNOWLEDGE;
  msg1->ChannelID = 22;
  msg1->ReliableSeqNumber = htons(2200);
  buff_it += sizeof(*msg1);

  struct message_send_raw *msg1_part2 =
      (struct message_send_raw *)(msg1->message_part2);
  msg1_part2->DataLength = htonl(msg1_payload_size);
  memcpy(msg1_part2->payload, msg1_payload, msg1_payload_size);

  buff_it += sizeof(*msg1_part2) + msg1_payload_size;

  printf("total_packet_size %d\n", total_packet_size);
  printf("buff_it %ld\n", buff_it);
  ssize_t n = send(fd, send_buff, total_packet_size, 0);

  printf("n %ld\n", n);

  close(fd);

  return 0;
}
