/* FreeCoAP server module (the SUT for the libcoap->FreeCoAP differential run). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coap_msg.h"
#include "coap_mem.h"
#include "coap_log.h"
#include "coap_server.h"
#include "bridge.h"

#define HOST "127.0.0.1"
/* Port from the SERVER_PORT env var so parallel KLEE lanes do not clash on bind. */
static const char *fc_port(void)
{
    const char *p = getenv("SERVER_PORT");
    return p ? p : "5683";
}

#define SMALL_BUF_NUM   128
#define SMALL_BUF_LEN   256
#define MEDIUM_BUF_NUM  128
#define MEDIUM_BUF_LEN  1024
#define LARGE_BUF_NUM   32
#define LARGE_BUF_LEN   8192

extern int coap_server_exchange_once(coap_server_t *server);

static coap_server_t g_server;

#define RESOURCE "test"
#define RESOURCE_PAYLOAD "Hello World"

static int reply(coap_msg_t *resp, unsigned code_class, unsigned code_detail,
                 const char *payload)
{
    int ret;

    ret = coap_msg_set_code(resp, code_class, code_detail);
    if (ret < 0)
        return ret;
    return coap_msg_set_payload(resp, (char *)payload, strlen(payload));
}

static int has_op(coap_msg_t *msg, unsigned num)
{
    coap_msg_op_t *op;

    for (op = coap_msg_get_first_op(msg); op != NULL; op = coap_msg_op_get_next(op))
        if (coap_msg_op_get_num(op) == num)
            return 1;
    return 0;
}

/* Uri-Path is exactly the single segment RESOURCE */
static int is_resource_path(coap_msg_t *msg)
{
    coap_msg_op_t *op;
    int segments = 0;
    int match = 0;

    for (op = coap_msg_get_first_op(msg); op != NULL; op = coap_msg_op_get_next(op))
    {
        if (coap_msg_op_get_num(op) != COAP_MSG_URI_PATH)
            continue;
        segments++;
        match = coap_msg_op_get_len(op) == strlen(RESOURCE)
             && memcmp(coap_msg_op_get_val(op), RESOURCE, strlen(RESOURCE)) == 0;
    }
    return segments == 1 && match;
}

/* Answers like libcoap's core does for one GET resource /test; rules in README.md. */
static int exchange_handle(coap_server_trans_t *trans, coap_msg_t *req,
                           coap_msg_t *resp)
{
    unsigned method = coap_msg_get_code_detail(req);

    (void)trans;

    if (has_op(req, COAP_MSG_PROXY_URI) || has_op(req, COAP_MSG_PROXY_SCHEME))
        return reply(resp, COAP_MSG_SERVER_ERR, COAP_MSG_PROXY_NOT_SUP, "Proxying Not Supported");

    if (!is_resource_path(req))
    {
        if (method == COAP_MSG_DELETE)
            return reply(resp, COAP_MSG_SUCCESS, COAP_MSG_DELETED, "Deleted");
        return reply(resp, COAP_MSG_CLIENT_ERR, COAP_MSG_NOT_FOUND, "Not Found");
    }

    if (has_op(req, COAP_MSG_IF_NONE_MATCH))
        return reply(resp, COAP_MSG_CLIENT_ERR, COAP_MSG_PRECOND_FAILED, "Precondition Failed");

    if (method != COAP_MSG_GET)
        return reply(resp, COAP_MSG_CLIENT_ERR, COAP_MSG_METHOD_NOT_ALLOWED, "Method Not Allowed");

    return reply(resp, COAP_MSG_SUCCESS, COAP_MSG_CONTENT, RESOURCE_PAYLOAD);
}

int fc_server_init(void)
{
    int ret;

    ret = coap_mem_all_create(SMALL_BUF_NUM, SMALL_BUF_LEN,
                              MEDIUM_BUF_NUM, MEDIUM_BUF_LEN,
                              LARGE_BUF_NUM, LARGE_BUF_LEN);
    if (ret < 0)
    {
        fprintf(stderr, "[fc-server] coap_mem_all_create failed: %d\n", ret);
        return ret;
    }
    coap_log_set_level(COAP_LOG_INFO);

    memset(&g_server, 0, sizeof g_server);
    ret = coap_server_create(&g_server, exchange_handle, HOST, fc_port());
    if (ret < 0)
    {
        fprintf(stderr, "[fc-server] coap_server_create failed: %d\n", ret);
        return ret;
    }
    fprintf(stderr, "[fc-server] FreeCoAP server created on %s:%s\n", HOST, fc_port());
    return 0;
}

int fc_server_step(void)
{
    return coap_server_exchange_once(&g_server);
}
