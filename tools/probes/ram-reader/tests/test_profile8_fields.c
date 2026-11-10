#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(RAM_HELPER_HOST_TEST)
#error "profile8-fields tests require the explicit host syscall seam"
#endif
#if defined(__linux__) && \
    (defined(__arm__) || defined(__aarch64__) || defined(__ARM_EABI__))
#error "profile8-fields tests must never build as a Linux ARM syscall shim"
#endif

/*
 * The helper exposes one host-only observation point between its two
 * stability reads of the profile8 field pair; the target build compiles it out.
 */
#ifndef RAM_HELPER_PROFILE8_HOOK
#define RAM_HELPER_PROFILE8_HOOK ram_helper_test_profile8_writer
#endif
static void ram_helper_test_profile8_writer(void);

#include "../ram_helper.c"

/*
 * Fixed layout recovered from the pinned RTOS image (parent-reviewed Ghidra).
 * The guard word and both field words live in the one ordinary RAM page
 * 81882000, the same page the settings10 profile already uses.
 */
#define PROFILE8_PAGE_NUMBER 0x81882u /* physical 0x81882000 */
#define GUARD_OFFSET 0x57cu           /* physical 0x8188257c */
#define FIELD0_OFFSET 0x8a4u          /* physical 0x818828a4 */
#define FIELD1_OFFSET 0x8a8u          /* physical 0x818828a8 */
#define EXPECTED_GUARD 0x80670310u

/* Fixture values standing in for a matching read and for a stable surprise. */
#define KNOWN_FIELD0 0x12345678u
#define KNOWN_FIELD1 0x9abcdef0u
#define OTHER_FIELD0 0xdeadbeefu
#define OTHER_FIELD1 0xcafebabeu
#define CHANGED_FIELD0 0x11223344u
#define CHANGED_FIELD1 0x55667788u
#define CHANGED_GUARD 0x80670311u

/*
 * The fixture is exactly one page long, so a read past the fixed page trips the
 * sanitizer's global red zone instead of silently succeeding.
 */
_Alignas(4) static unsigned char page_fixture[PAGE_SIZE];

enum writer_action {
  WRITER_NONE,
  WRITER_FIELD0,
  WRITER_FIELD1,
  WRITER_GUARD
};

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
  long map_pages[1];
  long map_lengths[1];
  long map_protections[1];
  long map_flags[1];
  long map_fds[1];
  long unmap_addresses[1];
  long unmap_lengths[1];
  unsigned char written[1024];
  usize written_length;
};

static struct shim_state state;
static enum writer_action writer_action;
static int writer_calls;

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
  check(!has_text("physical-offset 0x818828a4 bytes:"),
        "failure emitted the profile8 record");
  check(!has_text(" bytes:"), "failure emitted a record line");
}

/* Does this stdout payload carry the successful profile8 record? */
static int write_carries_record(const char *data, usize count) {
  static const char record[] = "physical-offset 0x818828a4 bytes:";
  usize record_length = sizeof(record) - 1u;
  if (count < record_length) return 0;
  for (usize start = 0; start + record_length <= count; ++start)
    if (memcmp(data + start, record, record_length) == 0) return 1;
  return 0;
}

static void reset_fixture(void) {
  memset(&state, 0, sizeof(state));
  memset(page_fixture, 0, sizeof(page_fixture));
  store_le32(page_fixture + GUARD_OFFSET, EXPECTED_GUARD);
  store_le32(page_fixture + FIELD0_OFFSET, KNOWN_FIELD0);
  store_le32(page_fixture + FIELD1_OFFSET, KNOWN_FIELD1);
  writer_action = WRITER_NONE;
  writer_calls = 0;
}

static long fixture_pointer(const void *pointer) {
  return (long)(uintptr_t)pointer;
}

/*
 * Host-only observation point: models a concurrent writer that updates a
 * field word or the guard word between the helper's two stability reads. It
 * is compiled out of the target build.
 */
static void ram_helper_test_profile8_writer(void) {
  writer_calls++;
  if (writer_action == WRITER_FIELD0)
    store_le32(page_fixture + FIELD0_OFFSET, CHANGED_FIELD0);
  else if (writer_action == WRITER_FIELD1)
    store_le32(page_fixture + FIELD1_OFFSET, CHANGED_FIELD1);
  else if (writer_action == WRITER_GUARD)
    store_le32(page_fixture + GUARD_OFFSET, CHANGED_GUARD);
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
    if (index >= 1 || a0 != 0 || a1 != PAGE_SIZE || a2 != PROT_READ ||
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
      /* Only the one fixed ordinary RAM page may ever be mapped. */
      if (a5 != (long)PROFILE8_PAGE_NUMBER) state.protocol_error = 1;
      return fixture_pointer(page_fixture);
    }
    state.protocol_error = 1;
    return -22;
  }
  if (nr == SYS_munmap) {
    int index = state.unmap_calls++;
    if (index >= 1 || a1 != PAGE_SIZE) {
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
    /* A successful record may only reach stdout after the unmap and close. */
    if (write_carries_record((const char *)(uintptr_t)a1, (usize)a2) &&
        (state.unmap_calls != 1 || state.close_calls != 1))
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
  check(!state.protocol_error,
        "syscall arguments violated the read-only contract");
  check(state.open_calls == 1, "success did not open /dev/mem exactly once");
  check(state.map_calls == 1, "success did not map exactly one page");
  check(state.map_pages[0] == (long)PROFILE8_PAGE_NUMBER,
        "the mapped page is not the fixed profile8 page");
  check(state.map_lengths[0] == PAGE_SIZE, "mapping length changed");
  check(state.map_protections[0] == PROT_READ,
        "mapping protection is not read-only");
  check(state.map_flags[0] == MAP_SHARED, "mapping flags changed");
  check(state.map_fds[0] == 7, "mapping fd changed");
  check(state.unmap_calls == 1, "success did not unmap the page");
  check(state.unmap_addresses[0] == fixture_pointer(page_fixture),
        "a different page was unmapped");
  check(state.unmap_lengths[0] == PAGE_SIZE, "unmap length changed");
  check(state.close_calls == 1, "success did not close exactly once");
  check(state.write_calls > 0, "success did not write output");
  check(writer_calls == 1,
        "the host observation point did not run exactly once");
}

static void test_known_fields_record(void) {
  reset_fixture();
  check(invoke(2, "profile8-fields") == 0, "success returned an error");
  check_success_contract();
  check_text("physical-offset 0x818828a4 bytes: 78 56 34 12 f0 de bc 9a\n");
}

static void test_unexpected_stable_fields_are_observed(void) {
  reset_fixture();
  store_le32(page_fixture + FIELD0_OFFSET, OTHER_FIELD0);
  store_le32(page_fixture + FIELD1_OFFSET, OTHER_FIELD1);
  check(invoke(2, "profile8-fields") == 0,
        "an unexpected but stable field pair was rejected");
  check_success_contract();
  check_text("physical-offset 0x818828a4 bytes: ef be ad de be ba fe ca\n");
}

static void test_wrong_initial_guard_reads_no_field(void) {
  reset_fixture();
  store_le32(page_fixture + GUARD_OFFSET, CHANGED_GUARD);
  check(invoke(2, "profile8-fields") == 1,
        "a wrong initial guard word returned success");
  check(state.map_calls == 1 && state.unmap_calls == 1 &&
            state.close_calls == 1,
        "wrong initial guard cleanup changed");
  check(writer_calls == 0,
        "the data fields were read before the guard matched");
  check_text("ERROR profile8-guard physical-offset 0x8188257c "
             "expected=0x80670310 actual=0x80670311\n");
  check_no_record();
}

static void test_changed_guard_is_rejected(void) {
  reset_fixture();
  writer_action = WRITER_GUARD;
  check(invoke(2, "profile8-fields") == 1,
        "a changed guard word returned success");
  check(state.map_calls == 1 && state.unmap_calls == 1 &&
            state.close_calls == 1,
        "changed guard cleanup changed");
  check(writer_calls == 1, "the observation point did not run once");
  check_text("ERROR profile8-guard-changed physical-offset 0x8188257c "
             "expected=0x80670310 actual=0x80670311\n");
  check_no_record();
}

static void test_changed_field0_is_rejected(void) {
  reset_fixture();
  writer_action = WRITER_FIELD0;
  check(invoke(2, "profile8-fields") == 1,
        "a changed first field word returned success");
  check(state.map_calls == 1 && state.unmap_calls == 1 &&
            state.close_calls == 1,
        "changed first field cleanup changed");
  check(writer_calls == 1, "the observation point did not run once");
  check_text("ERROR profile8-field0-changed physical-offset 0x818828a4 "
             "expected=0x12345678 actual=0x11223344\n");
  check_no_record();
}

static void test_changed_field1_is_rejected(void) {
  reset_fixture();
  writer_action = WRITER_FIELD1;
  check(invoke(2, "profile8-fields") == 1,
        "a changed second field word returned success");
  check(state.map_calls == 1 && state.unmap_calls == 1 &&
            state.close_calls == 1,
        "changed second field cleanup changed");
  check(writer_calls == 1, "the observation point did not run once");
  check_text("ERROR profile8-field1-changed physical-offset 0x818828a8 "
             "expected=0x9abcdef0 actual=0x55667788\n");
  check_no_record();
}

static void test_open_failure(void) {
  reset_fixture();
  state.fail_open = 1;
  check(invoke(2, "profile8-fields") == 1, "open failure returned success");
  check(state.map_calls == 0 && state.unmap_calls == 0 &&
            state.close_calls == 0,
        "open failure touched the device after the failure");
  check(writer_calls == 0, "open failure still read the fields");
  check_text("ERROR open /dev/mem physical-offset 0x00000000 errno=0x00000005\n");
  check_no_record();
}

static void test_map_failure(void) {
  reset_fixture();
  state.fail_map_at = 1;
  check(invoke(2, "profile8-fields") == 1, "map failure returned success");
  check(state.map_calls == 1 && state.unmap_calls == 0 &&
            state.close_calls == 1,
        "map failure cleanup changed");
  check(writer_calls == 0, "map failure still read the fields");
  check_text("ERROR mmap2 physical-offset 0x81882000 errno=0x0000000c\n");
  check_no_record();
}

static void test_unmap_failure(void) {
  reset_fixture();
  state.fail_unmap_at = 1;
  check(invoke(2, "profile8-fields") == 1, "unmap failure returned success");
  check(state.unmap_calls == 1 && state.close_calls == 1,
        "unmap failure cleanup changed");
  check_no_record();
  check_text("ERROR munmap physical-offset 0x81882000 errno=0x00000005\n");
}

static void test_close_failure(void) {
  reset_fixture();
  state.fail_close = 1;
  check(invoke(2, "profile8-fields") == 1, "close failure returned success");
  check(state.unmap_calls == 1 && state.close_calls == 1,
        "close failure cleanup changed");
  check_text("ERROR close /dev/mem errno=0x00000009\n");
  check_no_record();
}

static void test_write_failures(void) {
  reset_fixture();
  state.fail_write = 1;
  check(invoke(2, "profile8-fields") == 1, "write failure returned success");
  check(state.unmap_calls == 1 && state.close_calls == 1 &&
            state.write_calls == 1 && state.written_length == 0,
        "write failure cleanup changed");
  check_no_record();

  reset_fixture();
  state.zero_write = 1;
  check(invoke(2, "profile8-fields") == 1, "zero-byte write returned success");
  check(state.write_calls == 1 && state.written_length == 0 &&
            state.unmap_calls == 1 && state.close_calls == 1,
        "zero-byte write cleanup changed");
  check_no_record();

  reset_fixture();
  state.short_write = 1;
  check(invoke(2, "profile8-fields") == 0,
        "short writes were not completed");
  check(state.write_calls > 1, "the short-write retry path was not exercised");
  check_success_contract();
  check_text("physical-offset 0x818828a4 bytes: 78 56 34 12 f0 de bc 9a\n");
}

static void test_argument_rejection(void) {
  reset_fixture();
  check(invoke(1, "profile8-fields") == 2,
        "a missing mode argument was accepted");
  check(state.open_calls == 0, "a missing mode opened /dev/mem");

  reset_fixture();
  check(invoke(3, "profile8-fields") == 2,
        "an extra argument was accepted");
  check(state.open_calls == 0, "an extra argument opened /dev/mem");

  reset_fixture();
  check(invoke(2, "profile8-field") == 2, "an unknown mode was accepted");
  check(state.open_calls == 0, "an unknown mode opened /dev/mem");

  reset_fixture();
  check(invoke(2, "profile8") == 2, "an unknown mode was accepted");
  check(state.open_calls == 0, "an unknown mode opened /dev/mem");

  reset_fixture();
  check(invoke(2, "profile8-fields 0x818828a4") == 2,
        "an arbitrary address argument was accepted");
  check(state.open_calls == 0, "an arbitrary address argument opened /dev/mem");

  reset_fixture();
  check(invoke(2, "profile8-fields 8") == 2,
        "an arbitrary length argument was accepted");
  check(state.open_calls == 0, "an arbitrary length argument opened /dev/mem");
}

int main(void) {
  test_known_fields_record();
  test_unexpected_stable_fields_are_observed();
  test_wrong_initial_guard_reads_no_field();
  test_changed_guard_is_rejected();
  test_changed_field0_is_rejected();
  test_changed_field1_is_rejected();
  test_open_failure();
  test_map_failure();
  test_unmap_failure();
  test_close_failure();
  test_write_failures();
  test_argument_rejection();
  puts("profile8-fields host tests: PASS");
  return 0;
}
