#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <sys/time.h>

#include <poll.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include "net.h"

int handlMessage(struct NNet_cb_message *cb_msg);

// --------------------------------------------------
// MAIN Function
// --------------------------------------------------

int main(int argc, char **argv) {

    // -- Set-up socket
    NNet_context *nnet = NNet_CreateDefaultServer();
    NNet_Init(nnet);

    if (!nnet) {
        fprintf(stderr, "error while creating the socket\n");
        perror("");
        abort();
    }

    printf("listening on port '%d'\n", PORT);

    char file_json[2048];
    FILE *file = fopen("./example.jsonrpc", "r");
    if (file == NULL)
    {
        perror("fopen");
        abort();
    }
    size_t file_read = fread(file_json, 1, 2048, file);
    printf("file_read %ld\n", file_read);



    struct pollfd pfd = {.fd = nnet->fd, .events = POLLIN};

    const int tick_duration_msec = 1000 / 128;
    const int fast_sleep_msec = 1;

    int poll_timeout_msec = tick_duration_msec;

    // -- start infinite loop
    while (1) {

        poll(&pfd, 1, poll_timeout_msec);

        // -----------------------
        // Traitement reseaux
        // -----------------------

        int net_read = NNet_HandleRead(nnet);

        if (net_read < 0) {
            fprintf(stderr, "error while recv-ing() : %d\n", net_read);
            perror("");
        } else {
            poll_timeout_msec = net_read == BUDGET_HIT ? fast_sleep_msec : tick_duration_msec;
        }

        struct NNet_message msg = {0};

        while ( NNet_Poll(nnet, &msg) ) {

            if ( memcmp(msg.payload, "2", msg.DataLength) == 0 )
            {
                printf("\npayload = 2\n");

                printf("addr %x, port %d\n", msg.client->addr.sin_addr.s_addr, ntohs(msg.client->addr.sin_port));

                NNet_SendMessage(msg.client, (uint8_t*)file_json, file_read);
            }
        }

        NNet_HandleSend(nnet);
    }

    NNet_clean(nnet);

    return 0;
}

// --------------------
