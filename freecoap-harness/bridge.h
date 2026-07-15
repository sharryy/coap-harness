#ifndef FC_BRIDGE_H
#define FC_BRIDGE_H

/* FreeCoAP server (SUT) */
int  fc_server_init(void);   /* coap_mem + coap_server_create; 0 = ok, <0 = err */
int  fc_server_step(void);   /* one accept/recv/handle/respond iteration       */

/* libcoap client (driver; identical to the libcoap-harness client) */
int  lc_client_init(void);   /* context + client session + response handler    */
void lc_client_send(void);   /* send one CON GET /test with a 4-byte token      */
int  lc_client_step(void);   /* one non-blocking io pass; 1 once a response came */

/* Strong def provided by the Kleener socket model; weak no-op for native. */
void coap_exchange_finalize(void);

#endif
