#include <asm-generic/errno.h>
#include <complex.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define STB_DS_IMPLEMENTATION
#define STBDS_NO_SHORT_NAMES
#include "stb_ds.h"

#include "nes_ds.h"

// #define NDEBUG
#include <assert.h>

#include "arena.h"

// MTU ~= 1500, (sur ADSL en Wi-Fi MTU = 1 468 octets.) auquel on enleve header ip et header udp
// - ip header taille variable jusqu'a max 60octets
// - header udp 8 octets

#define ARENA_DEFAULT_SIZE (1024 * 1024) // Around 750 packets
#define BUDGET_DEFAULT (ARENA_DEFAULT_SIZE / MAX_PKT_SIZE)

#define MAX_PKT_SIZE (1400)

#define MAX_PKT_HEADER_SIZE (6)
#define MIN_PKT_HEADER_SIZE (4)

#define MIN_MSG_HEADER_SIZE (4)
#define MAX_MSG_HEADER_SIZE (32)

#include "net.h"

#if (BUDGET > (ARENA_SIZE / MAX_PKT_SIZE))
#error "the ARENA_SIZE can't have the defined budget, please up the ARENA_SIZE"
#endif

#define POSIX_ERROR (-1)

// --------------------------------------------------
// enum definition
// --------------------------------------------------

enum connexion_state { CS_CONNECTING, CS_ESTABLISHED };

// --------------------------------------------------
// Global variable
// --------------------------------------------------

Arena payload_arena;

// --------------------------------------------------
// Function declaration
// --------------------------------------------------

// Fonction principal

int readPacket(NNet_context *ctx);

// actual reading io

ssize_t readEntireDatagram(NNet_context *ctx, struct sockaddr_in *addr,
                           uint8_t buff_out[static MAX_PKT_SIZE + 1]);

// Serilize / de-serialize function

size_t deserializePacketHeader(unsigned char *buff, struct NNet_packet_header *ph_out);

size_t deserializePacketPayload(unsigned char *packet_payload, size_t packet_payload_size,
                                uint16_t command_count, struct NNet_cb_message *cb_msg_out);

size_t deserializeMessage(unsigned char *buff, size_t buff_size, struct NNet_message *msg_out);
size_t deserializeMessageHeader(unsigned char *buff, size_t buff_size,
                                struct NNet_message *msg_out);

// -

size_t serializePacketHeader(struct NNet_packet_header ph, uint8_t *serialized_data);
size_t serializeMessage(struct NNet_message *msg, uint8_t *buff);

size_t getMessageSize(struct NNet_message *msg, size_t payload_actual_size);

void sendConnect(struct NNet_client *clt);
void sendVerifyConnect(struct NNet_client *clt, uint16_t recv_seq_num);

// Timestamp function

static inline uint64_t now_ms(void);
uint16_t getTimestamp16(void);

// Client

uint16_t getNewPeerId();
static inline uint64_t hash_client(struct sockaddr_in sock_addr);

void addClient(NNet_context *ctx, struct sockaddr_in *addr, uint16_t peerId,
               enum connexion_state state);

size_t buildPacketPayload(struct NNet_cb_message *cb_message,
                          uint8_t buffer[static MAX_PKT_SIZE - MAX_PKT_HEADER_SIZE],
                          uint16_t *command_count);

// -----


int NNet_SendMessage(NNet_client *clt, uint8_t *buff, size_t buff_size) {

    if (clt->sendMessageBuff.count >= clt->sendMessageBuff.capacity) {
        // fprintf(stderr,"Impossible d'ajouter plus de message dans le buffer d'envois\n");
        return ERROR_FIFO_CAPACITY_EXCEEDED;
    }


    if (buff_size > MAX_PKT_SIZE) {
        assert(0 && "sendMEssage(), TODO : fragmenter le paquet");
    }

    struct NNet_message msg = {0};

    msg.Command = MSG_SEND_RELIABLE;
    msg.ChannelID = 0;
    msg.packet_header.PeerID = clt->peerId;
    msg.DataLength = buff_size;

    msg.payload = arena_alloc(&payload_arena, buff_size);
    if (msg.payload == NULL) {
        return ERROR_ARRENA_SIZE_EXCEEDED;
    }

    nesds_cbenqueue(clt->sendMessageBuff, msg);

    memcpy(msg.payload, buff, msg.DataLength);
}

// Copie client data in the global array of client
void addClient(NNet_context *ctx, struct sockaddr_in *addr, uint16_t peerId,
               enum connexion_state state) {

    struct NNet_client clt = {0};

    clt.peerId = peerId;
    clt.connexion_state = (uint8_t)state;
    clt.addr = *addr;

    nesds_cbinit(clt.recvMessageBuff, FIFO_CAPACITY);
    nesds_cbinit(clt.sendMessageBuff, FIFO_CAPACITY);

    assert(clt.recvMessageBuff.items != NULL);
    assert(clt.sendMessageBuff.items != NULL);

    uint64_t client_hmap_key = hash_client(clt.addr);

    stbds_hmput(ctx->hm_client, client_hmap_key, (clt));
}

static inline uint64_t hash_client(struct sockaddr_in sock_addr) {
    uint64_t h = ((uint64_t)sock_addr.sin_addr.s_addr << 16) | (uint64_t)sock_addr.sin_port;
    return h;
}

uint16_t getNewPeerId() {
    static uint16_t last_attributed_peer_id = 0;

    last_attributed_peer_id++;
    last_attributed_peer_id &= (uint16_t)(~PACKET_FLAG_MASK);

    // assert(last_attributed_peer_id > 0 );

    return last_attributed_peer_id == 0 ? ++last_attributed_peer_id : last_attributed_peer_id;
}

static inline uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

uint16_t getTimestamp16(void) {
    static uint64_t start = 0;

    uint64_t t = now_ms();

    if (start == 0)
        start = t;

    return (uint16_t)((t - start) / 10);
}

// @brief read datagram throught the socket context in buff_out
// @return the returned value from recvfrom if ok
// @return error : SOCKET_ERROR and DATAGRAM_TOO_BIG
// @note buff_out will be garbage if the function retrun an error
ssize_t readEntireDatagram(NNet_context *ctx, struct sockaddr_in *addr,
                           uint8_t buff_out[static MAX_PKT_SIZE + 1])
/* clang-format off */
{
    /* clang-format on */

    socklen_t addr_len = sizeof(*addr);

    int flag = ctx->shouldBlock ? 0 : MSG_DONTWAIT;

    ssize_t recv_return_value =
        recvfrom(ctx->fd, buff_out, MAX_PKT_SIZE + 1, flag, (struct sockaddr *)addr, &addr_len);

    // Check there is a problem
    if (recv_return_value == POSIX_ERROR) {
        return ERROR_SOCKET;
    }

    if (recv_return_value > MAX_PKT_SIZE) {
        return ERROR_DATAGRAM_TOO_BIG;
    }

#if DEBUG_LOG >= 1
    printf("[recvfrom] addr : %x:%d, %ld octets \n", addr->sin_addr.s_addr, ntohs(addr->sin_port),
           recv_return_value);
#endif

#ifdef HEX_DUMP
    printf("[read] packet : ");

    for (size_t i = 0; i < recv_return_value; i++) {
        printf("0x%.2x ", *(buff_out + i));
    }
    printf("\n");
#endif // HEX_DUMP
    return recv_return_value;
}

size_t deserializeMessage(unsigned char *raw_data, size_t data_size, struct NNet_message *msg_out) {

    size_t header_size = deserializeMessageHeader(raw_data, data_size, msg_out);

    if (header_size == 0) {
        return 0;
    }

    /* clang-format off */
    #if DEBUG_LOG >= 2
        printf("\t - msg_header_size %ld :\t", header_size);
        printf("command %d, flags %d, channel_id %d, seq_number %d\n", msg_out->Command, msg_out->flags,
            msg_out->ChannelID, msg_out->seq_number);
    #endif
    /* clang-format on */

    size_t message_payload_size = 0;

    switch (msg_out->Command) {

    case MSG_CONNECT:
        break;

    case MSG_VERIFY_CONNECT:
    case MSG_SEND_RELIABLE:
    case MSG_SEND_UNRELIABLE:
    case MSG_SEND_UNSEQUENCED: {

        if (msg_out->DataLength > data_size - header_size) {
            return 0;
        }

        // msg_out->payload = malloc(msg_out->DataLength);

        msg_out->payload = arena_alloc(&payload_arena, msg_out->DataLength);

        assert(msg_out->payload != NULL);

        memcpy(msg_out->payload, raw_data + header_size, msg_out->DataLength);
        message_payload_size += msg_out->DataLength;
    } break;

    case MSG_SEND_UNRELIABLE_FRAGMENT:
    case MSG_SEND_FRAGMENT: {
        ssize_t rest_payload = data_size - header_size;
        message_payload_size += rest_payload;

        if (rest_payload <= 0) {
            return 0;
        }

        printf("Le payload du message est de : '%ld' octet(s)\n", rest_payload);
        assert(1 && "FRAGMENTED ARE NOT IMPLEMENTED YET");
    } break;
    default:
        fprintf(stderr, "'%s' : UNREACHABLE POINT HIT, DROPPING PACKET\n", __FUNCTION__);
        break;
    }

    return header_size + message_payload_size;
}

int NNet_HandleIO(NNet_context *ctx) {

    ssize_t n = 0, i = 0;
    while (++i <= BUDGET && (n = readPacket(ctx)) > 0)
        ;

    if (i > BUDGET) {
        return BUDGET_HIT;
    }

    if (n < 0) {
        if (n == ERROR_SOCKET && errno == EAGAIN) {
            return 0;
        } else {
            return n;
        }
    }

    return 0;
}

/// @brief read the recieved packet, and de-serialize it
// @param ctx
// @return packet size if no error
// @return error : 0, ERROR_SOCKET, ERROR_DATAGRAM_TOO_BIG, ERROR_MESSAGE_PARSING
int readPacket(NNet_context *ctx) {

    // unsigned char *raw_packet = malloc(MAX_PKT_SIZE + 1);
    unsigned char raw_packet[MAX_PKT_SIZE + 1];
    struct sockaddr_in addr;

    ssize_t packet_size = readEntireDatagram(ctx, &addr, raw_packet);

    if (packet_size <= 0) {
        return packet_size;
    }

    /* clang-format off */
    #ifdef PROFILE
        struct timeval start, end;
        gettimeofday(&start, NULL);
    #endif
    /* clang-format on */

    // Parsing the packet header
    struct NNet_packet_header packet_header;
    size_t packet_header_size = deserializePacketHeader(raw_packet, &packet_header);
    assert(packet_header_size <= MAX_PKT_HEADER_SIZE);

    /* clang-format off */
    #if DEBUG_LOG >= 2
        printf("\tpkt_header_size %ld\n", packet_header_size);
    #endif

    #if DEBUG_LOG >= 3
        // Printing stuff
        printf("\t\tcmd_cnt = %d, flags = %d, peer_id = %d ", packet_header.CommandCount,
            packet_header.flags, packet_header.PeerID);

        if (packet_header.flags & PACKET_FLAG_SENT_TIME) {
            printf("timestamp : %d", packet_header.SentTime );
        }
        printf("\n");
    #endif
    /* clang-format on */

    // Si pas de message à traiter, on return
    if (packet_header.CommandCount == 0) {
        return packet_size;
    }

    uint64_t client_hmap_key = hash_client(addr);
    struct NNet_hm_client *hm_client_it = stbds_hmgetp_null(ctx->hm_client, client_hmap_key);

    if (hm_client_it == NULL) {

        /* clang-format off */
        #if DEBUG_LOG >= 2
            printf("\tnouveau client ! key : %ld\n", client_hmap_key);
        #endif
        /* clang-format on */

        addClient(ctx, &addr, 0, CS_CONNECTING);
    }

    struct NNet_client *clt = &ctx->hm_client[stbds_hmgeti(ctx->hm_client, client_hmap_key)].value;
    assert(clt != NULL);

    uint8_t *raw_packet_payload = raw_packet + packet_header_size;

    size_t packet_payload_size =
        deserializePacketPayload(raw_packet_payload, packet_size - packet_header_size,
                                 packet_header.CommandCount, &clt->recvMessageBuff);
    if (packet_payload_size == 0) {
        return ERROR_MESSAGE_PARSING;
    }

    assert(packet_header_size + packet_payload_size == packet_size &&
           "didn't read the all the packet, OR read too much ?");
    // assert(command_it == packet_header.CommandCount &&
    //        "didn't respected the command count from the packet header");

    /* clang-format off */
    #ifdef PROFILE
        gettimeofday(&end, NULL);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        printf("%s() - Temps écoulé : %f secondes\n", __FUNCTION__,  elapsed);
    #endif
    /* clang-format on */

    return packet_size;
}

size_t deserializePacketPayload(unsigned char *packet_payload, size_t packet_payload_size,
                                uint16_t command_count, struct NNet_cb_message *cb_msg_out) {

    size_t payload_cursor = 0;

    for (size_t i = 0; i < command_count && payload_cursor < packet_payload_size; i++) {
        size_t payload_rest = packet_payload_size - payload_cursor;

        if (payload_rest < MIN_MSG_HEADER_SIZE) {
            return 0;
        }

        struct NNet_message msg = {0};
        size_t message_size =
            deserializeMessage(packet_payload + payload_cursor, payload_rest, &msg);

        if (message_size == 0) {
            return 0;
        }

        nesds_cbenqueue(*cb_msg_out, msg);

        payload_cursor += message_size;
    }

    assert(packet_payload_size == payload_cursor);

    return payload_cursor;
}

size_t deserializeMessageHeader(unsigned char *raw_data, size_t data_size,
                                struct NNet_message *msg_out) {

    size_t header_size = 0;

    alignas(struct NNet_message_base_raw) uint8_t internal_buff[MAX_MSG_HEADER_SIZE];
    struct NNet_message_base_raw *read_msg = (struct NNet_message_base_raw *)internal_buff;
    memcpy(read_msg, raw_data, sizeof(*read_msg));

    // -- command & flags
    msg_out->flags = (read_msg->CommandFlags & MESSAGE_FLAG_MASK);
    msg_out->Command = (read_msg->CommandFlags & (uint8_t)(~MESSAGE_FLAG_MASK));

    msg_out->ChannelID = read_msg->ChannelID;

    header_size += sizeof(*read_msg);

    // -- seq_number
    msg_out->seq_number = ntohs(read_msg->seq_number);
    // if (msg_out->Command != MSG_SEND_UNSEQUENCED &&
    //     (msg_out->flags & MESSAGE_FLAG_UNSEQUENCED) == 0) {
    //     // Si command n'est pas 'SEND_UNSEQUENCED' ET qu'il n'y a le flag
    //     // 'MESSAGE_FLAG_UNSEQUENCED'
    //     // alors, il y a un numero de sequence
    //     // TODO ajouter le sizeof de seqnumber ?
    // }

    switch (msg_out->Command) {

    case MSG_ACKNOWLEDGE: {

        struct NNet_message_ack_raw *read_msg_ack;

        if (data_size < header_size + sizeof(*read_msg_ack)) {
            header_size = 0;
            break;
        }

        memcpy(read_msg->message_part2, raw_data + header_size, sizeof(*read_msg_ack));

        read_msg_ack = (struct NNet_message_ack_raw *)(read_msg->message_part2);
        header_size += sizeof(*read_msg_ack);

        msg_out->ReceivedSentTime = ntohs(read_msg_ack->ReceivedSentTime);
    } break;

    case MSG_SEND_RELIABLE:
    case MSG_SEND_UNRELIABLE:
    case MSG_SEND_UNSEQUENCED: {

        struct NNet_message_send_raw *read_msg_send;

        if (data_size < header_size + sizeof(*read_msg_send)) {
            header_size = 0;
            break;
        }

        memcpy(read_msg->message_part2, raw_data + header_size, sizeof(*read_msg_send));

        read_msg_send = (struct NNet_message_send_raw *)(read_msg->message_part2);
        header_size += sizeof(*read_msg_send);

        msg_out->DataLength = ntohl(read_msg_send->DataLength);
    } break;

    case MSG_SEND_UNRELIABLE_FRAGMENT:
    case MSG_SEND_FRAGMENT: {

        struct NNet_message_fragment_raw *read_msg_frgm;
        if (data_size < header_size + sizeof(struct NNet_message_fragment_raw)) {
            header_size = 0;
            break;
        }

        memcpy(read_msg->message_part2, raw_data + header_size, sizeof(*read_msg_frgm));

        read_msg_frgm = (struct NNet_message_fragment_raw *)(read_msg->message_part2);
        header_size += sizeof(*read_msg_frgm);

        msg_out->StartSeq = ntohs(read_msg_frgm->StartSeq);
        msg_out->FragmentCount = ntohl(read_msg_frgm->FragmentCount);
        msg_out->FragmentNumber = ntohl(read_msg_frgm->FragmentNumber);
        msg_out->TotalLength = ntohl(read_msg_frgm->TotalLength);
        msg_out->FragmentOffset = ntohl(read_msg_frgm->FragmentOffset);

    } break;

    case MSG_CONNECT:
    case MSG_VERIFY_CONNECT:
    case MSG_PING:
    case MSG_DISCONNECT:
        break;

    default:
        fprintf(stderr, "'%s' : UNREACHABLE POINT HIT, DROPPING PACKET\n", __FUNCTION__);
        header_size = 0;
        break;
    };

    return header_size;
}

size_t deserializePacketHeader(unsigned char *raw_data, struct NNet_packet_header *ph_out) {

    size_t offset_readed = 0;

    // On memcpy vers un endroit ou on est sûr de l'alignement mémoire
    alignas(struct NNet_packet_header_base_raw) uint8_t internal_buff[MAX_PKT_HEADER_SIZE];
    struct NNet_packet_header_base_raw *packet_header_raw =
        (struct NNet_packet_header_base_raw *)internal_buff;

    memcpy(packet_header_raw, raw_data, MAX_PKT_HEADER_SIZE);

    uint16_t peerIDFlags = ntohs(packet_header_raw->PeerIDFlags);

    ph_out->flags = peerIDFlags & PACKET_FLAG_MASK;
    ph_out->PeerID = peerIDFlags & ~PACKET_FLAG_MASK;
    ph_out->CommandCount = ntohs(packet_header_raw->CommandCount);

    offset_readed += sizeof(*packet_header_raw);

    if (ph_out->flags & PACKET_FLAG_SENT_TIME) {

        ph_out->SentTime = ntohs(packet_header_raw->opt_timeSpent->time);
        offset_readed += sizeof(*packet_header_raw->opt_timeSpent);
    }

    return offset_readed;
}

size_t getMessageSize(struct NNet_message *msg, size_t payload_actual_size) {
    size_t msg_size = sizeof(struct NNet_message_base_raw);

    switch (msg->Command) {
    case MSG_ACKNOWLEDGE:
        msg_size += sizeof(struct NNet_message_ack_raw);
        break;

    case MSG_DISCONNECT:
    case MSG_PING:
        assert(0 && "getMessageSize - CONNECT, VERIFY_CONNECT, DISCONNECT, PING : "
                    "Not implemented yet");
        break;
    case MSG_CONNECT:
    case MSG_VERIFY_CONNECT:
        break;

    case MSG_SEND_RELIABLE:
    case MSG_SEND_UNRELIABLE:
    case MSG_SEND_UNSEQUENCED:
        msg_size += sizeof(struct NNet_message_send_raw) + msg->DataLength;
        break;

    case MSG_SEND_FRAGMENT:
    case MSG_SEND_UNRELIABLE_FRAGMENT: {

        const size_t header_fragment =
            sizeof(struct NNet_message_base_raw) + sizeof(struct NNet_message_fragment_raw);

        msg_size += MAX_PKT_SIZE - MAX_PKT_HEADER_SIZE - payload_actual_size - header_fragment;

        assert(0 && "getMessageSize - SEND_FRAGMENT, SEND_UNRELIABLE_FRAGMENT : Not "
                    "implemented yet");

        // msg_size += sizeof(struct message_fragment_raw) + ???
    } break;

    case MSG_BANDWIDTH_LIMIT:
    case MSG_THROTTLE_CONFIGURE:
        assert(0 && "getMessageSize - BANDWIDTH_LIMIT, THROTTLE_CONFIGURE : Not "
                    "implemented");
        break;
    };

    return msg_size;
}

size_t serializeMessage(struct NNet_message *msg, uint8_t *buff) {

    size_t message_size = 0;

    alignas(struct NNet_message_base_raw) uint8_t internal_buff[MAX_PKT_SIZE];

    struct NNet_message_base_raw *msg_raw = (struct NNet_message_base_raw *)internal_buff;
    message_size += sizeof(*msg_raw);

    msg_raw->CommandFlags = (msg->Command & ~PACKET_FLAG_MASK) | msg->flags;
    msg_raw->ChannelID = msg->ChannelID;

    msg_raw->seq_number = htons(msg->seq_number);

    switch (msg->Command) {

    case MSG_ACKNOWLEDGE: {
        struct NNet_message_ack_raw *msg_ack =
            (struct NNet_message_ack_raw *)msg_raw + message_size;
        message_size += sizeof(*msg_ack);
        msg_ack->ReceivedSentTime = htons(msg->ReceivedSentTime);
        memcpy(buff, msg_raw, message_size);

    } break;

    case MSG_CONNECT:
    case MSG_VERIFY_CONNECT:
        memcpy(buff, msg_raw, message_size);
        break;

    case MSG_DISCONNECT:
    case MSG_PING:
        assert(0 && "sending of CONNECT, VERIFY_CONNECT, DISCONNECT, PING Not "
                    "implemented yet");
        break;

    case MSG_SEND_RELIABLE:
    case MSG_SEND_UNRELIABLE:
    case MSG_SEND_UNSEQUENCED: {

        struct NNet_message_send_raw *msg_send =
            (struct NNet_message_send_raw *)msg_raw->message_part2;
        message_size += sizeof(*msg_send);
        msg_send->DataLength = htonl(msg->DataLength);

        memcpy(buff, msg_raw, message_size);
        memcpy(buff + message_size, msg->payload, msg->DataLength);

        message_size += msg->DataLength;
    } break;

        break;
    case MSG_BANDWIDTH_LIMIT:
    case MSG_THROTTLE_CONFIGURE:
        assert(0 && "sending of CONNECT, VERIFY_CONNECT, DISCONNECT, PING Not "
                    "implemented yet");

        break;
    case MSG_SEND_FRAGMENT:
    case MSG_SEND_UNRELIABLE_FRAGMENT:
        assert(0 && "sending of CONNECT, VERIFY_CONNECT, DISCONNECT, PING Not "
                    "implemented yet");
        break;

    default:
        assert(0 && "UNREACHABLE POINT HIT");
    }

    return message_size;
}

void NNet_SendBuff(NNet_context *ctx) {

    // atm on envois tout le temps le opt sentTime

    /*
           pour gerer le sentTime
           faire un offset d'au moins MAX_HEADER_SIZE
           et construire le packet header juste avant l'envois
    */

    for (size_t client_it = 0; client_it < stbds_hmlen(ctx->hm_client); client_it++) {

        struct NNet_client *clt = &ctx->hm_client[client_it].value;
        // while (should_continue)

        uint16_t command_count;

        do {
            command_count = 0;
            size_t packet_size = 0;

            uint8_t packet[MAX_PKT_SIZE];

            packet_size += buildPacketPayload(&clt->sendMessageBuff, packet + MAX_PKT_HEADER_SIZE,
                                              &command_count);

            if (command_count > 0) {

                size_t packet_header_size = sizeof(struct NNet_packet_header_base_raw);

                struct NNet_packet_header ph = {
                    .CommandCount = command_count, .PeerID = clt->peerId, .flags = 0};

                // TODO c'est un peu beaucoup degueulasse de faire ca ici comme ca
                if (1) {
                    packet_header_size += sizeof(struct NNet_packet_header_opt_time_raw);

                    ph.SentTime = getTimestamp16();
                    ph.flags |= PACKET_FLAG_SENT_TIME;
                }
                if (0) {
                    ph.flags |= PACKET_FLAG_COMPRESSED;
                }

                uint8_t *begin_pkt = packet + MAX_PKT_HEADER_SIZE - packet_header_size;
                packet_size += serializePacketHeader(ph, begin_pkt);

                size_t n = sendto(ctx->fd, begin_pkt, packet_size, 0, (struct sockaddr *)&clt->addr,
                                  sizeof(clt->addr));

#if DEBUG_LOG >= 1
                printf("[sendto] addr : %x:%d, %ld octets\n", clt->addr.sin_addr.s_addr,
                       ntohs(clt->addr.sin_port), n);
#endif
            }

        } while (command_count > 0);
    }

    arena_reset(payload_arena);
}

// prend les messages de cb_message, et les ajoutes dans le buffer en format raw
// @return le nombre de message mis dans le buffer
size_t buildPacketPayload(struct NNet_cb_message *cb_message,
                          uint8_t buffer[static MAX_PKT_SIZE - MAX_PKT_HEADER_SIZE],
                          uint16_t *command_count) {

    uint8_t *packet_payload = buffer;
    size_t pkt_payload_size = 0;

    while (cb_message->count > 0) {

        struct NNet_message *msg_peek = {0};

        nesds_cbpeek(*cb_message, msg_peek);

        size_t msg_size = getMessageSize(msg_peek, pkt_payload_size);

        if (MAX_PKT_HEADER_SIZE + pkt_payload_size + msg_size > MAX_PKT_SIZE) {

            break;
        }

        struct NNet_message msg;
        nesds_cbdequeue((*cb_message), msg);

        size_t msg_raw_wrote = serializeMessage(&msg, packet_payload + pkt_payload_size);
        pkt_payload_size += msg_raw_wrote;

        (*command_count)++;
    }

    return pkt_payload_size;
}

size_t serializePacketHeader(struct NNet_packet_header ph, uint8_t *serialized_data) {

    // Creation du packet header
    alignas(struct NNet_packet_header_base_raw) uint8_t internal_buff[MAX_PKT_HEADER_SIZE];

    struct NNet_packet_header_base_raw *ph_base_raw =
        (struct NNet_packet_header_base_raw *)internal_buff;

    ph_base_raw->PeerIDFlags =
        htons((ph.PeerID & ~PACKET_FLAG_MASK) | (ph.flags & PACKET_FLAG_MASK));

    ph_base_raw->CommandCount = htons(ph.CommandCount);
    int packet_header_size = sizeof(*ph_base_raw);

    if (ph.flags & PACKET_FLAG_SENT_TIME) {
        ph_base_raw->opt_timeSpent->time = htons(ph.SentTime);
        packet_header_size += sizeof(*ph_base_raw->opt_timeSpent);
    }

    // Ajout dans le packet buffer
    memcpy(serialized_data, ph_base_raw, packet_header_size);

    return packet_header_size;
}

int NNet_Poll(struct NNet_context *ctx, struct NNet_message *msg_out) {


    struct NNet_client *clt;

    for (size_t client_it = 0; client_it < stbds_hmlen(ctx->hm_client); client_it++) {


        clt = &ctx->hm_client[client_it].value;

        while (clt->recvMessageBuff.count > 0) {

            nesds_cbdequeue(clt->recvMessageBuff, (*msg_out));

            if (clt->connexion_state == CS_CONNECTING) {
                // if (ctx->isServer && msg_out->Command != ACKNOWLEDGE) {
                //     continue;
                // } else
                if (!ctx->isServer && msg_out->Command != MSG_VERIFY_CONNECT) {
                    continue;
                }
            }



            if (msg_out->flags & MESSAGE_FLAG_ACKNOWLEDGE) {
                struct NNet_message msg2send = {
                    .Command = msg_out->Command,
                    .ChannelID = msg_out->ChannelID,
                    .ReceivedSeqNumber = msg_out->ReliableSeqNumber,
                    .ReceivedSentTime = msg_out->packet_header.SentTime,
                };

                nesds_cbenqueue(clt->sendMessageBuff, msg2send);
            }

            switch (msg_out->Command) {
            case MSG_THROTTLE_CONFIGURE:
            case MSG_BANDWIDTH_LIMIT:
                assert(0 && "THROTTLE_CONFIGURE & BANDWIDTH_LIMIT not implemented");
                break;

            case MSG_CONNECT:
                if (!ctx->isServer) {
                    break;
                }
                clt->peerId = getNewPeerId();
                clt->connexion_state = CS_ESTABLISHED;

                // TODO 22 oui
                sendVerifyConnect(clt, 22);

                break;

            case MSG_VERIFY_CONNECT:
                if (ctx->isServer) {
                    break;
                }
                // TODO !
                // if (msg_out->ReceivedSeqNumber == ?? le seqnum qu'on a envoyer pour le connect) {
                clt->peerId = msg_out->packet_header.PeerID;
                clt->connexion_state = CS_ESTABLISHED;
                // }

                break;

            case MSG_SEND_FRAGMENT:
            case MSG_SEND_UNRELIABLE_FRAGMENT:
                assert(0 && "fragment, not implemented yet");
                break;

            case MSG_ACKNOWLEDGE:
                // on doit verifier la liste des msg devant etre ack
                // clt->waitingForAck // -> linked list or hmap ot fifo ?
                break;

            default:
                return 1;
            }
        }
    }
    // TODO poll pending_clients pour les connects

    return 0;
}

int NNet_CheckRecv(NNet_context *ctx) {

    assert(ctx->fd >= 0);

    int flag = MSG_PEEK | ctx->shouldBlock ? 0 : MSG_DONTWAIT;

    // udp :
    ssize_t n = recvfrom(ctx->fd, (&(uint8_t){1}), 1, flag, NULL, NULL);

    // int client_fd = accept(s_pfd->fd, NULL, NULL);

    return n;
}

void NNet_free(NNet_context *ctx) {

    for (size_t i = 0; i < stbds_hmlen(ctx->hm_client); i++) {

        free(ctx->hm_client[i].value.recvMessageBuff.items);
        free(ctx->hm_client[i].value.sendMessageBuff.items);

        ctx->hm_client[i].value.recvMessageBuff.items = NULL;
        ctx->hm_client[i].value.sendMessageBuff.items = NULL;
    }

    stbds_hmfree(ctx->hm_client);
    arena_destroy(payload_arena);

    free(ctx);
    ctx = NULL;
}

NNet_context *NNet_CreateDefaultServer() {

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == POSIX_ERROR) {
        return NULL;
    }

#ifndef NDEBUG
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &((int){1}), sizeof(int));
#endif

    const struct sockaddr_in addr = {
        .sin_family = AF_INET,         /* Famille d'adresses : AF_INET */
        .sin_port = htons(PORT),       /* Port dans l'ordre des octets réseau */
        .sin_addr.s_addr = INADDR_ANY, /* Adresse Internet */
    };

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) == POSIX_ERROR) {
        return NULL;
    }

    NNet_context *ctx = malloc(sizeof(NNet_context));
    assert(ctx != NULL);
    ctx->isServer = 1;
    ctx->fd = fd;
    ctx->shouldBlock = 0;

    return ctx;
}

void NNet_Init(NNet_context *ctx) { arena_create(payload_arena, ARENA_SIZE); }

NNet_context *NNet_CreateDefaultClient(uint32_t addr_network_order) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,                 /* AF_INET */
        .sin_port = htons(PORT),               /* Port number */
        .sin_addr.s_addr = addr_network_order, /* IPv4 address */
    };

    NNet_context *ctx = malloc(sizeof(NNet_context));
    assert(ctx != NULL);
    ctx->isServer = 0;
    ctx->fd = fd;
    ctx->shouldBlock = 0;

    addClient(ctx, &addr, 0, CS_CONNECTING);

    sendConnect(&ctx->hm_client[0].value);

    NNet_SendBuff(ctx);

    return ctx;
}

void sendConnect(struct NNet_client *clt) {

    struct NNet_message msg = {
        .packet_header.PeerID = 0, .Command = MSG_CONNECT, .ChannelID = 0, .seq_number = 22};

    cbenqueue(clt->sendMessageBuff, msg);
}

void sendVerifyConnect(struct NNet_client *clt, uint16_t recv_seq_num) {
    struct NNet_message msg = {.packet_header.PeerID = clt->peerId,
                               .Command = MSG_VERIFY_CONNECT,
                               .ChannelID = 0,
                               .ReceivedSeqNumber = recv_seq_num};

    cbenqueue(clt->sendMessageBuff, msg);
}
