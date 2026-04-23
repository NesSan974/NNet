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

int handleSendMessage(struct NNet_cb_message *cb_msg);

int setDefaultServerSocket();

// --------------------------------------------------
// MAIN Function
// --------------------------------------------------

int main(int argc, char **argv) {

    // -- Set-up socket
    int servFd = setDefaultServerSocket();

    if (servFd == SOCK_ERR) {
        perror("impossible to create the socket");
    }

    printf("listening on port '%d'\n", PORT);

    // -- start infinite loop
    while (1) {
        // -----------------------
        // Traitement message reçu
        // -----------------------

        int n = NNet_ServerCheckRecv();

        if (n > 0) {

            printf("\nnew packet, fd : '%d'\n", n);

            NNet_HandleIO();

            struct NNet_message msg;
            while (NNet_Poll(&msg)) {
                printf("packet returned from net_poll()\n");
                printf("recv command %d\n", msg.Command);
            }

        } else if (n < 0) {
            fprintf(stderr, "error\n");
        }
    }

    // TODO De-allocate client da

    NNet_free();

    return 0;
}

// --------------------

int setDefaultServerSocket() {
    int serv_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (serv_fd == SOCK_ERR) {
        return SOCK_ERR;
    }

    setsockopt(serv_fd, SOL_SOCKET, SO_REUSEPORT, &((int){1}), sizeof(int));

    const struct sockaddr_in addr = {
        .sin_family = AF_INET,         /* Famille d'adresses : AF_INET */
        .sin_port = htons(PORT),       /* Port dans l'ordre des octets réseau */
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
