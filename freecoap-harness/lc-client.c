/*libcoap CLIENT module — the driver for BOTH harnesses.
 *
 * This is the same client the libcoap-harness uses (ported from
 * libcoap-standalone.c's client half): it sends a CON GET /test with a 4-byte
 * token and processes the response non-blocking. Using the identical libcoap
 * client against both servers means the only variable in the differential run
 * is the server implementation (libcoap vs FreeCoAP). Only libcoap headers are
 * included here. */

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <coap3/coap.h>
#include "bridge.h"

static coap_context_t *client_ctx;
static coap_session_t *client_session;
static int response_received_flag = 0;

static coap_response_t client_response_handler(coap_session_t *session,
                                               const coap_pdu_t *sent,
                                               const coap_pdu_t *received,
                                               const coap_mid_t mid)
{
    fprintf(stderr, "[lc-client] got response\n");
    coap_show_pdu(COAP_LOG_INFO, received);
    response_received_flag = 1;

    (void)session;
    (void)sent;
    (void)mid;

    return COAP_RESPONSE_OK;
}

int lc_client_init(void)
{
    coap_log_t log_level = COAP_LOG_WARN;
    const char *env_log = getenv("COAP_LOG_LEVEL");
    if (env_log != NULL)
        log_level = (coap_log_t)atoi(env_log);

    coap_startup();
    coap_set_log_level(log_level);

    client_ctx = coap_new_context(NULL);
    if (!client_ctx)
    {
        fprintf(stderr, "[lc-client] unable to create context\n");
        return -1;
    }

    coap_address_t dst_addr;
    coap_address_init(&dst_addr);
    dst_addr.addr.sin.sin_family = AF_INET;
    dst_addr.addr.sin.sin_port = htons(5683);
    dst_addr.addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    client_session = coap_new_client_session(client_ctx, NULL, &dst_addr, COAP_PROTO_UDP);
    if (!client_session)
    {
        fprintf(stderr, "[lc-client] unable to create session\n");
        return -1;
    }
    coap_register_response_handler(client_ctx, client_response_handler);
    fprintf(stderr, "[lc-client] libcoap client ready (%s)\n", coap_package_version());
    return 0;
}

void lc_client_send(void)
{
    coap_pdu_t *request = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, client_session);
    if (!request)
    {
        fprintf(stderr, "[lc-client] unable to create request\n");
        return;
    }
    coap_add_token(request, 4, (const uint8_t *)"\xDE\xAD\xBE\xEF");
    coap_add_option(request, COAP_OPTION_URI_PATH, 4, (const uint8_t *)"test");
    coap_send(client_session, request);
    fprintf(stderr, "[lc-client] sent CON GET /test\n");
}

int lc_client_step(void)
{
    coap_io_process(client_ctx, COAP_IO_NO_WAIT);
    return response_received_flag;
}
