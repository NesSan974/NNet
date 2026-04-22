#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/time.h>

#include <poll.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"

int handleSendMessage(struct cb_message *cb_msg);

int server_accept(struct pollfd *s_pfd);
int setDefaultServerSocket();

struct da_pollfd G_pollfds = {0};

// --------------------------------------------------
// MAIN Function
// --------------------------------------------------

int main(int argc, char **argv) {

    // -- Set-up socket
    int servFd = setDefaultServerSocket();

    if (servFd == SOCK_ERR) {
        perror("impossible to create the socket");
    }

    // -- setup before infinite loop
    {
        struct pollfd server_pfd_item = {.fd = servFd, .events = POLLIN};
        da_append(G_pollfds, server_pfd_item);
    }

    struct pollfd *server_pfd;

    printf("listening on port '%d'\n", PORT);

    // -- start infinite loop
    while (1) {

        // Waiting for incoming message
        if (poll(G_pollfds.items, G_pollfds.count, -1) == -1) {
            perror("error while poll-ing");
        }

        server_pfd = &G_pollfds.items[0];

        // -----------------------
        // Traitement message reçu
        // -----------------------

        int client_fd = server_accept(server_pfd);

        if (client_fd > 0) {

            printf("\nnew connection, fd : '%d'\n", client_fd);

            struct pollfd pfd = {.fd = client_fd, .events = POLLIN, .revents = POLLIN};
            da_append(G_pollfds, pfd);

        } else if (client_fd == -1) {
            fprintf(stderr, "poll error\n");
        } else if (client_fd == -2) {
            perror("error while accepting new client");
        }

        net_handle_io();

        // ------------------------------
        // Traitement des messages queues
        // ------------------------------

        struct message msg;
        while (net_poll(&msg)) {
            printf("packet returned from net_poll()\n");
            printf("recv command %d\n", msg.Command);
        }

        // getc(stdin);
    }

    // TODO De-allocate client da
    return 0;
}

// --------------------

int setDefaultServerSocket() {
    int serv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_fd == SOCK_ERR) {
        return SOCK_ERR;
    }

    setsockopt(serv_fd, SOL_SOCKET, SO_REUSEPORT, &((int){1}), sizeof(int));

    const struct sockaddr_in addr = {
        .sin_family = AF_INET,         /* Famille d'adresses : AF_INET */
        .sin_port = htons(2202),       /* Port dans l'ordre des octets réseau */
        .sin_addr.s_addr = INADDR_ANY, /* Adresse Internet */
    };

    if (bind(serv_fd, (const struct sockaddr *)&addr, sizeof(addr)) == SOCK_ERR) {
        return SOCK_ERR;
    }

    if (listen(serv_fd, BACKLOG) == SOCK_ERR) {
        return SOCK_ERR;
    }

    return serv_fd;
}

/// @brief accept new connexion and add the new client to the G_pollfds
// @param server_pfd if you don't use poll(), you can construct the `struct
// pollfd` with `.fd` equals to the server fd, and `.revents = POLLIN`
// @return the fd of the new connexion, 0 if no new connexion (.revents=0) and
// negative if an error occured
int server_accept(struct pollfd *s_pfd) {

    assert(s_pfd->fd >= 0);

    if (s_pfd->revents == 0) {
        return 0;
    }

    if ((s_pfd->revents & POLLIN) == 0) {
        fprintf(stderr,
                "le ServFd à été trigger par poll(), mais n'est pas en POLLIN\n"
                "\trevent = %d\n",
                s_pfd->revents);
        return -1;
    }

    // udp :
    // ssize_t n = recvfrom(fd, (&(uint8_t){1}), 1, MSG_DONTWAIT | MSG_PEEK,
    // NULL, NULL);

    int client_fd = accept(s_pfd->fd, NULL, NULL);

    if (client_fd == SOCK_ERR) {
        if (client_fd == EWOULDBLOCK || client_fd == EAGAIN) {
            return 0;
        }

        return -2;
    }

    s_pfd->revents = 0;

    return client_fd;
}
