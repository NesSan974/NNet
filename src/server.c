#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <poll.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"

int handleRecvMessage();
int handleSendMessage();
int setThisFuckinkSocket();

struct cb_message recvMessageBuff;
struct cb_message sendMessageBuff;
struct da_client da_client = {0};
struct da_pollfd pollfds = {0};

int servFd = -1;

// --------------------------------------------------
// MAIN Function
// --------------------------------------------------

int main(int argc, char **argv) {

  // -- Set-up socket
  int servFd = setThisFuckinkSocket();

  // -- setup before infinite loop
  struct pollfd pfd = {.fd = servFd, .events = POLLIN};
  da_append(pollfds, pfd);

  printf("listening on port '%d'\n", PORT);

  // -- start infinite loop
  while (1) {

    // Waiting for incoming message
    if (poll(pollfds.items, pollfds.count, -1) == -1) {
      perror("error whil poll-ing");
    }

    server_accept();

    // -----------------------
    // Traitement message reçu
    // -----------------------

    read_and_enqueue_message();

    // TODO on traitera par batch plus tard, atm, on traite tant qu'il y a à
    // traiter
    while (handleRecvMessage())
      ;

    // -----------------------
    // Traitement message à envoyer
    // -----------------------
    // TODO on traitera par batch plus tard, atm, on traite tant qu'il y a à
    // traiter
    while (handleSendMessage())
      ;
  }

  return 0;
}

// --------------------

int setThisFuckinkSocket() {
  int serv_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (serv_fd == SOCK_ERR) {
    perror("impossible to create socket");
    return SOCK_ERR;
  }

  setsockopt(serv_fd, SOL_SOCKET, SO_REUSEPORT, &((int){1}), sizeof(int));

  const struct sockaddr_in addr = {
      .sin_family = AF_INET,         /* Famille d'adresses : AF_INET */
      .sin_port = htons(2202),       /* Port dans l'ordre des octets réseau */
      .sin_addr.s_addr = INADDR_ANY, /* Adresse Internet */
  };

  if (bind(serv_fd, (const struct sockaddr *)&addr, sizeof(addr)) == SOCK_ERR) {
    perror("impossible to bind socket");
    return SOCK_ERR;
  }

  if (listen(serv_fd, BACKLOG) == SOCK_ERR) {
    perror("impossible to listen the socket");
    return SOCK_ERR;
  }

  return serv_fd;
}

/// @return false (0) on failure
int handleRecvMessage() {

  if (recvMessageBuff.count <= 0)
    return 0;

  struct message msg;
  cb_dequeue(recvMessageBuff, msg);

  switch (msg.Command) {
  case ACKNOWLEDGE:
    assert(1 && "Not implemented yet");
    // TODO : faire une liste de packet devant etre ack
    // lorsque l'on recoit un ack, on cherche dans la liste, et on la tej de la
    // liste
    break;

  case CONNECT: {
    assert(1 && "CONNECT : Not implemented yet");

    // TODO mettre dans le buff "outMessagebuff"
  } break;

  case DISCONNECT:
  case PING:
    assert(1 && "DISCONNECT & PING : Not implemented yet");
    break;

  case SEND_RELIABLE:
  case SEND_UNRELIABLE:
  case SEND_UNSEQUENCED:
    printf("traitement d'un msg ayant pour command 'SEND_RELIABLE', "
           "'SEND_UNRELIABLE' ou 'SEND_FRAGMENT'\n ");
    assert(1 && "SEND_XXX : Not implemented yet");

    break;

  case SEND_FRAGMENT:
  case SEND_UNRELIABLE_FRAGMENT:
    // TODO : faudra memcpy dans un cache le temps d'assembler tout les
    // fragments
    assert(1 && "FRAGMENT & UNRELIABLE_FRAGMENT : Not implemented yet");
    break;

  case BANDWIDTH_LIMIT:
  case THROTTLE_CONFIGURE:
    assert(1 && "BANDWIDTH_LIMIT & THROTTLE_CONFIGURE : Not implemented");

  default:
  case VERIFY_CONNECT:
    break;
  };

  if (msg.payload != NULL) {
    free(msg.payload);
    msg.payload = NULL;
  }

  return 1;
}

/// @return false (0) on failure

int handleSendMessage() {

  if (sendMessageBuff.count <= 0)
    return 0;

  struct message msg;
  cb_dequeue(sendMessageBuff, msg);

  switch (msg.Command) {
  case ACKNOWLEDGE:
    assert(1 && "Not implemented yet");
    // TODO : faire une liste de packet devant etre ack
    // lorsque l'on recoit un ack, on cherche dans la liste, et on la tej de la
    // liste
    break;

  case CONNECT: {
    assert(1 && "CONNECT : Not implemented yet");

    // TODO mettre dans le buff "outMessagebuff"
  } break;

  case DISCONNECT:
  case PING:
    assert(1 && "DISCONNECT & PING : Not implemented yet");
    break;

  case SEND_RELIABLE:
  case SEND_UNRELIABLE:
  case SEND_UNSEQUENCED:
    printf("traitement d'un msg ayant pour command 'SEND_RELIABLE', "
           "'SEND_UNRELIABLE' ou 'SEND_FRAGMENT'\n ");
    assert(1 && "SEND_XXX : Not implemented yet");

    break;

  case SEND_FRAGMENT:
  case SEND_UNRELIABLE_FRAGMENT:
    // TODO : faudra memcpy dans un cache le temps d'assembler tout les
    // fragments
    assert(1 && "FRAGMENT & UNRELIABLE_FRAGMENT : Not implemented yet");
    break;

  case BANDWIDTH_LIMIT:
  case THROTTLE_CONFIGURE:
    assert(1 && "BANDWIDTH_LIMIT & THROTTLE_CONFIGURE : Not implemented");

  default:
  case VERIFY_CONNECT:
    break;
  };

  return 1;
}
