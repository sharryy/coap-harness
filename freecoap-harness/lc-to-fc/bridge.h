#ifndef FC_BRIDGE_H
#define FC_BRIDGE_H

/* FreeCoAP server (SUT) */
int  fc_server_init(void);
int  fc_server_step(void);

/* libcoap client (driver) */
int  lc_client_init(void);
void lc_client_send(void);
int  lc_client_step(void);

void coap_exchange_finalize(void);

#endif
