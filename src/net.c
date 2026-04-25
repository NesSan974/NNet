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

#define NDEBUG
#include <assert.h>

#include "net.h"

#define PROFILE

#define POSIX_ERROR (-1)


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

// Timestamp function

static inline uint64_t now_ms(void);
uint16_t getTimestamp16(void);

// Client

uint16_t getNewPeerId();
static inline uint64_t hash_client(struct sockaddr_in sock_addr);
void addClient(NNet_context *ctx, struct NNet_client *clt);

uint8_t buildPacketPayload(struct NNet_cb_message *cb_message,
                           uint8_t buffer[static MAX_PKT_SIZE - MAX_PKT_HEADER_SIZE],
                           uint16_t *command_count);

void sendPacket(uint8_t *packet, size_t packet_payload_size, int fd, uint16_t peerId,
                uint16_t command_count, uint16_t flag);
// -----

// Copie client data in the global array of client
void addClient(NNet_context *ctx, struct NNet_client *clt) {

    cb_init(clt->recvMessageBuff, 100);
    cb_init(clt->sendMessageBuff, 100);

    assert(clt->recvMessageBuff.items != NULL);
    assert(clt->sendMessageBuff.items != NULL);

    uint64_t client_hmap_key = hash_client(clt->addr);

    stbds_hmput(ctx->hm_client, client_hmap_key, (*clt));
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
        // perror("Error while recv-ing payload");
        return ERROR_SOCKET;
    }

    if (recv_return_value > MAX_PKT_SIZE) {
        return ERROR_DATAGRAM_TOO_BIG;
    }

    return recv_return_value;
}

size_t deserializeMessage(unsigned char *raw_data, size_t data_size, struct NNet_message *msg_out) {

    size_t header_size = deserializeMessageHeader(raw_data, data_size, msg_out);

    if (header_size == 0) {
        return 0;
    }

    /* clang-format off */
    #ifndef NDEBUG
        printf("msg_header_size %ld :\t", header_size);
        printf("command %d, flags %d, channel_id %d, seq_number %d\n", msg_out->Command, msg_out->flags,
            msg_out->ChannelID, msg_out->seq_number);
    #endif
    /* clang-format on */

    size_t message_payload_size = 0;

    switch (msg_out->Command) {

    case CONNECT:
    case VERIFY_CONNECT:
    case SEND_RELIABLE:
    case SEND_UNRELIABLE:
    case SEND_UNSEQUENCED: {

        /* clang-format off */
        #ifndef NDEBUG
            printf("datalen : %d\n", msg_out->DataLength);
        #endif
        /* clang-format on */

        if (msg_out->DataLength > data_size - header_size) {
            return 0;
        }

        msg_out->payload = malloc(msg_out->DataLength);
        memcpy(msg_out->payload, raw_data + header_size, msg_out->DataLength);
        message_payload_size += msg_out->DataLength;
    } break;

    case SEND_UNRELIABLE_FRAGMENT:
    case SEND_FRAGMENT: {
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

    ssize_t n = readPacket(ctx);

    if (n < 0) {
        return n;
    }

    return PACKET_RECEIVED;
}

/// @brief read the recieved packet, and enqueue message to buffer
// @param fd fd to recv
// @return false (0) if no update
int readPacket(NNet_context *ctx) {

    // unsigned char *raw_packet = malloc(MAX_PKT_SIZE + 1);
    unsigned char raw_packet[MAX_PKT_SIZE + 1];
    struct sockaddr_in addr;

    ssize_t packet_size = readEntireDatagram(ctx, &addr, raw_packet);

    printf("packet_size %ld\n", packet_size);
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
    #ifndef NDEBUG
        // Printing stuff
        printf("pkt_header_size %ld :\t", packet_header_size);
        printf("cmd_cnt = %d, flags = %d, peer_id = %d \n", packet_header.CommandCount,
            packet_header.flags, packet_header.PeerID);

        if (packet_header.flags & PACKET_FLAG_SENT_TIME) {
            printf("timestamp : %d\n", packet_header.SentTime );
        }
        printf("\n");
    #endif
    /* clang-format on */

    // Si pas de message à traiter, on return
    if (packet_header.CommandCount == 0) {
        return 0;
    }

    // NOTE : Est-ce que je dois traiter connect et verify connect ici ?
    // si non, je dois créé un client peut importe le message,
    // si oui, pourquoi on traiterai verfyconnect et pas ack ?

    uint64_t client_hmap_key = hash_client(addr);
    struct NNet_hm_client *hm_client_it = stbds_hmgetp_null(ctx->hm_client, client_hmap_key);

    if (hm_client_it == NULL) {

        /* clang-format off */
        #ifndef NDEBUG
            printf("nouveau client ! key : %ld\n", client_hmap_key);
        #endif
        /* clang-format on */

        addClient(ctx,
                  &(struct NNet_client){.addr = addr, .peerId = getNewPeerId(), .fd = ctx->fd});
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

    return 0;
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

        cb_enqueue(*cb_msg_out, msg);
        payload_cursor += message_size;
    }

    assert(packet_payload_size == payload_cursor);

    return payload_cursor;
}

size_t deserializeMessageHeader(unsigned char *raw_data, size_t data_size,
                                struct NNet_message *msg_out) {

    size_t actual_header_size = 0;

    alignas(struct NNet_message_base_raw) uint8_t internal_buff[MAX_MSG_HEADER_SIZE];
    struct NNet_message_base_raw *read_msg = (struct NNet_message_base_raw *)internal_buff;
    memcpy(read_msg, raw_data, sizeof(*read_msg));

    // -- command & flags
    msg_out->flags = (read_msg->CommandFlags & MESSAGE_FLAG_MASK);
    msg_out->Command = (read_msg->CommandFlags & (uint16_t)(~MESSAGE_FLAG_MASK));

    msg_out->ChannelID = read_msg->ChannelID;

    actual_header_size += sizeof(*read_msg);

    // -- seq_number
    if (msg_out->Command != SEND_UNSEQUENCED && (msg_out->flags & MESSAGE_FLAG_UNSEQUENCED) == 0) {
        // Si command n'est pas 'SEND_UNSEQUENCED' ET qu'il n'y a le flag
        // 'MESSAGE_FLAG_UNSEQUENCED'
        // alors, il y a un numero de sequence

        msg_out->seq_number = ntohs(read_msg->seq_number);
        // TODO ajouter le sizeof de seqnumber ?
    }

    switch (msg_out->Command) {

    case ACKNOWLEDGE: {

        struct NNet_message_ack_raw *read_msg_ack;

        if (data_size < actual_header_size + sizeof(*read_msg_ack)) {
            actual_header_size = 0;
            break;
        }

        memcpy(read_msg->message_part2, raw_data + actual_header_size, sizeof(*read_msg_ack));

        read_msg_ack = (struct NNet_message_ack_raw *)(read_msg->message_part2);
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

        struct NNet_message_send_raw *read_msg_send;

        if (data_size < actual_header_size + sizeof(*read_msg_send)) {
            actual_header_size = 0;
            break;
        }

        memcpy(read_msg->message_part2, raw_data + actual_header_size, sizeof(*read_msg_send));

        read_msg_send = (struct NNet_message_send_raw *)(read_msg->message_part2);
        actual_header_size += sizeof(*read_msg_send);

        msg_out->DataLength = ntohl(read_msg_send->DataLength);
    } break;

    case SEND_UNRELIABLE_FRAGMENT:
    case SEND_FRAGMENT: {

        struct NNet_message_fragment_raw *read_msg_frgm;
        if (data_size < actual_header_size + sizeof(struct NNet_message_fragment_raw)) {
            actual_header_size = 0;
            break;
        }

        memcpy(read_msg->message_part2, raw_data + actual_header_size, sizeof(*read_msg_frgm));

        read_msg_frgm = (struct NNet_message_fragment_raw *)(read_msg->message_part2);
        actual_header_size += sizeof(*read_msg_frgm);

        msg_out->StartSeq = ntohs(read_msg_frgm->StartSeq);
        msg_out->FragmentCount = ntohl(read_msg_frgm->FragmentCount);
        msg_out->FragmentNumber = ntohl(read_msg_frgm->FragmentNumber);
        msg_out->TotalLength = ntohl(read_msg_frgm->TotalLength);
        msg_out->FragmentOffset = ntohl(read_msg_frgm->FragmentOffset);

    } break;

    case PING:
        break;
    case DISCONNECT:
        break;

    default:
        fprintf(stderr, "'%s' : UNREACHABLE POINT HIT, DROPPING PACKET\n", __FUNCTION__);
        actual_header_size = 0;
        break;
    };

    return actual_header_size;
}

size_t deserializePacketHeader(unsigned char *raw_data, struct NNet_packet_header *ph_out) {

    size_t offset_readed = 0;

    // On memcpy vers un endroit ou on est sûr de l'alignement mémoire
    alignas(struct NNet_packet_header_base_raw) uint8_t internal_buff[MAX_PKT_HEADER_SIZE];
    struct NNet_packet_header_base_raw *packet_header_raw =
        (struct NNet_packet_header_base_raw *)internal_buff;
    memcpy(packet_header_raw, raw_data, MAX_PKT_HEADER_SIZE);

    ph_out->flags = (packet_header_raw->PeerIDFlags & PACKET_FLAG_MASK);
    ph_out->PeerID = ntohs(packet_header_raw->PeerIDFlags & (uint16_t)(~PACKET_FLAG_MASK));
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
    case ACKNOWLEDGE:
        msg_size += sizeof(struct NNet_message_ack_raw);
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
        msg_size += sizeof(struct NNet_message_send_raw) + msg->DataLength;
        break;

    case SEND_FRAGMENT:
    case SEND_UNRELIABLE_FRAGMENT: {

        const size_t header_fragment =
            sizeof(struct NNet_message_base_raw) + sizeof(struct NNet_message_fragment_raw);

        msg_size += MAX_PKT_SIZE - MAX_PKT_HEADER_SIZE - payload_actual_size - header_fragment;

        assert(0 && "getMessageSize - SEND_FRAGMENT, SEND_UNRELIABLE_FRAGMENT : Not "
                    "implemented yet");

        // msg_size += sizeof(struct message_fragment_raw) + ???
    } break;

    case BANDWIDTH_LIMIT:
    case THROTTLE_CONFIGURE:
        assert(0 && "getMessageSize - BANDWIDTH_LIMIT, THROTTLE_CONFIGURE : Not "
                    "implemented");
        break;
    };

    return msg_size;
}

size_t serializeMessage(struct NNet_message *msg, uint8_t *buff) {

    size_t header_size = 0;

    alignas(struct NNet_message_base_raw) uint8_t internal_buff[MAX_MSG_HEADER_SIZE];

    struct NNet_message_base_raw *msg_raw = (struct NNet_message_base_raw *)internal_buff;
    header_size += sizeof(*msg_raw);

    msg_raw->CommandFlags = msg->Command | msg->flags;
    msg_raw->ChannelID = msg->ChannelID;

    msg_raw->seq_number = htons(msg->seq_number);

    switch (msg->Command) {

    case ACKNOWLEDGE: {
        struct NNet_message_ack_raw *msg_ack = (struct NNet_message_ack_raw *)msg_raw + header_size;
        header_size += sizeof(*msg_ack);
        msg_ack->ReceivedSentTime = htons(msg->ReceivedSentTime);
        memcpy(buff, msg_raw, header_size);

    } break;

    case CONNECT:
    case VERIFY_CONNECT:
    case DISCONNECT:
    case PING:
        assert(0 && "sending of CONNECT, VERIFY_CONNECT, DISCONNECT, PING Not "
                    "implemented yet");
        break;

    case SEND_RELIABLE:
    case SEND_UNRELIABLE:
    case SEND_UNSEQUENCED: {
        struct NNet_message_send_raw *msg_send =
            (struct NNet_message_send_raw *)msg_raw + header_size;
        header_size += sizeof(*msg_send);
        msg_send->DataLength = htons(msg->ReceivedSentTime);

        memcpy(buff, msg_raw, header_size);
        memcpy(buff + header_size, msg->payload, msg_send->DataLength);
        free(msg->payload);
    } break;

        break;
    case BANDWIDTH_LIMIT:
    case THROTTLE_CONFIGURE:
        assert(0 && "sending of CONNECT, VERIFY_CONNECT, DISCONNECT, PING Not "
                    "implemented yet");

        break;
    case SEND_FRAGMENT:
    case SEND_UNRELIABLE_FRAGMENT:
        assert(0 && "sending of CONNECT, VERIFY_CONNECT, DISCONNECT, PING Not "
                    "implemented yet");
        break;

    default:
        assert(0 && "UNREACHABLE POINT HIT");
    }

    return header_size;
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

            while (clt->sendMessageBuff.count > 0) {

                packet_size += buildPacketPayload(&clt->sendMessageBuff,
                                                  packet + MAX_PKT_HEADER_SIZE, &command_count);
            }

            if (command_count > 0) {

                size_t packet_header_size = sizeof(struct NNet_packet_header_base_raw);

                struct NNet_packet_header ph = {
                    .CommandCount = command_count, .PeerID = clt->peerId, .flags = 0};

                // TODO c'est un peu beaucoup degueulasse de faire caici comme ca
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

                sendto(ctx->fd, begin_pkt, packet_size, 0, (struct sockaddr *)&clt->addr,
                       sizeof(clt->addr));
            }

        } while (command_count > 0);
    }
}

// prend les messages de cb_message, et les ajoutes dans le buffer en format raw
// @return le nombre de message mis dans le buffer
uint8_t buildPacketPayload(struct NNet_cb_message *cb_message,
                           uint8_t buffer[static MAX_PKT_SIZE - MAX_PKT_HEADER_SIZE],
                           uint16_t *command_count) {

    uint8_t *packet_payload = buffer;
    int pkt_payload_size = 0;

    while (cb_message->count > 0) {

        struct NNet_message *msg_peek = {0};

        cb_peek(*cb_message, msg_peek);

        size_t msg_size = getMessageSize(msg_peek, pkt_payload_size);

        if (MAX_PKT_HEADER_SIZE + pkt_payload_size + msg_size > MAX_PKT_SIZE) {

            if (msg_peek->Command == SEND_RELIABLE || msg_peek->Command == SEND_UNSEQUENCED ||
                msg_peek->Command == SEND_UNRELIABLE) {

                const size_t send_msg_header_size =
                    sizeof(struct NNet_message_base_raw) + sizeof(struct NNet_message_send_raw);

                if (msg_size > MAX_PKT_SIZE - MAX_PKT_HEADER_SIZE - send_msg_header_size) {
                    // créer les fragments
                    // remplacer le send actuel par un fragment
                    // ajouter le fragment dans le packet_buffer
                    // ajouter les fragment dans le sendBuffer OU les envoyer direct
                    // ?

                    assert(1 && "message too big, not implemented the fragment send yet");
                }
            }
            break;
        }

        struct NNet_message msg;
        cb_dequeue((*cb_message), msg);

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

    ph_base_raw->PeerIDFlags = htons(ph.PeerID) | ph.flags;
    ph_base_raw->CommandCount = htons(ph.CommandCount);
    int packet_header_size = sizeof(*ph_base_raw);

    if (ph.flags & PACKET_FLAG_SENT_TIME) {
        ph_base_raw->opt_timeSpent->time = ph.SentTime;
        packet_header_size += sizeof(*ph_base_raw->opt_timeSpent);
    }

    // Ajout dans le packet buffer
    memcpy(serialized_data, ph_base_raw, packet_header_size);

    return packet_header_size;
}

int NNet_Poll(struct NNet_message *msg_out, struct NNet_context *ctx) {

    struct NNet_client *clt;
    for (size_t client_it = 0; client_it < stbds_hmlen(ctx->hm_client); client_it++) {

        clt = &ctx->hm_client[client_it].value;

        while (clt->recvMessageBuff.count > 0) {

            cb_dequeue(clt->recvMessageBuff, (*msg_out));

            if (msg_out->flags & MESSAGE_FLAG_ACKNOWLEDGE) {
                struct NNet_message msg2send = {
                    .Command = msg_out->Command,
                    .ChannelID = msg_out->ChannelID,
                    .ReceivedSeqNumber = msg_out->seq_number,
                    .ReceivedSentTime = msg_out->packet_header.SentTime,
                };
                cb_enqueue(clt->sendMessageBuff, msg2send);
            }

            switch (msg_out->Command) {
            case THROTTLE_CONFIGURE:
            case BANDWIDTH_LIMIT:
                assert(0 && "THROTTLE_CONFIGURE & BANDWIDTH_LIMIT not implemented");

                break;
            case CONNECT:
                if (!ctx->isServer) {
                    break;
                }
                assert(0 && "TODO : implement CONNECT here");

            case VERIFY_CONNECT:
                if (ctx->isServer) {
                    break;
                }
                assert(0 && "TODO : implement VERIFY_CONNECT here");

                break;

            case SEND_FRAGMENT:
            case SEND_UNRELIABLE_FRAGMENT:
                assert(0 && "fragment, not implemented yet");
                break;

            case ACKNOWLEDGE:
                // on doit verifier la liste des msg devant etre ack
                // clt->waitingForAck // -> linked list or hmap ?
                break;

            default:
                return 1;
            }
        }
    }
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

    free(ctx);
    ctx = NULL;
}

NNet_context *createDefaultServer() {

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == POSIX_ERROR) {
        return NULL;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &((int){1}), sizeof(int));

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
    ctx->shouldBlock = 1;

    return ctx;
}
