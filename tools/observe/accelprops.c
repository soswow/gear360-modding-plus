/*
 * Read-only check of the motion-history properties the stock app's gyro
 * responder reads before ACGY_GetAngle_Still (strace 2 November 2026):
 * three _IOR('l', n, 4) "get" ioctls on /dev/accel (0x80046c0a, 0x80046c06,
 * 0x80046c07), then the start of the history region at physical 0x9cd00000
 * mapped PROT_READ through /dev/mem.
 *
 *   accelprops [count] [interval-ms]
 *
 * Prints one line per sample: monotonic ms, the three property values, and
 * the first eight words of the history region. Never writes a device.
 */
#include <stddef.h>
#include <stdint.h>

#define SYS_exit 1u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_ioctl 54u
#define SYS_nanosleep 162u
#define SYS_mmap2 192u
#define SYS_clock_gettime 263u
#define O_RDONLY 0
#define O_RDWR 2
#define PROT_READ 1
#define MAP_SHARED 1

#define HISTORY_PHYS 0x9cd00000u

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

static int failed(long r) { return (unsigned long)r >= (unsigned long)-4095; }

static char line[512];
static size_t pos;

static void put_char(char c) {
  if (pos < sizeof(line) - 1) line[pos++] = c;
}
static void put_text(const char *t) {
  while (*t) put_char(*t++);
}
static void put_hex(uint32_t v) {
  static const char hex[] = "0123456789abcdef";
  put_text("0x");
  for (int i = 28; i >= 0; i -= 4) put_char(hex[(v >> i) & 15u]);
}
static void put_dec(uint32_t v) {
  char d[11];
  int n = 0;
  do {
    d[n++] = (char)('0' + v % 10u);
    v /= 10u;
  } while (v);
  while (n) put_char(d[--n]);
}
static void flush_line(void) {
  put_char('\n');
  sys6(SYS_write, 1, (long)line, (long)pos, 0, 0, 0);
  pos = 0;
}
static uint32_t parse_dec(const char *t) {
  uint32_t v = 0;
  while (*t >= '0' && *t <= '9') v = v * 10u + (uint32_t)(*t++ - '0');
  return v;
}

void c_entry(long *stack) {
  int argc = (int)stack[0];
  char **argv = (char **)(stack + 1);
  uint32_t count = argc > 1 ? parse_dec(argv[1]) : 1u;
  uint32_t interval = argc > 2 ? parse_dec(argv[2]) : 500u;
  static const uint32_t cmds[3] = {0x80046c0au, 0x80046c06u, 0x80046c07u};
  long mem = sys6(SYS_open, (long)"/dev/mem", O_RDONLY, 0, 0, 0, 0);
  const volatile uint32_t *hist = 0;
  if (!failed(mem)) {
    long m = sys6(SYS_mmap2, 0, 4096, PROT_READ, MAP_SHARED, mem, (long)(HISTORY_PHYS >> 12));
    if (!failed(m)) hist = (const volatile uint32_t *)m;
    sys6(SYS_close, mem, 0, 0, 0, 0, 0);
  }
  for (uint32_t i = 0; i < count; i++) {
    struct timespec32 t;
    sys6(SYS_clock_gettime, 1, (long)&t, 0, 0, 0, 0);
    put_dec((uint32_t)t.sec * 1000u + (uint32_t)t.nsec / 1000000u);
    for (int c = 0; c < 3; c++) {
      int32_t value = 0x7eadbeef;
      long fd = sys6(SYS_open, (long)"/dev/accel", O_RDWR, 0, 0, 0, 0);
      long rc = failed(fd) ? fd : sys6(SYS_ioctl, fd, (long)cmds[c], (long)&value, 0, 0, 0);
      if (!failed(fd)) sys6(SYS_close, fd, 0, 0, 0, 0, 0);
      put_text(" p");
      put_dec((uint32_t)c);
      put_text("=");
      if (failed(rc)) {
        put_text("ERR");
        put_dec((uint32_t)(-rc));
      } else {
        put_hex((uint32_t)value);
        put_text("(");
        put_dec((uint32_t)value);
        put_text(")");
      }
    }
    put_text(" hist");
    if (hist) {
      for (int w = 0; w < 8; w++) {
        put_char(' ');
        put_hex(hist[w]);
      }
    } else {
      put_text(" unmapped");
    }
    flush_line();
    if (i + 1 < count) {
      struct timespec32 d = {(long)(interval / 1000u), (long)((interval % 1000u) * 1000000u)};
      sys6(SYS_nanosleep, (long)&d, 0, 0, 0, 0, 0);
    }
  }
  sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
