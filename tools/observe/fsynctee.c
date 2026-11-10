/*
 * Copy stdin to a file, fsync after every read, stop after N seconds of
 * wall time or at end of input. Used to keep `dlogutil -v time` output on the
 * SD card across an abrupt power cut:
 *
 *   dlogutil -v time | fsynctee /opt/storage/sdcard/obs/dlog.txt 180
 *
 * Freestanding static ARM binary, same build style as rtoslog.
 */
#include <stddef.h>
#include <stdint.h>

#define SYS_exit 1u
#define SYS_read 3u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_fsync 118u
#define SYS_clock_gettime 263u
#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000

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

static uint32_t parse_dec(const char *t) {
  uint32_t v = 0;
  while (*t >= '0' && *t <= '9') v = v * 10u + (uint32_t)(*t++ - '0');
  return v;
}

static uint32_t now_s(void) {
  struct timespec32 t;
  sys6(SYS_clock_gettime, 1, (long)&t, 0, 0, 0, 0);
  return (uint32_t)t.sec;
}

static char buffer[16384];

void c_entry(long *stack) {
  int argc = (int)stack[0];
  char **argv = (char **)(stack + 1);
  if (argc != 3) {
    sys6(SYS_write, 2, (long)"usage: fsynctee <file> <seconds>\n", 33, 0, 0, 0);
    sys6(SYS_exit, 2, 0, 0, 0, 0, 0);
  }
  long fd = sys6(SYS_open, (long)argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
  if (fd < 0) sys6(SYS_exit, 1, 0, 0, 0, 0, 0);
  uint32_t end = now_s() + parse_dec(argv[2]);
  for (;;) {
    long n = sys6(SYS_read, 0, (long)buffer, sizeof(buffer), 0, 0, 0);
    if (n <= 0) break;
    long off = 0;
    while (off < n) {
      long w = sys6(SYS_write, fd, (long)(buffer + off), n - off, 0, 0, 0);
      if (w <= 0) break;
      off += w;
    }
    sys6(SYS_fsync, fd, 0, 0, 0, 0, 0);
    if (now_s() >= end) break;
  }
  sys6(SYS_close, fd, 0, 0, 0, 0, 0);
  sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
