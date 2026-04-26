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

        int n = NNet_HandleIO(nnet);

        if (n < 0) {
            fprintf(stderr, "error while recv-ing() : %d\n", n);
            perror("");
        } else {
            poll_timeout_msec = n == BUDGET_HIT ? fast_sleep_msec : tick_duration_msec;
        }

        struct NNet_message msg;
        while (NNet_Poll(nnet, &msg)) {
            if (msg.ChannelID == 0) {
                for (int i = 0; i < msg.DataLength; i++) {
                    printf("%c", msg.payload[i]);
                }
                printf("\n");
            }
        }

        // STUFF

        NNet_SendBuff(nnet);
    }

    NNet_free(nnet);

    return 0;
}

// --------------------
