/*
 * Read-only recorder for the motion history (20-byte samples) the stock app's
 * gyro responder reads. Reads the position property, waits, reads it again and
 * prints every sample written in between as raw hex words. Never writes a device.
 *
 *   accelrec [seconds]
 *
 * Output: "# t0_ms pos0 t1_ms pos1" then "S index w0 w1 w2 w3 w4" per sample.
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
#define HISTORY_SIZE 8000000u
#define SAMPLE_BYTES 20u
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


static uint32_t now_ms(void) {
  struct timespec32 t;
  sys6(SYS_clock_gettime, 1, (long)&t, 0, 0, 0, 0);
  return (uint32_t)t.sec * 1000u + (uint32_t)t.nsec / 1000000u;
}
static uint32_t read_pos(void) {
  int32_t value = 0;
  long fd = sys6(SYS_open, (long)"/dev/accel", O_RDWR, 0, 0, 0, 0);
  if (failed(fd)) return 0xffffffffu;
  long rc = sys6(SYS_ioctl, fd, (long)0x80046c07u, (long)&value, 0, 0, 0);
  sys6(SYS_close, fd, 0, 0, 0, 0, 0);
  return failed(rc) ? 0xffffffffu : (uint32_t)value;
}

void c_entry(long *stack) {
  int argc = (int)stack[0];
  char **argv = (char **)(stack + 1);
  uint32_t seconds = argc > 1 ? parse_dec(argv[1]) : 10u;
  long mem = sys6(SYS_open, (long)"/dev/mem", O_RDONLY, 0, 0, 0, 0);
  const volatile uint32_t *hist = 0;
  if (!failed(mem)) {
    long m = sys6(SYS_mmap2, 0, HISTORY_SIZE, PROT_READ, MAP_SHARED, mem, (long)(HISTORY_PHYS >> 12));
    if (!failed(m)) hist = (const volatile uint32_t *)m;
    sys6(SYS_close, mem, 0, 0, 0, 0, 0);
  }
  if (!hist) {
    put_text("# error: history not mapped");
    flush_line();
    sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  }
  uint32_t t0 = now_ms();
  uint32_t p0 = read_pos();
  struct timespec32 d = {(long)seconds, 0};
  sys6(SYS_nanosleep, (long)&d, 0, 0, 0, 0, 0);
  uint32_t t1 = now_ms();
  uint32_t p1 = read_pos();
  put_text("# ");
  put_dec(t0); put_char(' '); put_dec(p0); put_char(' ');
  put_dec(t1); put_char(' '); put_dec(p1);
  flush_line();
  uint32_t slots = HISTORY_SIZE / SAMPLE_BYTES;
  uint32_t n = p1 - p0;
  if (n > slots) n = slots;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t idx = (p0 + i) % slots;
    const volatile uint32_t *s = hist + idx * (SAMPLE_BYTES / 4u);
    put_text("S "); put_dec(idx);
    for (int w = 0; w < 5; w++) { put_char(' '); put_hex(s[w]); }
    flush_line();
  }
  sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
