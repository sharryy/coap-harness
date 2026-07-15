/* libcoap standalone client<->server harness for KLEE differential experiments. */
#include <assert.h>
#include <coap3/coap.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/* compat shims: builds against libcoap 4.3.1 and 4.3.5 */
#ifndef COAP_LOG_INFO
#define COAP_LOG_INFO LOG_INFO
#endif
#ifndef COAP_LOG_WARN
#define COAP_LOG_WARN LOG_WARNING
#endif
#ifndef COAP_RESOURCE_SAFE_REQUEST_HANDLER
#define COAP_RESOURCE_SAFE_REQUEST_HANDLER 0
#endif

static int response_received_flag = 0;

/* weak no-op for native builds; socket model provides strong def under KLEE */
__attribute__((weak)) void coap_exchange_finalize(void) {}

static void hnd_get_test(coap_resource_t *resource, coap_session_t *session,
                         const coap_pdu_t *request,
                         const coap_string_t *query COAP_UNUSED,
                         coap_pdu_t *response) {
  coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
  coap_add_data(response, 7, (const uint8_t *)"sharryy");
  (void)resource;
  (void)session;
  (void)request;
}

static coap_response_t client_response_handler(coap_session_t *session,
                                               const coap_pdu_t *sent,
                                               const coap_pdu_t *received,
                                               const coap_mid_t mid) {
  fprintf(stderr, "[harness] client got response\n");
  coap_show_pdu(COAP_LOG_INFO, received);
  response_received_flag = 1;

  /* exp 11/14: token-spoof checks (RFC 7252 5.3.2); exp 15: control */
  {
    const char *exp = getenv("KLEE_SYMBOLIC_EXPERIMENT");
    int e = (exp != NULL) ? atoi(exp) : 0;
    if (e == 11 || e == 14 || e == 15) {
      static const uint8_t sent_token[4] = {0xDE, 0xAD, 0xBE, 0xEF};
      coap_bin_const_t tok = coap_pdu_get_token(received);
      fprintf(stderr,
              "[harness] exp%d: sent=%s type=%d mid=0x%04x delivered token "
              "len=%zu bytes=",
              e, sent ? "MATCHED-REQUEST" : "NULL(no-match)",
              coap_pdu_get_type(received), coap_pdu_get_mid(received),
              tok.length);
      for (size_t i = 0; i < tok.length; i++)
        fprintf(stderr, "%02X", tok.s[i]);
      fprintf(stderr, "\n");
      int mismatch = (tok.length != sizeof(sent_token)) ||
                     (memcmp(tok.s, sent_token, sizeof(sent_token)) != 0);
      /* violation only if matched (sent != NULL) but token differs */
      if (e != 15)
        assert(!(sent != NULL && mismatch) &&
               "client delivered a response as MATCHED to a request "
               "(sent != NULL) whose token does not match (RFC 7252 "
               "5.3.2; piggybacked-ACK MID-only match, spoofable)");
    }
    /* exp 17: mid-spoof (dual of exp 11); wrong-MID response should be dropped */
    if (e == 17) {
      fprintf(stderr,
              "[harness] exp17: DELIVERED wrong-MID response  sent=%s type=%d "
              "mid=0x%04x\n",
              sent ? "MATCHED-REQUEST" : "NULL(no-match)",
              coap_pdu_get_type(received), coap_pdu_get_mid(received));
      assert(sent == NULL &&
             "client delivered a response as MATCHED (sent != NULL) whose "
             "Message ID does not match any outstanding request "
             "(RFC 7252 5.3.2)");
    }
  }

  (void)session;
  (void)sent;
  (void)mid;

  return COAP_RESPONSE_OK;
}

int main(void) {
  coap_log_t log_level = COAP_LOG_WARN;

  const char *env_log = getenv("COAP_LOG_LEVEL");

  if (env_log != NULL)
    log_level = (coap_log_t)atoi(env_log);

  coap_startup();
  coap_set_log_level(log_level);

  coap_context_t *server_ctx = coap_new_context(NULL);

  if (!server_ctx) {
    fprintf(stderr, "[harness] failed to create server context\n");
    return 1;
    ;
  }

  coap_address_t server_addr;
  coap_address_init(&server_addr);
  server_addr.addr.sin.sin_family = AF_INET;
  server_addr.addr.sin.sin_port = htons(5683);
  server_addr.addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  coap_resource_t *resource = coap_resource_init(
      coap_make_str_const("test"), COAP_RESOURCE_SAFE_REQUEST_HANDLER);

  if (!resource) {
    fprintf(stderr, "[harness] unable to create test resource");
    return 1;
  }

  coap_register_request_handler(resource, COAP_REQUEST_GET, hnd_get_test);
  coap_add_resource(server_ctx, resource);

  coap_endpoint_t *server_endpoint =
      coap_new_endpoint(server_ctx, &server_addr, COAP_PROTO_UDP);

  if (!server_endpoint) {
    fprintf(stderr, "[harness] unable to create server endpoint");
    return 1;
  }

  fprintf(stderr, "[harness] libcoap server listening on 127.0.0.1:5683\n");
  fprintf(stderr, "[harness] libcoap version: %s\n", coap_package_version());

  coap_context_t *client_ctx = coap_new_context(NULL);

  if (!client_ctx) {
    fprintf(stderr, "[harness] unable to create client context\n");
    return 1;
  }

  coap_address_t dst_addr;
  coap_address_init(&dst_addr);
  dst_addr.addr.sin.sin_family = AF_INET;
  dst_addr.addr.sin.sin_port = htons(5683);
  dst_addr.addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  coap_session_t *client_session =
      coap_new_client_session(client_ctx, NULL, &dst_addr, COAP_PROTO_UDP);

  if (!client_session) {
    fprintf(stderr, "[harness] unable to create client session\n");
    return 1;
  }

  coap_register_response_handler(client_ctx, client_response_handler);

  coap_pdu_t *request =
      coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, client_session);

  if (!request) {
    fprintf(stderr, "[harness] unable to create request\n");
    return 1;
  }

  /* 4-byte token for the token-match monitor (RFC 7252 5.3.1) */
  coap_add_token(request, 4, (const uint8_t *)"\xDE\xAD\xBE\xEF");

  coap_add_option(request, COAP_OPTION_URI_PATH, 4, (const uint8_t *)"test");
  coap_send(client_session, request);

  fprintf(stderr, "[harness] client send test GET request\n");

  fprintf(stderr, "[harness] entering the IO loop\n");

  /* bounded exchange: fixed window to respond, then finalize */
  struct timeval tv_start;
  gettimeofday(&tv_start, NULL);

  coap_tick_t ct_start;
  coap_ticks(&ct_start);
  for (int cycle = 0; cycle < 16 && !response_received_flag; cycle++) {
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    coap_tick_t ct_now;
    coap_ticks(&ct_now);
    long wall_ms = (tv_now.tv_sec - tv_start.tv_sec) * 1000L +
                   (tv_now.tv_usec - tv_start.tv_usec) / 1000L;
    unsigned long long coap_ms = (unsigned long long)(ct_now - ct_start) *
                                 1000ULL / COAP_TICKS_PER_SECOND;
    fprintf(stderr,
            "[TIME] cycle=%d  wallclock=%ld ms  coap_ticks=%llu ms  "
            "(ACK_TIMEOUT=2000 ms)\n",
            cycle, wall_ms, coap_ms);
    fflush(stderr);
    coap_io_process(server_ctx, COAP_IO_NO_WAIT);
    coap_io_process(client_ctx, COAP_IO_NO_WAIT);
  }
  coap_exchange_finalize();

  fprintf(stderr, "[harness] test exchange completed\n");

  coap_session_release(client_session);
  coap_free_context(client_ctx);
  coap_free_context(server_ctx);
  coap_cleanup();

  return 0;
}
