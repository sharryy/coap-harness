/* Native libcoap server with the harness's /test resource, for checking KLEE paths natively. */
#include <coap3/coap.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* compat shims: builds against libcoap 4.3.1 and 4.3.5 */
#ifndef COAP_LOG_WARN
#define COAP_LOG_WARN LOG_WARNING
#endif
#ifndef COAP_RESOURCE_SAFE_REQUEST_HANDLER
#define COAP_RESOURCE_SAFE_REQUEST_HANDLER 0
#endif

#define RESOURCE_PAYLOAD "Hello World"

static volatile sig_atomic_t running = 1;

static void on_term(int sig) { (void)sig; running = 0; }

static int server_port(void) {
  const char *p = getenv("SERVER_PORT");
  return p ? atoi(p) : 5683;
}

/* same handler as libcoap-standalone.c */
static void hnd_get_test(coap_resource_t *resource, coap_session_t *session,
                         const coap_pdu_t *request, const coap_string_t *query,
                         coap_pdu_t *response) {
  (void)resource; (void)session; (void)request; (void)query;
  coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
  coap_add_data(response, strlen(RESOURCE_PAYLOAD), (const uint8_t *)RESOURCE_PAYLOAD);
}

int main(void) {
  signal(SIGTERM, on_term);
  signal(SIGINT, on_term);
  coap_startup();
  coap_set_log_level(COAP_LOG_WARN);

  coap_context_t *ctx = coap_new_context(NULL);
  if (!ctx)
    return 1;

  coap_address_t addr;
  coap_address_init(&addr);
  addr.addr.sin.sin_family = AF_INET;
  addr.addr.sin.sin_port = htons(server_port());
  addr.addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  coap_resource_t *resource = coap_resource_init(
      coap_make_str_const("test"), COAP_RESOURCE_SAFE_REQUEST_HANDLER);
  coap_register_request_handler(resource, COAP_REQUEST_GET, hnd_get_test);
  coap_add_resource(ctx, resource);

  if (!coap_new_endpoint(ctx, &addr, COAP_PROTO_UDP)) {
    fprintf(stderr, "[lc-native] endpoint failed (port busy?)\n");
    return 1;
  }

  while (running)
    coap_io_process(ctx, 500);

  coap_free_context(ctx);
  coap_cleanup();
  return 0;
}
