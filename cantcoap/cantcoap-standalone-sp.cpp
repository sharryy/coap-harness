/* cantcoap single-process CoAP exchange, shaped for the Kleener socket model.
 * One process because the model carries packets through in-process queues that a
 * fork would split; cantcoap has no network layer, so the harness owns the sockets.
 * Order: client send -> server recv/parse/reply -> client recv -> finalize.
 * The same source also runs natively (sequential, no fork) for a tcpdump check.
 * Build to bitcode, link the CoAP model + monitors, run under klee with
 *   PROTOCOL=CoAP SERVER_PORT=5683 KLEE_SYMBOLIC_EXPERIMENT=<id> STATE_TO_CHECK=SERVER */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "cantcoap.h"

#define HOST    "127.0.0.1"
#define PORT    5683
#define BUF_LEN 512

/* Weak no-op for the native build; the model provides the strong def under KLEE. */
__attribute__((weak)) void coap_exchange_finalize(void) {}

static void fill_loopback(struct sockaddr_in *a)
{
    memset(a, 0, sizeof(*a));
    a->sin_family = AF_INET;
    a->sin_port = htons(PORT);
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

/* Server step: read one datagram, parse+validate through cantcoap, reply 2.05
 * echoing the MID and token. Returns 0 on a sent reply, -1 on drop/malformed. */
static int server_step(int sockfd)
{
    char buffer[BUF_LEN];
    struct sockaddr_storage from;
    socklen_t fromLen = sizeof(from);

    ssize_t n = recvfrom(sockfd, buffer, BUF_LEN, 0,
                         (struct sockaddr *)&from, &fromLen);
    if (n < 0) {
        fprintf(stderr, "[server] recvfrom: %s\n", strerror(errno));
        return -1;
    }

    CoapPDU *req = new CoapPDU((uint8_t *)buffer, (int)n);
    if (req->validate() != 1) {
        fprintf(stderr, "[server] malformed CoAP request (%zd bytes)\n", n);
        delete req;
        return -1;       /* drop: model's finalize decides if that is a violation */
    }
    fprintf(stderr, "[server] valid request code=%d type=%d mid=%d\n",
            req->getCode(), req->getType(), req->getMessageID());

    CoapPDU *resp = new CoapPDU();
    resp->setVersion(1);
    resp->setType(CoapPDU::COAP_ACKNOWLEDGEMENT);
    resp->setCode(CoapPDU::COAP_CONTENT);
    resp->setMessageID(req->getMessageID());
    resp->setToken(req->getTokenPointer(), req->getTokenLength());
    resp->setContentFormat(CoapPDU::COAP_CONTENT_FORMAT_TEXT_PLAIN);

    static const char body[] = "hello-from-cantcoap";
    resp->setPayload((uint8_t *)body, strlen(body));

    ssize_t sent = sendto(sockfd, resp->getPDUPointer(), resp->getPDULength(), 0,
                          (struct sockaddr *)&from, fromLen);
    if (sent < 0)
        fprintf(stderr, "[server] sendto: %s\n", strerror(errno));
    else
        fprintf(stderr, "[server] sent 2.05 Content (%d bytes)\n",
                resp->getPDULength());

    delete resp;
    delete req;
    return sent < 0 ? -1 : 0;
}

/* Client send: CON GET /test with a 4-byte token for the token monitor to corrupt. */
static int client_send(int sockfd)
{
    CoapPDU *req = new CoapPDU();
    req->setVersion(1);
    req->setType(CoapPDU::COAP_CONFIRMABLE);
    req->setCode(CoapPDU::COAP_GET);
    req->setToken((uint8_t *)"\xDE\xAD\xBE\xEF", 4);
    req->setMessageID(0x0042);
    req->setURI((char *)"test", 4);

    fprintf(stderr, "[client] sending CON GET /test (%d bytes)\n",
            req->getPDULength());
    ssize_t s = send(sockfd, req->getPDUPointer(), req->getPDULength(), 0);
    delete req;
    if (s < 0) {
        fprintf(stderr, "[client] send: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

/* Client receive: read the server's reply and validate it through cantcoap. */
static int client_recv(int sockfd)
{
    char buffer[BUF_LEN];
    ssize_t n = recv(sockfd, buffer, BUF_LEN, 0);
    if (n < 0) {
        fprintf(stderr, "[client] recv: %s\n", strerror(errno));
        return -1;
    }
    CoapPDU *resp = new CoapPDU((uint8_t *)buffer, (int)n);
    if (resp->validate() != 1) {
        fprintf(stderr, "[client] malformed response (%zd bytes)\n", n);
        delete resp;
        return -1;
    }
    fprintf(stderr, "[client] valid response code=%d mid=%d tokenLen=%d payloadLen=%d\n",
            resp->getCode(), resp->getMessageID(),
            resp->getTokenLength(), resp->getPayloadLength());
    delete resp;
    return 0;
}

int main(void)
{
    /* Server socket: bind the well-known port so the model marks it server_fd
     * and creates the in-process queues. */
    int srv = socket(AF_INET, SOCK_DGRAM, 0);
    if (srv < 0) { perror("server socket"); return 1; }

    struct sockaddr_in srv_addr;
    fill_loopback(&srv_addr);
    if (bind(srv, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) != 0) {
        perror("server bind");
        close(srv);
        return 1;
    }

    /* Don't hang the native run if a (correct or buggy) drop loses a datagram. */
    struct timeval tv = { 2, 0 };
    setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Client socket: a separate fd, connected to the server. */
    int cli = socket(AF_INET, SOCK_DGRAM, 0);
    if (cli < 0) { perror("client socket"); close(srv); return 1; }

    struct sockaddr_in dst;
    fill_loopback(&dst);
    if (connect(cli, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
        perror("client connect");
        close(cli); close(srv);
        return 1;
    }
    setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    fprintf(stderr, "[harness] single-process cantcoap exchange on %s:%d\n",
            HOST, PORT);

    /* Drive the exchange in order, one datagram per step. */
    int status = 0;
    if (client_send(cli) != 0)      status = 1;
    else if (server_step(srv) != 0) status = 1;     /* a drop here is allowed; finalize judges */
    else if (client_recv(cli) != 0) status = 1;

    /* Lets the model fire if the active requirement demanded a reply that the
     * SUT never produced. */
    coap_exchange_finalize();

    close(cli);
    close(srv);
    fprintf(stderr, "[harness] exchange complete (status=%d)\n", status);
    return status;
}
