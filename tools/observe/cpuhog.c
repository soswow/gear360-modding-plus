/*
 * Keep the Linux CPU busy for N seconds, then exit (plan step 4i).
 *
 *   cpuhog <seconds>
 *
 * Pure user-space spin; checks the monotonic clock every ~1M iterations.
 * Touches no device. Freestanding static ARM binary, same build style as
 * rtoslog.
 */
#include <stdint.h>

#define SYS_exit 1u
#define SYS_clock_gettime 263u

struct timespec32 {
  long sec;
  long nsec;
};

static inline long sys6(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
  register long r7 __asm__("r7") = nr;
  register long r0 __asm__("r0") = a0;
  register long r1 __asm__("r1") = a1;
  register long r2 __asm__("r2") = a2;
  register long r3 __asm__("r3") = a3;
  register long r4 __asm__("r4") = a4;
  register long r5 __asm__("r5") = a5;
  __asm__ volatile("svc #0"
                   : "+r"(r0)
                   : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
                   : "memory");
  return r0;
}

static uint32_t now_s(void) {
  struct timespec32 t;
  sys6(SYS_clock_gettime, 1, (long)&t, 0, 0, 0, 0);
  return (uint32_t)t.sec;
}

void c_entry(long *stack) {
  int argc = (int)stack[0];
  char **argv = (char **)(stack + 1);
  uint32_t seconds = 60;
  if (argc > 1) {
    seconds = 0;
    for (const char *t = argv[1]; *t >= '0' && *t <= '9'; t++) seconds = seconds * 10u + (uint32_t)(*t - '0');
  }
  uint32_t end = now_s() + seconds;
  volatile uint32_t sink = 0;
  for (;;) {
    for (uint32_t i = 0; i < 1000000u; i++) sink += i;
    if (now_s() >= end) break;
  }
  sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
