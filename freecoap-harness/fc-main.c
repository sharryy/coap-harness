/* Orchestrator for the libcoap-client -> FreeCoAP-server differential run.
 *
 * Single process, no fork. The Kleener CoAP socket model (routed by
 * PROTOCOL=CoAP) shuttles datagrams between the two sides via its per-side
 * queues, running the selected monitor exactly once per datagram. We drive the
 * libcoap client and the FreeCoAP server by hand, alternating one non-blocking
 * step each, bounded so a (correct or buggy) silent drop can't spin forever.
 *
 * Includes neither CoAP library's headers — only the lib-agnostic bridge. */

#include <stdio.h>
#include "bridge.h"

/* Weak no-op so a native build resolves it; the socket model provides the
 * strong definition under KLEE (fires if a requirement demanded a response
 * that never arrived). */
__attribute__((weak)) void coap_exchange_finalize(void) {}

int main(void)
{
    int got = 0;
    int i;

    /* Server binds :5683 first (-> server_fd), then the client binds its
     * ephemeral port (-> client_fd). No traffic flows until lc_client_send. */
    if (fc_server_init() < 0)
        return 1;


    if (lc_client_init() < 0)
        return 1;

    lc_client_send();

    fprintf(stderr, "[fc-main] entering bounded exchange loop\n");
    for (i = 0; i < 16 && !got; i++)
    {
        fc_server_step();        /* FreeCoAP: peek->peek->consume->handle->respond */
        got = lc_client_step();  /* libcoap: process any response                  */
    }

    coap_exchange_finalize();
    fprintf(stderr, "[fc-main] exchange complete (response_received=%d)\n", got);
    return 0;
}
