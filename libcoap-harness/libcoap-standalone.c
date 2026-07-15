#include <assert.h>
#include <coap3/coap.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h> /* TEMP DIAGNOSTIC: gettimeofday for the wall-clock probe */
#include <unistd.h>

/* Compatibility shims so the same source builds against libcoap 4.3.1
 * (which uses syslog-style log levels and lacks the SAFE_REQUEST_HANDLER
 * flag) and against 4.3.5 (which renamed the log levels and added the
 * flag). 4.3.5 defines all three first, so these guards are no-ops there. */
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

/* Finalize hook for the Kleener socket model's bounded-exchange check. Weak
 * no-op so the native build (which doesn't link the model) still resolves it;
 * the model provides the strong definition under KLEE. */
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

  /* Experiment 11 (token-spoof, RFC 7252 §5.3.2): the token-spoof monitor
   * corrupts this response's token on the wire while keeping its MID. If
   * libcoap delivered it to us anyway, it matched the piggybacked ACK by MID
   * alone and never verified the token — a response-spoofing gap. The token
   * we actually sent is the 4-byte DE AD BE EF below. Gated on the experiment
   * id so the symbolic-token experiment (7) is unaffected. */
  {
    const char *exp = getenv("KLEE_SYMBOLIC_EXPERIMENT");
    int e = (exp != NULL) ? atoi(exp) : 0;
    /* exp 11 = piggybacked-ACK token spoof; exp 14 = separate-response token
     * spoof. Both corrupt the response token; if libcoap delivered it to us,
     * it did not verify the token before delivery (RFC 7252 §5.3.2). exp 15
     * is the separate-response control (token kept) — no assert, we only need
     * to see whether delivery happens. */
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
      /* Only a FALSE MATCH is a violation: libcoap bound this response to a
       * real outstanding request (sent != NULL) yet the token differs. With
       * sent==NULL libcoap asserts no correlation (app must match by token),
       * so that is not flagged. exp 15 is the control. */
      if (e != 15)
        assert(!(sent != NULL && mismatch) &&
               "client delivered a response as MATCHED to a request "
               "(sent != NULL) whose token does not match (RFC 7252 "
               "5.3.2; piggybacked-ACK MID-only match, spoofable)");
    }
    /* Experiment 17 (mid-spoof, dual of exp 11): the monitor flips the
     * response's Message ID while keeping the token correct. We observe
     * whether libcoap discards a wrong-MID response. Reaching this handler
     * at all means libcoap delivered it; sent != NULL would mean it matched
     * a request despite the wrong MID (a second defect). A MID-keyed client
     * drops the datagram and this handler never fires for exp 17 — that
     * absence (no log line below) is the expected, conformant outcome. */
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

  /* Carry a 4-byte token (added before options, per libcoap PDU build order)
   * so the token-match monitor (RFC 7252 §5.3.1) has a token to corrupt and
   * verify the server echoes it back in the response. */
  coap_add_token(request, 4, (const uint8_t *)"\xDE\xAD\xBE\xEF");

  coap_add_option(request, COAP_OPTION_URI_PATH, 4, (const uint8_t *)"test");
  coap_send(client_session, request);

  fprintf(stderr, "[harness] client send test GET request\n");

  fprintf(stderr, "[harness] entering the IO loop\n");

  /* Bounded exchange: give the SUT a fixed window to respond, then finalize.
   * Replaces the old open-ended spin so a (correct or buggy) drop no longer
   * loops until KLEE's max-time. 16 cycles is ample for one request/response;
   * we break early once a response arrives. coap_exchange_finalize() then lets
   * the model fire if a requirement demanded a response that never came. */
  /* ===== TEMP DIAGNOSTIC (remove after checking) =========================
   * Claim under test: under KLEE, real host wall-clock advances past the 2 s
   * CoAP ACK timeout DURING this loop (because KLEE interprets libcoap slowly
   * and gettimeofday is NOT modeled), so the CON client retransmits. Print,
   * each cycle, elapsed time as (a) raw host wall-clock and (b) libcoap's own
   * coap_ticks clock. If these climb past ~2000 ms, that's the retransmit
   * window. fflush so the lines survive KLEE state termination. */
  struct timeval tv_start;
  gettimeofday(&tv_start, NULL);
  
  coap_tick_t ct_start;
  coap_ticks(&ct_start);
  /* ======================================================================= */
  for (int cycle = 0; cycle < 16 && !response_received_flag; cycle++) {
    /* --- TEMP DIAGNOSTIC --- */
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
    /* ----------------------- */
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
