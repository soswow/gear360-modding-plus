/*
 * Read-only watcher of individual 32-bit device registers (image-engine
 * investigation, 3 November 2026).
 *
 *   regwatch [-r] [-s] [-b <ms>] <out-file> <seconds> <interval-us> [<phys-hex>...]
 *
 * Maps the page of each listed physical register PROT_READ through /dev/mem
 * and, every interval, loads exactly the listed words (no other word in the
 * page is touched; nothing is ever written). Logs:
 *   START <monotonic-sec>.<usec> interval <us> regs <n> rings <0|1> sync <0|1>
 *   I <phys> <value>           first value of each register
 *   C <us> <phys> <old> <new>  a register changed
 *   R <us> <ring> <old> <new>  with -r: ring A/B write cursor advanced
 *                              (IPCC window 0x02010000, +0x4c / +0x54)
 *   B <us>                     with -b: heartbeat every <ms> milliseconds
 *   GAP <us> <gap-us>          a pass started late (> 4 intervals, min 2 ms)
 *   END <us> passes <n> maxgap <us> gaps <n>
 * <us> is monotonic microseconds since START. Output is fsynced at most every
 * 100 ms; with -s after every pass that produced lines, so a power cut loses
 * at most the pass in progress. A load that never returns stops the output:
 * the last heartbeat then bounds when it began. With no register listed it
 * touches no device register at all: a pure heartbeat (plus -r ring cursors).
 * Freestanding static ARM binary, same build as irqwatch.
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
#define CLOCK_MONOTONIC 1
#define MAX_REGS 16
#define OUT_CAP (1u << 20)
#define IPCC_PHYS 0x02010000u
#define IPCC_BYTES 0x2000u

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

#define UDP_SYSCALL6 sys6
#include "udpout.h"

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
static void put_dec6(uint32_t v) {
  for (uint32_t div = 100000u; div; div /= 10u) put_char((char)('0' + (v / div) % 10u));
}
static void put_hex(uint32_t v) {
  static const char h[] = "0123456789abcdef";
  put_text("0x");
  for (int i = 28; i >= 0; i -= 4) put_char(h[(v >> i) & 15u]);
}
static uint32_t parse_dec(const char *t) {
  uint32_t v = 0;
  while (*t >= '0' && *t <= '9') v = v * 10u + (uint32_t)(*t++ - '0');
  return v;
}
static uint32_t parse_hex(const char *t) {
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
  return v;
}

static long sec0, usec0;
static void clock_now(long *sec, long *usec) {
  struct timespec32 t;
  sys6(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&t, 0, 0, 0, 0);
  *sec = t.sec;
  *usec = t.nsec / 1000;
}
/* Microseconds since START; wraps after 71 minutes. */
static uint32_t rel_us(void) {
  long s, u;
  clock_now(&s, &u);
  return (uint32_t)(s - sec0) * 1000000u + (uint32_t)(u - usec0);
}
static void sleep_us(uint32_t us) {
  struct timespec32 t = {(long)(us / 1000000u), (long)((us % 1000000u) * 1000u)};
  sys6(SYS_nanosleep, (long)&t, 0, 0, 0, 0, 0);
}
static void flush(long fd) {
  if (!out_len) return;
  udp_send(out, out_len);
  if (fd < 0) {
    out_len = 0;
    return;
  }
  long off = 0;
  while ((uint32_t)off < out_len) {
    long w = sys6(SYS_write, fd, (long)(out + off), (long)(out_len - (uint32_t)off), 0, 0, 0);
    if (w <= 0) break;
    off += w;
  }
  sys6(SYS_fsync, fd, 0, 0, 0, 0, 0);
  out_len = 0;
}
static const volatile uint32_t *map_word(long mem, uint32_t phys) {
  long base = sys6(SYS_mmap2, 0, 4096, PROT_READ, MAP_SHARED, mem, (long)(phys >> 12));
  if (failed(base)) return 0;
  return (const volatile uint32_t *)(base + (long)(phys & 0xffcu));
}

void c_entry(long *stack) {
  int argc = (int)stack[0];
  char **argv = (char **)(stack + 1);
  int rings = 0, sync_each = 0;
  uint32_t beat_ms = 0;
  for (;;) {
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'r' && argv[1][2] == 0) {
      rings = 1;
    } else if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 's' && argv[1][2] == 0) {
      sync_each = 1;
    } else if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'b' && argv[1][2] == 0) {
      beat_ms = parse_dec(argv[2]);
      argc--;
      argv++;
    } else if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'n' && argv[1][2] == 0) {
      if (udp_open(argv[2]) != 0) sys6(SYS_exit, 2, 0, 0, 0, 0, 0);
      argc--;
      argv++;
    } else {
      break;
    }
    argc--;
    argv++;
  }
  if (argc < 4) {
    static const char usage[] =
        "usage: regwatch [-r] [-s] [-b ms] [-n ip:port] <out|-> <seconds> <interval-us> [<phys-hex>...]\n";
    sys6(SYS_write, 2, (long)usage, sizeof usage - 1, 0, 0, 0);
    sys6(SYS_exit, 2, 0, 0, 0, 0, 0);
  }
  long buffer = sys6(SYS_mmap2, 0, OUT_CAP, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (failed(buffer)) sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  out = (char *)buffer;
  uint32_t seconds = parse_dec(argv[2]);
  uint32_t interval = parse_dec(argv[3]);
  if (interval < 100u) interval = 100u;

  long mem = sys6(SYS_open, (long)"/dev/mem", O_RDONLY, 0, 0, 0, 0);
  if (failed(mem)) sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  uint32_t phys[MAX_REGS], last[MAX_REGS];
  const volatile uint32_t *reg[MAX_REGS];
  int count = 0;
  for (int i = 4; i < argc && count < MAX_REGS; i++) {
    uint32_t p = parse_hex(argv[i]) & ~3u;
    const volatile uint32_t *r = map_word(mem, p);
    if (!r) sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
    phys[count] = p;
    reg[count] = r;
    count++;
  }
  const volatile uint32_t *ring_a = 0, *ring_b = 0;
  uint32_t last_a = 0, last_b = 0;
  if (rings) {
    ring_a = map_word(mem, IPCC_PHYS + 0x4cu);
    ring_b = map_word(mem, IPCC_PHYS + 0x54u);
    if (!ring_a || !ring_b) sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
    last_a = *ring_a;
    last_b = *ring_b;
  }
  sys6(SYS_close, mem, 0, 0, 0, 0, 0);
  long fd = -1;
  if (!(argv[1][0] == '-' && argv[1][1] == 0)) {
    fd = sys6(SYS_open, (long)argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
    if (failed(fd)) sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  }

  clock_now(&sec0, &usec0);
  put_text("START ");
  put_dec((uint32_t)sec0);
  put_char('.');
  put_dec6((uint32_t)usec0);
  put_text(" interval ");
  put_dec(interval);
  put_text(" regs ");
  put_dec((uint32_t)count);
  put_text(" rings ");
  put_dec((uint32_t)rings);
  put_text(" sync ");
  put_dec((uint32_t)sync_each);
  put_char('\n');
  for (int i = 0; i < count; i++) {
    last[i] = *reg[i];
    put_text("I ");
    put_hex(phys[i]);
    put_char(' ');
    put_hex(last[i]);
    put_char('\n');
  }
  if (rings) {
    put_text("I ring A ");
    put_dec(last_a);
    put_text(" B ");
    put_dec(last_b);
    put_char('\n');
  }
  flush(fd);

  uint32_t end = seconds * 1000000u;
  uint32_t gap_limit = interval * 4u < 2000u ? 2000u : interval * 4u;
  uint32_t passes = 0, max_gap = 0, gaps = 0, prev = 0, last_flush = 0, last_beat = 0;
  for (;;) {
    uint32_t t = rel_us();
    if (passes) {
      uint32_t gap = t - prev;
      if (gap > max_gap) max_gap = gap;
      if (gap > gap_limit) {
        gaps++;
        put_text("GAP ");
        put_dec(t);
        put_char(' ');
        put_dec(gap);
        put_char('\n');
      }
    }
    prev = t;
    passes++;
    if (beat_ms && t - last_beat >= beat_ms * 1000u) {
      put_text("B ");
      put_dec(t);
      put_char('\n');
      last_beat = t;
    }
    if (rings) {
      uint32_t a = *ring_a, b = *ring_b;
      if (a != last_a) {
        put_text("R ");
        put_dec(t);
        put_text(" A ");
        put_dec(last_a);
        put_char(' ');
        put_dec(a);
        put_char('\n');
        last_a = a;
      }
      if (b != last_b) {
        put_text("R ");
        put_dec(t);
        put_text(" B ");
        put_dec(last_b);
        put_char(' ');
        put_dec(b);
        put_char('\n');
        last_b = b;
      }
    }
    for (int i = 0; i < count; i++) {
      uint32_t v = *reg[i];
      if (v != last[i]) {
        put_text("C ");
        put_dec(t);
        put_char(' ');
        put_hex(phys[i]);
        put_char(' ');
        put_hex(last[i]);
        put_char(' ');
        put_hex(v);
        put_char('\n');
        last[i] = v;
      }
    }
    if (out_len && (sync_each || t - last_flush >= 100000u)) {
      flush(fd);
      last_flush = t;
    }
    if (t >= end) break;
    sleep_us(interval);
  }
  put_text("END ");
  put_dec(rel_us());
  put_text(" passes ");
  put_dec(passes);
  put_text(" maxgap ");
  put_dec(max_gap);
  put_text(" gaps ");
  put_dec(gaps);
  put_char('\n');
  flush(fd);
  if (fd >= 0) sys6(SYS_close, fd, 0, 0, 0, 0, 0);
  sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
