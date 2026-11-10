#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(RAM_HELPER_HOST_TEST)
#error "enc-export-state tests require the explicit host syscall seam"
#endif
#include "../ram_helper.c"

_Alignas(4) static unsigned char page[PAGE_SIZE];
static char result[1024];
static usize result_size;
static int opened, mapped, unmapped, closed, fault, short_write, zero_write;
static void check(int ok) { if (!ok) { fputs("enc-export-state: FAIL\n", stderr); exit(1); } }

long ram_helper_test_syscall6(long nr, long a0, long a1, long a2,
                              long a3, long a4, long a5) {
  if (nr == SYS_open) {
    check(!strcmp((const char *)(uintptr_t)a0, "/dev/mem"));
    check(a1 == 0 && a2 == 0); ++opened;
    return fault == 1 ? -5 : 7;
  }
  if (nr == SYS_mmap2) {
    check(a0 == 0 && a1 == PAGE_SIZE && a2 == PROT_READ &&
          a3 == MAP_SHARED && a4 == 7 && a5 == (0x81482000u >> 12));
    ++mapped;
    return fault == 2 ? -12 : (long)(uintptr_t)page;
  }
  if (nr == SYS_munmap) {
    check(a0 == (long)(uintptr_t)page && a1 == PAGE_SIZE); ++unmapped;
    return fault == 3 ? -5 : 0;
  }
  if (nr == SYS_close) {
    check(a0 == 7); ++closed;
    return fault == 4 ? -5 : 0;
  }
  if (nr == SYS_write) {
    check(a0 == 1 && a2 > 0);
    if (opened) check(closed == (fault == 1 ? 0 : 1));
    if (fault == 5) return -5;
    if (zero_write) return 0;
    usize n = (usize)a2;
    if (short_write && n > 3u) n = 3u;
    check(result_size + n < sizeof(result));
    memcpy(result + result_size, (const void *)(uintptr_t)a1, n);
    result_size += n; result[result_size] = 0;
    return (long)n;
  }
  check(0); return -38;
}

static int run(int argc, const char *mode) {
  usize stack[] = {(usize)argc, (usize)(uintptr_t)"reader",
                   (usize)(uintptr_t)mode, (usize)(uintptr_t)"extra", 0};
  return c_entry(stack);
}
static void reset(void) {
  opened = mapped = unmapped = closed = fault = short_write = zero_write = 0;
  result_size = 0; result[0] = 0; memset(page, 0, sizeof(page));
}
int main(void) {
  const u32 values[] = {0, 1, 2, 4, 0xabcdef02u};
  for (usize i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
    reset(); store_le32(page + 0xea4u, values[i]);
    check(run(2, "enc-export-state") == 0);
    check(opened == 1 && mapped == 1 && unmapped == 1 && closed == 1);
    char expected[120];
    snprintf(expected, sizeof(expected),
      "physical-offset 0x81482ea4 bytes: %02x %02x %02x %02x\n",
      values[i]&255u, (values[i]>>8)&255u,
      (values[i]>>16)&255u, (values[i]>>24)&255u);
    check(!strcmp(result, expected));
  }
  for (int i = 1; i <= 5; ++i) {
    reset(); fault = i; check(run(2, "enc-export-state") == 1);
    check(opened == 1 && mapped == (i == 1 ? 0 : 1));
    check(unmapped == (i <= 2 ? 0 : 1) && closed == (i == 1 ? 0 : 1));
    check(!strstr(result, "physical-offset 0x81482ea4 bytes:"));
  }
  reset(); short_write = 1;
  check(run(2, "enc-export-state") == 0);
  check(!strcmp(result, "physical-offset 0x81482ea4 bytes: 00 00 00 00\n"));
  reset(); zero_write = 1;
  check(run(2, "enc-export-state") == 1 && result_size == 0);
  reset(); check(run(1, "enc-export-state") == 2 && opened == 0);
  reset(); check(run(3, "enc-export-state") == 2 && opened == 0);
  reset(); check(run(2, "enc-export-state 0x81482ea8") == 2 && opened == 0);
  puts("enc-export-state host tests: PASS"); return 0;
}
