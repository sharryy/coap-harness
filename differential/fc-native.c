/* Native FreeCoAP server from the same fc-server.c the KLEE target links. */

#include "bridge.h"

int main(void)
{
    if (fc_server_init() < 0)
        return 1;
    for (;;)
        fc_server_step();
}
