/* Stubs for libc functions that KLEE's POSIX runtime doesn't model.
 * Each one returns the "no/failure" branch that lets libcoap fall
 * through to a safe default path. */

#include <bits/types/clockid_t.h>
#include <errno.h>
#include <ifaddrs.h>
#include <stddef.h>
#include <time.h>

int getifaddrs(struct ifaddrs **ifap) {
  if (ifap)
    *ifap = NULL;
  errno = ENOSYS;
  return -1;
}

void freeifaddrs(struct ifaddrs *ifa) { (void)ifa; }

int clock_gettime(clockid_t clk, struct timespec *tp) {
  (void)clk;
  if (tp) {
    tp->tv_sec = 0;
    tp->tv_nsec = 0;
  }
  return 0;
}

typedef int coap_log_t;
void coap_log_impl(coap_log_t level, const char *format, ...) {
  (void)level;
  (void)format;
}
