/* FreeCoAP server module (the SUT for the libcoap->FreeCoAP differential run). */

#include <stdio.h>
#include <string.h>
#include "coap_msg.h"
#include "coap_mem.h"
#include "coap_log.h"
#include "coap_server.h"
#include "bridge.h"

#define HOST "127.0.0.1"
#define PORT "5683"

#define SMALL_BUF_NUM   128
#define SMALL_BUF_LEN   256
#define MEDIUM_BUF_NUM  128
#define MEDIUM_BUF_LEN  1024
#define LARGE_BUF_NUM   32
#define LARGE_BUF_LEN   8192

extern int coap_server_exchange_once(coap_server_t *server);

static coap_server_t g_server;

/* GET -> 2.05 Content; other methods -> 5.01 */
static int exchange_handle(coap_server_trans_t *trans, coap_msg_t *req,
                           coap_msg_t *resp)
{
    static const char body[] = "sharryy";
    unsigned code_detail;

    (void)trans;
    code_detail = coap_msg_get_code_detail(req);
    if (code_detail != COAP_MSG_GET)
        return coap_msg_set_code(resp, COAP_MSG_SERVER_ERR, COAP_MSG_NOT_IMPL);

    fprintf(stderr, "[fc-server] got GET, replying 2.05 Content\n");
    coap_msg_set_code(resp, COAP_MSG_SUCCESS, COAP_MSG_CONTENT);
    return coap_msg_set_payload(resp, (char *)body, strlen(body));
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
    ret = coap_server_create(&g_server, exchange_handle, HOST, PORT);
    if (ret < 0)
    {
        fprintf(stderr, "[fc-server] coap_server_create failed: %d\n", ret);
        return ret;
    }
    fprintf(stderr, "[fc-server] FreeCoAP server created on %s:%s\n", HOST, PORT);
    return 0;
}

int fc_server_step(void)
{
    return coap_server_exchange_once(&g_server);
}
