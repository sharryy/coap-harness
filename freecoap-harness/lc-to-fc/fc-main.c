/* Orchestrator for the libcoap-client -> FreeCoAP-server differential run. */

#include <stdio.h>
#include "bridge.h"

/* weak no-op for native builds; socket model provides strong def under KLEE */
__attribute__((weak)) void coap_exchange_finalize(void) {}

int main(void)
{
    int got = 0;
    int i;

    if (fc_server_init() < 0)
        return 1;


    if (lc_client_init() < 0)
        return 1;

    lc_client_send();

    fprintf(stderr, "[fc-main] entering bounded exchange loop\n");
    for (i = 0; i < 16 && !got; i++)
    {
        fc_server_step();
        got = lc_client_step();
    }

    coap_exchange_finalize();
    fprintf(stderr, "[fc-main] exchange complete (response_received=%d)\n", got);
    return 0;
}
