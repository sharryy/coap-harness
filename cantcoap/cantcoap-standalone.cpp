/* cantcoap native client<->server exchange over loopback UDP:5683 (tcpdump-visible).
 * cantcoap is a PDU codec only, so the harness owns the sockets; the two roles
 * fork because cantcoap's server example is a blocking recvfrom loop. */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include "cantcoap.h"

#define HOST    "127.0.0.1"
#define PORT    5683
#define BUF_LEN 512

static void fill_loopback(struct sockaddr_in *a)
{
    memset(a, 0, sizeof(*a));
    a->sin_family = AF_INET;
    a->sin_port = htons(PORT);
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

/* Server role (child): recvfrom one request, reply 2.05 echoing the MID and token. */
static int run_server(void)
{
    struct sockaddr_in addr;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("[server] socket");
        return 1;
    }

    fill_loopback(&addr);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("[server] bind");
        close(sockfd);
        return 1;
    }
    fprintf(stderr, "[server] listening on %s:%d (UDP)\n", HOST, PORT);

    char buffer[BUF_LEN];
    struct sockaddr_storage from;
    socklen_t fromLen = sizeof(from);

    ssize_t n = recvfrom(sockfd, buffer, BUF_LEN, 0,
                         (struct sockaddr *)&from, &fromLen);
    if (n < 0) {
        perror("[server] recvfrom");
        close(sockfd);
        return 1;
    }

    /* Parse + validate the request through cantcoap (the SUT decode path). */
    CoapPDU *req = new CoapPDU((uint8_t *)buffer, (int)n);
    if (req->validate() != 1) {
        fprintf(stderr, "[server] malformed CoAP packet (%zd bytes)\n", n);
        delete req;
        close(sockfd);
        return 1;
    }
    fprintf(stderr, "[server] got valid request, code=%d type=%d mid=%d\n",
            req->getCode(), req->getType(), req->getMessageID());
    req->printHuman();

    /* Build the response PDU through cantcoap (the SUT encode path). */
    CoapPDU *resp = new CoapPDU();
    resp->setVersion(1);
    resp->setType(CoapPDU::COAP_ACKNOWLEDGEMENT);
    resp->setCode(CoapPDU::COAP_CONTENT);
    resp->setMessageID(req->getMessageID());
    resp->setToken(req->getTokenPointer(), req->getTokenLength());
    resp->setContentFormat(CoapPDU::COAP_CONTENT_FORMAT_TEXT_PLAIN);

    static const char body[] = "hello-from-cantcoap";
    resp->setPayload((uint8_t *)body, strlen(body));

    fprintf(stderr, "[server] replying 2.05 Content (%d bytes on wire)\n",
            resp->getPDULength());
    ssize_t sent = sendto(sockfd, resp->getPDUPointer(), resp->getPDULength(), 0,
                          (struct sockaddr *)&from, fromLen);
    if (sent < 0)
        perror("[server] sendto");

    delete resp;
    delete req;
    close(sockfd);
    return 0;
}

/* Client role (parent): send CON GET /test with a 4-byte token, recv the ACK, validate. */
static int run_client(void)
{
    struct sockaddr_in dst;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("[client] socket");
        return 1;
    }

    fill_loopback(&dst);
    if (connect(sockfd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
        perror("[client] connect");
        close(sockfd);
        return 1;
    }

    /* Don't block forever if the SUT drops the request. */
    struct timeval tv = { 2, 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Build the request PDU through cantcoap. */
    CoapPDU *req = new CoapPDU();
    req->setVersion(1);
    req->setType(CoapPDU::COAP_CONFIRMABLE);
    req->setCode(CoapPDU::COAP_GET);
    req->setToken((uint8_t *)"\xDE\xAD\xBE\xEF", 4);
    req->setMessageID(0x0042);
    req->setURI((char *)"test", 4);

    fprintf(stderr, "[client] sending CON GET /test to %s:%d (%d bytes on wire)\n",
            HOST, PORT, req->getPDULength());
    if (send(sockfd, req->getPDUPointer(), req->getPDULength(), 0)
            != req->getPDULength()) {
        perror("[client] send");
        delete req;
        close(sockfd);
        return 1;
    }

    char buffer[BUF_LEN];
    ssize_t n = recv(sockfd, buffer, BUF_LEN, 0);
    if (n < 0) {
        fprintf(stderr, "[client] recv failed: %s\n", strerror(errno));
        delete req;
        close(sockfd);
        return 1;
    }

    CoapPDU *resp = new CoapPDU((uint8_t *)buffer, (int)n);
    if (resp->validate() != 1) {
        fprintf(stderr, "[client] malformed response (%zd bytes)\n", n);
        delete resp;
        delete req;
        close(sockfd);
        return 1;
    }

    fprintf(stderr, "[client] got valid response: code=%d mid=%d tokenLen=%d payloadLen=%d\n",
            resp->getCode(), resp->getMessageID(),
            resp->getTokenLength(), resp->getPayloadLength());
    resp->printHuman();

    delete resp;
    delete req;
    close(sockfd);
    return 0;
}

int main(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* child: the server. */
        _exit(run_server());
    }

    /* parent: the client. Give the server a moment to bind the socket. */
    usleep(300 * 1000);
    int status = run_client();

    /* tear the server down */
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    fprintf(stderr, "[harness] exchange complete (status=%d)\n", status);
    return status;
}
