#ifndef __net_H__
#define __net_H__

#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// --------------------------------------------------
// DEFINE ZONE
// --------------------------------------------------
#define SOCK_ERR (-1)
#define POLLING_ERROR (-1)
#define PORT 2202
#define BACKLOG 512
#define MAX_PKT_HEADER_SIZE (64)

#define PKT_PEER_MASK (0x3FFF)

// -- define dynamic array

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

// -- define fixed size circual buffer

// -- define circualr buffer

#define cb_init(cb, fixed_size)                                                \
  do {                                                                         \
    cb.capacity = fixed_size;                                                  \
    cb.items = malloc(fixed_size);                                             \
  } while (0)

#define cb_enqueue(cb, item)                                                   \
  do {                                                                         \
    if (cb.count < cb.capacity) {                                              \
      cb.items[cb.tail] = item;                                                \
      cb.tail = (cb.tail + 1) % cb.capacity;                                   \
      cb.count++;                                                              \
    }                                                                          \
  } while (0)

#define cb_dequeue(cb, out)                                                    \
  do {                                                                         \
    if (cb.count > 0) {                                                        \
      out = cb.items[cb.head];                                                 \
      cb.head = (cb.head + 1) % cb.capacity;                                   \
      cb.count--;                                                              \
    }                                                                          \
  } while (0)

// -- enum

enum flag {
  MESSAGE_FLAG_ACKNOWLEDGE = (1 << 7),
  MESSAGE_FLAG_UNSEQUENCED = (1 << 6),
  MESSAGE_FLAG_MASK = MESSAGE_FLAG_ACKNOWLEDGE | MESSAGE_FLAG_UNSEQUENCED,

  PACKET_FLAG_COMPRESSED = (1 << 14), // si commpressé
  PACKET_FLAG_SENT_TIME = (1 << 15),  // Si on envoit le timestamp
  PACKET_FLAG_OFFSET = (8),
  PACKET_FLAG_MASK = (PACKET_FLAG_COMPRESSED | PACKET_FLAG_SENT_TIME),

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

// -- CLIENT
struct client {
  int peerId;
  uint8_t metadata;
};

struct da_client {
  struct client *items;
  size_t count;
  size_t capacity;
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
  uint8_t flags;
  char isDisable;

  // --

  uint16_t StartSeq;

  uint32_t DataLength;
  uint32_t FragmentCount;
  uint32_t FragmentNumber;
  uint32_t TotalLength;
  uint8_t *payload;
  uint32_t FragmentOffset;

  struct packet_header packet_header;
  uint16_t ReceivedSentTime;
};

struct cb_message {
  struct message *items;

  size_t head;
  size_t tail;
  size_t count;
  size_t capacity;
};

// -- Message brut
struct message_ack_raw {
  uint16_t ReceivedSentTime;
};

struct message_send_raw {
  uint32_t DataLength;
  uint8_t payload[];
};

struct message_fragment_raw {
  uint32_t FragmentCount;
  uint32_t FragmentNumber;
  uint32_t TotalLength;
  uint32_t fragmentOffset;
  uint16_t StartSeq;
  uint8_t payload[];
};

struct message_base_raw {
  uint8_t CommandFlags;
  uint8_t ChannelID;
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
  uint16_t PeerID;
  uint16_t CommandCount;
  struct packet_header_opt_time_raw opt_timeSpent[];

  /*
  union {
  struct packet_header_opt_time_raw opt_timeSpent[];
  };
   */
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

extern struct cb_message recvMessageBuff;
extern struct cb_message sendMessageBuff;
// struct da_xxx waitingAck;

extern struct da_client da_client;

extern struct da_pollfd pollfds;
// --------------------------------------------------
// Function definition
// --------------------------------------------------

ssize_t index_of_client(int id);
ssize_t index_of_poll(int fd);

void read_and_enqueue_message();
int server_accept();
ssize_t readEntirePayload(int fd, uint8_t **buff_out);

#endif // __net_H__
