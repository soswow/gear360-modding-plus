/*
 * Read-only recorder of the Linux<->RTOS IPCC rings (plan step 2).
 *
 * The shared IPCC window is physical 0x02010000-0x02011fff (/proc/iomem names
 * it d5_ipcc). This helper maps it PROT_READ through /dev/mem and never writes.
 *
 *   ipccrec peek <phys-hex> <length>
 *       hex-dump <length> bytes (<= 8192) at <phys-hex>.
 *   ipccrec record <out-file> <seconds> <period-us> <ring-spec>...
 *       ring-spec = name:write_off:data_off:capacity (offsets in hex, capacity
 *       decimal), for example A:4c:ec:395. Each producer-cursor advance is
 *       written as one line:
 *         <monotonic-us> <ring> <old-w> <new-w> <read-cursor> <hex bytes>
 *       containing every byte between the old and the new write cursor. Each
 *       poll that produced output is written and fsynced immediately.
 *
 * Freestanding static ARM binary (no libc), same build style as rtoslog.
 */
#include <stddef.h>
#include <stdint.h>

#define SYS_exit 1u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_munmap 91u
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
#define CLOCK_MONOTONIC 1

#define WINDOW_PHYS 0x02010000u
#define WINDOW_BYTES 0x2000u
#define MAX_RINGS 8
#define OUT_CAP (4u * 1024u * 1024u)

struct timespec32 {
  long sec;
  long nsec;
};

static inline long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
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

static size_t text_length(const char *t) {
  size_t n = 0;
  while (t[n]) n++;
  return n;
}

static void write_all(long fd, const void *data, size_t length) {
  const uint8_t *p = (const uint8_t *)data;
  while (length > 0) {
    long n = syscall6(SYS_write, fd, (long)p, (long)length, 0, 0, 0);
    if (failed(n) || n == 0) return;
    p += n;
    length -= (size_t)n;
  }
}

static __attribute__((noreturn)) void finish(int code, const char *text) {
  write_all(code ? 2 : 1, text, text_length(text));
  syscall6(SYS_exit, code, 0, 0, 0, 0, 0);
  for (;;) {
  }
}

static char *out;
static uint32_t out_len;
static uint32_t out_dropped;

static void put_char(char c) {
  if (out_len < OUT_CAP) {
    out[out_len++] = c;
  } else {
    out_dropped++;
  }
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

static void put_hex8(uint8_t v) {
  static const char table[] = "0123456789abcdef";
  put_char(table[v >> 4]);
  put_char(table[v & 15u]);
}

static int parse_hex(const char *t, uint32_t *v) {
  uint32_t r = 0;
  if (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t += 2;
  if (!*t) return 0;
  for (; *t && *t != ':'; t++) {
    uint32_t d;
    if (*t >= '0' && *t <= '9') d = (uint32_t)(*t - '0');
    else if (*t >= 'a' && *t <= 'f') d = (uint32_t)(*t - 'a' + 10);
    else if (*t >= 'A' && *t <= 'F') d = (uint32_t)(*t - 'A' + 10);
    else return 0;
    r = r * 16u + d;
  }
  *v = r;
  return 1;
}

static int parse_dec(const char *t, uint32_t *v) {
  uint32_t r = 0;
  if (!*t) return 0;
  for (; *t && *t != ':'; t++) {
    if (*t < '0' || *t > '9') return 0;
    r = r * 10u + (uint32_t)(*t - '0');
  }
  *v = r;
  return 1;
}

static const char *after_colon(const char *t) {
  while (*t && *t != ':') t++;
  return *t ? t + 1 : t;
}

static uint32_t now_us(void) {
  struct timespec32 t;
  syscall6(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&t, 0, 0, 0, 0);
  return (uint32_t)t.sec * 1000000u + (uint32_t)t.nsec / 1000u;
}

static void sleep_us(uint32_t us) {
  struct timespec32 t = {(long)(us / 1000000u), (long)((us % 1000000u) * 1000u)};
  syscall6(SYS_nanosleep, (long)&t, 0, 0, 0, 0, 0);
}

static const volatile uint8_t *map_phys(uint32_t phys, uint32_t length) {
  long fd = syscall6(SYS_open, (long)"/dev/mem", O_RDONLY, 0, 0, 0, 0);
  if (failed(fd)) finish(1, "ERROR open /dev/mem\n");
  long addr = syscall6(SYS_mmap2, 0, (long)length, PROT_READ, MAP_SHARED, fd, (long)(phys >> 12));
  syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  if (failed(addr)) finish(1, "ERROR mmap /dev/mem\n");
  return (const volatile uint8_t *)addr;
}

static uint32_t rd32(const volatile uint8_t *base, uint32_t off) {
  return *(const volatile uint32_t *)(base + off);
}

static void peek(uint32_t phys, uint32_t length) {
  if (length == 0 || length > WINDOW_BYTES) finish(2, "ERROR length\n");
  uint32_t page = phys & ~4095u;
  uint32_t span = ((phys - page) + length + 4095u) & ~4095u;
  const volatile uint8_t *m = map_phys(page, span);
  const volatile uint8_t *p = m + (phys - page);
  for (uint32_t i = 0; i < length; i += 16) {
    put_text("+");
    put_hex8((uint8_t)(i >> 8));
    put_hex8((uint8_t)i);
    put_text(":");
    for (uint32_t j = 0; j < 16 && i + j < length; j++) {
      put_char(' ');
      put_hex8(p[i + j]);
    }
    put_char('\n');
  }
  syscall6(SYS_munmap, (long)m, (long)span, 0, 0, 0, 0);
  write_all(1, out, out_len);
}

struct ring {
  const char *name;
  uint32_t write_off;
  uint32_t data_off;
  uint32_t capacity;
  uint32_t last;
};

static void flush(long fd) {
  write_all(fd, out, out_len);
  syscall6(SYS_fsync, fd, 0, 0, 0, 0, 0);
  out_len = 0;
}

static void record(const char *path, uint32_t seconds, uint32_t period_us, int count, char **specs) {
  struct ring rings[MAX_RINGS];
  if (count < 1 || count > MAX_RINGS) finish(2, "ERROR ring count\n");
  for (int i = 0; i < count; i++) {
    const char *t = specs[i];
    rings[i].name = t;
    t = after_colon(t);
    if (!parse_hex(t, &rings[i].write_off)) finish(2, "ERROR ring write offset\n");
    t = after_colon(t);
    if (!parse_hex(t, &rings[i].data_off)) finish(2, "ERROR ring data offset\n");
    t = after_colon(t);
    if (!parse_dec(t, &rings[i].capacity) || rings[i].capacity == 0) finish(2, "ERROR ring capacity\n");
    if (rings[i].write_off + 8u > WINDOW_BYTES || rings[i].data_off + rings[i].capacity > WINDOW_BYTES)
      finish(2, "ERROR ring outside window\n");
  }
  const volatile uint8_t *w = map_phys(WINDOW_PHYS, WINDOW_BYTES);
  long fd = syscall6(SYS_open, (long)path, O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
  if (failed(fd)) finish(1, "ERROR open output\n");

  uint32_t start = now_us();
  uint32_t frames = 0;
  uint32_t odd = 0;
  for (int i = 0; i < count; i++) {
    rings[i].last = rd32(w, rings[i].write_off);
    put_text("INIT ");
    put_text(rings[i].name);
    put_text(" W ");
    put_dec(rings[i].last);
    put_text(" R ");
    put_dec(rd32(w, rings[i].write_off + 4u));
    put_char('\n');
  }
  flush(fd);
  for (;;) {
    uint32_t now = now_us();
    uint32_t elapsed = now - start;
    for (int i = 0; i < count; i++) {
      struct ring *r = &rings[i];
      uint32_t wc = rd32(w, r->write_off);
      if (wc == r->last) continue;
      uint32_t rc = rd32(w, r->write_off + 4u);
      put_dec(elapsed);
      put_char(' ');
      for (const char *n = r->name; *n && *n != ':'; n++) put_char(*n);
      put_char(' ');
      put_dec(r->last);
      put_char(' ');
      put_dec(wc);
      put_char(' ');
      put_dec(rc);
      put_char(' ');
      if (wc >= r->capacity || r->last >= r->capacity) {
        put_text("BADCURSOR");
        odd++;
      } else {
        uint32_t at = r->last;
        while (at != wc) {
          put_hex8(w[r->data_off + at]);
          at++;
          if (at == r->capacity) at = 0;
        }
      }
      put_char('\n');
      r->last = wc;
      frames++;
    }
    /* Flush every pass that produced output: a dying camera must not lose
       the last frames (the 2 November step-4 trial lost ~370 ms). */
    if (out_len) {
      flush(fd);
    }
    if (elapsed >= seconds * 1000000u) break;
    if (period_us) sleep_us(period_us);
  }
  put_text("SUMMARY frames ");
  put_dec(frames);
  put_text(" badcursor ");
  put_dec(odd);
  put_text(" dropped ");
  put_dec(out_dropped);
  put_char('\n');
  flush(fd);
  syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  syscall6(SYS_munmap, (long)w, WINDOW_BYTES, 0, 0, 0, 0);
  finish(0, "OK record\n");
}

static int same(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

void c_entry(long *stack) {
  int argc = (int)stack[0];
  char **argv = (char **)(stack + 1);
  long buffer = syscall6(SYS_mmap2, 0, OUT_CAP, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (failed(buffer)) finish(1, "ERROR output buffer\n");
  out = (char *)buffer;
  uint32_t a = 0, b = 0, c = 0;
  if (argc == 4 && same(argv[1], "peek") && parse_hex(argv[2], &a) && parse_dec(argv[3], &b)) {
    peek(a, b);
    finish(0, "");
  }
  if (argc >= 6 && same(argv[1], "record") && parse_dec(argv[3], &b) && parse_dec(argv[4], &c)) {
    record(argv[2], b, c, argc - 5, argv + 5);
  }
  finish(2,
         "usage: ipccrec peek <phys-hex> <len> | "
         "ipccrec record <out> <seconds> <period-us> <name:woff:doff:cap>...\n");
}
