#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(RAM_HELPER_HOST_TEST)
#error "ipcc-map-state tests require the explicit host syscall seam"
#endif
#if defined(__linux__) && \
    (defined(__arm__) || defined(__aarch64__) || defined(__ARM_EABI__))
#error "ipcc-map-state tests must never build as a Linux ARM syscall shim"
#endif

#include "../ram_helper.c"

static unsigned char guard_fixture[PAGE_SIZE];
static unsigned char data_fixture[PAGE_SIZE];

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
  int zero_write;
  int short_write;
  int mutate_guard_on_data_map;
  long map_pages[2];
  long map_lengths[2];
  long map_protections[2];
  long map_flags[2];
  long map_fds[2];
  long unmap_addresses[2];
  long unmap_lengths[2];
  unsigned char written[1024];
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

static int contains_bytes(const unsigned char *actual, usize actual_length,
                          const char *needle) {
  usize needle_length = strlen(needle);
  if (needle_length > actual_length) return 0;
  for (usize start = 0; start + needle_length <= actual_length; ++start)
    if (memcmp(actual + start, needle, needle_length) == 0) return 1;
  return 0;
}

static void check_no_data_records(void) {
  static const char first_address[] = "physical-offset 0x81000080";
  static const char second_address[] = "physical-offset 0x81000180";
  check(!contains_bytes(state.written, state.written_length, first_address),
        "failure emitted the first successful data record");
  check(!contains_bytes(state.written, state.written_length, second_address),
        "failure emitted the second successful data record");
}

static void reset_fixture(void) {
  memset(&state, 0, sizeof(state));
  memset(guard_fixture, 0, sizeof(guard_fixture));
  memset(data_fixture, 0, sizeof(data_fixture));
  guard_fixture[0x218u] = 0x00u;
  guard_fixture[0x219u] = 0x00u;
  guard_fixture[0x21au] = 0x00u;
  guard_fixture[0x21bu] = 0x81u;
  data_fixture[0x80u] = 0x11u;
  data_fixture[0x81u] = 0x22u;
  data_fixture[0x82u] = 0x33u;
  data_fixture[0x83u] = 0x44u;
  data_fixture[0x180u] = 0xa0u;
  data_fixture[0x181u] = 0xb0u;
  data_fixture[0x182u] = 0xc0u;
  data_fixture[0x183u] = 0xd0u;
}

static long fixture_pointer(const void *pointer) {
  return (long)(uintptr_t)pointer;
}

long ram_helper_test_syscall6(long nr, long a0, long a1, long a2,
                              long a3, long a4, long a5) {
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
    if (index == 0 && a5 == (long)(0x80008000u >> 12))
      return fixture_pointer(guard_fixture);
    if (index == 1 && a5 == (long)(0x81000000u >> 12)) {
      if (state.mutate_guard_on_data_map)
        guard_fixture[0x218u] = 0x01u;
      return fixture_pointer(data_fixture);
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
    if (a0 != 1 || a2 < 0 || (usize)a2 > sizeof(state.written) ||
        state.close_calls != 1)
      state.protocol_error = 1;
    if (state.fail_write) return -5;
    if (state.zero_write) return 0;
    usize count = (usize)a2;
    if (state.short_write && count > 7u) count = 7u;
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

static void check_success_calls(void) {
  check(!state.protocol_error, "syscall arguments violated read-only contract");
  check(state.open_calls == 1, "success did not open exactly once");
  check(state.map_calls == 2, "success did not map exactly two pages");
  check(state.map_pages[0] == (long)(0x80008000u >> 12),
        "guard page number changed");
  check(state.map_pages[1] == (long)(0x81000000u >> 12),
        "data page number changed");
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
  check(state.unmap_addresses[0] == fixture_pointer(data_fixture) &&
            state.unmap_addresses[1] == fixture_pointer(guard_fixture),
        "pages were not unmapped in reverse acquisition order");
  check(state.close_calls == 1, "success did not close exactly once");
  check(state.write_calls > 0, "success did not write output");
}

static void test_success(void) {
  static const char expected[] =
      "physical-offset 0x81000080 bytes: 11 22 33 44\n"
      "physical-offset 0x81000180 bytes: a0 b0 c0 d0\n";
  reset_fixture();
  check(invoke(2, "ipcc-map-state") == 0, "success returned an error");
  check_success_calls();
  check_bytes(state.written, state.written_length, expected);
}

static void test_short_and_zero_writes(void) {
  static const char expected[] =
      "physical-offset 0x81000080 bytes: 11 22 33 44\n"
      "physical-offset 0x81000180 bytes: a0 b0 c0 d0\n";
  reset_fixture();
  state.short_write = 1;
  check(invoke(2, "ipcc-map-state") == 0, "short writes were not completed");
  check(state.write_calls > 1, "short-write test did not exercise retry");
  check_success_calls();
  check_bytes(state.written, state.written_length, expected);

  reset_fixture();
  state.zero_write = 1;
  check(invoke(2, "ipcc-map-state") == 1, "zero write returned success");
  check(state.write_calls == 1 && state.written_length == 0 &&
            state.close_calls == 1 && state.unmap_calls == 2,
        "zero-write cleanup changed");
}

static void test_guard_failures(void) {
  reset_fixture();
  guard_fixture[0x218u] = 1u;
  check(invoke(2, "ipcc-map-state") == 1,
        "initial guard mismatch returned success");
  check(state.map_calls == 1 && state.unmap_calls == 1 &&
            state.close_calls == 1,
        "initial guard mismatch cleanup changed");
  check_bytes(state.written, state.written_length,
              "ERROR ipcc-root physical-offset 0x80008218 expected=0x81000000 actual=0x81000001\n");
  check_no_data_records();

  reset_fixture();
  state.mutate_guard_on_data_map = 1;
  check(invoke(2, "ipcc-map-state") == 1,
        "changed guard returned success");
  check(state.map_calls == 2 && state.unmap_calls == 2 &&
            state.close_calls == 1,
        "changed guard cleanup changed");
  check_bytes(state.written, state.written_length,
              "ERROR ipcc-root-changed physical-offset 0x80008218 expected=0x81000000 actual=0x81000001\n");
  check_no_data_records();
}

static void test_mapping_failures(void) {
  reset_fixture();
  state.fail_map_at = 1;
  check(invoke(2, "ipcc-map-state") == 1, "guard map failure returned success");
  check(state.map_calls == 1 && state.unmap_calls == 0 &&
            state.close_calls == 1,
        "guard map failure cleanup changed");
  check_no_data_records();

  reset_fixture();
  state.fail_map_at = 2;
  check(invoke(2, "ipcc-map-state") == 1, "data map failure returned success");
  check(state.map_calls == 2 && state.unmap_calls == 1 &&
            state.close_calls == 1,
        "data map failure did not unmap guard");
  check_no_data_records();
}

static void test_cleanup_failures(void) {
  reset_fixture();
  state.fail_unmap_at = 1;
  check(invoke(2, "ipcc-map-state") == 1, "data unmap failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "data unmap failure did not finish cleanup");
  check_no_data_records();

  reset_fixture();
  state.fail_unmap_at = 2;
  check(invoke(2, "ipcc-map-state") == 1, "guard unmap failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "guard unmap failure did not finish cleanup");
  check_no_data_records();

  reset_fixture();
  state.fail_close = 1;
  check(invoke(2, "ipcc-map-state") == 1, "close failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "close failure did not retain cleanup");
  check_bytes(state.written, state.written_length,
              "ERROR close /dev/mem errno=0x00000009\n");
  check_no_data_records();

  reset_fixture();
  state.fail_write = 1;
  check(invoke(2, "ipcc-map-state") == 1, "write failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1 &&
            state.write_calls == 1 && state.written_length == 0,
        "write failure cleanup changed");
}

static void test_argument_rejection(void) {
  reset_fixture();
  check(invoke(1, "ipcc-map-state") == 2,
        "missing mode argument was accepted");
  check(state.open_calls == 0, "missing mode opened /dev/mem");

  reset_fixture();
  check(invoke(3, "ipcc-map-state") == 2,
        "extra argument was accepted");
  check(state.open_calls == 0, "extra argument opened /dev/mem");

  reset_fixture();
  check(invoke(2, "ipcc-map-state-extra") == 2,
        "arbitrary mode argument was accepted");
  check(state.open_calls == 0, "arbitrary mode opened /dev/mem");
}

int main(void) {
  test_success();
  test_short_and_zero_writes();
  test_guard_failures();
  test_mapping_failures();
  test_cleanup_failures();
  test_argument_rejection();
  puts("ipcc-map-state host tests: PASS");
  return 0;
}
