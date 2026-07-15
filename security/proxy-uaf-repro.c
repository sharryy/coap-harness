/* proxy-uaf-repro.c — native reproducer for libcoap proxy use-after-free of the
 * incoming client session. One harness demonstrates TWO related bugs; which one
 * fires depends on the libcoap version linked and the REGISTER_EVT env toggle.
 *
 * The invariant being violated:
 *   A proxy_req record borrows the incoming client session pointer
 *   (proxy_req->incoming) WITHOUT taking a reference. So that record MUST be
 *   removed whenever the incoming session is destroyed, otherwise the pointer
 *   dangles. Server sessions are reaped by the library (idle timeout, or
 *   max_idle_sessions eviction), each emitting COAP_EVENT_SERVER_SESSION_DEL and
 *   then coap_session_free(). The eventual upstream response is matched purely by
 *   the rewritten upstream token and dereferences proxy_req->incoming with no
 *   liveness check (coap_proxy_forward_response_lkd / coap_proxy_map_outgoing_
 *   request) => use-after-free of the freed coap_session_t.
 *
 * BUG A ("removes only first"): coap_proxy_remove_association IS called (an event
 *   handler is registered) but in 4.3.5 it removes only the first matching
 *   proxy_req then breaks (coap_proxy.c:329-341), leaving siblings dangling.
 *   FIXED on develop (LL_FOREACH + goto retry removes all).
 *
 * BUG B ("cleanup gated on optional event_cb"): the SESSION_DEL -> remove_
 *   association call is nested inside `if (context->event_cb)` (coap_net.c
 *   4.3.5:4362 / develop:5274). With NO event handler registered, cleanup never
 *   runs at all, so even a SINGLE in-flight non-observe request dangles.
 *   LIVE on develop HEAD and every release <=4.3.5.
 *
 * Truth table this harness produces (proxy-uaf-repro built against each lib):
 *     lib       REGISTER_EVT=unset   REGISTER_EVT=1
 *     4.3.5     UAF (Bug B)          UAF (Bug A)
 *     develop   UAF (Bug B)          clean   <- control: proves it's the gate
 *
 * REPRO STRATEGY (single process, three contexts, manual io pumping so we own
 * the ordering and turn the reap-vs-response race into a deterministic sequence):
 *   ORIGIN  127.0.0.1:5701  - plain CoAP server, GET /test -> "upstream-data".
 *   PROXY   127.0.0.1:5700  - reverse proxy -> ORIGIN, max_idle_sessions = 1.
 *   CLIENT  S  -> PROXY      - sends TWO CON GETs (tokens T1,T2) over ONE socket.
 *   CLIENT  S2 -> PROXY      - a SECOND client socket sends one GET; on the
 *                             proxy's new-session path num_idle(=S) >=
 *                             max_idle_sessions(1) => evict + free S.
 *   Origin is deliberately NOT pumped until after S is freed; only then does the
 *   proxy forward the response through the dangling incoming pointer => ASan
 *   heap-use-after-free.
 *
 *   Must link an ASan-instrumented libcoap (the dereference is INSIDE libcoap, so
 *   only an instrumented libcoap traps it) — see Makefile targets proxy-uaf-repro
 *   (4.3.5) and proxy-uaf-repro-dev (develop).
 */
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <coap3/coap.h>

#define ORIGIN_PORT 5701
#define PROXY_PORT  5700

/* ---- reverse-proxy server list: forward everything to ORIGIN ------------- */
static coap_proxy_server_t      rp_entry;     /* .uri filled in main()         */
static coap_proxy_server_list_t reverse_proxy = {
    &rp_entry, 1, 0,
    COAP_PROXY_REVERSE,   /* non-STRIP: forward to the configured upstream entry
                           * directly (no Proxy-Uri option required in request) */
    0,    /* track_client_session = 0  -> the shared-proxy_entry (Mode A) bug  */
    0     /* idle_timeout_secs (we force eviction via max_idle_sessions)       */
};

/* ---- ORIGIN side -------------------------------------------------------- */
static void
origin_get(coap_resource_t *resource COAP_UNUSED,
           coap_session_t *session COAP_UNUSED,
           const coap_pdu_t *request COAP_UNUSED,
           const coap_string_t *query COAP_UNUSED,
           coap_pdu_t *response) {
  coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
  coap_add_data(response, 13, (const uint8_t *)"upstream-data");
}

/* ---- PROXY side --------------------------------------------------------- */
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

/* Optional proxy event handler. Registering ANY event callback is what gates
 * libcoap's proxy-association cleanup on SERVER_SESSION_DEL (coap_net.c:
 * `if (context->event_cb) { ... coap_proxy_remove_association(...) }`). Set
 * REGISTER_EVT=1 to register it and exercise the cleanup path; leave it unset
 * to show that without an app event callback the proxy never cleans up at all. */
static int proxy_event(coap_session_t *session COAP_UNUSED, coap_event_t ev COAP_UNUSED) {
  return 0;
}

/* ---- helpers ------------------------------------------------------------ */
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

  /* upstream URI -> ORIGIN (host ptr lives in this static buffer) */
  static uint8_t uri_buf[] = "coap://127.0.0.1:5701";
  coap_uri_t uri;
  if (coap_split_uri(uri_buf, strlen((char *)uri_buf), &uri) < 0) {
    fprintf(stderr, "split_uri failed\n");
    return 1;
  }
  rp_entry.uri = uri;

  /* ORIGIN */
  coap_context_t *origin = make_server(ORIGIN_PORT);
  coap_resource_t *ores =
      coap_resource_init(coap_make_str_const("test"), 0);
  coap_register_request_handler(ores, COAP_REQUEST_GET, origin_get);
  coap_add_resource(origin, ores);

  /* PROXY */
  coap_context_t *proxy = make_server(PROXY_PORT);
  coap_context_set_max_idle_sessions(proxy, 1);   /* force eviction at 2nd client */
  coap_resource_t *pres = coap_resource_reverse_proxy_init(proxy_handler, 0);
  coap_add_resource(proxy, pres);
  coap_register_response_handler(proxy, proxy_response);
  if (getenv("REGISTER_EVT")) {
    coap_register_event_handler(proxy, proxy_event);
    fprintf(stderr, "[repro] event handler REGISTERED (proxy cleanup path active)\n");
  } else {
    fprintf(stderr, "[repro] NO event handler (proxy cleanup gated-off)\n");
  }

  /* CLIENT context (holds both client sockets S and S2) */
  coap_context_t *client = coap_new_context(NULL);

  /* ---- Phase 1: S issues TWO requests; forward them, but DO NOT pump ORIGIN
   *      so it cannot answer yet. After this the proxy_entry holds 2 req
   *      entries, both .incoming == S. -------------------------------------- */
  coap_session_t *S = make_client(client, PROXY_PORT);
  send_get(S, (const uint8_t *)"\x11\x11\x11\x11", 4);
  send_get(S, (const uint8_t *)"\x22\x22\x22\x22", 4);
  {
    coap_context_t *cp[] = {client, proxy, NULL};  /* note: ORIGIN excluded */
    pump(12, cp);
  }
  fprintf(stderr, "[repro] phase1 done: 2 requests forwarded, origin held\n");

  /* ---- Phase 2: a SECOND client session hits the proxy. On the proxy's
   *      new-session path, num_idle (= S, ref==0) >= max_idle_sessions(1) so
   *      the oldest idle server session (S) is evicted and freed. That runs
   *      coap_proxy_remove_association(S), which removes ONLY the first req
   *      entry -> the second still points at the now-freed S. ---------------- */
  coap_session_t *S2 = make_client(client, PROXY_PORT);
  send_get(S2, (const uint8_t *)"\x33\x33\x33\x33", 4);
  {
    coap_context_t *cp[] = {client, proxy, NULL};  /* still no origin */
    pump(12, cp);
  }
  fprintf(stderr, "[repro] phase2 done: S2 sent -> S should be evicted+freed\n");

  /* ---- Phase 3: now let ORIGIN answer. The proxy forwards the surviving
   *      (T2) response and dereferences the freed S => UAF (ASan fires). ----- */
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
