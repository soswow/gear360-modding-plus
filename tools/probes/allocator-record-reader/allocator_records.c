#include <stdint.h>
#include <stddef.h>

typedef uint32_t u32;
typedef unsigned long usize;

#define PAGE 4096u
#define MANAGER 0x81cbb000u
#define MANAGER_CELL 0x81c0d000u
#define ARENA_FIELD_PAGE 0x81d23000u
#define GLOBALS 0x81952000u
#define IMAGE 0x82246000u
#define ARENA_BASE 0xa6000000u
#define TABLE_OFFSET 0x00b0u
#define ARENA_FIELD_OFFSET 0x00b0u
#define PREFIX_BYTES 0x7000u
#define TABLE_RECORD_BYTES 16u
#define UNIT_BYTES 0x4000u
#define SAMPLE_PHYSICAL 0xa77bb000u
#define PIXEL_BYTES 2764800u
#define PROT_READ 1u
#define PROT_WRITE 2u
#define MAP_SHARED 1u
#define MAP_PRIVATE 2u
#define MAP_ANONYMOUS 0x20u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_munmap 91u
#define SYS_mmap2 192u
#define OUTPUT_CAP 2048u

_Static_assert(PREFIX_BYTES == 7u * PAGE, "fixed metadata prefix");
_Static_assert((MANAGER & (PAGE - 1u)) == 0u, "manager aligned");
_Static_assert((SAMPLE_PHYSICAL - ARENA_BASE) / UNIT_BYTES == 1518u, "pixel start unit");
_Static_assert((SAMPLE_PHYSICAL + PIXEL_BYTES - 1u - ARENA_BASE) / UNIT_BYTES == 1687u,
               "pixel end unit");
_Static_assert((TABLE_OFFSET + 1781u * TABLE_RECORD_BYTES) <= PREFIX_BYTES,
               "prefix covers all required records");

static const char devmem[] = "/dev/mem";
static char output[OUTPUT_CAP];

#ifdef ALLOCATOR_HOST_TEST
long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5);
u32 read_u32_volatile(long mapped, u32 offset);
#else
static inline long syscall6(long nr, long a0, long a1, long a2,
                            long a3, long a4, long a5) {
  register long r0 __asm__("r0") = a0;
  register long r1 __asm__("r1") = a1;
  register long r2 __asm__("r2") = a2;
  register long r3 __asm__("r3") = a3;
  register long r4 __asm__("r4") = a4;
  register long r5 __asm__("r5") = a5;
  register long r7 __asm__("r7") = nr;
  __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3),
                   "r"(r4), "r"(r5), "r"(r7) : "memory", "cc");
  return r0;
}
static u32 read_u32_volatile(long mapped, u32 offset) {
  volatile const u32 *p = (volatile const u32 *)((usize)mapped + offset);
  return *p;
}
#endif

static int failed(long r) { return (usize)r >= (usize)-4095; }

void *memcpy(void *dst, const void *src, usize count) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  for (usize i = 0; i < count; ++i) d[i] = s[i];
  return dst;
}

static long map_ro(int fd, u32 address, u32 length) {
  return syscall6(SYS_mmap2, 0, (long)length, PROT_READ, MAP_SHARED, fd,
                  (long)(address >> 12));
}
static long map_bounce(void) {
  return syscall6(SYS_mmap2, 0, PREFIX_BYTES, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}
static long unmap_span(long p, u32 n) {
  return syscall6(SYS_munmap, p, n, 0, 0, 0, 0);
}
static void append_char(usize *n, char c) { if (*n < sizeof(output)) output[(*n)++] = c; }
static void append_text(usize *n, const char *s) { while (*s) append_char(n, *s++); }
static void append_hex(usize *n, u32 v) {
  static const char digits[] = "0123456789abcdef";
  append_text(n, "0x");
  for (int shift = 28; shift >= 0; shift -= 4) append_char(n, digits[(v >> shift) & 15u]);
}
static void append_error(usize *n, const char *op, u32 address, long err) {
  append_text(n, "ERROR "); append_text(n, op); append_text(n, " physical-offset ");
  append_hex(n, address); append_text(n, " errno="); append_hex(n, (u32)-err); append_char(n, '\n');
}
static void append_guard_error(usize *n, const char *name, u32 address, u32 expected, u32 actual) {
  append_text(n, "ERROR "); append_text(n, name); append_text(n, " physical-offset ");
  append_hex(n, address); append_text(n, " expected="); append_hex(n, expected);
  append_text(n, " actual="); append_hex(n, actual); append_char(n, '\n');
}
static long write_all(int fd, const char *p, usize n) {
  while (n) {
    long r = syscall6(SYS_write, fd, (long)p, n, 0, 0, 0);
    if (failed(r) || !r) return failed(r) ? r : -5;
    p += (usize)r; n -= (usize)r;
  }
  return 0;
}
static int same(const char *a, const char *b) {
  usize i = 0; while (a[i] && b[i] && a[i] == b[i]) ++i;
  return a[i] == 0 && b[i] == 0;
}
static void copy_prefix_volatile(unsigned char *dst, long mapped) {
  volatile const u32 *src = (volatile const u32 *)(usize)mapped;
  u32 *out = (u32 *)(usize)dst;
  for (usize i = 0; i < PREFIX_BYTES / 4u; ++i) out[i] = src[i];
}

int c_entry(usize *initial_stack) {
  int argc = (int)initial_stack[0];
  char **argv = (char **)(initial_stack + 1);
  if (argc != 2 || !argv[1] || !same(argv[1], "allocator-records")) {
    static const char usage[] = "ERROR: expected exactly one mode: allocator-records\n";
    (void)write_all(2, usage, sizeof(usage) - 1); return 2;
  }
  static const u32 pages[4] = {MANAGER_CELL, ARENA_FIELD_PAGE, GLOBALS, IMAGE};
  static const u32 offsets[9] = {0x7e0u, ARENA_FIELD_OFFSET, 0xe50u, 0xfa4u,
                                  0u, 4u, 8u, 0x60u, 0x84u};
  static const u32 addresses[9] = {0x81c0d7e0u, 0x81d230b0u, 0x81952e50u, 0x81952fa4u,
                                    IMAGE, IMAGE + 4u, IMAGE + 8u, IMAGE + 0x60u, IMAGE + 0x84u};
  static const u32 expected[9] = {MANAGER, ARENA_BASE, IMAGE, IMAGE,
                                  0x8067a158u, 1920u, 960u, 2880u, SAMPLE_PHYSICAL};
  static const u32 movie_work_offsets[5] = {0x59cu, 0x5a0u, 0x5a4u, 0x5a8u, 0x5acu};
  static const u32 image_field_offsets[6] = {0x54u, 0x58u, 0x5cu, 0xa4u, 0xa8u, 0xb0u};
  long fd = -1, maps[5] = {-1,-1,-1,-1,-1}, bounce = -1;
  u32 before[9], after[9];
  u32 movie_work_before[5], movie_work_after[5];
  u32 image_fields_before[6], image_fields_after[6];
  usize n = 0;
  int status = 0;
  fd = syscall6(SYS_open, (long)devmem, 0, 0, 0, 0, 0);
  if (failed(fd)) { append_error(&n, "open /dev/mem", 0, fd); status = 1; goto done; }
  for (int i = 0; i < 4; ++i) {
    maps[i] = map_ro((int)fd, pages[i], PAGE);
    if (failed(maps[i])) { append_error(&n, "mmap2-guard", pages[i], maps[i]); status = 1; goto cleanup; }
  }
  maps[4] = map_ro((int)fd, MANAGER, PREFIX_BYTES);
  if (failed(maps[4])) { append_error(&n, "mmap2-prefix", MANAGER, maps[4]); status = 1; goto cleanup; }
  for (int i = 0; i < 9; ++i) {
    int page = i == 0 ? 0 : (i == 1 ? 1 : (i < 4 ? 2 : 3));
    before[i] = read_u32_volatile(maps[page], offsets[i]);
    if (before[i] != expected[i]) {
      append_guard_error(&n, "metadata-guard", addresses[i], expected[i], before[i]);
      status = 1;
    }
  }
  if (status) goto cleanup;
  bounce = map_bounce();
  if (failed(bounce)) { append_error(&n, "mmap2-bounce", 0, bounce); status = 1; goto cleanup; }
  for (int i = 0; i < 5; ++i)
    movie_work_before[i] = read_u32_volatile(maps[1], movie_work_offsets[i]);
  for (int i = 0; i < 6; ++i)
    image_fields_before[i] = read_u32_volatile(maps[3], image_field_offsets[i]);
  copy_prefix_volatile((unsigned char *)(usize)bounce, maps[4]);
  for (int i = 0; i < 5; ++i)
    movie_work_after[i] = read_u32_volatile(maps[1], movie_work_offsets[i]);
  for (int i = 0; i < 6; ++i)
    image_fields_after[i] = read_u32_volatile(maps[3], image_field_offsets[i]);
  for (int i = 0; i < 9; ++i) {
    int page = i == 0 ? 0 : (i == 1 ? 1 : (i < 4 ? 2 : 3));
    after[i] = read_u32_volatile(maps[page], offsets[i]);
    if (after[i] != expected[i] || after[i] != before[i]) {
      append_guard_error(&n, "metadata-postcheck", addresses[i], before[i], after[i]);
      status = 1;
    }
  }
cleanup:
  for (int i = 4; i >= 0; --i) if (!failed(maps[i])) {
    long r = unmap_span(maps[i], i == 4 ? PREFIX_BYTES : PAGE);
    if (failed(r)) { append_error(&n, "munmap", i == 4 ? MANAGER : pages[i], r); status = 1; }
  }
  if (fd >= 0) {
    long r = syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
    if (failed(r)) { append_error(&n, "close /dev/mem", 0, r); status = 1; }
  }
  if (!status && !failed(bounce)) {
    long r = write_all(1, (const char *)(usize)bounce, PREFIX_BYTES);
    if (failed(r)) { append_error(&n, "write stdout", MANAGER, r); status = 1; }
  }
  if (!failed(bounce)) {
    long r = unmap_span(bounce, PREFIX_BYTES);
    if (failed(r)) { append_error(&n, "munmap-bounce", 0, r); status = 1; }
  }
done:
  if (!status) {
    n = 0;
    append_text(&n, "UNVERIFIED_ALLOCATOR_RECORDS physical-offset 0x81cbb000 bytes 28672 table-indices 0-1780\nMOVIE_WORK_BEFORE");
    for (int i = 0; i < 5; ++i) { append_char(&n, ' '); append_hex(&n, movie_work_before[i]); }
    append_text(&n, "\nMOVIE_WORK_AFTER");
    for (int i = 0; i < 5; ++i) { append_char(&n, ' '); append_hex(&n, movie_work_after[i]); }
    append_text(&n, "\nIMAGE_FIELDS_BEFORE");
    for (int i = 0; i < 6; ++i) { append_char(&n, ' '); append_hex(&n, image_fields_before[i]); }
    append_text(&n, "\nIMAGE_FIELDS_AFTER");
    for (int i = 0; i < 6; ++i) { append_char(&n, ' '); append_hex(&n, image_fields_after[i]); }
    append_char(&n, '\n');
    if (failed(write_all(2, output, n))) status = 1;
  } else if (n) (void)write_all(2, output, n);
  return status;
}
