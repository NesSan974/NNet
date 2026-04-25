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
    NNet_context *nnet = createDefaultServer();

    if (!nnet) {
        fprintf(stderr, "error while creating the socket\n");
        perror("");
        abort();
    }

    printf("listening on port '%d'\n", PORT);

    // -- start infinite loop
    while (1) {

        // -----------------------
        // Traitement reseaux
        // -----------------------

        while (NNet_HandleIO(nnet) == PACKET_RECEIVED)
            ;

        struct NNet_message msg;
        while (NNet_Poll(&msg, nnet)) {
            printf("packet returned from net_poll()\n");
            printf("recv command %d\n", msg.Command);
        }

        // STUFF
        NNet_SendBuff(nnet);
    }

    NNet_free(nnet);

    return 0;
}

// --------------------
