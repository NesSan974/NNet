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

// #define NDEBUG
#include <assert.h>

#define STB_DS_IMPLEMENTATION
#define STBDS_NO_SHORT_NAMES
#include "stb_ds.h"

#include "net.h"

struct NNet_hm_client *G_hm_client = {0};

int readPacket(int fd);

ssize_t readPacketHeader(int fd, struct NNet_packet_header *ph_out);
size_t parsePacketHeader(unsigned char *buff, struct NNet_packet_header *ph_out);

size_t parseMessageRaw(unsigned char *buff, size_t buff_size, struct NNet_message *msg_out);
size_t parseMessageHeaderRaw(unsigned char *buff, size_t buff_size, struct NNet_message *msg_out);

ssize_t readEntirePayload(int fd, struct sockaddr_in *addr,
                          uint8_t buff_out[static MAX_PKT_SIZE + 1]);

size_t getMessageSize(struct NNet_message *msg, size_t payload_actual_size);

void handleSendBuff();

static inline uint64_t now_ms(void);
uint16_t get_timestamp16(void);

uint16_t getNewPeerId();

static inline uint64_t hash_client(struct sockaddr_in sock_addr);

// -----

uint16_t getNewPeerId() {
    static uint16_t last_attributed_peer_id = 0;

    last_attributed_peer_id++;
    last_attributed_peer_id &= (uint16_t)(~PACKET_FLAG_MASK);

    // assert(last_attributed_peer_id > 0 );

    return last_attributed_peer_id == 0 ? ++last_attributed_peer_id : last_attributed_peer_id;
}

static inline uint64_t hash_client(struct sockaddr_in sock_addr) {
    uint64_t a = ((uint64_t)sock_addr.sin_addr.s_addr << 16) | (uint64_t)sock_addr.sin_port;
    return a;
}

static inline uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

uint16_t get_timestamp16(void) {
    static uint64_t start = 0;

    uint64_t t = now_ms();

    if (start == 0)
        start = t;

    return (uint16_t)((t - start) / 10);
}

// @note buff_out will be garbage if the function retrun an error
ssize_t readEntirePayload(int fd, struct sockaddr_in *addr,
                          uint8_t buff_out[static MAX_PKT_SIZE + 1])
/* clang-format off */
{
    /* clang-format on */

    socklen_t addr_len = sizeof(*addr);

    // ssize_t recv_return_value =
    //     recvfrom(fd, buff_out, MAX_PKT_SIZE + 1, 0, (struct sockaddr *)addr, &addr_len);

    ssize_t recv_return_value = recvfrom(fd, buff_out, MAX_PKT_SIZE + 1, 0, NULL, NULL);

    // tcp dependant :
    getpeername(fd, (struct sockaddr *)addr, &addr_len);
    // ssize_t recv_return_value = recv(fd, buff_out, MAX_PKT_SIZE + 1, 0);

    // Check there is a problem
    if (recv_return_value == -1) {

        // TCP dependant, flush fd
        while (recvfrom(fd, buff_out, MAX_PKT_SIZE, MSG_DONTWAIT, NULL, NULL) > 0)
            ;

        perror("Error while recv-ing payload");
        return -1;
    }

    if (recv_return_value > MAX_PKT_SIZE) {
        return -2;
    }

    return recv_return_value;
}

size_t parseMessageRaw(unsigned char *buff, size_t buff_size, struct NNet_message *msg_out) {

    size_t header_size = parseMessageHeaderRaw(buff, buff_size, msg_out);

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

        msg_out->payload = malloc(msg_out->DataLength);
        memcpy(msg_out->payload, buff + header_size, msg_out->DataLength);
        message_payload_size += msg_out->DataLength;
    } break;

    case SEND_UNRELIABLE_FRAGMENT:
    case SEND_FRAGMENT: {
        size_t rest_payload = buff_size - header_size;
        message_payload_size += rest_payload;
        printf("Le payload du message est de : '%ld' octet(s)\n", rest_payload);
        assert(1 && "FRAGMENTED ARE NOT IMPLEMENTED YET");
    } break;
    default:
        fprintf(stderr, "Error '%s' : UNREACHABLE POINT HIT\n", __FUNCTION__);
        break;
    }

    return header_size + message_payload_size;
}

/// @brief read the recieved packet, and enqueue message into
// 'incMessageToHandle'
// @param fd fd to recv
// @return false (0) if no update
int readPacket(int fd) {

    unsigned char *recv_buff = malloc(MAX_PKT_SIZE + 1);
    struct sockaddr_in addr;

    ssize_t total_packet_size = readEntirePayload(fd, &addr, recv_buff);

    if (total_packet_size < 0) {
        free(recv_buff);
        return -1;
    }
    // NOTE / TODO : tcp related
    else if (total_packet_size == 0) {
        printf("connexion close from client, fd : '%d'\n", fd);

        // deleting it from pollfds
        G_pollfds.items[index_of_poll(fd)].fd = -1;

        // uint16_t key = ADDR_TO_KEY(addr);
        stbds_hmdel(G_hm_client, hash_client(addr));

        free(recv_buff);
        return 0;
    }

    // Parsing the packet header
    struct NNet_packet_header packet_header;
    size_t packet_header_size = parsePacketHeader(recv_buff, &packet_header);
    assert(packet_header_size <= MAX_PKT_SIZE);

    /* clang-format off */
    #ifndef NDEBUG
        // Printing stuff
        printf("pkt_header_size %ld :\t", packet_header_size);
        printf("cmd_cnt = %d, flags = %d, peer_id = %d \n", packet_header.CommandCount,
            packet_header.flags, packet_header.PeerID);
    #endif
    /* clang-format on */

    // Si pas de message à traiter, on return
    if (packet_header.CommandCount == 0) {
        free(recv_buff);
        return 0;
    }

    uint64_t client_hmap_key = hash_client(addr);
    struct NNet_hm_client *hm_client_it = stbds_hmgetp_null(G_hm_client, client_hmap_key);

    if (hm_client_it == NULL) {

        /* clang-format off */
        #ifndef NDEBUG
            printf("nouveau client ! key : %ld\n", client_hmap_key);
        #endif
        /* clang-format on */

        addClient(&(struct NNet_client){.addr = addr, .peerId = getNewPeerId(), .fd = fd});
    }

    struct NNet_client *clt = &G_hm_client[stbds_hmgeti(G_hm_client, client_hmap_key)].value;

    assert(clt != NULL);

    size_t offset_message_payload = packet_header_size;
    size_t command_it = 0;
    for (command_it = 0;
         command_it < packet_header.CommandCount && offset_message_payload < total_packet_size;
         command_it++) {

        // Parse message

        if (offset_message_payload < MIN_MSG_HEADER_SIZE) {
            fprintf(stderr, "impossible to parse header message, packet too small");
            free(recv_buff);
            return -2;
        }

        struct NNet_message msg = {0};
        size_t parse_size = parseMessageRaw(recv_buff + offset_message_payload,
                                            total_packet_size - offset_message_payload, &msg);

        if (parse_size == 0) {
            fprintf(stderr, "impossible to parse the payload message, packet too small\n");
            free(recv_buff);
            return -2;
        }

        offset_message_payload += parse_size;

        cb_enqueue(clt->recvMessageBuff, msg);
    }

    assert(offset_message_payload == total_packet_size &&
           "didn't read the all the packet, OR read too much ?");
    assert(command_it == packet_header.CommandCount &&
           "didn't respected the command count from the packet header");

    free(recv_buff);
    return 0;
}

void net_handle_io() {

    /* clang-format off */
    #ifndef NDEBUG
        struct timeval start, end;
        gettimeofday(&start, NULL);
    #endif
    /* clang-format on */

    for (size_t i = 0; i < G_pollfds.count; i++) {
        if (G_pollfds.items[i].revents & POLLIN) {

            /* clang-format off */
            #ifndef NDEBUG
                printf("\nnew message from '%d'\n", G_pollfds.items[i].fd);
            #endif
            /* clang-format on */

            readPacket(G_pollfds.items[i].fd);
        }
    }

    /* clang-format off */
    #ifndef NDEBUG
        gettimeofday(&end, NULL);
        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        printf("Temps écoulé : %f secondes\n", elapsed);
    #endif
    /* clang-format on */
}

size_t parseMessageHeaderRaw(unsigned char *message_buff, size_t buff_size,
                             struct NNet_message *msg_out) {

    size_t actual_header_size = 0;

    alignas(struct NNet_message_base_raw) uint8_t internal_buff[MAX_MSG_HEADER_SIZE];
    struct NNet_message_base_raw *read_msg = (struct NNet_message_base_raw *)internal_buff;
    memcpy(read_msg, message_buff, sizeof(*read_msg));

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

        // TODO memcpy quand on aura dispatch le seq number
        msg_out->seq_number = ntohs(read_msg->seq_number);
    }

    switch (msg_out->Command) {

    case ACKNOWLEDGE: {

        struct NNet_message_ack_raw *read_msg_ack;

        if (buff_size < actual_header_size + sizeof(*read_msg_ack)) {
            actual_header_size = 0;
            break;
        }

        memcpy(read_msg->message_part2, message_buff + actual_header_size, sizeof(*read_msg_ack));

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

        if (buff_size < actual_header_size + sizeof(*read_msg_send)) {
            actual_header_size = 0;
            break;
        }

        memcpy(read_msg->message_part2, message_buff + actual_header_size, sizeof(*read_msg_send));

        read_msg_send = (struct NNet_message_send_raw *)(read_msg->message_part2);
        actual_header_size += sizeof(*read_msg_send);

        msg_out->DataLength = ntohl(read_msg_send->DataLength);
    } break;

    case SEND_UNRELIABLE_FRAGMENT:
    case SEND_FRAGMENT: {

        struct NNet_message_fragment_raw *read_msg_frgm;
        if (buff_size < actual_header_size + sizeof(struct NNet_message_fragment_raw)) {
            actual_header_size = 0;
            break;
        }

        memcpy(read_msg->message_part2, message_buff + actual_header_size, sizeof(*read_msg_frgm));

        read_msg_frgm = (struct NNet_message_fragment_raw *)(read_msg->message_part2);
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

size_t parsePacketHeader(unsigned char *packet_buff, struct NNet_packet_header *ph_out) {

    size_t offset_readed = 0;

    // On memcpy vers un endroit ou on est sûr de l'alignement mémoire
    alignas(struct NNet_packet_header_base_raw) uint8_t internal_buff[MAX_PKT_HEADER_SIZE];
    struct NNet_packet_header_base_raw *packet_header_raw =
        (struct NNet_packet_header_base_raw *)internal_buff;
    memcpy(packet_header_raw, packet_buff, sizeof(*packet_header_raw));

    ph_out->flags = (packet_header_raw->PeerIDFlags & PACKET_FLAG_MASK);
    ph_out->PeerID = ntohs(packet_header_raw->PeerIDFlags & (uint16_t)(~PACKET_FLAG_MASK));
    ph_out->CommandCount = ntohs(packet_header_raw->CommandCount);

    offset_readed += sizeof(*packet_header_raw);

    if (ph_out->flags & PACKET_FLAG_SENT_TIME) {
        memcpy(packet_header_raw->opt_timeSpent, packet_buff + offset_readed,
               sizeof(*packet_header_raw->opt_timeSpent));

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

size_t addMessageToPacketRaw(uint8_t *buff, struct NNet_message *msg) {

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

void handleSendBuff() {

    printf("%s()\n", __FUNCTION__);
    // atm on envois tout le temps le opt sentTime
    const size_t packet_header_size =
        sizeof(struct NNet_packet_header_base_raw) + sizeof(struct NNet_packet_header_opt_time_raw);
    /*
           pour gerer le sentTime
           faire un offset d'au moins MAX_HEADER_SIZE
           et construire le packet header juste avant l'envois
    */
    struct NNet_client *clt = {0};

    for (size_t client_it = 0; client_it < stbds_hmlen(G_hm_client); client_it++) {

        int should_continue = 1;
        uint16_t command_count = 0;
        clt = &G_hm_client[client_it].value;
        printf("clt.fd %d\n", clt->fd);
        printf("clt->sendMessageBuff.count %ld\n", clt->sendMessageBuff.count);

        while (should_continue) {
            command_count = 0;
            should_continue = 0;

            uint8_t *packet_buffer = malloc(MAX_PKT_SIZE);
            uint8_t *packet_payload = packet_buffer + MAX_PKT_HEADER_SIZE;
            int pkt_payload_actual_size = 0;

            while (clt->sendMessageBuff.count > 0) {
                printf("\tsendMessageBuff.count > 0\n");

                struct NNet_message msg_peek = {0};
                cb_peek(clt->sendMessageBuff, msg_peek);

                size_t msg_size = getMessageSize(&msg_peek, pkt_payload_actual_size);

                if (MAX_PKT_HEADER_SIZE + pkt_payload_actual_size + msg_size > MAX_PKT_SIZE) {

                    if (msg_peek.Command == SEND_RELIABLE || msg_peek.Command == SEND_UNSEQUENCED ||
                        msg_peek.Command == SEND_UNRELIABLE) {

                        const size_t send_msg_header_size = sizeof(struct NNet_message_base_raw) +
                                                            sizeof(struct NNet_message_send_raw);

                        if (msg_size > MAX_PKT_SIZE - MAX_PKT_HEADER_SIZE - send_msg_header_size) {
                            // créer les fragments
                            // remplacer le send actuel par un fragment
                            // ajouter le fragment dans le packet_buffer
                            // ajouter les fragment dans le sendBuffer OU les envoyer direct
                            // ?

                            assert(1 && "not implemented the fragment send yet");
                        }
                    }
                    should_continue = 1;
                    break;
                }

                struct NNet_message msg;
                cb_dequeue(clt->sendMessageBuff, msg);

                size_t msg_raw_wrote =
                    addMessageToPacketRaw(packet_payload + pkt_payload_actual_size, &msg);
                pkt_payload_actual_size += msg_raw_wrote;
                command_count++;
                printf("\tcommand_count++\n");
            }

            if (command_count > 0) {

                printf("\tsend a packeet\n");

                // Creation du packet header
                alignas(struct NNet_packet_header_base_raw) uint8_t b[MAX_PKT_HEADER_SIZE];

                struct NNet_packet_header_base_raw *phraw = (struct NNet_packet_header_base_raw *)b;
                phraw->CommandCount = htons(command_count);
                phraw->PeerIDFlags = htons(clt->peerId) | PACKET_FLAG_SENT_TIME;
                phraw->opt_timeSpent->time = get_timestamp16();

                // Ajout dans le packet buffer
                uint8_t *begin_pkt_header =
                    packet_buffer + MAX_PKT_HEADER_SIZE - packet_header_size;
                memcpy(begin_pkt_header, phraw, packet_header_size);

                // envois
                size_t total_pkt_size = packet_header_size + pkt_payload_actual_size;
                send(clt->fd, begin_pkt_header, total_pkt_size, 0);
            }

            free(packet_buffer);
        }
    }
}

int net_poll(struct NNet_message *msg_out) {
    struct NNet_client *clt;
    for (size_t client_it = 0; client_it < stbds_hmlen(G_hm_client); client_it++) {

        clt = &G_hm_client[client_it].value;

        while (clt->recvMessageBuff.count > 0) {

            cb_dequeue(clt->recvMessageBuff, (*msg_out));

            if (msg_out->flags & MESSAGE_FLAG_ACKNOWLEDGE) {
                struct NNet_message msg2send = {
                    .Command = msg_out->Command,
                    .ChannelID = msg_out->ChannelID,
                    .ReceivedSeqNumber = msg_out->seq_number,
                    .ReceivedSentTime = msg_out->packet_header.SentTime,
                };
                printf("on enqueu du ack ici ------\n");
                printf("clt fd : %d\n", clt->fd);
                printf("clt->sendMessageBuff.count %ld\n", clt->sendMessageBuff.count);
                cb_enqueue(clt->sendMessageBuff, msg2send);
                printf("clt->sendMessageBuff.count %ld\n", clt->sendMessageBuff.count);
            }

            switch (msg_out->Command) {
            case CONNECT:
            case THROTTLE_CONFIGURE:
            case BANDWIDTH_LIMIT:
            case VERIFY_CONNECT:
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

    handleSendBuff();
    return 0;
}

ssize_t index_of_poll(int fd) {

    for (size_t i = 0; i < G_pollfds.count; i++) {
        if (G_pollfds.items[i].fd == fd) {
            return i;
        }
    }

    return -1;
}

// Copie client data in the global array of client
void addClient(struct NNet_client *clt) {

    cb_init(clt->recvMessageBuff, 100);
    cb_init(clt->sendMessageBuff, 100);

    assert(clt->recvMessageBuff.items != NULL);
    assert(clt->sendMessageBuff.items != NULL);

    uint64_t client_hmap_key = hash_client(clt->addr);

    stbds_hmput(G_hm_client, client_hmap_key, (*clt));
}
