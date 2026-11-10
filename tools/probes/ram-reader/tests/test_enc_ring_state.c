#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(RAM_HELPER_HOST_TEST)
#error "enc-ring-state tests require the explicit host syscall seam"
#endif
#if defined(__linux__) && \
    (defined(__arm__) || defined(__aarch64__) || defined(__ARM_EABI__))
#error "enc-ring-state tests must never build as a Linux ARM syscall shim"
#endif

#include "../ram_helper.c"

static unsigned char cursor_fixture[PAGE_SIZE];
static unsigned char ready_fixture[PAGE_SIZE];

struct syscall_state {
  int open_calls;
  int map_calls;
  int unmap_calls;
  int close_calls;
  int write_calls;
  int protocol_error;
  int fail_open;
  int fail_map_at;
  int fail_unmap_at;
  int fail_close;
  int fail_write;
  int fail_write_at;
  int zero_write;
  int short_write;
  int mutate_on_second_map;
  long map_pages[2];
  long map_lengths[2];
  long map_protections[2];
  long map_flags[2];
  long map_fds[2];
  long unmap_addresses[2];
  long unmap_lengths[2];
  unsigned char written[2048];
  usize written_length;
};

static struct syscall_state state;

static void fail(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  exit(1);
}

static void check(int condition, const char *message) {
  if (!condition) fail(message);
}

static void check_bytes(const unsigned char *actual, usize actual_length,
                        const char *expected) {
  usize expected_length = strlen(expected);
  if (actual_length != expected_length ||
      memcmp(actual, expected, expected_length) != 0) {
    fprintf(stderr, "FAIL: output mismatch\nexpected:\n%sactual:\n%.*s",
            expected, (int)actual_length, actual);
    exit(1);
  }
}

static void reset_fixture(void) {
  memset(&state, 0, sizeof(state));
  memset(cursor_fixture, 0, sizeof(cursor_fixture));
  memset(ready_fixture, 0, sizeof(ready_fixture));
  cursor_fixture[0xc50u] = 0x12u;
  cursor_fixture[0xc51u] = 0x34u;
  for (usize i = 0; i < 25u; ++i)
    ready_fixture[0x2adu + i] = (unsigned char)(0xa0u + i);
}

static long fixture_pointer(const void *pointer) {
  return (long)(uintptr_t)pointer;
}

long ram_helper_test_syscall6(long nr, long a0, long a1, long a2,
                              long a3, long a4, long a5) {
  (void)a0;
  if (nr == SYS_open) {
    state.open_calls++;
    if (a1 != 0 || a2 != 0) state.protocol_error = 1;
    return state.fail_open ? -5 : 7;
  }
  if (nr == SYS_mmap2) {
    int index = state.map_calls++;
    if (index >= 2 || a0 != 0 || a1 != PAGE_SIZE || a2 != PROT_READ ||
        a3 != MAP_SHARED || a4 != 7) {
      state.protocol_error = 1;
    } else {
      state.map_pages[index] = a5;
      state.map_lengths[index] = a1;
      state.map_protections[index] = a2;
      state.map_flags[index] = a3;
      state.map_fds[index] = a4;
    }
    if (state.fail_map_at == index + 1) return -12;
    if (index == 0 && a5 == (long)(0x85401000u >> 12))
      return fixture_pointer(cursor_fixture);
    if (index == 1 && a5 == (long)(0x85434000u >> 12)) {
      if (state.mutate_on_second_map) {
        cursor_fixture[0xc50u] = 0x56u;
        cursor_fixture[0xc51u] = 0x78u;
      }
      return fixture_pointer(ready_fixture);
    }
    state.protocol_error = 1;
    return -22;
  }
  if (nr == SYS_munmap) {
    int index = state.unmap_calls++;
    if (index >= 2 || a1 != PAGE_SIZE) {
      state.protocol_error = 1;
    } else {
      state.unmap_addresses[index] = a0;
      state.unmap_lengths[index] = a1;
    }
    return state.fail_unmap_at == index + 1 ? -5 : 0;
  }
  if (nr == SYS_close) {
    state.close_calls++;
    if (a0 != 7) state.protocol_error = 1;
    return state.fail_close ? -9 : 0;
  }
  if (nr == SYS_write) {
    state.write_calls++;
    if (a0 != 1 || a2 < 0 || (usize)a2 > sizeof(state.written))
      state.protocol_error = 1;
    if (state.fail_write || state.fail_write_at == state.write_calls) return -5;
    if (state.zero_write) return 0;
    usize count = (usize)a2;
    if (state.short_write && state.write_calls == 1 && count > 1u)
      count /= 2u;
    memcpy(state.written + state.written_length, (const void *)(uintptr_t)a1,
           count);
    state.written_length += count;
    return (long)count;
  }
  state.protocol_error = 1;
  return -38;
}

static int invoke(int argc, const char *mode) {
  char *argv[4] = {(char *)"ram-reader", (char *)mode, (char *)"extra", 0};
  usize stack[5] = {0, 0, 0, 0, 0};
  stack[0] = (usize)argc;
  for (int i = 0; i < argc; ++i)
    stack[i + 1] = (usize)(uintptr_t)argv[i];
  return c_entry(stack);
}

static void check_common_success_calls(void) {
  check(!state.protocol_error, "syscall arguments violated read-only contract");
  check(state.open_calls == 1, "success did not open exactly once");
  check(state.map_calls == 2, "success did not map exactly two pages");
  check(state.map_pages[0] == (long)(0x85401000u >> 12),
        "first map page number changed");
  check(state.map_pages[1] == (long)(0x85434000u >> 12),
        "second map page number changed");
  check(state.map_lengths[0] == PAGE_SIZE && state.map_lengths[1] == PAGE_SIZE,
        "mapping length changed");
  check(state.map_protections[0] == PROT_READ &&
            state.map_protections[1] == PROT_READ,
        "mapping protection changed");
  check(state.map_flags[0] == MAP_SHARED && state.map_flags[1] == MAP_SHARED,
        "mapping flags changed");
  check(state.map_fds[0] == 7 && state.map_fds[1] == 7,
        "mapping fd changed");
  check(state.unmap_calls == 2, "success did not unmap both pages");
  check(state.unmap_addresses[0] == fixture_pointer(ready_fixture) &&
            state.unmap_addresses[1] == fixture_pointer(cursor_fixture),
        "pages were not unmapped in reverse acquisition order");
  check(state.close_calls == 1, "success did not close exactly once");
}

static void test_success(void) {
  static const char expected[] =
      "physical-offset 0x85401c50 bytes: 12 34\n"
      "physical-offset 0x854342ad bytes: a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af b0 b1 b2 b3 b4 b5 b6 b7 b8\n";
  reset_fixture();
  check(invoke(2, "enc-ring-state") == 0, "success returned an error");
  check_common_success_calls();
  check_bytes(state.written, state.written_length, expected);
}

static void test_short_write(void) {
  static const char expected[] =
      "physical-offset 0x85401c50 bytes: 12 34\n"
      "physical-offset 0x854342ad bytes: a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aa ab ac ad ae af b0 b1 b2 b3 b4 b5 b6 b7 b8\n";
  reset_fixture();
  state.short_write = 1;
  check(invoke(2, "enc-ring-state") == 0, "short write returned an error");
  check(state.write_calls == 2, "short write was not completed");
  check_bytes(state.written, state.written_length, expected);
}

static void test_cursor_change(void) {
  reset_fixture();
  state.mutate_on_second_map = 1;
  check(invoke(2, "enc-ring-state") == 1,
        "changed cursors were accepted");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "changed cursor path did not clean up");
  check_bytes(state.written, state.written_length,
              "ERROR enc-ring-cursors-changed physical-offset 0x85401c50 expected=0x00003412 actual=0x00007856\n");
}

static void test_failure_paths(void) {
  reset_fixture();
  state.fail_open = 1;
  check(invoke(2, "enc-ring-state") == 1, "open failure returned success");
  check(state.open_calls == 1 && state.map_calls == 0 && state.close_calls == 0,
        "open failure used an owned resource");
  check_bytes(state.written, state.written_length,
              "ERROR open /dev/mem physical-offset 0x00000000 errno=0x00000005\n");

  reset_fixture();
  state.fail_map_at = 1;
  check(invoke(2, "enc-ring-state") == 1, "first map failure returned success");
  check(state.map_calls == 1 && state.unmap_calls == 0 && state.close_calls == 1,
        "first map failure cleanup changed");
  check_bytes(state.written, state.written_length,
              "ERROR mmap2 physical-offset 0x85401000 errno=0x0000000c\n");

  reset_fixture();
  state.fail_map_at = 2;
  check(invoke(2, "enc-ring-state") == 1, "second map failure returned success");
  check(state.map_calls == 2 && state.unmap_calls == 1 && state.close_calls == 1,
        "second map failure did not unmap first page");
  check_bytes(state.written, state.written_length,
              "ERROR mmap2 physical-offset 0x85434000 errno=0x0000000c\n");

  reset_fixture();
  state.fail_unmap_at = 1;
  check(invoke(2, "enc-ring-state") == 1, "second unmap failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "second unmap failure did not attempt first unmap");
  check_bytes(state.written, state.written_length,
              "ERROR munmap physical-offset 0x85434000 errno=0x00000005\n");

  reset_fixture();
  state.fail_unmap_at = 2;
  check(invoke(2, "enc-ring-state") == 1, "first unmap failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "first unmap failure cleanup changed");
  check_bytes(state.written, state.written_length,
              "ERROR munmap physical-offset 0x85401000 errno=0x00000005\n");

  reset_fixture();
  state.fail_close = 1;
  check(invoke(2, "enc-ring-state") == 1, "close failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "close failure did not retain prior cleanup");
  check_bytes(state.written, state.written_length,
              "ERROR close /dev/mem errno=0x00000009\n");

  reset_fixture();
  state.fail_write = 1;
  check(invoke(2, "enc-ring-state") == 1, "write failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1 &&
            state.write_calls == 1 && state.written_length == 0,
        "write failure cleanup or output changed");

  reset_fixture();
  state.zero_write = 1;
  check(invoke(2, "enc-ring-state") == 1,
        "zero-byte write returned success");
  check(state.write_calls == 1 && state.written_length == 0,
        "zero-byte write did not stop");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "zero-byte write cleanup changed");

  reset_fixture();
  state.short_write = 1;
  state.fail_write_at = 2;
  check(invoke(2, "enc-ring-state") == 1,
        "failure after a partial write returned success");
  check(state.write_calls == 2 && state.written_length > 0,
        "partial-write failure fixture was not exercised");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "partial-write failure cleanup changed");
}

static void test_argument_rejection(void) {
  reset_fixture();
  check(invoke(1, "enc-ring-state") == 2,
        "missing mode argument was accepted");
  check(state.open_calls == 0, "missing mode opened /dev/mem");

  reset_fixture();
  check(invoke(3, "enc-ring-state") == 2,
        "extra argument was accepted");
  check(state.open_calls == 0, "extra argument opened /dev/mem");

  reset_fixture();
  check(invoke(2, "unknown") == 2, "invalid mode was accepted");
  check(state.open_calls == 0, "invalid mode opened /dev/mem");
}

int main(void) {
  test_success();
  test_short_write();
  test_cursor_change();
  test_failure_paths();
  test_argument_rejection();
  puts("enc-ring-state host tests: PASS");
  return 0;
}
