/*
 * FreeCoAP standalone client<->server exchange harness (no DTLS).
 *
 * Named to match the libcoap harness (libcoap-standalone.c): it performs a
 * genuine CoAP request/response over loopback UDP:5683, so the traffic is
 * visible to tcpdump (e.g. `sudo tcpdump -i lo -n udp port 5683`).
 *
 * Native demo/validation binary (not a KLEE target — real sockets + fork do
 * not symbolically execute usefully).
 *
 * FreeCoAP's coap_server_run() blocks (while(1) select loop), unlike libcoap's
 * non-blocking IO, so the two roles cannot share one loop. We fork:
 *   child  -> coap_server_create() + coap_server_run()   (the SUT server)
 *   parent -> coap_client_create() + coap_client_exchange() (the client)
 * After the exchange the parent tears the child down.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "coap_msg.h"
#include "coap_mem.h"
#include "coap_log.h"
#include "coap_client.h"
#include "coap_server.h"

#define HOST            "127.0.0.1"
#define PORT            "5683"

#define SMALL_BUF_NUM   128
#define SMALL_BUF_LEN   256
#define MEDIUM_BUF_NUM  128
#define MEDIUM_BUF_LEN  1024
#define LARGE_BUF_NUM   32
#define LARGE_BUF_LEN   8192

/* Server-side request handler: answer any GET with 2.05 Content + payload. */
static int exchange_handle(coap_server_trans_t *trans, coap_msg_t *req,
                           coap_msg_t *resp)
{
    static const char body[] = "hello-from-freecoap";
    unsigned code_detail;
    int ret;

    (void)trans;
    code_detail = coap_msg_get_code_detail(req);
    if (code_detail != COAP_MSG_GET)
        return coap_msg_set_code(resp, COAP_MSG_SERVER_ERR, COAP_MSG_NOT_IMPL);

    fprintf(stderr, "[server] got GET, replying 2.05 Content\n");
    coap_msg_set_code(resp, COAP_MSG_SUCCESS, COAP_MSG_CONTENT);
    ret = coap_msg_add_op(resp, COAP_MSG_URI_PATH, 4, "test");
    if (ret < 0)
        return ret;
    return coap_msg_set_payload(resp, (char *)body, strlen(body));
}

static int run_server(void)
{
    coap_server_t server = {0};
    int ret;

    ret = coap_server_create(&server, exchange_handle, HOST, PORT);
    if (ret < 0)
    {
        fprintf(stderr, "[server] create failed: %s\n", strerror(-ret));
        return 1;
    }
    fprintf(stderr, "[server] listening on %s:%s (UDP, no DTLS)\n", HOST, PORT);
    coap_server_run(&server);          /* blocks until the process is killed */
    coap_server_destroy(&server);
    return 0;
}

static int run_client(void)
{
    coap_client_t client = {0};
    coap_msg_t req;
    coap_msg_t resp;
    int ret;

    ret = coap_client_create(&client, HOST, PORT);
    if (ret < 0)
    {
        fprintf(stderr, "[client] create failed: %s\n", strerror(-ret));
        return 1;
    }

    coap_msg_create(&req);
    coap_msg_set_type(&req, COAP_MSG_CON);
    coap_msg_set_code(&req, COAP_MSG_REQ, COAP_MSG_GET);
    coap_msg_add_op(&req, COAP_MSG_URI_PATH, 4, "test");

    coap_msg_create(&resp);
    fprintf(stderr, "[client] sending CON GET /test to %s:%s\n", HOST, PORT);
    ret = coap_client_exchange(&client, &req, &resp);
    if (ret < 0)
    {
        fprintf(stderr, "[client] exchange failed: %s\n", strerror(-ret));
        coap_msg_destroy(&resp);
        coap_msg_destroy(&req);
        coap_client_destroy(&client);
        return 1;
    }

    fprintf(stderr, "[client] got response: %u.%02u, payload_len=%zu, payload='%.*s'\n",
            coap_msg_get_code_class(&resp), coap_msg_get_code_detail(&resp),
            (size_t)coap_msg_get_payload_len(&resp),
            (int)coap_msg_get_payload_len(&resp),
            (char *)coap_msg_get_payload(&resp));

    coap_msg_destroy(&resp);
    coap_msg_destroy(&req);
    coap_client_destroy(&client);
    return 0;
}

int main(void)
{
    pid_t pid;
    int ret;
    int status = 0;

    coap_log_set_level(COAP_LOG_INFO);

    ret = coap_mem_all_create(SMALL_BUF_NUM, SMALL_BUF_LEN,
                              MEDIUM_BUF_NUM, MEDIUM_BUF_LEN,
                              LARGE_BUF_NUM, LARGE_BUF_LEN);
    if (ret < 0)
    {
        fprintf(stderr, "[harness] coap_mem_all_create failed: %s\n", strerror(-ret));
        return 1;
    }

    pid = fork();
    if (pid < 0)
    {
        perror("fork");
        coap_mem_all_destroy();
        return 1;
    }

    if (pid == 0)
    {
        /* child: the server. Its own allocator is inherited via fork. */
        _exit(run_server());
    }

    /* parent: the client. Give the server a moment to bind the socket. */
    usleep(300 * 1000);
    status = run_client();

    /* tear the server down */
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    coap_mem_all_destroy();
    fprintf(stderr, "[harness] exchange complete (status=%d)\n", status);
    return status;
}
