#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(RAM_HELPER_HOST_TEST)
#error "svc6-dispatch-state tests require the explicit host syscall seam"
#endif
#if defined(__linux__) && \
    (defined(__arm__) || defined(__aarch64__) || defined(__ARM_EABI__))
#error "svc6-dispatch-state tests must never build as a Linux ARM syscall shim"
#endif

/*
 * The helper exposes one host-only observation point between its two
 * stability reads of the dispatch slot; the target build compiles it out.
 */
#ifndef RAM_HELPER_SLOT_HOOK
#define RAM_HELPER_SLOT_HOOK ram_helper_test_slot_writer
#endif
static void ram_helper_test_slot_writer(void);

#include "../ram_helper.c"

/* Fixed layout recovered from the pinned RTOS image. */
#define GUARD_PAGE_NUMBER 0x80000u /* physical 0x80000000 */
#define GUARD_OFFSET 0x138u         /* physical 0x80000138 */
#define EXPECTED_ROOT 0x81004000u
#define DATA_PAGE_NUMBER 0x81004u /* physical 0x81004000 */
#define DATA_OFFSET 0x18u         /* physical 0x81004018 */
#define REGISTERED_TARGET 0x80023fc0u

static unsigned char guard_fixture[PAGE_SIZE];
static unsigned char data_fixture[PAGE_SIZE];

struct shim_state {
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
  int mutate_root_on_data_map;
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

static struct shim_state state;
static int slot_writer_armed;

static void fail(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  exit(1);
}

static void check(int condition, const char *message) {
  if (!condition) fail(message);
}

static void check_text(const char *expected) {
  usize expected_length = strlen(expected);
  if (state.written_length != expected_length ||
      memcmp(state.written, expected, expected_length) != 0) {
    fprintf(stderr, "FAIL: output mismatch\nexpected:\n%sactual:\n%.*s",
            expected, (int)state.written_length, state.written);
    exit(1);
  }
}

static int has_text(const char *needle) {
  usize needle_length = strlen(needle);
  if (needle_length > state.written_length) return 0;
  for (usize start = 0; start + needle_length <= state.written_length; ++start)
    if (memcmp(state.written + start, needle, needle_length) == 0) return 1;
  return 0;
}

static void check_no_record(void) {
  check(!has_text("physical-offset 0x81004018 bytes:"),
        "failure emitted the dispatch slot record");
  check(!has_text(" bytes:"), "failure emitted a record line");
}

static void reset_fixture(void) {
  memset(&state, 0, sizeof(state));
  memset(guard_fixture, 0, sizeof(guard_fixture));
  memset(data_fixture, 0, sizeof(data_fixture));
  store_le32(guard_fixture + GUARD_OFFSET, EXPECTED_ROOT);
  store_le32(data_fixture + DATA_OFFSET, REGISTERED_TARGET);
  slot_writer_armed = 0;
}

static long fixture_pointer(const void *pointer) {
  return (long)(uintptr_t)pointer;
}

/*
 * Host-only observation point: models a concurrent writer that updates the
 * dispatch slot between the helper's two stability reads. It is compiled out
 * of the target build.
 */
static void ram_helper_test_slot_writer(void) {
  if (!slot_writer_armed) return;
  slot_writer_armed = 0;
  store_le32(data_fixture + DATA_OFFSET, 0x11223344u);
}

long ram_helper_test_syscall6(long nr, long a0, long a1, long a2,
                              long a3, long a4, long a5) {
  if (nr == SYS_open) {
    state.open_calls++;
    if (strcmp((const char *)(uintptr_t)a0, "/dev/mem") != 0 || a1 != 0 ||
        a2 != 0)
      state.protocol_error = 1;
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
    if (index == 0) {
      /* The guard page must be mapped first, alone. */
      if (a5 != (long)GUARD_PAGE_NUMBER) state.protocol_error = 1;
      return fixture_pointer(guard_fixture);
    }
    if (index == 1) {
      /*
       * The data page may only be requested after the guard word was read
       * and matched, so the root must still be the expected value here.
       */
      if (a5 != (long)DATA_PAGE_NUMBER) state.protocol_error = 1;
      if (load_le32(guard_fixture + GUARD_OFFSET) != EXPECTED_ROOT)
        state.protocol_error = 1;
      if (state.mutate_root_on_data_map)
        store_le32(guard_fixture + GUARD_OFFSET, 0x81004001u);
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
    if (a0 != 1 || a2 < 0 || (usize)a2 > sizeof(state.written))
      state.protocol_error = 1;
    /* A record may only reach stdout after both unmaps and the close. */
    if (state.open_calls > 0 && (state.unmap_calls != 2 || state.close_calls != 1))
      state.protocol_error = 1;
    if (state.fail_write) return -5;
    if (state.zero_write) return 0;
    usize count = (usize)a2;
    if (state.short_write && count > 5u) count = 5u;
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

static void check_success_contract(void) {
  check(!state.protocol_error, "syscall arguments violated the read-only contract");
  check(state.open_calls == 1, "success did not open /dev/mem exactly once");
  check(state.map_calls == 2, "success did not map exactly two pages");
  check(state.map_pages[0] == (long)GUARD_PAGE_NUMBER &&
            state.map_pages[1] == (long)DATA_PAGE_NUMBER,
        "mapped pages are not the fixed guard and data pages");
  check(state.map_lengths[0] == PAGE_SIZE && state.map_lengths[1] == PAGE_SIZE,
        "mapping length changed");
  check(state.map_protections[0] == PROT_READ &&
            state.map_protections[1] == PROT_READ,
        "mapping protection is not read-only");
  check(state.map_flags[0] == MAP_SHARED && state.map_flags[1] == MAP_SHARED,
        "mapping flags changed");
  check(state.map_fds[0] == 7 && state.map_fds[1] == 7, "mapping fd changed");
  check(state.unmap_calls == 2, "success did not unmap both pages");
  check(state.unmap_addresses[0] == fixture_pointer(data_fixture) &&
            state.unmap_addresses[1] == fixture_pointer(guard_fixture),
        "pages were not unmapped in reverse acquisition order");
  check(state.close_calls == 1, "success did not close exactly once");
  check(state.write_calls > 0, "success did not write output");
}

static void test_registered_target_record(void) {
  reset_fixture();
  check(invoke(2, "svc6-dispatch-state") == 0, "success returned an error");
  check_success_contract();
  check_text("physical-offset 0x81004018 bytes: c0 3f 02 80\n");
}

static void test_unexpected_stable_word_is_observed(void) {
  reset_fixture();
  store_le32(data_fixture + DATA_OFFSET, 0xdeadbeefu);
  check(invoke(2, "svc6-dispatch-state") == 0,
        "an unexpected but stable slot word was rejected");
  check_success_contract();
  check_text("physical-offset 0x81004018 bytes: ef be ad de\n");
}

static void test_wrong_initial_root_never_maps_data(void) {
  reset_fixture();
  store_le32(guard_fixture + GUARD_OFFSET, 0x81004001u);
  check(invoke(2, "svc6-dispatch-state") == 1,
        "a wrong initial root word returned success");
  check(state.map_calls == 1, "the data page was mapped after a root mismatch");
  check(state.unmap_calls == 1 && state.close_calls == 1,
        "root mismatch cleanup changed");
  check_text("ERROR svc6-root physical-offset 0x80000138 "
             "expected=0x81004000 actual=0x81004001\n");
  check_no_record();
}

static void test_changed_root_is_rejected(void) {
  reset_fixture();
  state.mutate_root_on_data_map = 1;
  check(invoke(2, "svc6-dispatch-state") == 1,
        "a changed root word returned success");
  check(state.map_calls == 2 && state.unmap_calls == 2 &&
            state.close_calls == 1,
        "changed root cleanup changed");
  check_text("ERROR svc6-root-changed physical-offset 0x80000138 "
             "expected=0x81004000 actual=0x81004001\n");
  check_no_record();
}

static void test_changed_slot_is_rejected(void) {
  reset_fixture();
  slot_writer_armed = 1;
  check(invoke(2, "svc6-dispatch-state") == 1,
        "a changed dispatch slot returned success");
  check(state.map_calls == 2 && state.unmap_calls == 2 &&
            state.close_calls == 1,
        "changed slot cleanup changed");
  check_text("ERROR svc6-slot-changed physical-offset 0x81004018 "
             "expected=0x80023fc0 actual=0x11223344\n");
  check_no_record();
}

static void test_map_failures(void) {
  reset_fixture();
  state.fail_map_at = 1;
  check(invoke(2, "svc6-dispatch-state") == 1,
        "guard map failure returned success");
  check(state.map_calls == 1 && state.unmap_calls == 0 &&
            state.close_calls == 1,
        "guard map failure cleanup changed");
  check_text("ERROR mmap2 physical-offset 0x80000000 errno=0x0000000c\n");
  check_no_record();

  reset_fixture();
  state.fail_map_at = 2;
  check(invoke(2, "svc6-dispatch-state") == 1,
        "data map failure returned success");
  check(state.map_calls == 2 && state.unmap_calls == 1 &&
            state.close_calls == 1,
        "data map failure did not unmap the guard page");
  check_text("ERROR mmap2 physical-offset 0x81004000 errno=0x0000000c\n");
  check_no_record();
}

static void test_unmap_failures(void) {
  reset_fixture();
  state.fail_unmap_at = 1;
  check(invoke(2, "svc6-dispatch-state") == 1,
        "data unmap failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "data unmap failure did not finish cleanup");
  check_no_record();

  reset_fixture();
  state.fail_unmap_at = 2;
  check(invoke(2, "svc6-dispatch-state") == 1,
        "guard unmap failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "guard unmap failure cleanup changed");
  check_no_record();
}

static void test_close_failure(void) {
  reset_fixture();
  state.fail_close = 1;
  check(invoke(2, "svc6-dispatch-state") == 1, "close failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "close failure did not retain prior cleanup");
  check_text("ERROR close /dev/mem errno=0x00000009\n");
  check_no_record();
}

static void test_write_failures(void) {
  reset_fixture();
  state.fail_write = 1;
  check(invoke(2, "svc6-dispatch-state") == 1, "write failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1 &&
            state.write_calls == 1 && state.written_length == 0,
        "write failure cleanup changed");
  check_no_record();

  reset_fixture();
  state.zero_write = 1;
  check(invoke(2, "svc6-dispatch-state") == 1,
        "zero-byte write returned success");
  check(state.write_calls == 1 && state.written_length == 0 &&
            state.unmap_calls == 2 && state.close_calls == 1,
        "zero-byte write cleanup changed");

  reset_fixture();
  state.short_write = 1;
  check(invoke(2, "svc6-dispatch-state") == 0,
        "short writes were not completed");
  check(state.write_calls > 1, "the short-write retry path was not exercised");
  check_success_contract();
  check_text("physical-offset 0x81004018 bytes: c0 3f 02 80\n");
}

static void test_argument_rejection(void) {
  reset_fixture();
  check(invoke(1, "svc6-dispatch-state") == 2,
        "a missing mode argument was accepted");
  check(state.open_calls == 0, "a missing mode opened /dev/mem");

  reset_fixture();
  check(invoke(3, "svc6-dispatch-state") == 2,
        "an extra argument was accepted");
  check(state.open_calls == 0, "an extra argument opened /dev/mem");

  reset_fixture();
  check(invoke(2, "svc6-dispatch") == 2, "an unknown mode was accepted");
  check(state.open_calls == 0, "an unknown mode opened /dev/mem");

  reset_fixture();
  check(invoke(2, "svc6-dispatch-state 0x81004018") == 2,
        "an arbitrary address argument was accepted");
  check(state.open_calls == 0, "an arbitrary address argument opened /dev/mem");
}

int main(void) {
  test_registered_target_record();
  test_unexpected_stable_word_is_observed();
  test_wrong_initial_root_never_maps_data();
  test_changed_root_is_rejected();
  test_changed_slot_is_rejected();
  test_map_failures();
  test_unmap_failures();
  test_close_failure();
  test_write_failures();
  test_argument_rejection();
  puts("svc6-dispatch-state host tests: PASS");
  return 0;
}
