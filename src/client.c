
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <assert.h>

#include "net.h"

int main(int argc, char **argv) {

    // uint32_t ip_add;
    // inet_pton(AF_INET, "192.168.1.130", &ip_add);

    NNet_context *ctx = NNet_CreateDefaultClient(htonl(INADDR_LOOPBACK));

    NNet_Init(ctx);

    struct pollfd pfd = {.fd = ctx->fd, .events = POLLIN};

    while (1) {

        poll(&pfd, 1, 1000);

        int n = NNet_HandleIO(ctx);

        if (n < 0) {
            fprintf(stderr, "error while recv-ing()\n");
            perror("");
        }

        struct NNet_message msg = {0};
        while (NNet_Poll(ctx, &msg)) {
        }

        char buff[50];
        for (size_t i = 0; i < 213; i++) {
            sprintf(buff, "message n°%ld - j'adore les pastabox avec du sucre", i);
            int a = NNet_SendMessage(&ctx->hm_client[0].value, (uint8_t *)buff, strlen(buff));
        }

        NNet_SendBuff(ctx);
    }

    return 0;
}
