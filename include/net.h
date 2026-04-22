#ifndef __net_H__
#define __net_H__

#include <netinet/in.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// --------------------------------------------------
// DEFINE ZONE
// --------------------------------------------------
// #define CLIENT_TO_KEY(client) (((client).addr.sin_addr.s_addr >> 24) | ((client).addr.sin_port <<
// 8))

// ASKIP au bout de 300 entré ca va faire pas mal de collision

#define SOCK_ERR (-1)
#define POLLING_ERROR (-1)

#define MAX_MTU_SIZE (1400)

// MTU ~= 1500, (sur ADSL en Wi-Fi MTU = 1 468 octets.) auquel on enleve header ip et header udp
// - ip header taille variable jusqu'a max 60octets
// - header udp 8 octets
#define MAX_PKT_SIZE (1400)

#define MAX_PKT_HEADER_SIZE (6)
#define MIN_PKT_HEADER_SIZE (4)

#define MIN_MSG_HEADER_SIZE (6)
#define MAX_MSG_HEADER_SIZE (32)

#ifndef PORT
#define PORT 2202
#endif

#ifndef BACKLOG
#define BACKLOG 512
#endif

// -- define dynamic array

#define da_append(da, item)                                                                        \
  do {                                                                                             \
    if (da.count >= da.capacity) {                                                                 \
      if (da.capacity == 0)                                                                        \
        da.capacity = 256;                                                                         \
      else                                                                                         \
        da.capacity *= 2;                                                                          \
      da.items = realloc(da.items, da.capacity * sizeof(*da.items));                               \
    }                                                                                              \
    da.items[da.count++] = item;                                                                   \
  } while (0)

// --------------------------------------------------
// Structure
// --------------------------------------------------

// -- define fixed size circual buffer

// -- define circular buffer

#define cb_init(cb, fixed_size)                                                                    \
  do {                                                                                             \
    cb.count = 0;                                                                                  \
    cb.capacity = fixed_size;                                                                      \
    cb.items = malloc(fixed_size * sizeof(*cb.items));                                             \
  } while (0)

#define cb_enqueue(cb, item)                                                                       \
  do {                                                                                             \
    if (cb.count < cb.capacity) {                                                                  \
      cb.items[cb.tail] = item;                                                                    \
      cb.tail = (cb.tail + 1) % cb.capacity;                                                       \
      cb.count++;                                                                                  \
    }                                                                                              \
  } while (0)

#define cb_dequeue(cb, out)                                                                        \
  do {                                                                                             \
    if (cb.count > 0) {                                                                            \
      out = cb.items[cb.head];                                                                     \
      cb.head = (cb.head + 1) % cb.capacity;                                                       \
      cb.count--;                                                                                  \
    }                                                                                              \
  } while (0)

#define cb_peek(cb, out)                                                                           \
  do {                                                                                             \
    if (cb.count > 0) {                                                                            \
      out = cb.items[cb.head];                                                                     \
    }                                                                                              \
  } while (0)

// -- enum

enum flag {
  PACKET_FLAG_COMPRESSED = (1 << 14), // si commpressé
  PACKET_FLAG_SENT_TIME = (1 << 15),  // Si on envoit le timestamp
  PACKET_FLAG_OFFSET = (8),
  PACKET_FLAG_MASK = (PACKET_FLAG_COMPRESSED | PACKET_FLAG_SENT_TIME),

  MESSAGE_FLAG_ACKNOWLEDGE = (1 << 7),
  MESSAGE_FLAG_UNSEQUENCED = (1 << 6),
  MESSAGE_FLAG_MASK = MESSAGE_FLAG_ACKNOWLEDGE | MESSAGE_FLAG_UNSEQUENCED,

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

// -- Message parsé

struct packet_header {
  uint16_t PeerID;
  uint16_t CommandCount;
  uint16_t SentTime;
  uint16_t flags;
};

struct message {
  uint8_t Command;
  uint8_t ChannelID;
  union {
    uint16_t seq_number;
    uint16_t ReceivedSeqNumber;
    uint16_t UnreliableSeqNumber;
    uint16_t ReliableSeqNumber;
  };
  unsigned char isDisable;
  uint8_t flags;

  struct packet_header packet_header;
  // --

  uint8_t *payload;

  union {
    struct { // fragment
      uint32_t FragmentCount;
      uint32_t FragmentNumber;
      uint32_t FragmentOffset;
      uint32_t TotalLength;
      uint16_t StartSeq;
    };
    struct { // ack
      uint16_t ReceivedSentTime;
    };
    struct { // send
      uint32_t DataLength;
    };
  };

  // padding + 6 mais osef
};

struct cb_message {
  struct message *items;

  size_t head;
  size_t tail;
  size_t count;
  size_t capacity;
};

// -- CLIENT
struct client {

  struct cb_message recvMessageBuff;
  struct cb_message sendMessageBuff;

  struct sockaddr_in addr;
  int peerId;
  int fd;
};

struct da_client {
  struct client *items;
  size_t count;
  size_t capacity;
};

struct hm_client {
  uint32_t key;
  struct client value;
};

// -- Message brut
struct message_ack_raw {
  uint16_t ReceivedSentTime;
};

struct message_send_raw {
  uint32_t DataLength;
  uint8_t payload[];
};

struct __attribute__((packed)) message_fragment_raw {
  uint32_t FragmentCount;
  uint32_t FragmentNumber;
  uint32_t TotalLength;
  uint32_t FragmentOffset;
  uint16_t StartSeq;
  uint8_t payload[];
};

struct message_base_raw {
  uint8_t CommandFlags;
  uint8_t ChannelID;

  // TODO : dispactch seq number dans les struct de part2 et créer un
  // struct 'message_send_unseq' ou il ne sera pas présent
  union {
    uint16_t seq_number;
    uint16_t UnreliableSeqNumber;
    uint16_t ReceivedSeqNumber;
    uint16_t ReliableSeqNumber;
  };
  uint8_t message_part2[];
};

struct packet_header_opt_time_raw {
  uint16_t time;
};

struct packet_header_base_raw {
  uint16_t PeerIDFlags;
  uint16_t CommandCount;
  struct packet_header_opt_time_raw opt_timeSpent[];
};

// -- poll

struct da_pollfd {
  struct pollfd *items;
  size_t capacity;
  size_t count;
};

// --------------------------------------------------
// Global variable
// --------------------------------------------------

// struct da_xxx waitingAck;

extern struct hm_client *G_hm_client;
extern struct da_pollfd G_pollfds;

// --------------------------------------------------
// Function definition
// --------------------------------------------------

ssize_t index_of_poll(int fd);

void net_handle_io();
// int server_accept(struct pollfd *s_pfd);

void addClient(struct client *clt);

int net_poll(struct message *msg_out);

#endif // __net_H__
