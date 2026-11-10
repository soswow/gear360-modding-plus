#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef memcpy

#define FULL_BUFFER_HOST_TEST 1
#if !defined(__APPLE__) || defined(__linux__)
#error "host shim is restricted to macOS and must not be linked into the camera ELF"
#endif
#include "full_buffer.c"

/* Host stand-in for the camera maps. The ARM ELF is not executed. */
static unsigned char extent_mem[BUFFER_BYTES];
static unsigned char bounce_mem[BUFFER_BYTES];
static unsigned char guard_mem[PAGE];
static unsigned char image_mem[PAGE];
static unsigned char stdout_mem[BUFFER_BYTES];
static char stderr_mem[8192];

struct H {
  long guard_ptr, image_ptr, bounce_ptr, extent_ptr;
  u32 guards[2], fields[5], reads[20];
  int open_n, map_n, unmap_n, close_n, write_n, read_n, errors;
  int fail_map, fail_unmap, wrong_pre_index, mutate_post;
  int zero_write;
  long open_result, close_result, write_result;
  usize chunk, stdout_n, stderr_n;
  long unmap_ptrs[8];
  u32 unmap_lens[8];
  u32 extent_hash, bounce_hash, guard_hash, image_hash;
} h;

static void fail(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  exit(1);
}
static void ck(int condition, const char *message) {
  if (!condition) fail(message);
}

static u32 fnv(const unsigned char *p, usize n) {
  u32 hash = 2166136261u;
  for (usize i = 0; i < n; ++i) hash = (hash ^ p[i]) * 16777619u;
  return hash;
}

static void fill_extent(void) {
  for (usize i = 0; i < BUFFER_BYTES; ++i)
    extent_mem[i] = (unsigned char)((i * 17u + 0x3du) & 255u);
}

static void reset(void) {
  memset(&h, 0, sizeof(h));
  memset(guard_mem, 0, sizeof(guard_mem));
  memset(image_mem, 0, sizeof(image_mem));
  memset(stdout_mem, 0, sizeof(stdout_mem));
  memset(stderr_mem, 0, sizeof(stderr_mem));
  memset(bounce_mem, 0xa5, sizeof(bounce_mem));
  fill_extent();
  h.open_result = 3;
  h.fail_map = -1;
  h.fail_unmap = -1;
  h.wrong_pre_index = -1;
  h.guards[0] = h.guards[1] = IMAGE_OBJECT;
  h.fields[0] = 0x8067a158u;
  h.fields[1] = WIDTH;
  h.fields[2] = HEIGHT;
  h.fields[3] = STRIDE;
  h.fields[4] = SAMPLE_PHYSICAL;
  h.extent_hash = fnv(extent_mem, BUFFER_BYTES);
  h.bounce_hash = fnv(bounce_mem, BUFFER_BYTES);
  h.guard_hash = fnv(guard_mem, PAGE);
  h.image_hash = fnv(image_mem, PAGE);
}

static int invoke(int argc, const char *mode, const char *extra) {
  const char *argv0 = "helper";
  usize stack[4] = {(usize)argc, (usize)argv0, (usize)mode, (usize)extra};
  return c_entry(stack);
}

long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
  if (nr == SYS_open) {
    h.open_n++;
    if (strcmp((char *)(usize)a0, "/dev/mem") || a1 || a2 || a3 || a4 || a5) h.errors++;
    return h.open_result;
  }
  if (nr == SYS_mmap2) {
    int k = h.map_n++;
    u32 length = (u32)a1;
    u32 prot = (u32)a2;
    u32 flags = (u32)a3;
    if (a0) h.errors++;
    if (k == 0 || k == 1) {
      u32 expect = k == 0 ? 0x81952000u : IMAGE_OBJECT;
      if (length != PAGE || prot != PROT_READ || flags != MAP_SHARED ||
          a4 != h.open_result || ((u32)a5 << 12) != expect)
        h.errors++;
      if (k == h.fail_map) return -12;
      long ptr = (long)(usize)(k == 0 ? guard_mem : image_mem);
      if (k == 0) h.guard_ptr = ptr;
      else h.image_ptr = ptr;
      return ptr;
    }
    if (k == 2) {
      if (length != BUFFER_BYTES || prot != (PROT_READ | PROT_WRITE) ||
          flags != (MAP_PRIVATE | MAP_ANONYMOUS) || a4 != -1 || a5 != 0)
        h.errors++;
      if (k == h.fail_map) return -12;
      h.bounce_ptr = (long)(usize)bounce_mem;
      return h.bounce_ptr;
    }
    if (k == 3) {
      if (length != BUFFER_BYTES || prot != PROT_READ || flags != MAP_SHARED ||
          a4 != h.open_result || ((u32)a5 << 12) != SAMPLE_PHYSICAL)
        h.errors++;
      if (k == h.fail_map) return -12;
      h.extent_ptr = (long)(usize)extent_mem;
      return h.extent_ptr;
    }
    h.errors++;
    return -22;
  }
  if (nr == SYS_munmap) {
    int k = h.unmap_n++;
    if (k >= 8 || a2 || a3 || a4 || a5) h.errors++;
    h.unmap_ptrs[k] = a0;
    h.unmap_lens[k] = (u32)a1;
    if (a0 == h.guard_ptr || a0 == h.image_ptr) {
      if ((u32)a1 != PAGE) h.errors++;
    } else if (a0 == h.bounce_ptr || a0 == h.extent_ptr) {
      if ((u32)a1 != BUFFER_BYTES) h.errors++;
    } else {
      h.errors++;
    }
    if (k == h.fail_unmap) return -5;
    return 0;
  }
  if (nr == SYS_close) {
    h.close_n++;
    if (a0 != h.open_result || a1 || a2 || a3 || a4 || a5) h.errors++;
    return h.close_result;
  }
  if (nr == SYS_write) {
    h.write_n++;
    if (a3 || a4 || a5 || a2 <= 0) h.errors++;
    usize count = (usize)a2;
    if (h.chunk && count > h.chunk) count = h.chunk;
    if (a0 == 1) {
      /* Device maps and the descriptor must already be gone. Stdout failures stay visible on stderr. */
      if (h.write_result) return h.write_result;
      if (h.zero_write) return 0;
      if (h.unmap_n < 3 || h.close_n != 1) h.errors++;
      if (h.stdout_n + count > BUFFER_BYTES) {
        h.errors++;
        return -28;
      }
      memcpy(stdout_mem + h.stdout_n, (void *)(usize)a1, count);
      h.stdout_n += count;
      return (long)count;
    }
    if (a0 == 2) {
      if (h.stderr_n + count >= sizeof(stderr_mem)) {
        h.errors++;
        return -28;
      }
      memcpy(stderr_mem + h.stderr_n, (void *)(usize)a1, count);
      h.stderr_n += count;
      stderr_mem[h.stderr_n] = 0;
      return (long)count;
    }
    h.errors++;
    return -9;
  }
  h.errors++;
  return -38;
}

u32 read_u32_volatile(long mapped, u32 offset) {
  int ordinal = h.read_n++;
  if (ordinal < 20) h.reads[ordinal] = offset;
  else h.errors++;
  if (ordinal == 7 && h.mutate_post == 1) h.guards[0] ^= 1u;
  if (ordinal == 7 && h.mutate_post == 2) h.fields[2] ^= 1u;
  if (ordinal == 7 && h.mutate_post == 3) h.fields[4] ^= 1u;
  u32 value = 0;
  if (mapped == h.guard_ptr && offset == 0xd38u) value = ordinal == 7 ? 0x1234u : 0x1235u;
  else if (mapped == h.guard_ptr && offset == 0xe50u) value = h.guards[0];
  else if (mapped == h.guard_ptr && offset == 0xfa4u) value = h.guards[1];
  else if (mapped == h.image_ptr && offset == 0u) value = h.fields[0];
  else if (mapped == h.image_ptr && offset == 4u) value = h.fields[1];
  else if (mapped == h.image_ptr && offset == 8u) value = h.fields[2];
  else if (mapped == h.image_ptr && offset == 0x60u) value = h.fields[3];
  else if (mapped == h.image_ptr && offset == 0x84u) value = h.fields[4];
  else h.errors++;
  if (h.wrong_pre_index >= 0 && ordinal == h.wrong_pre_index) value ^= 1u;
  return value;
}

static void unchanged_device_bytes(void) {
  ck(fnv(extent_mem, BUFFER_BYTES) == h.extent_hash, "extent bytes unchanged");
  ck(fnv(guard_mem, PAGE) == h.guard_hash, "guard page unchanged");
  ck(fnv(image_mem, PAGE) == h.image_hash, "image page unchanged");
}

static void resources_ok(void) {
  ck(h.errors == 0, "syscall and read contract");
}

static void success(usize chunk) {
  static const u32 expected_offsets[14] = {
      0xe50u, 0xfa4u, 0u, 4u, 8u, 0x60u, 0x84u,
      0xe50u, 0xfa4u, 0u, 4u, 8u, 0x60u, 0x84u};
  reset();
  h.chunk = chunk;
  ck(invoke(2, "full-buffer", 0) == 0, "success status");
  ck(h.open_n == 1 && h.close_n == 1, "one open and close");
  ck(h.map_n == 4 && h.unmap_n == 4, "four maps and four unmaps");
  ck(h.read_n == 14, "seven guards before and after");
  for (int i = 0; i < 14; ++i) ck(h.reads[i] == expected_offsets[i], "guard offset order");
  ck(h.unmap_ptrs[0] == h.extent_ptr && h.unmap_lens[0] == BUFFER_BYTES, "unmap extent first");
  ck(h.unmap_ptrs[1] == h.image_ptr && h.unmap_lens[1] == PAGE, "unmap image page");
  ck(h.unmap_ptrs[2] == h.guard_ptr && h.unmap_lens[2] == PAGE, "unmap guard page");
  ck(h.unmap_ptrs[3] == h.bounce_ptr && h.unmap_lens[3] == BUFFER_BYTES, "unmap bounce last");
  ck(h.stdout_n == BUFFER_BYTES, "exact binary length");
  ck(memcmp(stdout_mem, extent_mem, BUFFER_BYTES) == 0, "stdout matches mapped bytes");
  ck(memcmp(bounce_mem, extent_mem, BUFFER_BYTES) == 0, "bounce received the copy");
  ck(strcmp(stderr_mem,
            "UNVERIFIED_FULL_BUFFER physical-offset 0xa77bb000 bytes 2764800\n") == 0,
     "diagnostic is separate and exact");
  ck(h.stderr_n < 200, "diagnostic stays small");
  unchanged_device_bytes();
  resources_ok();
  if (chunk) ck(h.write_n > 2, "short writes retried");
}

static void observed_success(void) {
  static const u32 expected_offsets[16] = {
      0xe50u, 0xfa4u, 0u, 4u, 8u, 0x60u, 0x84u,
      0xd38u, 0xd38u, 0xe50u, 0xfa4u, 0u, 4u, 8u, 0x60u, 0x84u};
  reset();
  ck(invoke(2, "full-buffer-observed", 0) == 0, "observed success status");
  ck(h.map_n == 4 && h.unmap_n == 4 && h.read_n == 16, "observed mapping and read count");
  for (int i = 0; i < 16; ++i) ck(h.reads[i] == expected_offsets[i], "observed counter surrounds copy");
  ck(h.stdout_n == BUFFER_BYTES && memcmp(stdout_mem, extent_mem, BUFFER_BYTES) == 0,
     "observed stdout matches mapped bytes");
  ck(strcmp(stderr_mem,
            "UNVERIFIED_FULL_BUFFER_OBSERVED physical-offset 0xa77bb000 bytes 2764800 vd_falling_before 0x00001234 vd_falling_after 0x00001235\n") == 0,
     "observed diagnostic includes the two cadence samples");
  unchanged_device_bytes();
  resources_ok();
}

static void rejected(int argc, const char *mode, const char *extra) {
  reset();
  ck(invoke(argc, mode, extra) == 2, "bad arguments return 2");
  ck(h.open_n == 0 && h.map_n == 0 && h.read_n == 0 && h.unmap_n == 0, "reject before open");
  ck(h.stdout_n == 0, "no binary on usage failure");
  ck(strcmp(stderr_mem, "ERROR: expected exactly one mode: full-buffer or full-buffer-observed\n") == 0, "usage text");
  ck(fnv(bounce_mem, BUFFER_BYTES) == h.bounce_hash, "bounce untouched");
  resources_ok();
}

int main(void) {
  success(0);
  success(4096);
  success(100);
  observed_success();
  rejected(1, "", 0);
  rejected(2, "preview-sample", 0);
  rejected(2, "other", 0);
  rejected(2, 0, 0);
  rejected(3, "full-buffer", "extra");

  for (int k = 0; k < 4; ++k) {
    reset();
    h.fail_map = k;
    ck(invoke(2, "full-buffer", 0) != 0, "map failure status");
    ck(h.stdout_n == 0, "map failure writes no binary");
    if (k < 3) ck(h.extent_ptr == 0, "failed before extent map");
    ck(fnv(extent_mem, BUFFER_BYTES) == h.extent_hash, "failed map does not copy");
    ck(strstr(stderr_mem, "ERROR ") == stderr_mem, "map error diagnostic");
    resources_ok();
  }

  for (int k = 0; k < 7; ++k) {
    reset();
    h.wrong_pre_index = k;
    ck(invoke(2, "full-buffer", 0) != 0, "precheck mismatch status");
    ck(h.map_n == 2 && h.read_n == 7 && h.stdout_n == 0, "precheck stops before bounce");
    ck(h.bounce_ptr == 0 && h.extent_ptr == 0, "no extent map after guard mismatch");
    ck(fnv(bounce_mem, BUFFER_BYTES) == h.bounce_hash, "no copy after guard mismatch");
    ck(strstr(stderr_mem, k < 2 ? "global-pointer-guard" : "image-metadata-guard") != 0,
       "named guard error");
    resources_ok();
  }

  for (int m = 1; m <= 3; ++m) {
    reset();
    h.mutate_post = m;
    ck(invoke(2, "full-buffer", 0) != 0, "postcheck change status");
    ck(h.read_n == 14 && h.map_n == 4 && h.stdout_n == 0, "postcheck suppresses binary");
    ck(memcmp(bounce_mem, extent_mem, BUFFER_BYTES) == 0, "copy happened before postcheck");
    ck(strstr(stderr_mem, "postcheck") != 0, "postcheck diagnostic");
    resources_ok();
  }

  reset();
  h.open_result = -13;
  ck(invoke(2, "full-buffer", 0) != 0 && h.map_n == 0 && h.stdout_n == 0, "open failure");
  ck(strstr(stderr_mem, "open /dev/mem") != 0, "open diagnostic");
  resources_ok();

  for (int k = 0; k < 3; ++k) {
    reset();
    h.fail_unmap = k;
    ck(invoke(2, "full-buffer", 0) != 0, "device unmap failure status");
    ck(h.stdout_n == 0, "device unmap failure suppresses binary");
    resources_ok();
  }

  reset();
  h.fail_unmap = 3;
  ck(invoke(2, "full-buffer", 0) != 0, "bounce unmap failure status");
  ck(h.stdout_n == BUFFER_BYTES && memcmp(stdout_mem, extent_mem, BUFFER_BYTES) == 0,
     "bounce unmap failure is still a nonzero exit");
  ck(strstr(stderr_mem, "munmap-bounce") != 0, "bounce unmap diagnostic");
  resources_ok();

  reset();
  h.close_result = -5;
  ck(invoke(2, "full-buffer", 0) != 0 && h.stdout_n == 0, "close failure suppresses binary");
  ck(strstr(stderr_mem, "close /dev/mem") != 0, "close diagnostic");
  resources_ok();

  reset();
  h.write_result = -5;
  ck(invoke(2, "full-buffer", 0) != 0, "write failure status");
  ck(strstr(stderr_mem, "write stdout") != 0, "write diagnostic");
  resources_ok();

  reset();
  h.zero_write = 1;
  ck(invoke(2, "full-buffer", 0) != 0, "zero write status");
  ck(strstr(stderr_mem, "write stdout") != 0, "zero write diagnostic");
  resources_ok();

  puts("PASS: fixed 2764800-byte extent, guards, lengths, binary stdout, and failures");
  return 0;
}
