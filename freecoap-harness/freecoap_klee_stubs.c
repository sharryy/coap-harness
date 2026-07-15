/* Stubs for libc facilities the FreeCoAP *server* uses during setup/teardown
 * that KLEE's POSIX runtime doesn't model. Linked with --override so these win.
 *
 * Only the server-setup path is stubbed (we don't run FreeCoAP's client):
 *   - getaddrinfo/freeaddrinfo : coap_server_create resolves host:port. We
 *     return a single static 127.0.0.1:<port> UDP addrinfo so socket()+bind()
 *     (the latter routed to CoAP_bind_model) proceed deterministically.
 *   - getifaddrs/freeifaddrs   : libcoap client interface probe (unmodeled). */

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* NOTE 1: fcntl on a real socket fd is handled by a fallback in KLEE's POSIX
 * runtime (runtime/POSIX/fd.c) — it can't be overridden here because the
 * runtime already defines fcntl (would be "multiply defined").
 *
 * NOTE 2: timerfd_create/timerfd_settime are NOT stubbed. KLEE passes them to
 * the real kernel as external calls (like epoll_*), so both FreeCoAP's
 * per-transaction timer and libcoap's context timer get REAL timerfds — which
 * libcoap needs, since it adds its timerfd to epoll (a fake fd breaks that). */

/* libcoap client also probes interfaces; KLEE doesn't model getifaddrs.
 * (Same stubs as the libcoap-harness klee_stubs.c.) */
int getifaddrs(struct ifaddrs **ifap) { if (ifap) *ifap = NULL; errno = ENOSYS; return -1; }
void freeifaddrs(struct ifaddrs *ifa) { (void)ifa; }

/* Freeze libcoap's clock: coap_ticks() -> clock_gettime() reads real host
 * wall-clock, which under KLEE advances with interpretation cost and trips the
 * 2 s ACK-timeout retransmit (coap_io.c:1353-1355) on drop forks — the libcoap
 * CON client then re-sends a concrete request that the FreeCoAP server answers,
 * fabricating a phantom response on ACK/RST paths. Pinning it to 0 keeps the
 * retransmit deadline in the future forever. (Mirror of libcoap-harness fix.) */
int clock_gettime(clockid_t clk, struct timespec *tp) {
    (void)clk;
    if (tp) { tp->tv_sec = 0; tp->tv_nsec = 0; }
    return 0;
}

static struct sockaddr_in g_sin;
static struct addrinfo    g_ai;

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    (void)node;
    (void)hints;

    memset(&g_sin, 0, sizeof g_sin);

    g_sin.sin_family      = AF_INET;
    g_sin.sin_port        = htons((unsigned short)(service ? atoi(service) : 5683));
    g_sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    memset(&g_ai, 0, sizeof g_ai);
    g_ai.ai_family   = AF_INET;
    g_ai.ai_socktype = SOCK_DGRAM;
    g_ai.ai_protocol = 0;
    g_ai.ai_addr     = (struct sockaddr *)&g_sin;
    g_ai.ai_addrlen  = sizeof g_sin;
    g_ai.ai_next     = NULL;

    *res = &g_ai;
    return 0;
}

void freeaddrinfo(struct addrinfo *res) { (void)res; }
