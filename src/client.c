
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <assert.h>

#include "net.h"


int main(int argc, char **argv) {

    // uint32_t ip_add;
    // inet_pton(AF_INET, "192.168.2.2", &ip_add);

    NNet_context *ctx = NNet_CreateDefaultClient(htonl(INADDR_LOOPBACK));
    NNet_Init(ctx);

    struct pollfd pfd = {.fd = ctx->fd, .events = POLLIN};
    size_t i;

    char buff[16];
    int a = 0;

    for (i = 0; i < 64 * 300 && a >= 0; i++) {
        sprintf(buff, "%ld", i++);
        a = NNet_SendMessage(&ctx->hm_client[0].value, (uint8_t *)buff, strlen(buff));
        // printf("NNet_SendMessage(&ctx->hm_client[0].value, (uint8_t *)buff, strlen(buff))");
    }

    while (1) {

        poll(&pfd, 1, 1000);

        int n = NNet_HandleRead(ctx);

        if (n < 0) {
            fprintf(stderr, "error while recv-ing()\n");
            perror("");
        }

        struct NNet_message msg = {0};

        size_t aze=0;
        while ((aze = NNet_Poll(ctx, &msg))) {

            printf("-----msg recv----\n");
            for (int i = 0; i < msg.DataLength; i++) {
                printf("msg from %x:%d", msg.client->addr.sin_addr.s_addr, ntohs(msg.client->addr.sin_port));
                printf("%c", msg.payload[i]);
            }
            printf("\n-----------------\n");

        }



        NNet_HandleSend(ctx);
    }

    return 0;
}
