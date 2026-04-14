
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 2202

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

  uint8_t buff[1024];

  size_t offset_readed = 0;

  uint16_t *peerID = (uint16_t *)(buff + offset_readed);
  offset_readed += sizeof(*peerID);

  *peerID = htons(1);
  // no flag

  // Packet header
  uint16_t *command_count = (uint16_t *)(buff + offset_readed);
  offset_readed += sizeof(*command_count);
  *command_count = ntohs(1);


  // Message 1
  // -- command & flags
  uint8_t *command_and_flags = (uint8_t *)(buff + offset_readed);
  offset_readed += sizeof(*command_and_flags);
  *command_and_flags = SEND_RELIABLE;
  *command_and_flags |= MESSAGE_FLAG_ACKNOWLEDGE;

  // -- channel_id
  uint8_t *channel_id = (uint8_t *)(buff + offset_readed);
  offset_readed += sizeof(*channel_id);

  *channel_id = 1;

  uint16_t *seq_number = (uint16_t *)(buff + offset_readed);
  offset_readed += sizeof(*seq_number);

  *seq_number = htons(2200);

  uint32_t *data_len = (uint32_t *)(buff + offset_readed);
  offset_readed += sizeof(*data_len);
  *data_len = htons(strlen("coucou les harribo"));

  char *payload = (char *)(buff + offset_readed);
  offset_readed += strlen("coucou les harribo");
  memcpy(payload, "coucou les harribo", strlen("coucou les harribo"));

  printf("offset_readed %ld\n", offset_readed);

  send(fd, buff, offset_readed, 0);

  close(fd);

  return 0;
}
