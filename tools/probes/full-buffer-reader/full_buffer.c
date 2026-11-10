#include <stdint.h>
#include <stddef.h>

typedef uint32_t u32;
typedef unsigned long usize;

#define PAGE 4096u
#define WIDTH 1920u
#define HEIGHT 960u
#define STRIDE 2880u
/* 2880 * 960 = 2,764,800 = 675 * 4096. The copy length is this constant. */
#define BUFFER_BYTES (STRIDE * HEIGHT)
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
#define OUTPUT_CAP 4096u
#define IMAGE_OBJECT 0x82246000u
#define SAMPLE_PHYSICAL 0xa77bb000u

_Static_assert(BUFFER_BYTES == 2764800u, "full extent");
_Static_assert(BUFFER_BYTES == 675u * PAGE, "page count");
_Static_assert((SAMPLE_PHYSICAL & (PAGE - 1u)) == 0u, "page aligned");
_Static_assert(BUFFER_BYTES % 4u == 0u, "word copy");
_Static_assert(WIDTH * 12u / 8u == STRIDE, "12-bit stride candidate");

static const char devmem[] = "/dev/mem";
static char output[OUTPUT_CAP];

#ifdef FULL_BUFFER_HOST_TEST
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
  __asm__ volatile("svc #0"
                   : "+r"(r0)
                   : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
                   : "memory", "cc");
  return r0;
}

static u32 read_u32_volatile(long mapped, u32 offset) {
  volatile const u32 *p = (volatile const u32 *)((usize)mapped + offset);
  return *p;
}
#endif

static int failed(long r) { return (usize)r >= (usize)-4095; }

/* Clang emits this for local array setup. It must not come from a hosted libc. */
void *memcpy(void *dst, const void *src, usize count) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  for (usize i = 0; i < count; ++i) d[i] = s[i];
  return dst;
}

static long map_readonly(int fd, u32 address, u32 length) {
  return syscall6(SYS_mmap2, 0, (long)length, PROT_READ, MAP_SHARED, fd,
                  (long)(address >> 12));
}

/* Linux-owned destination. Never a stack buffer: the extent is 2.7 MiB. */
static long map_bounce(u32 length) {
  return syscall6(SYS_mmap2, 0, (long)length, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

static long unmap_span(long mapped, u32 length) {
  return syscall6(SYS_munmap, mapped, (long)length, 0, 0, 0, 0);
}

/* Word copies are exact: both maps are page-aligned and the length is a multiple of 4. */
static void copy_extent_volatile(unsigned char *dst, long mapped) {
  volatile const u32 *src = (volatile const u32 *)(usize)mapped;
  u32 *out = (u32 *)(usize)dst;
  for (usize i = 0; i < BUFFER_BYTES / 4u; ++i) out[i] = src[i];
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
static long write_fd_all(int fd, const char *p, usize n) {
  while (n) {
    long r = syscall6(SYS_write, fd, (long)p, (long)n, 0, 0, 0);
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

/* Fixed guards, then one read-only extent. Binary stdout only after postchecks. */
int c_entry(usize *initial_stack) {
  int argc = (int)initial_stack[0];
  char **argv = (char **)(initial_stack + 1);
  int observed = argc == 2 && argv[1] && same(argv[1], "full-buffer-observed");
  if (argc != 2 || !argv[1] || (!same(argv[1], "full-buffer") && !observed)) {
    static const char usage[] = "ERROR: expected exactly one mode: full-buffer or full-buffer-observed\n";
    (void)write_fd_all(2, usage, sizeof(usage) - 1);
    return 2;
  }

  const u32 guard_page_address = 0x81952000u;
  const u32 image_page_address = IMAGE_OBJECT;
  const u32 guard_offsets[2] = {0xe50u, 0xfa4u};
  const u32 guard_cells[2] = {0x81952e50u, 0x81952fa4u};
  const u32 image_offsets[5] = {0u, 4u, 8u, 0x60u, 0x84u};
  const u32 image_fields[5] = {0x8067a158u, WIDTH, HEIGHT, STRIDE, SAMPLE_PHYSICAL};
  const u32 image_addresses[5] = {IMAGE_OBJECT, IMAGE_OBJECT + 4u, IMAGE_OBJECT + 8u,
                                  IMAGE_OBJECT + 0x60u, IMAGE_OBJECT + 0x84u};
  long guard_map = -1, image_map = -1, bounce = -1, extent = -1;
  u32 guard_before[2], image_before[5], guard_after[2], image_after[5];
  u32 cadence_before = 0, cadence_after = 0;
  usize n = 0;
  int status = 0;

  long fd = syscall6(SYS_open, (long)devmem, 0, 0, 0, 0, 0);
  if (failed(fd)) {
    append_error(&n, "open /dev/mem", 0, fd);
    (void)write_fd_all(2, output, n);
    return 1;
  }

  guard_map = map_readonly((int)fd, guard_page_address, PAGE);
  if (failed(guard_map)) {
    append_error(&n, "mmap2", guard_page_address, guard_map);
    status = 1;
  }
  if (!status) {
    image_map = map_readonly((int)fd, image_page_address, PAGE);
    if (failed(image_map)) {
      append_error(&n, "mmap2", image_page_address, image_map);
      status = 1;
    }
  }
  if (!status) {
    for (usize i = 0; i < 2; ++i)
      guard_before[i] = read_u32_volatile(guard_map, guard_offsets[i]);
    for (usize i = 0; i < 5; ++i)
      image_before[i] = read_u32_volatile(image_map, image_offsets[i]);
    for (usize i = 0; i < 2; ++i) {
      if (guard_before[i] != IMAGE_OBJECT) {
        append_guard_error(&n, "global-pointer-guard", guard_cells[i],
                           IMAGE_OBJECT, guard_before[i]);
        status = 1;
      }
    }
    for (usize i = 0; i < 5; ++i) {
      if (image_before[i] != image_fields[i]) {
        append_guard_error(&n, "image-metadata-guard", image_addresses[i],
                           image_fields[i], image_before[i]);
        status = 1;
      }
    }
  }
  /* Abort on any mismatch. Do not read a different pointer or a different length. */
  if (!status) {
    bounce = map_bounce(BUFFER_BYTES);
    if (failed(bounce)) {
      append_error(&n, "mmap2-bounce", 0, bounce);
      status = 1;
    }
  }
  if (!status) {
    extent = map_readonly((int)fd, SAMPLE_PHYSICAL, BUFFER_BYTES);
    if (failed(extent)) {
      append_error(&n, "mmap2-extent", SAMPLE_PHYSICAL, extent);
      status = 1;
    } else {
      if (observed) cadence_before = read_u32_volatile(guard_map, 0xd38u);
      copy_extent_volatile((unsigned char *)(usize)bounce, extent);
      if (observed) cadence_after = read_u32_volatile(guard_map, 0xd38u);
      for (usize i = 0; i < 2; ++i)
        guard_after[i] = read_u32_volatile(guard_map, guard_offsets[i]);
      for (usize i = 0; i < 5; ++i)
        image_after[i] = read_u32_volatile(image_map, image_offsets[i]);
      for (usize i = 0; i < 2; ++i) {
        if (guard_after[i] != IMAGE_OBJECT || guard_after[i] != guard_before[i]) {
          append_guard_error(&n, "global-pointer-postcheck", guard_cells[i],
                             guard_before[i], guard_after[i]);
          status = 1;
        }
      }
      for (usize i = 0; i < 5; ++i) {
        if (image_after[i] != image_fields[i] || image_after[i] != image_before[i]) {
          append_guard_error(&n, "image-metadata-postcheck", image_addresses[i],
                             image_before[i], image_after[i]);
          status = 1;
        }
      }
    }
  }

  /* Drop the device maps before exporting. The bytes then live only in the bounce map. */
  if (!failed(extent)) {
    long r = unmap_span(extent, BUFFER_BYTES);
    if (failed(r)) {
      append_error(&n, "munmap-extent", SAMPLE_PHYSICAL, r);
      status = 1;
    }
  }
  if (!failed(image_map)) {
    long r = unmap_span(image_map, PAGE);
    if (failed(r)) {
      append_error(&n, "munmap", image_page_address, r);
      status = 1;
    }
  }
  if (!failed(guard_map)) {
    long r = unmap_span(guard_map, PAGE);
    if (failed(r)) {
      append_error(&n, "munmap", guard_page_address, r);
      status = 1;
    }
  }
  long cr = syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  if (failed(cr)) {
    append_error(&n, "close /dev/mem", 0, cr);
    status = 1;
  }

  if (!status) {
    long wr = write_fd_all(1, (const char *)(usize)bounce, BUFFER_BYTES);
    if (failed(wr)) {
      append_error(&n, "write stdout", SAMPLE_PHYSICAL, wr);
      status = 1;
    }
  }
  if (!failed(bounce)) {
    long r = unmap_span(bounce, BUFFER_BYTES);
    if (failed(r)) {
      append_error(&n, "munmap-bounce", 0, r);
      status = 1;
    }
  }
  if (!status) {
    if (observed) {
      n = 0;
      append_text(&n, "UNVERIFIED_FULL_BUFFER_OBSERVED physical-offset 0xa77bb000 bytes 2764800 vd_falling_before ");
      append_hex(&n, cadence_before);
      append_text(&n, " vd_falling_after ");
      append_hex(&n, cadence_after);
      append_char(&n, '\n');
      if (failed(write_fd_all(2, output, n))) status = 1;
    } else {
      static const char ok[] =
          "UNVERIFIED_FULL_BUFFER physical-offset 0xa77bb000 bytes 2764800\n";
      if (failed(write_fd_all(2, ok, sizeof(ok) - 1))) status = 1;
    }
  } else if (n && failed(write_fd_all(2, output, n))) {
    status = 1;
  }
  return status;
}
