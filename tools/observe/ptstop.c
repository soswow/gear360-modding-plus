/*
 * Stop chosen threads of one process for a while, then release them
 * (plan step 4l: per-thread bisection of the running stock app).
 *
 *   ptstop [--after-gyro-reply | --until-gyro-reply] <out-file> <pid> <seconds> <thread-name>...
 *
 * Trigger modes watch IPCC ring B (Linux->RTOS; physical window 0x02010000,
 * write cursor at +0x54, 395 data bytes at +0x277, mapped read-only) for the
 * gyro reply frame bytes 83 00 10 02 04 00 09 00 (category 0x83, attribute
 * 0x0210, value 9):
 *   --after-gyro-reply  wait up to 60 s for the reply, then stop the threads
 *                       and hold them <seconds>;
 *   --until-gyro-reply  stop the threads now and release them as soon as the
 *                       reply appears (or after <seconds>);
 *   --after-gyro-adjust wait for the gyro reply, then for the next adjust reply
 *                       (83 00 0b aa 04 00, attribute 0xaa0b) within 2 s; stop
 *                       the threads then and hold them <seconds>. No adjust
 *                       reply: log NO_ADJUST and stop nothing.
 * Without a mode the threads are stopped at once and held <seconds>.
 *
 * Finds every thread of <pid> whose /proc/<pid>/task/<tid>/comm equals one
 * of the names, attaches with PTRACE_SEIZE and stops it with
 * PTRACE_INTERRUPT. Only those threads stop; the rest of the process keeps
 * running. A thread blocked in an interruptible system call leaves it and
 * restarts it after release. Holds for <seconds>, logging each stopped
 * thread's /proc state once a second, then detaches (PTRACE_DETACH, no
 * signal). If any match does not stop within 2 s, nothing is held: the
 * stopped ones are released at once. If this program exits or is killed
 * early, the kernel detaches the threads and they resume.
 *
 * Output lines (fsync after every line):
 *   START <ms> pid <pid>
 *   FOUND <ms> <tid> <name>
 *   TRIGGER <ms> gyro-reply | NO_TRIGGER <ms> | TIMEOUT <ms>   (trigger modes)
 *   SEEN <ms> gyro-reply, then TRIGGER <ms> adjust-reply-after-gyro | NO_ADJUST <ms>
 *   STOP <ms> <tid> rc <seize>/<interrupt> wait <status-hex> state <c>
 *   ALL_STOPPED <ms> <count>       (only if every match stopped)
 *   HOLD <ms> <tid> <state> ...
 *   DETACH <ms> <tid> rc <rc> state <c>
 *   END <ms>
 * <ms> is CLOCK_MONOTONIC in milliseconds. Reads /proc only; never touches
 * device memory. Freestanding static ARM binary.
 */
#include <stdint.h>

#define SYS_exit 1u
#define SYS_read 3u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_ptrace 26u
#define SYS_wait4 114u
#define SYS_fsync 118u
#define SYS_nanosleep 162u
#define SYS_getdents64 217u
#define SYS_clock_gettime 263u
#define SYS_mmap2 192u
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_DIRECTORY 040000
#define PTRACE_DETACH 17
#define PTRACE_SEIZE 0x4206
#define PTRACE_INTERRUPT 0x4207
#define WALL 0x40000000
#define WNOHANG 1
#define MAX_THREADS 64
#define PROT_READ 1
#define MAP_SHARED 1
#define WINDOW_PHYS 0x02010000u
#define WINDOW_BYTES 0x2000u
#define RING_B_WRITE 0x54u
#define RING_B_DATA 0x277u
#define RING_B_CAP 395u

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

static long out_fd = -1;
static char line[256];
static uint32_t line_len;

static void put_char(char c) {
  if (line_len < sizeof line - 1) line[line_len++] = c;
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
static void put_sdec(long v) {
  if (v < 0) {
    put_char('-');
    put_dec((uint32_t)(-v));
  } else {
    put_dec((uint32_t)v);
  }
}
static void put_hex(uint32_t v) {
  static const char h[] = "0123456789abcdef";
  put_text("0x");
  for (int i = 28; i >= 0; i -= 4) put_char(h[(v >> i) & 15u]);
}
static void end_line(void) {
  put_char('\n');
  if (out_fd >= 0) {
    sys6(SYS_write, out_fd, (long)line, (long)line_len, 0, 0, 0);
    sys6(SYS_fsync, out_fd, 0, 0, 0, 0, 0);
  }
  sys6(SYS_write, 1, (long)line, (long)line_len, 0, 0, 0);
  line_len = 0;
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
static void sleep_ms(uint32_t ms) {
  struct timespec32 t = {(long)(ms / 1000u), (long)((ms % 1000u) * 1000000u)};
  sys6(SYS_nanosleep, (long)&t, 0, 0, 0, 0, 0);
}
static int same(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

/* Builds "/proc/<pid>/task/<tid>/<leaf>" into path. tid 0 = task directory. */
static char path[96];
static void task_path(uint32_t pid, uint32_t tid, const char *leaf) {
  uint32_t n = 0;
  const char *p = "/proc/";
  char d[11];
  int k;
  while (*p) path[n++] = *p++;
  k = 0;
  do {
    d[k++] = (char)('0' + pid % 10u);
    pid /= 10u;
  } while (pid);
  while (k) path[n++] = d[--k];
  p = "/task";
  while (*p) path[n++] = *p++;
  if (tid) {
    path[n++] = '/';
    k = 0;
    do {
      d[k++] = (char)('0' + tid % 10u);
      tid /= 10u;
    } while (tid);
    while (k) path[n++] = d[--k];
    path[n++] = '/';
    while (*leaf) path[n++] = *leaf++;
  }
  path[n] = 0;
}

static char small[512];
static long read_small(void) {
  long fd = sys6(SYS_open, (long)path, O_RDONLY, 0, 0, 0, 0);
  if (failed(fd)) return -1;
  long n = sys6(SYS_read, fd, (long)small, sizeof small - 1, 0, 0, 0);
  sys6(SYS_close, fd, 0, 0, 0, 0, 0);
  if (failed(n)) return -1;
  small[n] = 0;
  return n;
}

/* State letter from /proc/<pid>/task/<tid>/stat ("tid (comm) S ..."). */
static char thread_state(uint32_t pid, uint32_t tid) {
  task_path(pid, tid, "stat");
  long n = read_small();
  if (n <= 0) return '?';
  long close_paren = -1;
  for (long i = 0; i < n; i++)
    if (small[i] == ')') close_paren = i;
  if (close_paren < 0 || close_paren + 2 >= n) return '?';
  return small[close_paren + 2];
}

static char dents[2048];
static uint32_t tids[MAX_THREADS];
static uint32_t stopped[MAX_THREADS];
static uint32_t count;
static uint32_t pid;

static void sleep_us(uint32_t us) {
  struct timespec32 t = {(long)(us / 1000000u), (long)((us % 1000000u) * 1000u)};
  sys6(SYS_nanosleep, (long)&t, 0, 0, 0, 0, 0);
}

static const volatile uint8_t *window;

static void map_window(void) {
  long fd = sys6(SYS_open, (long)"/dev/mem", O_RDONLY, 0, 0, 0, 0);
  long addr = failed(fd) ? fd : sys6(SYS_mmap2, 0, WINDOW_BYTES, PROT_READ, MAP_SHARED, fd, (long)(WINDOW_PHYS >> 12));
  if (!failed(fd)) sys6(SYS_close, fd, 0, 0, 0, 0, 0);
  if (failed(addr)) {
    put_text("ERROR map ipcc window");
    end_line();
    sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  }
  window = (const volatile uint8_t *)addr;
}

static uint32_t ring_b_write(void) { return *(const volatile uint32_t *)(window + RING_B_WRITE); }

/* Ring B scanner. The cursor and byte history persist across calls, so a
   second pattern is looked for in the bytes written after the first match. */
static uint8_t scan_hist[8];
static uint32_t scan_last;
static int scan_started;

/* Returns 1 when the n-byte pattern (n <= 8) appears in ring B within
   timeout_ms of this call. */
static int wait_for_pattern(const uint8_t *pattern, int n, uint32_t timeout_ms) {
  if (!scan_started) {
    scan_last = ring_b_write();
    scan_started = 1;
  }
  uint32_t start = now_ms();
  while (now_ms() - start < timeout_ms) {
    uint32_t wc = ring_b_write();
    if (wc != scan_last && wc < RING_B_CAP && scan_last < RING_B_CAP) {
      while (scan_last != wc) {
        for (int k = 0; k < 7; k++) scan_hist[k] = scan_hist[k + 1];
        scan_hist[7] = window[RING_B_DATA + scan_last];
        scan_last++;
        if (scan_last == RING_B_CAP) scan_last = 0;
        int match = 1;
        for (int k = 0; k < n; k++)
          if (scan_hist[8 - n + k] != pattern[k]) match = 0;
        if (match) return 1;
      }
    } else if (wc != scan_last) {
      scan_last = wc; /* cursor out of range: resynchronize */
    }
    sleep_us(200u);
  }
  return 0;
}

/* Gyro reply: category 0x83, attribute 0x0210, value 9. */
static int wait_for_gyro_reply(uint32_t timeout_ms) {
  static const uint8_t gyro[8] = {0x83, 0x00, 0x10, 0x02, 0x04, 0x00, 0x09, 0x00};
  return wait_for_pattern(gyro, 8, timeout_ms);
}

/* Adjust reply: category 0x83, attribute 0xaa0b, 4-byte value. */
static int wait_for_adjust_reply(uint32_t timeout_ms) {
  static const uint8_t adjust[6] = {0x83, 0x00, 0x0b, 0xaa, 0x04, 0x00};
  return wait_for_pattern(adjust, 6, timeout_ms);
}

static void log_mark(const char *what) {
  put_text(what);
  put_char(' ');
  put_dec(now_ms());
  end_line();
}

static uint32_t stop_all(void) {
  uint32_t ok = 0;
  for (uint32_t i = 0; i < count; i++) {
    long seize = sys6(SYS_ptrace, PTRACE_SEIZE, (long)tids[i], 0, 0, 0, 0);
    long intr = failed(seize) ? seize : sys6(SYS_ptrace, PTRACE_INTERRUPT, (long)tids[i], 0, 0, 0, 0);
    int status = 0;
    long waited = -1;
    /* Poll for the stop every 1 ms for up to 2 s: a thread in an
       uninterruptible wait stops only when that wait ends. */
    for (int tries = 0; !failed(seize) && !failed(intr) && tries < 2000; tries++) {
      waited = sys6(SYS_wait4, (long)tids[i], (long)&status, WALL | WNOHANG, 0, 0, 0);
      if (waited != 0) break;
      sleep_us(1000u);
    }
    stopped[i] = !failed(seize) && !failed(intr) && waited == (long)tids[i];
    if (stopped[i]) ok++;
    put_text("STOP ");
    put_dec(now_ms());
    put_char(' ');
    put_dec(tids[i]);
    put_text(" rc ");
    put_sdec(seize);
    put_char('/');
    put_sdec(intr);
    put_text(" wait ");
    put_hex((uint32_t)status);
    put_text(" state ");
    put_char(thread_state(pid, tids[i]));
    end_line();
  }
  put_text(count && ok == count ? "ALL_STOPPED " : "NOT_ALL_STOPPED ");
  put_dec(now_ms());
  put_char(' ');
  put_dec(ok);
  if (!(count && ok == count)) {
    put_char('/');
    put_dec(count);
  }
  end_line();
  return count && ok == count;
}

static void hold(uint32_t seconds) {
  uint32_t start = now_ms();
  while (now_ms() - start < seconds * 1000u) {
    sleep_ms(1000u);
    put_text("HOLD ");
    put_dec(now_ms());
    for (uint32_t i = 0; i < count; i++) {
      put_char(' ');
      put_dec(tids[i]);
      put_char(' ');
      put_char(thread_state(pid, tids[i]));
    }
    end_line();
  }
}

static void release_all(void) {
  for (uint32_t i = 0; i < count; i++) {
    if (!stopped[i]) continue;
    long rc = sys6(SYS_ptrace, PTRACE_DETACH, (long)tids[i], 0, 0, 0, 0);
    put_text("DETACH ");
    put_dec(now_ms());
    put_char(' ');
    put_dec(tids[i]);
    put_text(" rc ");
    put_sdec(rc);
    end_line();
  }
  sleep_ms(5u);
  for (uint32_t i = 0; i < count; i++) {
    if (!stopped[i]) continue;
    put_text("STATE ");
    put_dec(now_ms());
    put_char(' ');
    put_dec(tids[i]);
    put_char(' ');
    put_char(thread_state(pid, tids[i]));
    end_line();
  }
}

void c_entry(long *stack) {
  int argc = (int)stack[0];
  char **argv = (char **)(stack + 1);
  int mode = 0; /* 0 now, 1 after gyro reply, 2 until gyro reply, 3 after gyro + next adjust reply */
  if (argc > 1 && same(argv[1], "--after-gyro-reply")) mode = 1;
  if (argc > 1 && same(argv[1], "--until-gyro-reply")) mode = 2;
  if (argc > 1 && same(argv[1], "--after-gyro-adjust")) mode = 3;
  if (mode) {
    argc--;
    argv++;
  }
  if (argc < 5) {
    static const char usage[] =
        "usage: ptstop [--after-gyro-reply|--until-gyro-reply|--after-gyro-adjust] <out> <pid> <seconds> <thread-name>...\n";
    sys6(SYS_write, 2, (long)usage, sizeof usage - 1, 0, 0, 0);
    sys6(SYS_exit, 2, 0, 0, 0, 0, 0);
  }
  out_fd = sys6(SYS_open, (long)argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
  if (failed(out_fd)) sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  pid = parse_dec(argv[2]);
  uint32_t seconds = parse_dec(argv[3]);
  put_text("START ");
  put_dec(now_ms());
  put_text(" pid ");
  put_dec(pid);
  put_text(mode == 1   ? " mode after-gyro-reply"
           : mode == 2 ? " mode until-gyro-reply"
           : mode == 3 ? " mode after-gyro-adjust"
                       : " mode now");
  end_line();

  /* Find the matching threads. */
  task_path(pid, 0, "");
  long dir = sys6(SYS_open, (long)path, O_RDONLY | O_DIRECTORY, 0, 0, 0, 0);
  if (failed(dir)) {
    put_text("ERROR no task directory");
    end_line();
    sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  }
  for (;;) {
    long n = sys6(SYS_getdents64, dir, (long)dents, sizeof dents, 0, 0, 0);
    if (failed(n) || n == 0) break;
    for (long off = 0; off < n;) {
      /* linux_dirent64: u64 ino, s64 off, u16 reclen, u8 type, name[] */
      uint16_t reclen = (uint16_t)((uint8_t)dents[off + 16] | ((uint8_t)dents[off + 17] << 8));
      const char *name = dents + off + 19;
      if (name[0] >= '0' && name[0] <= '9' && count < MAX_THREADS) {
        uint32_t tid = parse_dec(name);
        task_path(pid, tid, "comm");
        long len = read_small();
        if (len > 0) {
          if (small[len - 1] == '\n') small[len - 1] = 0;
          for (int i = 4; i < argc; i++) {
            if (same(small, argv[i])) {
              tids[count++] = tid;
              put_text("FOUND ");
              put_dec(now_ms());
              put_char(' ');
              put_dec(tid);
              put_char(' ');
              put_text(small);
              end_line();
              break;
            }
          }
        }
      }
      if (reclen == 0) break;
      off += reclen;
    }
  }
  sys6(SYS_close, dir, 0, 0, 0, 0, 0);
  if (!count) {
    log_mark("NO_THREADS");
    sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  }

  if (mode == 1) {
    map_window();
    log_mark("ARMED");
    if (!wait_for_gyro_reply(60000u)) {
      log_mark("NO_TRIGGER");
      sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
    }
    log_mark("TRIGGER gyro-reply");
    if (stop_all()) hold(seconds);
  } else if (mode == 3) {
    map_window();
    log_mark("ARMED");
    if (!wait_for_gyro_reply(60000u)) {
      log_mark("NO_TRIGGER");
      sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
    }
    log_mark("SEEN gyro-reply");
    if (!wait_for_adjust_reply(2000u)) {
      log_mark("NO_ADJUST");
      sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
    }
    log_mark("TRIGGER adjust-reply-after-gyro");
    if (stop_all()) hold(seconds);
  } else if (mode == 2) {
    map_window();
    if (stop_all()) {
      log_mark("ARMED");
      log_mark(wait_for_gyro_reply(seconds * 1000u) ? "TRIGGER gyro-reply" : "TIMEOUT");
    }
  } else {
    if (stop_all()) hold(seconds);
  }
  release_all();
  log_mark("END");
  sys6(SYS_close, out_fd, 0, 0, 0, 0, 0);
  sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
