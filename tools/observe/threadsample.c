/*
 * Read-only sampler of one process's threads (plan step 4c).
 *
 *   threadsample <pid> <out-file> <seconds> <period-us>
 *
 * Every period it lists /proc/<pid>/task, and for each thread reads
 * /proc/<pid>/task/<tid>/stat (state, utime+stime) and .../syscall (the
 * system call the thread is in, or "running"). A line is written only when a
 * thread's state, CPU ticks or syscall line changed:
 *   T <monotonic-us> <tid> <comm> <state> <ticks> <syscall line>
 * It also maps the IPCC window (physical 0x02010000) PROT_READ and logs ring
 * A/B write-cursor changes on the same clock:
 *   R <monotonic-us> <ring> <old> <new>
 * Nothing attaches to the target (no ptrace) and nothing is written to device
 * memory. Output is buffered and written (with fsync) every 250 ms and at the
 * end. Freestanding static ARM binary, same build style as rtoslog.
 */
#include <stddef.h>
#include <stdint.h>

#define SYS_exit 1u
#define SYS_read 3u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_munmap 91u
#define SYS_fsync 118u
#define SYS_nanosleep 162u
#define SYS_mmap2 192u
#define SYS_getdents64 217u
#define SYS_clock_gettime 263u

#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_DIRECTORY 040000
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define CLOCK_MONOTONIC 1

#define WINDOW_PHYS 0x02010000u
#define WINDOW_BYTES 0x2000u
#define MAX_THREADS 96
#define OUT_CAP (8u * 1024u * 1024u)

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

static size_t len(const char *t) {
  size_t n = 0;
  while (t[n]) n++;
  return n;
}

static void write_all(long fd, const void *data, size_t length) {
  const char *p = (const char *)data;
  while (length) {
    long n = sys6(SYS_write, fd, (long)p, (long)length, 0, 0, 0);
    if (failed(n) || n == 0) return;
    p += n;
    length -= (size_t)n;
  }
}

static __attribute__((noreturn)) void finish(int code, const char *text) {
  write_all(code ? 2 : 1, text, len(text));
  sys6(SYS_exit, code, 0, 0, 0, 0, 0);
  for (;;) {
  }
}

static char *out;
static uint32_t out_len;
static uint32_t dropped;

static void put_char(char c) {
  if (out_len < OUT_CAP) out[out_len++] = c;
  else dropped++;
}
static void put_text(const char *t) {
  while (*t) put_char(*t++);
}
static void put_n(const char *t, size_t n) {
  for (size_t i = 0; i < n; i++) put_char(t[i]);
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

static uint32_t parse_dec(const char *t) {
  uint32_t v = 0;
  while (*t >= '0' && *t <= '9') v = v * 10u + (uint32_t)(*t++ - '0');
  return v;
}

static uint32_t now_us(void) {
  struct timespec32 t;
  sys6(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&t, 0, 0, 0, 0);
  return (uint32_t)t.sec * 1000000u + (uint32_t)t.nsec / 1000u;
}

static void sleep_us(uint32_t us) {
  struct timespec32 t = {(long)(us / 1000000u), (long)((us % 1000000u) * 1000u)};
  sys6(SYS_nanosleep, (long)&t, 0, 0, 0, 0, 0);
}

static char *copy_text(char *dst, const char *src) {
  while (*src) *dst++ = *src++;
  *dst = 0;
  return dst;
}

static char *copy_dec(char *dst, uint32_t v) {
  char d[11];
  int n = 0;
  do {
    d[n++] = (char)('0' + v % 10u);
    v /= 10u;
  } while (v);
  while (n) *dst++ = d[--n];
  *dst = 0;
  return dst;
}

/* Read a small proc file into buf (NUL-terminated); returns length or -1. */
static long read_file(const char *path, char *buf, size_t cap) {
  long fd = sys6(SYS_open, (long)path, O_RDONLY, 0, 0, 0, 0);
  if (failed(fd)) return -1;
  long n = sys6(SYS_read, fd, (long)buf, (long)(cap - 1), 0, 0, 0);
  sys6(SYS_close, fd, 0, 0, 0, 0, 0);
  if (failed(n)) return -1;
  buf[n] = 0;
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = 0;
  return n;
}

struct thread {
  uint32_t tid;
  char state;
  uint32_t ticks;
  char syscall[96];
  int seen;
};

static struct thread threads[MAX_THREADS];
static int thread_count;

static struct thread *find_thread(uint32_t tid) {
  for (int i = 0; i < thread_count; i++)
    if (threads[i].tid == tid) return &threads[i];
  if (thread_count == MAX_THREADS) return 0;
  struct thread *t = &threads[thread_count++];
  t->tid = tid;
  t->state = 0;
  t->ticks = 0xffffffffu;
  t->syscall[0] = 0;
  return t;
}

static int same_text(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

static char dents[8192];

static void sample(uint32_t pid, uint32_t elapsed) {
  char path[64];
  char *p = copy_text(path, "/proc/");
  p = copy_dec(p, pid);
  copy_text(p, "/task");
  long dir = sys6(SYS_open, (long)path, O_RDONLY | O_DIRECTORY, 0, 0, 0, 0);
  if (failed(dir)) return;
  for (;;) {
    long n = sys6(SYS_getdents64, dir, (long)dents, sizeof(dents), 0, 0, 0);
    if (failed(n) || n == 0) break;
    long off = 0;
    while (off < n) {
      /* linux_dirent64: u64 ino, s64 off, u16 reclen, u8 type, char name[] */
      uint16_t reclen = (uint16_t)((uint8_t)dents[off + 16] | ((uint8_t)dents[off + 17] << 8));
      const char *name = dents + off + 19;
      off += reclen;
      if (name[0] < '0' || name[0] > '9') continue;
      uint32_t tid = parse_dec(name);
      struct thread *t = find_thread(tid);
      if (!t) continue;
      char tpath[96];
      char *q = copy_text(tpath, path);
      q = copy_text(q, "/");
      q = copy_dec(q, tid);
      char *base = q;
      static char stat[512];
      copy_text(base, "/stat");
      if (read_file(tpath, stat, sizeof(stat)) < 0) continue;
      /* comm sits between the first '(' and the last ')'. */
      const char *open_paren = stat;
      while (*open_paren && *open_paren != '(') open_paren++;
      const char *close_paren = stat + len(stat);
      while (close_paren > stat && *close_paren != ')') close_paren--;
      if (*open_paren != '(' || *close_paren != ')') continue;
      const char *f = close_paren + 2;
      char state = *f;
      /* fields after ") ": 0 state ... 11 utime, 12 stime */
      int field = 0;
      uint32_t utime = 0, stime = 0;
      for (const char *c = f; *c; c++) {
        if (*c == ' ') {
          field++;
          if (field == 11) utime = parse_dec(c + 1);
          if (field == 12) {
            stime = parse_dec(c + 1);
            break;
          }
        }
      }
      static char sc[96];
      copy_text(base, "/syscall");
      if (read_file(tpath, sc, sizeof(sc)) < 0) copy_text(sc, "?");
      uint32_t ticks = utime + stime;
      if (state != t->state || ticks != t->ticks || !same_text(sc, t->syscall)) {
        put_text("T ");
        put_dec(elapsed);
        put_char(' ');
        put_dec(tid);
        put_char(' ');
        put_n(open_paren + 1, (size_t)(close_paren - open_paren - 1) > 24 ? 24
                                                                          : (size_t)(close_paren - open_paren - 1));
        put_char(' ');
        put_char(state);
        put_char(' ');
        put_dec(ticks);
        put_char(' ');
        put_text(sc);
        put_char('\n');
        t->state = state;
        t->ticks = ticks;
        copy_text(t->syscall, sc);
      }
    }
  }
  sys6(SYS_close, dir, 0, 0, 0, 0, 0);
}

void c_entry(long *stack) {
  int argc = (int)stack[0];
  char **argv = (char **)(stack + 1);
  if (argc != 5) finish(2, "usage: threadsample <pid> <out> <seconds> <period-us>\n");
  uint32_t pid = parse_dec(argv[1]);
  uint32_t seconds = parse_dec(argv[3]);
  uint32_t period = parse_dec(argv[4]);
  long buffer = sys6(SYS_mmap2, 0, OUT_CAP, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (failed(buffer)) finish(1, "ERROR buffer\n");
  out = (char *)buffer;
  long mem = sys6(SYS_open, (long)"/dev/mem", O_RDONLY, 0, 0, 0, 0);
  if (failed(mem)) finish(1, "ERROR open /dev/mem\n");
  long win = sys6(SYS_mmap2, 0, WINDOW_BYTES, PROT_READ, MAP_SHARED, mem, (long)(WINDOW_PHYS >> 12));
  sys6(SYS_close, mem, 0, 0, 0, 0, 0);
  if (failed(win)) finish(1, "ERROR mmap window\n");
  const volatile uint32_t *cursor_a = (const volatile uint32_t *)((const char *)win + 0x4c);
  const volatile uint32_t *cursor_b = (const volatile uint32_t *)((const char *)win + 0x54);
  long fd = sys6(SYS_open, (long)argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
  if (failed(fd)) finish(1, "ERROR open output\n");
  uint32_t start = now_us();
  put_text("START monotonic_us ");
  put_dec(start);
  put_text(" pid ");
  put_dec(pid);
  put_char('\n');
  uint32_t last_a = *cursor_a, last_b = *cursor_b, last_flush = start;
  for (;;) {
    uint32_t now = now_us();
    uint32_t elapsed = now - start;
    uint32_t a = *cursor_a, b = *cursor_b;
    if (a != last_a) {
      put_text("R ");
      put_dec(elapsed);
      put_text(" A ");
      put_dec(last_a);
      put_char(' ');
      put_dec(a);
      put_char('\n');
      last_a = a;
    }
    if (b != last_b) {
      put_text("R ");
      put_dec(elapsed);
      put_text(" B ");
      put_dec(last_b);
      put_char(' ');
      put_dec(b);
      put_char('\n');
      last_b = b;
    }
    sample(pid, elapsed);
    if (now - last_flush > 250000u) {
      write_all(fd, out, out_len);
      sys6(SYS_fsync, fd, 0, 0, 0, 0, 0);
      out_len = 0;
      last_flush = now;
    }
    if (elapsed >= seconds * 1000000u) break;
    if (period) sleep_us(period);
  }
  put_text("END dropped ");
  put_dec(dropped);
  put_char('\n');
  write_all(fd, out, out_len);
  sys6(SYS_fsync, fd, 0, 0, 0, 0, 0);
  sys6(SYS_close, fd, 0, 0, 0, 0, 0);
  sys6(SYS_munmap, win, WINDOW_BYTES, 0, 0, 0, 0);
  finish(0, "OK threadsample\n");
}
