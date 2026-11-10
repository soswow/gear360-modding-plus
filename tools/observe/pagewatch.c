/*
 * Read-only watcher of physical memory ranges (plan step 4j).
 *
 *   pagewatch [-a] [-u] <out-file> <seconds> <interval> <phys-hex:len-hex>...
 *
 * Maps each range PROT_READ through /dev/mem, keeps a private copy, and every
 * interval logs each 32-bit word that changed since the previous pass:
 *   C <monotonic-ms> <phys-address> <old> <new>
 * (at most 8 words per 4 KB page per pass, then "+N more"; -a logs every
 * changed word; -u takes the interval in microseconds and prints times in
 * monotonic microseconds instead of milliseconds). The first line is
 * "START <monotonic-ms>". Output is written with fsync every pass that
 * produced lines. Never writes device memory. Freestanding static ARM binary.
 */
#include <stddef.h>
#include <stdint.h>

#define SYS_exit 1u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_fsync 118u
#define SYS_nanosleep 162u
#define SYS_mmap2 192u
#define SYS_clock_gettime 263u
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAX_RANGES 16
#define OUT_CAP (1u << 20)

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

static char *out;
static uint32_t out_len;

static void put_char(char c) {
  if (out_len < OUT_CAP) out[out_len++] = c;
}
static void put_text(const char *t) {
  while (*t) put_char(*t++);
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
static void put_hex(uint32_t v) {
  static const char h[] = "0123456789abcdef";
  put_text("0x");
  for (int i = 28; i >= 0; i -= 4) put_char(h[(v >> i) & 15u]);
}
static uint32_t parse_hex(const char *t, const char **end) {
  uint32_t v = 0;
  if (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t += 2;
  for (;; t++) {
    uint32_t d;
    if (*t >= '0' && *t <= '9') d = (uint32_t)(*t - '0');
    else if (*t >= 'a' && *t <= 'f') d = (uint32_t)(*t - 'a' + 10);
    else if (*t >= 'A' && *t <= 'F') d = (uint32_t)(*t - 'A' + 10);
    else break;
    v = v * 16u + d;
  }
  if (end) *end = t;
  return v;
}
static uint32_t parse_dec(const char *t) {
  uint32_t v = 0;
  while (*t >= '0' && *t <= '9') v = v * 10u + (uint32_t)(*t++ - '0');
  return v;
}
static uint32_t now_us(void) {
  struct timespec32 t;
  sys6(SYS_clock_gettime, 1, (long)&t, 0, 0, 0, 0);
  return (uint32_t)t.sec * 1000000u + (uint32_t)t.nsec / 1000u;
}
static uint32_t now_ms(void) {
  struct timespec32 t;
  sys6(SYS_clock_gettime, 1, (long)&t, 0, 0, 0, 0);
  return (uint32_t)t.sec * 1000u + (uint32_t)t.nsec / 1000000u;
}
static void sleep_ms(uint32_t ms) {
  struct timespec32 t = {(long)(ms / 1000u), (long)((ms % 1000u) * 1000000u)};
  sys6(SYS_nanosleep, (long)&t, 0, 0, 0, 0, 0);
}
static void flush(long fd) {
  if (!out_len) return;
  long off = 0;
  while ((uint32_t)off < out_len) {
    long w = sys6(SYS_write, fd, (long)(out + off), (long)(out_len - (uint32_t)off), 0, 0, 0);
    if (w <= 0) break;
    off += w;
  }
  sys6(SYS_fsync, fd, 0, 0, 0, 0, 0);
  out_len = 0;
}

struct range {
  uint32_t phys;
  uint32_t len;
  const volatile uint32_t *live;
  uint32_t *copy;
};

void c_entry(long *stack) {
  int argc = (int)stack[0];
  char **argv = (char **)(stack + 1);
  uint32_t page_cap = 8u;
  int micro = 0;
  while (argc > 1 && argv[1][0] == '-' && argv[1][2] == 0 && (argv[1][1] == 'a' || argv[1][1] == 'u')) {
    if (argv[1][1] == 'a') page_cap = 0xffffffffu;
    if (argv[1][1] == 'u') micro = 1;
    argc--;
    argv++;
  }
  if (argc < 5) {
    sys6(SYS_write, 2, (long)"usage: pagewatch [-a] [-u] <out> <seconds> <interval> <phys:len>...\n", 68, 0, 0, 0);
    sys6(SYS_exit, 2, 0, 0, 0, 0, 0);
  }
  long buffer = sys6(SYS_mmap2, 0, OUT_CAP, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (failed(buffer)) sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  out = (char *)buffer;
  uint32_t seconds = parse_dec(argv[2]);
  uint32_t interval = parse_dec(argv[3]);
  struct range ranges[MAX_RANGES];
  int count = 0;
  long mem = sys6(SYS_open, (long)"/dev/mem", O_RDONLY, 0, 0, 0, 0);
  if (failed(mem)) sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  for (int i = 4; i < argc && count < MAX_RANGES; i++) {
    const char *end;
    uint32_t phys = parse_hex(argv[i], &end);
    uint32_t len = *end == ':' ? parse_hex(end + 1, 0) : 4096u;
    len = (len + 4095u) & ~4095u;
    long live = sys6(SYS_mmap2, 0, (long)len, PROT_READ, MAP_SHARED, mem, (long)(phys >> 12));
    long copy = sys6(SYS_mmap2, 0, (long)len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (failed(live) || failed(copy)) {
      put_text("ERROR map ");
      put_hex(phys);
      put_char('\n');
      continue;
    }
    ranges[count].phys = phys;
    ranges[count].len = len;
    ranges[count].live = (const volatile uint32_t *)live;
    ranges[count].copy = (uint32_t *)copy;
    for (uint32_t w = 0; w < len / 4u; w++) ranges[count].copy[w] = ranges[count].live[w];
    count++;
  }
  sys6(SYS_close, mem, 0, 0, 0, 0, 0);
  long fd = sys6(SYS_open, (long)argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
  if (failed(fd)) sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  uint32_t start = micro ? now_us() : now_ms();
  put_text("START ");
  put_dec(start);
  put_text(" ranges ");
  put_dec((uint32_t)count);
  put_char('\n');
  flush(fd);
  for (;;) {
    uint32_t t = micro ? now_us() : now_ms();
    for (int r = 0; r < count; r++) {
      struct range *g = &ranges[r];
      for (uint32_t page = 0; page < g->len; page += 4096u) {
        uint32_t shown = 0, more = 0;
        for (uint32_t w = page / 4u; w < (page + 4096u) / 4u; w++) {
          uint32_t v = g->live[w];
          if (v != g->copy[w]) {
            if (shown < page_cap) {
              put_text("C ");
              put_dec(t);
              put_char(' ');
              put_hex(g->phys + w * 4u);
              put_char(' ');
              put_hex(g->copy[w]);
              put_char(' ');
              put_hex(v);
              put_char('\n');
              shown++;
            } else {
              more++;
            }
            g->copy[w] = v;
          }
        }
        if (more) {
          put_text("C ");
          put_dec(t);
          put_char(' ');
          put_hex(g->phys + page);
          put_text(" +");
          put_dec(more);
          put_text(" more\n");
        }
      }
    }
    flush(fd);
    if (t - start >= seconds * (micro ? 1000000u : 1000u)) break;
    if (micro) {
      struct timespec32 ts = {(long)(interval / 1000000u), (long)((interval % 1000000u) * 1000u)};
      sys6(SYS_nanosleep, (long)&ts, 0, 0, 0, 0, 0);
    } else {
      sleep_ms(interval);
    }
  }
  put_text("END\n");
  flush(fd);
  sys6(SYS_close, fd, 0, 0, 0, 0, 0);
  sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
