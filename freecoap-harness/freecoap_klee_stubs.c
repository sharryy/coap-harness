/* Stubs for libc facilities KLEE's POSIX runtime doesn't model (server setup path). */

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int getifaddrs(struct ifaddrs **ifap) { if (ifap) *ifap = NULL; errno = ENOSYS; return -1; }
void freeifaddrs(struct ifaddrs *ifa) { (void)ifa; }

/* Freeze libcoap's clock at 0 to prevent the ACK-timeout retransmit under KLEE. */
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
