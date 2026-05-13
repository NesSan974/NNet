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

// Options

#ifndef PORT
#define PORT (2202)
#endif


// The more budget you put, more you process packet per loop, more the buffers/cache will scale consequently
#ifndef BUDGET
#define BUDGET (BUDGET_DEFAULT)
#endif

// log the time of some functions if defined
// #define PROFILE

// Dump the send buffer and the recv buffer in hexa in the stdin
// #define HEX_DUMP

// log precision, 0 = none
#define DEBUG_LOG 1

#define TIME_PER_WHEEL_SLOT_MS 16
#define ACK_TIME_OUT_MS 64

typedef struct NNet_context NNet_context;
typedef struct NNet_client  NNet_client;
typedef struct NNet_message NNet_message;



// RETURN FUNCTION ERROR
enum FUNCTION_RETURN {

    BUDGET_HIT                      = 1,

    ERROR_SOCKET                    = -1,
    ERROR_DATAGRAM_TOO_BIG          = -2,
    ERROR_MESSAGE_PARSING           = -3,
    ERROR_FIFO_CAPACITY_EXCEEDED    = -4,
    ERROR_ARRENA_SIZE_EXCEEDED      = -5,
    ERROR_TOO_MUCH_MESSAGE          = -6,
};



enum NNet_flag {
    PACKET_FLAG_COMPRESSED = (1 << 14), // si commpressé
    PACKET_FLAG_SENT_TIME  = (1 << 15),  // Si on envoit le timestamp
    PACKET_FLAG_OFFSET     = (8),
    PACKET_FLAG_MASK       = (PACKET_FLAG_COMPRESSED | PACKET_FLAG_SENT_TIME),

    MESSAGE_FLAG_ACKNOWLEDGE = (1 << 7),
    MESSAGE_FLAG_UNSEQUENCED = (1 << 6),
    MESSAGE_FLAG_MASK        = MESSAGE_FLAG_ACKNOWLEDGE | MESSAGE_FLAG_UNSEQUENCED,

};

enum NNet_message_command {

    MSG_ACKNOWLEDGE              = 0x01,
    MSG_CONNECT                  = 0x02,
    MSG_VERIFY_CONNECT           = 0x03,
    MSG_DISCONNECT               = 0x04,
    MSG_PING                     = 0x05,
    MSG_SEND_RELIABLE            = 0x06,
    MSG_SEND_UNRELIABLE          = 0x07,
    MSG_SEND_FRAGMENT            = 0x08,
    MSG_SEND_UNSEQUENCED         = 0x09,
    MSG_BANDWIDTH_LIMIT          = 0x0A,
    MSG_THROTTLE_CONFIGURE       = 0x0B,
    MSG_SEND_UNRELIABLE_FRAGMENT = 0x0C,
};

// -- Message parsé

struct NNet_packet_header {
    uint32_t SentTime;
    uint16_t PeerID;
    uint16_t MessageCount;
    uint16_t flags;
};

struct NNet_message {
    uint8_t Type;
    uint8_t ChannelID;
    union {
        uint16_t seq_number;
        uint16_t ReceivedSeqNumber;
        uint16_t UnreliableSeqNumber;
        uint16_t ReliableSeqNumber;
    };
    uint8_t flags;

    struct NNet_packet_header packet_header;

    NNet_client *client;
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
            uint32_t ReceivedSentTime;
        };
        struct { // send
            uint32_t DataLength;
        };
    };
};

struct NNet_cb_message {
    struct NNet_message *items;

    size_t head;
    size_t tail;
    size_t count;
    size_t capacity;
};

struct da_message {
    struct NNet_message *items;

    size_t capacity;
    size_t count;
};

// -- CLIENT
struct NNet_client {

    struct NNet_cb_message recvMessageBuff;
    struct NNet_cb_message sendMessageBuff;

    struct da_message waitingAck;

    struct sockaddr_in addr;

    uint16_t peerId;
    uint8_t  connexion_state;
};

struct NNet_hm_client {
    uint64_t key;
    struct   NNet_client value;
};

// -- Message brut
struct NNet_message_ack_raw {
    uint32_t ReceivedSentTime;
};

struct NNet_message_send_raw {
    uint32_t DataLength;
    uint8_t  payload[];
};

struct __attribute__((packed)) NNet_message_fragment_raw {
    uint32_t FragmentCount;
    uint32_t FragmentNumber;
    uint32_t TotalLength;
    uint32_t FragmentOffset;
    uint16_t StartSeq;
    uint8_t  payload[];
};

struct __attribute__((packed)) NNet_message_base_raw {
    uint8_t TypeFlags;
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

struct NNet_packet_header_opt_time_raw {
    uint32_t time;
};

struct NNet_packet_header_base_raw {
    uint16_t PeerIDFlags;
    uint16_t MessageCount;
    struct NNet_packet_header_opt_time_raw opt_timeSpent[];
};

// --

struct NNet_context {
    int fd;
    uint16_t isServer;
    uint16_t shouldBlock;

    struct NNet_hm_client *hm_client;

    // struct da_message shared_broadcast_buffer;
};

// --------------------------------------------------
// Function declaration
// --------------------------------------------------

void NNet_Init(NNet_context *ctx);

// @return 0 if all good
// @return BUDGET_HIT if budget hit (BUDGET macro can be defined)
// @return negative number if error
// @error ERROR_SOCKET, ERROR_DATAGRAM_TOO_BIG, ERROR_MESSAGE_PARSING
int NNet_HandleRead(NNet_context *ctx);

// @return 0 if all good
// @return negative number if error
// @error ERROR_FIFO_CAPACITY_EXCEEDED, ERROR_ARRENA_SIZE_EXCEEDED
int NNet_SendMessage(NNet_client *clt, uint8_t *buff, size_t buff_size);

void NNet_HandleSend(NNet_context *ctx);

NNet_context *NNet_CreateDefaultServer();
NNet_context *NNet_CreateDefaultClient(uint32_t addr_network_order);

int NNet_Poll(struct NNet_context *ctx, struct NNet_message *msg_out);

void NNet_clean(NNet_context *ctx);

#endif // __net_H__
