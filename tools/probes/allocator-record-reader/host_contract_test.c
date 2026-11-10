#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef memcpy

#define ALLOCATOR_HOST_TEST 1
#if !defined(__APPLE__) || defined(__linux__)
#error "host shim is restricted to macOS and must not be linked into camera ELF"
#endif
#include "allocator_records.c"

static unsigned char guard_pages[4][PAGE];
static unsigned char prefix_mem[PREFIX_BYTES];
static unsigned char bounce_mem[PREFIX_BYTES];
static unsigned char stdout_mem[PREFIX_BYTES];
static char stderr_mem[4096];
struct H {
  long ptrs[6], unmaps[8];
  u32 unmap_lens[8];
  int map_n, unmap_n, open_n, close_n, read_n, write_n, errors;
  int mismatch, postchange, descriptor_change, image_change, fail_map;
  usize stdout_n, stderr_n;
} h;

static void ck(int ok, const char *message) {
  if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static void setup(void) {
  memset(&h, 0, sizeof(h));
  memset(guard_pages, 0, sizeof(guard_pages));
  memset(prefix_mem, 0, sizeof(prefix_mem));
  memset(bounce_mem, 0xa5, sizeof(bounce_mem));
  memset(stdout_mem, 0, sizeof(stdout_mem));
  memset(stderr_mem, 0, sizeof(stderr_mem));
  h.fail_map = -1;
  h.mismatch = -1;
  h.postchange = -1;
  h.descriptor_change = -1;
  h.image_change = -1;
  for (usize i = 0; i < PREFIX_BYTES; ++i) prefix_mem[i] = (unsigned char)((i * 31u + 9u) & 255u);
}
static int run_helper(const char *mode) {
  const char *argv0 = "helper";
  usize stack[] = {2, (usize)argv0, (usize)mode};
  return c_entry(stack);
}
long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
  if (nr == SYS_open) {
    h.open_n++;
    if (strcmp((const char *)(usize)a0, "/dev/mem") || a1 || a2 || a3 || a4 || a5) h.errors++;
    return 5;
  }
  if (nr == SYS_mmap2) {
    int k = h.map_n++;
    if (k > 5 || a0) { h.errors++; return -22; }
    if (k < 5) {
      static const u32 addresses[5] = {MANAGER_CELL, ARENA_FIELD_PAGE, GLOBALS, IMAGE, MANAGER};
      u32 expected_len = k == 4 ? PREFIX_BYTES : PAGE;
      if (a1 != expected_len || a2 != PROT_READ || a3 != MAP_SHARED || a4 != 5 ||
          ((u32)a5 << 12) != addresses[k]) h.errors++;
      if (k == h.fail_map) return -12;
      long ptr = (long)(usize)(k == 4 ? prefix_mem : guard_pages[k]);
      h.ptrs[k] = ptr;
      return ptr;
    }
    if (a1 != PREFIX_BYTES || a2 != (PROT_READ | PROT_WRITE) ||
        a3 != (MAP_PRIVATE | MAP_ANONYMOUS) || a4 != -1 || a5) h.errors++;
    if (k == h.fail_map) return -12;
    h.ptrs[5] = (long)(usize)bounce_mem;
    return h.ptrs[5];
  }
  if (nr == SYS_munmap) {
    int k = h.unmap_n++;
    if (k >= 8 || a2 || a3 || a4 || a5) { h.errors++; return 0; }
    h.unmaps[k] = a0; h.unmap_lens[k] = (u32)a1;
    int known = 0;
    for (int i = 0; i < 6; ++i) if (a0 == h.ptrs[i]) known = 1;
    if (!known || (a0 == h.ptrs[4] || a0 == h.ptrs[5] ? (u32)a1 != PREFIX_BYTES : (u32)a1 != PAGE)) h.errors++;
    return 0;
  }
  if (nr == SYS_close) { h.close_n++; if (a0 != 5 || a1 || a2 || a3 || a4 || a5) h.errors++; return 0; }
  if (nr == SYS_write) {
    h.write_n++;
    if (a2 <= 0 || a3 || a4 || a5) h.errors++;
    usize n = (usize)a2;
    if (a0 == 1) {
      if (h.unmap_n != 5 || h.close_n != 1 || h.stdout_n + n > PREFIX_BYTES) h.errors++;
      memcpy(stdout_mem + h.stdout_n, (const void *)(usize)a1, n); h.stdout_n += n; return n;
    }
    if (a0 == 2) {
      if (h.stderr_n + n >= sizeof(stderr_mem)) { h.errors++; return -28; }
      memcpy(stderr_mem + h.stderr_n, (const void *)(usize)a1, n); h.stderr_n += n; stderr_mem[h.stderr_n] = 0; return n;
    }
  }
  h.errors++;
  return -38;
}
u32 read_u32_volatile(long mapped, u32 offset) {
  static const u32 expected[9] = {MANAGER, ARENA_BASE, IMAGE, IMAGE, 0x8067a158u, 1920u, 960u, 2880u, SAMPLE_PHYSICAL};
  static const u32 movie_work[5] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u, 0x55555555u};
  static const u32 movie_offsets[5] = {0x59cu, 0x5a0u, 0x5a4u, 0x5a8u, 0x5acu};
  static const u32 image_fields[6] = {0x0000002bu, 0x11223344u, 2u, 0x55667788u, 0x00123456u, 0x99aabbccu};
  static const u32 image_offsets[6] = {0x54u, 0x58u, 0x5cu, 0xa4u, 0xa8u, 0xb0u};
  int ordinal = h.read_n++;
  if (mapped == h.ptrs[1]) {
    for (int i = 0; i < 5; ++i) if (offset == movie_offsets[i]) {
      if (ordinal >= 20 && h.descriptor_change == i) return movie_work[i] ^ 1u;
      return movie_work[i];
    }
  }
  if (mapped == h.ptrs[3]) {
    for (int i = 0; i < 6; ++i) if (offset == image_offsets[i]) {
      if (ordinal >= 25 && h.image_change == i) return image_fields[i] ^ 1u;
      return image_fields[i];
    }
  }
  int idx;
  if (mapped == h.ptrs[0] && offset == 0x7e0u) idx = 0;
  else if (mapped == h.ptrs[1] && offset == 0xb0u) idx = 1;
  else if (mapped == h.ptrs[2] && offset == 0xe50u) idx = 2;
  else if (mapped == h.ptrs[2] && offset == 0xfa4u) idx = 3;
  else if (mapped == h.ptrs[3] && offset == 0u) idx = 4;
  else if (mapped == h.ptrs[3] && offset == 4u) idx = 5;
  else if (mapped == h.ptrs[3] && offset == 8u) idx = 6;
  else if (mapped == h.ptrs[3] && offset == 0x60u) idx = 7;
  else if (mapped == h.ptrs[3] && offset == 0x84u) idx = 8;
  else { h.errors++; return 0; }
  if ((h.mismatch == idx && ordinal < 9) || (h.postchange == idx && ordinal >= 9)) return expected[idx] ^ 1u;
  return expected[idx];
}

static void success(void) {
  setup();
  ck(run_helper("allocator-records") == 0, "success status");
  ck(h.open_n == 1 && h.close_n == 1 && h.map_n == 6 && h.unmap_n == 6, "fixed map and cleanup counts");
  ck(h.read_n == 40, "pre/post guards and both metadata observations");
  ck(h.stdout_n == PREFIX_BYTES && memcmp(stdout_mem, prefix_mem, PREFIX_BYTES) == 0, "exact prefix output");
  ck(memcmp(bounce_mem, prefix_mem, PREFIX_BYTES) == 0, "prefix copied into Linux bounce buffer");
  ck(h.unmaps[0] == h.ptrs[4] && h.unmap_lens[0] == PREFIX_BYTES, "prefix unmapped before export");
  ck(strcmp(stderr_mem,
      "UNVERIFIED_ALLOCATOR_RECORDS physical-offset 0x81cbb000 bytes 28672 table-indices 0-1780\n"
      "MOVIE_WORK_BEFORE 0x11111111 0x22222222 0x33333333 0x44444444 0x55555555\n"
      "MOVIE_WORK_AFTER 0x11111111 0x22222222 0x33333333 0x44444444 0x55555555\n"
      "IMAGE_FIELDS_BEFORE 0x0000002b 0x11223344 0x00000002 0x55667788 0x00123456 0x99aabbcc\n"
      "IMAGE_FIELDS_AFTER 0x0000002b 0x11223344 0x00000002 0x55667788 0x00123456 0x99aabbcc\n") == 0,
      "success diagnostic includes descriptor observations");
  ck(h.errors == 0, "fixed syscall and address contract");
}
static void image_field_change_is_observation(void) {
  setup(); h.image_change = 4;
  ck(run_helper("allocator-records") == 0, "image-field change does not reject read");
  ck(h.stdout_n == PREFIX_BYTES && memcmp(stdout_mem, prefix_mem, PREFIX_BYTES) == 0,
     "image-field change still exports exact prefix");
  ck(strstr(stderr_mem, "IMAGE_FIELDS_BEFORE 0x0000002b 0x11223344 0x00000002 0x55667788 0x00123456") != 0,
     "image fields before observation retained");
  ck(strstr(stderr_mem, "IMAGE_FIELDS_AFTER 0x0000002b 0x11223344 0x00000002 0x55667788 0x00123457") != 0,
     "image fields after observation retained");
  ck(h.errors == 0, "image-field observation preserves fixed syscall contract");
}
static void descriptor_change_is_observation(void) {
  setup(); h.descriptor_change = 2;
  ck(run_helper("allocator-records") == 0, "descriptor change does not reject read");
  ck(h.stdout_n == PREFIX_BYTES && memcmp(stdout_mem, prefix_mem, PREFIX_BYTES) == 0,
     "descriptor change still exports exact prefix");
  ck(strstr(stderr_mem, "MOVIE_WORK_BEFORE 0x11111111 0x22222222 0x33333333") != 0,
     "descriptor before observation retained");
  ck(strstr(stderr_mem, "MOVIE_WORK_AFTER 0x11111111 0x22222222 0x33333332") != 0,
     "descriptor after observation retained");
  ck(h.errors == 0, "descriptor observation preserves fixed syscall contract");
}
static void guard_failure(int idx, int post) {
  setup(); h.mismatch = post ? -1 : idx; h.postchange = post ? idx : -1;
  ck(run_helper("allocator-records") != 0, "guard mismatch status");
  ck(h.stdout_n == 0, "guard mismatch suppresses binary export");
  ck(h.errors == 0 && h.close_n == 1 && h.unmap_n == (post ? 6 : 5), "guard mismatch cleanup");
  ck(strstr(stderr_mem, post ? "metadata-postcheck" : "metadata-guard") != 0, "guard diagnostic");
}
int main(void) {
  success();
  descriptor_change_is_observation();
  image_field_change_is_observation();
  for (int i = 0; i < 9; ++i) { guard_failure(i, 0); guard_failure(i, 1); }
  setup();
  ck(run_helper("full-buffer") == 2 && h.open_n == 0 && h.map_n == 0, "reject any other mode before open");
  for (int i = 0; i < 6; ++i) {
    setup(); h.fail_map = i;
    ck(run_helper("allocator-records") != 0 && h.stdout_n == 0, "map failure suppresses output");
    ck(h.errors == 0, "map failure contract");
  }
  puts("PASS: fixed allocator prefix, guards, read-only maps, exact output, and cleanup");
  return 0;
}
