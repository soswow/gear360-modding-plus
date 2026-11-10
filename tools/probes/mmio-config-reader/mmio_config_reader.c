#include <stdint.h>
#include <stddef.h>

typedef uint32_t u32;
typedef unsigned long usize;

#define PAGE_SIZE 4096u
#define PROT_READ 1u
#define MAP_SHARED 1u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_munmap 91u
#define SYS_mmap2 192u
#define OUTPUT_SIZE 512u

/* Fixed Linux ARM EABI access: O_RDONLY /dev/mem, one read-only shared page. */
static const char devmem[] = "/dev/mem";
static char output[OUTPUT_SIZE];

static inline long syscall6(long nr, long a0, long a1, long a2,
                            long a3, long a4, long a5) {
  register long r0 __asm__("r0") = a0;
  register long r1 __asm__("r1") = a1;
  register long r2 __asm__("r2") = a2;
  register long r3 __asm__("r3") = a3;
  register long r4 __asm__("r4") = a4;
  register long r5 __asm__("r5") = a5;
  register long r7 __asm__("r7") = nr;
  __asm__ volatile("svc #0"
                   : "+r"(r0)
                   : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5),
                     "r"(r7)
                   : "memory", "cc");
  return r0;
}

static int failed(long r) {
  return (usize)r >= (usize)-4095;
}

static long map_page(int fd, u32 physical_page) {
  return syscall6(SYS_mmap2, 0, PAGE_SIZE, PROT_READ, MAP_SHARED, fd,
                  (long)(physical_page >> 12));
}

static long unmap_page(long mapped) {
  return syscall6(SYS_munmap, mapped, PAGE_SIZE, 0, 0, 0, 0);
}

static u32 read_u32_volatile(long mapped, u32 offset) {
  volatile const u32 *address = (volatile const u32 *)((usize)mapped + offset);
  return *address;
}

static void append_char(usize *n, char c) {
  if (*n < sizeof(output)) output[(*n)++] = c;
}

static void append_text(usize *n, const char *s) {
  while (*s) append_char(n, *s++);
}

static void append_hex(usize *n, u32 value) {
  static const char digits[] = "0123456789abcdef";
  append_text(n, "0x");
  for (int shift = 28; shift >= 0; shift -= 4)
    append_char(n, digits[(value >> shift) & 15u]);
}

static void append_scalar(usize *n, u32 address, u32 value) {
  static const char digits[] = "0123456789abcdef";
  append_text(n, "physical-offset ");
  append_hex(n, address);
  append_text(n, " value=");
  append_hex(n, value);
  append_text(n, " bytes:");
  for (unsigned i = 0; i < 4; ++i) {
    unsigned char b = (unsigned char)(value >> (8u * i));
    append_char(n, ' ');
    append_char(n, digits[b >> 4]);
    append_char(n, digits[b & 15u]);
  }
  append_char(n, '\n');
}

static void append_error(usize *n, const char *op, u32 address, long err) {
  append_text(n, "ERROR ");
  append_text(n, op);
  append_text(n, " physical-offset ");
  append_hex(n, address);
  append_text(n, " errno=");
  append_hex(n, (u32)-err);
  append_char(n, '\n');
}

static long write_all(const char *p, usize n) {
  while (n) {
    long r = syscall6(SYS_write, 1, (long)p, (long)n, 0, 0, 0);
    if (failed(r) || r == 0) return failed(r) ? r : -5;
    p += (usize)r;
    n -= (usize)r;
  }
  return 0;
}

static int same(const char *a, const char *b) {
  usize i = 0;
  while (a[i] && b[i] && a[i] == b[i]) ++i;
  return a[i] == 0 && b[i] == 0;
}

/* Only this mode and three source-RMW-proven offsets are accepted. */
int c_entry(usize *initial_stack) {
  int argc = (int)initial_stack[0];
  char **argv = (char **)(initial_stack + 1);
  if (argc != 2 || !argv[1] || !same(argv[1], "ssif-config")) {
    static const char usage[] =
        "ERROR: expected exactly one mode: ssif-config\n";
    (void)write_all(usage, sizeof(usage) - 1);
    return 2;
  }

  usize n = 0;
  int status = 0;
  const u32 page_address = 0x20800000u;
  static const u32 addresses[3] = {
      0x20800024u, 0x20800340u, 0x2080034cu};
  static const u32 offsets[3] = {0x24u, 0x340u, 0x34cu};
  u32 values[3];

  long fd = syscall6(SYS_open, (long)devmem, 0, 0, 0, 0, 0);
  if (failed(fd)) {
    append_error(&n, "open /dev/mem", 0, fd);
    (void)write_all(output, n);
    return 1;
  }

  long page = map_page((int)fd, page_address);
  if (failed(page)) {
    append_error(&n, "mmap2", page_address, page);
    status = 1;
  } else {
    values[0] = read_u32_volatile(page, offsets[0]);
    values[1] = read_u32_volatile(page, offsets[1]);
    values[2] = read_u32_volatile(page, offsets[2]);
    long r = unmap_page(page);
    if (failed(r)) {
      append_error(&n, "munmap", page_address, r);
      status = 1;
    }
  }

  long close_result = syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  if (failed(close_result)) {
    append_error(&n, "close /dev/mem", 0, close_result);
    status = 1;
  }

  if (!status)
    for (usize i = 0; i < 3; ++i) append_scalar(&n, addresses[i], values[i]);
  if (n && failed(write_all(output, n))) status = 1;
  return status;
}
