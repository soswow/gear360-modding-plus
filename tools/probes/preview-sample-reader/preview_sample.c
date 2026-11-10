#include <stdint.h>
#include <stddef.h>

typedef uint32_t u32;
typedef unsigned long usize;
#define PAGE 4096u
#define SAMPLE_SIZE 4096u
#define PROT_READ 1u
#define MAP_SHARED 1u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_exit 1u
#define SYS_munmap 91u
#define SYS_mmap2 192u
#define OUTPUT_CAP 16384u
#define IMAGE_OBJECT 0x82246000u
#define SAMPLE_PHYSICAL 0xa77bb000u
#define PREVIEW_MARKER "UNVERIFIED_PREVIEW_SAMPLE "

static const char devmem[] = "/dev/mem";
static char output[OUTPUT_CAP];

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
                   : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
                   : "memory", "cc");
  return r0;
}

static int failed(long r) { return (usize)r >= (usize)-4095; }

/* Freestanding byte copy; avoids a libc/loader dependency for local arrays. */
void *memcpy(void *dst, const void *src, usize count) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  for (usize i = 0; i < count; ++i) d[i] = s[i];
  return dst;
}
static long map_page(int fd, u32 address) {
  return syscall6(SYS_mmap2, 0, PAGE, PROT_READ, MAP_SHARED, fd,
                  (long)(address >> 12));
}
static long unmap_page(long mapped) {
  return syscall6(SYS_munmap, mapped, PAGE, 0, 0, 0, 0);
}
static u32 read_u32_volatile(long mapped, u32 offset) {
  volatile const u32 *p = (volatile const u32 *)((usize)mapped + offset);
  return *p;
}
static void copy_sample_volatile(unsigned char *dst, long mapped) {
  volatile const unsigned char *p = (volatile const unsigned char *)(usize)mapped;
  for (usize i = 0; i < SAMPLE_SIZE; ++i) dst[i] = p[i];
}
static void append_char(usize *n, char c) {
  if (*n < sizeof(output)) output[(*n)++] = c;
}
static void append_text(usize *n, const char *s) {
  while (*s) append_char(n, *s++);
}
static void append_hex(usize *n, u32 v) {
  static const char digits[] = "0123456789abcdef";
  append_text(n, "0x");
  for (int shift = 28; shift >= 0; shift -= 4)
    append_char(n, digits[(v >> shift) & 15u]);
}
static void append_error(usize *n, const char *op, u32 address, long err) {
  append_text(n, "ERROR "); append_text(n, op);
  append_text(n, " physical-offset "); append_hex(n, address);
  append_text(n, " errno="); append_hex(n, (u32)-err); append_char(n, '\n');
}
static void append_guard_error(usize *n, const char *name, u32 address,
                               u32 expected, u32 actual) {
  append_text(n, "ERROR "); append_text(n, name);
  append_text(n, " physical-offset "); append_hex(n, address);
  append_text(n, " expected="); append_hex(n, expected);
  append_text(n, " actual="); append_hex(n, actual); append_char(n, '\n');
}
static long write_all(const char *p, usize n) {
  while (n) {
    long r = syscall6(SYS_write, 1, (long)p, (long)n, 0, 0, 0);
    if (failed(r) || r == 0) return failed(r) ? r : -5;
    p += (usize)r; n -= (usize)r;
  }
  return 0;
}
static int same(const char *a, const char *b) {
  usize i = 0;
  while (a[i] && b[i] && a[i] == b[i]) ++i;
  return a[i] == 0 && b[i] == 0;
}
static void append_sample(usize *n, const unsigned char *sample) {
  static const char digits[] = "0123456789abcdef";
  append_text(n, PREVIEW_MARKER "physical-offset 0xa77bb000 bytes:");
  for (usize i = 0; i < SAMPLE_SIZE; ++i) {
    unsigned char b = sample[i];
    append_char(n, ' '); append_char(n, digits[b >> 4]); append_char(n, digits[b & 15u]);
  }
  append_char(n, '\n');
}

/* The only accepted mode is a fixed guarded preview sample. */
int c_entry(usize *initial_stack) {
  int argc = (int)initial_stack[0];
  char **argv = (char **)(initial_stack + 1);
  if (argc != 2 || !argv[1] || !same(argv[1], "preview-sample")) {
    static const char usage[] = "ERROR: expected exactly one mode: preview-sample\n";
    (void)write_all(usage, sizeof(usage) - 1);
    return 2;
  }

  const u32 guard_page_address = 0x81952000u;
  const u32 image_page_address = IMAGE_OBJECT;
  const u32 sample_page_address = SAMPLE_PHYSICAL;
  const u32 guard_offsets[2] = {0xe50u, 0xfa4u};
  const u32 guard_cells[2] = {0x81952e50u, 0x81952fa4u};
  const u32 image_offsets[5] = {0u, 4u, 8u, 0x60u, 0x84u};
  const u32 image_fields[5] = {0x8067a158u, 1920u, 960u, 2880u, SAMPLE_PHYSICAL};
  const u32 image_addresses[5] = {IMAGE_OBJECT, IMAGE_OBJECT+4u, IMAGE_OBJECT+8u,
                                  IMAGE_OBJECT+0x60u, IMAGE_OBJECT+0x84u};
  long pages[3] = {-1, -1, -1};
  u32 guard_before[2], image_before[5], guard_after[2], image_after[5];
  unsigned char sample[SAMPLE_SIZE];
  usize n = 0;
  int status = 0;
  long fd = syscall6(SYS_open, (long)devmem, 0, 0, 0, 0, 0);
  if (failed(fd)) {
    append_error(&n, "open /dev/mem", 0, fd);
    (void)write_all(output, n);
    return 1;
  }

  const u32 page_addresses[3] = {guard_page_address, image_page_address, sample_page_address};
  for (usize i = 0; i < 2; ++i) {
    pages[i] = map_page((int)fd, page_addresses[i]);
    if (failed(pages[i])) {
      append_error(&n, "mmap2", page_addresses[i], pages[i]); status = 1; break;
    }
  }
  if (!status) {
    for (usize i = 0; i < 2; ++i)
      guard_before[i] = read_u32_volatile(pages[0], guard_offsets[i]);
    for (usize i = 0; i < 5; ++i)
      image_before[i] = read_u32_volatile(pages[1], image_offsets[i]);
    for (usize i = 0; i < 2; ++i) {
      if (guard_before[i] != IMAGE_OBJECT) {
        append_guard_error(&n, "global-pointer-guard", guard_cells[i], IMAGE_OBJECT, guard_before[i]); status = 1;
      }
    }
    for (usize i = 0; i < 5; ++i) {
      if (image_before[i] != image_fields[i]) {
        append_guard_error(&n, "image-metadata-guard", image_addresses[i], image_fields[i], image_before[i]); status = 1;
      }
    }
  }
  if (!status) {
    pages[2] = map_page((int)fd, sample_page_address);
    if (failed(pages[2])) {
      append_error(&n, "mmap2", sample_page_address, pages[2]); status = 1;
    } else {
      copy_sample_volatile(sample, pages[2]);
      for (usize i = 0; i < 2; ++i)
        guard_after[i] = read_u32_volatile(pages[0], guard_offsets[i]);
      for (usize i = 0; i < 5; ++i)
        image_after[i] = read_u32_volatile(pages[1], image_offsets[i]);
      for (usize i = 0; i < 2; ++i) {
        if (guard_after[i] != IMAGE_OBJECT || guard_after[i] != guard_before[i]) {
          append_guard_error(&n, "global-pointer-postcheck", guard_cells[i], guard_before[i], guard_after[i]); status = 1;
        }
      }
      for (usize i = 0; i < 5; ++i) {
        if (image_after[i] != image_fields[i] || image_after[i] != image_before[i]) {
          append_guard_error(&n, "image-metadata-postcheck", image_addresses[i], image_before[i], image_after[i]); status = 1;
        }
      }
    }
  }

  for (int i = 2; i >= 0; --i) {
    if (!failed(pages[i])) {
      long r = unmap_page(pages[i]);
      if (failed(r)) { append_error(&n, "munmap", page_addresses[i], r); status = 1; }
    }
  }
  long cr = syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  if (failed(cr)) { append_error(&n, "close /dev/mem", 0, cr); status = 1; }
  if (!status) append_sample(&n, sample);
  if (n && failed(write_all(output, n))) status = 1;
  return status;
}
