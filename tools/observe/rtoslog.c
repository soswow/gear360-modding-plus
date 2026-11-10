/*
 * Read-only reader of the RTOS console log buffer.
 *
 * `st cap log` maps physical 0x85300000 through /dev/mem: first 8 bytes, then
 * 0xC0000 bytes, and prints the text. This helper maps the same range with
 * PROT_READ only and never writes device memory.
 *
 *   rtoslog dump <out-file>
 *       write the whole 0xC0000-byte region to <out-file>.
 *   rtoslog follow <out-file> <seconds> <interval-ms> [<ip:port>|- [stats]]
 *       poll the region and append every new log byte to <out-file>
 *       (fsync after each append), framed as "\n@@T <us> W <pos> R <w0>\n",
 *       plus a "\n@@HB <us>\n" Linux heartbeat every 100 ms and every new
 *       Linux kernel record from /dev/kmsg as "\n@@K <us> <record>".
 *       With <ip:port>, every chunk is also sent as UDP datagrams (before the
 *       file write); <out> "-" skips the file (no SD writes).
 *       With stats, a "\n@@S <us>" record every second adds memory, load,
 *       battery and CPU-clock readings (see stat_files).
 *
 * Freestanding static ARM binary (no libc), same build style as
 * capture-timeline-observer.
 */
#include <stddef.h>
#include <stdint.h>

#define SYS_exit 1u
#define SYS_read 3u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_lseek 19u
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
#define O_APPEND 02000
#define O_SYNC 04010000
#define O_NONBLOCK 04000
#define SEEK_END 2
#define PROT_READ 1
#define MAP_SHARED 1
#define CLOCK_MONOTONIC 1

#define LOG_PHYS 0x85300000u
#define LOG_BYTES 0xC0000u

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

#define UDP_SYSCALL6 syscall6
#include "udpout.h"

static size_t text_length(const char *text) {
  size_t n = 0;
  while (text[n]) n++;
  return n;
}

static void write_all(long fd, const void *data, size_t length) {
  const uint8_t *p = (const uint8_t *)data;
  while (length > 0) {
    long n = syscall6(SYS_write, fd, (long)p, (long)length, 0, 0, 0);
    if (n <= 0) return;
    p += n;
    length -= (size_t)n;
  }
}

static void put(const char *text) { write_all(1, text, text_length(text)); }

static __attribute__((noreturn)) void die(const char *text) {
  write_all(2, text, text_length(text));
  syscall6(SYS_exit, 1, 0, 0, 0, 0, 0);
  for (;;) {
  }
}

static char *format_u32(char *out, uint32_t value, int base) {
  char digits[16];
  int n = 0;
  do {
    uint32_t d = value % (uint32_t)base;
    digits[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    value /= (uint32_t)base;
  } while (value);
  while (n) *out++ = digits[--n];
  *out = 0;
  return out;
}

static uint32_t parse_u32(const char *text) {
  uint32_t value = 0;
  while (*text >= '0' && *text <= '9') value = value * 10u + (uint32_t)(*text++ - '0');
  return value;
}

static uint64_t monotonic_us(void) {
  struct timespec32 t;
  syscall6(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&t, 0, 0, 0, 0);
  return (uint64_t)(uint32_t)t.sec * 1000000u + (uint32_t)t.nsec / 1000u;
}

static void sleep_ms(uint32_t ms) {
  struct timespec32 t = {(long)(ms / 1000u), (long)((ms % 1000u) * 1000000u)};
  syscall6(SYS_nanosleep, (long)&t, 0, 0, 0, 0, 0);
}

static const volatile uint8_t *map_log(void) {
  long fd = syscall6(SYS_open, (long)"/dev/mem", O_RDONLY, 0, 0, 0, 0);
  if (fd < 0) die("ERROR open /dev/mem\n");
  long addr = syscall6(SYS_mmap2, 0, LOG_BYTES, PROT_READ, MAP_SHARED, fd, (long)(LOG_PHYS >> 12));
  syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  if (addr < 0 && addr > -4096) die("ERROR mmap log\n");
  return (const volatile uint8_t *)addr;
}

static uint32_t read_u32(const volatile uint8_t *base, uint32_t offset) {
  return *(const volatile uint32_t *)(base + offset);
}

/* Copy from uncached device memory into a normal buffer, word by word. */
static void copy_out(uint8_t *dst, const volatile uint8_t *src, uint32_t length) {
  uint32_t i = 0;
  for (; i + 4 <= length; i += 4) *(uint32_t *)(dst + i) = *(const volatile uint32_t *)(src + i);
  for (; i < length; i++) dst[i] = src[i];
}

static uint8_t chunk[65536];

static void dump(const char *path) {
  const volatile uint8_t *log = map_log();
  long out = syscall6(SYS_open, (long)path, O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
  if (out < 0) die("ERROR open output\n");
  for (uint32_t off = 0; off < LOG_BYTES; off += sizeof(chunk)) {
    copy_out(chunk, log + off, sizeof(chunk));
    write_all(out, chunk, sizeof(chunk));
  }
  syscall6(SYS_fsync, out, 0, 0, 0, 0, 0);
  syscall6(SYS_close, out, 0, 0, 0, 0, 0);
  syscall6(SYS_munmap, (long)log, LOG_BYTES, 0, 0, 0, 0);
  put("OK dump\n");
}

/*
 * Follow mode. Header (checked with `dump` on 2 November 2026): word 1 at
 * offset 4 is the write position into the text area that starts at offset 8;
 * word 0 read 0 and is treated as a wrap counter; unused text bytes are 0xff.
 * The text area is assumed to wrap at LOG_BYTES - 8.
 */
static void emit(long out, const void *data, size_t length) {
  udp_send(data, (uint32_t)length);
  if (out >= 0) write_all(out, data, length);
}

static void sync_out(long out) {
  if (out >= 0) syscall6(SYS_fsync, out, 0, 0, 0, 0, 0);
}

/*
 * With `stats`, once a second: "\n@@S <us>\n" and then, for each file below,
 * "<name> <its first bytes>" (missing files are skipped). Shows memory, load,
 * battery and CPU clock leading up to a stall.
 */
static const char *const stat_files[] = {
    "/proc/meminfo",
    "/proc/loadavg",
    "/sys/class/power_supply/battery/temp",
    "/sys/class/power_supply/battery/voltage_now",
    "/sys/class/power_supply/battery/current_now",
    "/sys/class/power_supply/battery/capacity",
    /* No cpufreq on this kernel: the clock driver's rates (Hz). */
    "/sys/devices/platform/drime5-clock/acpu/rate",
    "/sys/devices/platform/drime5-clock/mbus/rate",
    "/sys/devices/platform/drime5-clock/ep_a/rate",
    "/sys/devices/platform/drime5-ddrfreq/ch_a",
};

static void emit_stats(long out, uint32_t since_start_us) {
  static char text[1536];
  char head[32];
  char *p = head;
  const char *a = "\n@@S ";
  while (*a) *p++ = *a++;
  p = format_u32(p, since_start_us, 10);
  *p++ = '\n';
  emit(out, head, (size_t)(p - head));
  for (uint32_t i = 0; i < sizeof stat_files / sizeof stat_files[0]; i++) {
    long fd = syscall6(SYS_open, (long)stat_files[i], O_RDONLY, 0, 0, 0, 0);
    if (fd < 0) continue;
    long n = syscall6(SYS_read, fd, (long)text, sizeof text, 0, 0, 0);
    syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
    if (n <= 0) continue;
    emit(out, stat_files[i], text_length(stat_files[i]));
    emit(out, " ", 1);
    emit(out, text, (size_t)n);
  }
  sync_out(out);
}

static void follow(const char *path, uint32_t seconds, uint32_t interval_ms, int stats) {
  const volatile uint8_t *log = map_log();
  const uint32_t area = LOG_BYTES - 8u;
  long out = -1;
  if (!(path[0] == '-' && path[1] == 0)) {
    out = syscall6(SYS_open, (long)path, O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
    if (out < 0) die("ERROR open output\n");
  }
  uint32_t last = read_u32(log, 4);
  uint64_t start = monotonic_us();
  uint64_t end = start + (uint64_t)seconds * 1000000u;
  uint64_t last_beat = start;
  uint64_t last_stats = 0;
  char head[96];
  if (stats) {
    emit_stats(out, 0);
    last_stats = start;
  }
  /*
   * Linux kernel log, read non-destructively from /dev/kmsg (one record per
   * read; other readers such as syslogd are unaffected). Start at the end so
   * only new records are copied. Missing /dev/kmsg is not fatal.
   */
  long kmsg = syscall6(SYS_open, (long)"/dev/kmsg", O_RDONLY | O_NONBLOCK, 0, 0, 0, 0);
  if (kmsg >= 0) syscall6(SYS_lseek, kmsg, 0, SEEK_END, 0, 0, 0);
  static char record[1024];
  for (;;) {
    uint64_t now = monotonic_us();
    while (kmsg >= 0) {
      long n = syscall6(SYS_read, kmsg, (long)record, sizeof(record), 0, 0, 0);
      if (n == -32) continue; /* EPIPE: records were overwritten; keep reading */
      if (n <= 0) break;      /* EAGAIN (no new record) or error */
      char *p = head;
      const char *a = "\n@@K ";
      while (*a) *p++ = *a++;
      p = format_u32(p, (uint32_t)(now - start), 10);
      *p++ = ' ';
      emit(out, head, (size_t)(p - head));
      emit(out, record, (size_t)n);
      sync_out(out);
    }
    uint32_t pos = read_u32(log, 4);
    if (pos != last && pos < area && last < area) {
      char *p = head;
      const char *a = "\n@@T ";
      while (*a) *p++ = *a++;
      p = format_u32(p, (uint32_t)(now - start), 10);
      a = " W ";
      while (*a) *p++ = *a++;
      p = format_u32(p, pos, 10);
      a = " R ";
      while (*a) *p++ = *a++;
      p = format_u32(p, read_u32(log, 0), 10);
      *p++ = '\n';
      emit(out, head, (size_t)(p - head));
      uint32_t from = last;
      while (from != pos) {
        uint32_t stop = pos > from ? pos : area;
        uint32_t n = stop - from;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        copy_out(chunk, log + 8u + from, n);
        emit(out, chunk, n);
        from += n;
        if (from == area) from = 0;
      }
      sync_out(out);
      last = pos;
    }
    /* Linux heartbeat every 100 ms: shows when Linux itself stopped. */
    if (now - last_beat >= 100000u) {
      char *p = head;
      const char *a = "\n@@HB ";
      while (*a) *p++ = *a++;
      p = format_u32(p, (uint32_t)(now - start), 10);
      *p++ = '\n';
      emit(out, head, (size_t)(p - head));
      sync_out(out);
      last_beat = now;
    }
    if (stats && now - last_stats >= 1000000u) {
      emit_stats(out, (uint32_t)(now - start));
      last_stats = now;
    }
    if (now >= end) break;
    sleep_ms(interval_ms);
  }
  if (kmsg >= 0) syscall6(SYS_close, kmsg, 0, 0, 0, 0, 0);
  if (out >= 0) syscall6(SYS_close, out, 0, 0, 0, 0, 0);
  syscall6(SYS_munmap, (long)log, LOG_BYTES, 0, 0, 0, 0);
  put("OK follow\n");
}

static int same(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

void c_entry(long *stack) {
  long argc = stack[0];
  char **argv = (char **)(stack + 1);
  if (argc == 3 && same(argv[1], "dump")) {
    dump(argv[2]);
  } else if ((argc == 5 || argc == 6 || (argc == 7 && same(argv[6], "stats"))) && same(argv[1], "follow")) {
    if (argc >= 6 && !same(argv[5], "-") && udp_open(argv[5]) != 0) die("ERROR udp destination\n");
    follow(argv[2], parse_u32(argv[3]), parse_u32(argv[4]), argc == 7);
  } else {
    die("usage: rtoslog dump <out> | rtoslog follow <out|-> <seconds> <interval-ms> [<ip:port>|- [stats]]\n");
  }
  syscall6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
