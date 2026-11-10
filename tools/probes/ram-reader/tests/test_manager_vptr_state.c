#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(RAM_HELPER_HOST_TEST)
#error "manager-vptr-state tests require the explicit host syscall seam"
#endif
#if defined(__linux__) && \
    (defined(__arm__) || defined(__aarch64__) || defined(__ARM_EABI__))
#error "manager-vptr-state tests must never build as a Linux ARM syscall shim"
#endif

/*
 * The helper exposes one host-only observation point between its two
 * stability reads of the manager vptr word; the target build compiles it out.
 */
#ifndef RAM_HELPER_VPTR_HOOK
#define RAM_HELPER_VPTR_HOOK ram_helper_test_vptr_writer
#endif
static void ram_helper_test_vptr_writer(void);

#include "../ram_helper.c"

/* Fixed layout recovered from the pinned RTOS image. */
#define GUARD_PAGE_NUMBER 0x81c0du /* physical 0x81c0d000 */
#define MANAGER_CELL_OFFSET 0x7e0u  /* physical 0x81c0d7e0 */
#define FACTORY_VPTR_OFFSET 0x7bcu  /* physical 0x81c0d7bc */
#define EXPECTED_MANAGER 0x81cbb000u
#define EXPECTED_FACTORY_VPTR 0x80626500u
#define DATA_PAGE_NUMBER 0x81cbbu /* physical 0x81cbb000 */
#define DATA_OFFSET 0x0u           /* physical 0x81cbb000 */

_Alignas(4) static unsigned char guard_fixture[PAGE_SIZE];
_Alignas(4) static unsigned char data_fixture[PAGE_SIZE];

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
  int mutate_manager_cell_on_data_map;
  int mutate_factory_vptr_on_data_map;
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
static int vptr_writer_armed;

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
  check(!has_text("physical-offset 0x81cbb000 bytes:"),
        "failure emitted the manager vptr record");
  check(!has_text(" bytes:"), "failure emitted a record line");
}

static void reset_fixture(void) {
  memset(&state, 0, sizeof(state));
  memset(guard_fixture, 0, sizeof(guard_fixture));
  memset(data_fixture, 0, sizeof(data_fixture));
  store_le32(guard_fixture + MANAGER_CELL_OFFSET, EXPECTED_MANAGER);
  store_le32(guard_fixture + FACTORY_VPTR_OFFSET, EXPECTED_FACTORY_VPTR);
  store_le32(data_fixture + DATA_OFFSET, EXPECTED_FACTORY_VPTR);
  vptr_writer_armed = 0;
}

static long fixture_pointer(const void *pointer) {
  return (long)(uintptr_t)pointer;
}

/*
 * Host-only observation point: models a concurrent writer that updates the
 * manager vptr word between the helper's two stability reads. It is compiled
 * out of the target build.
 */
static void ram_helper_test_vptr_writer(void) {
  if (!vptr_writer_armed) return;
  vptr_writer_armed = 0;
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
       * The data page may only be requested after both guard words were read
       * and matched, so both guards must still hold here.
       */
      if (a5 != (long)DATA_PAGE_NUMBER) state.protocol_error = 1;
      if (load_le32(guard_fixture + MANAGER_CELL_OFFSET) != EXPECTED_MANAGER)
        state.protocol_error = 1;
      if (load_le32(guard_fixture + FACTORY_VPTR_OFFSET) !=
          EXPECTED_FACTORY_VPTR)
        state.protocol_error = 1;
      if (state.mutate_manager_cell_on_data_map)
        store_le32(guard_fixture + MANAGER_CELL_OFFSET, 0x81cbb001u);
      if (state.mutate_factory_vptr_on_data_map)
        store_le32(guard_fixture + FACTORY_VPTR_OFFSET, 0x80626501u);
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

static void test_manager_vptr_record(void) {
  reset_fixture();
  check(invoke(2, "manager-vptr-state") == 0, "success returned an error");
  check_success_contract();
  check_text("physical-offset 0x81cbb000 bytes: 00 65 62 80\n");
}

static void test_unexpected_stable_vptr_is_observed(void) {
  reset_fixture();
  store_le32(data_fixture + DATA_OFFSET, 0xdeadbeefu);
  check(invoke(2, "manager-vptr-state") == 0,
        "an unexpected but stable manager vptr word was rejected");
  check_success_contract();
  check_text("physical-offset 0x81cbb000 bytes: ef be ad de\n");
}

static void test_wrong_manager_cell_never_maps_data(void) {
  reset_fixture();
  store_le32(guard_fixture + MANAGER_CELL_OFFSET, 0x81cbb001u);
  check(invoke(2, "manager-vptr-state") == 1,
        "a wrong manager cell returned success");
  check(state.map_calls == 1, "the data page was mapped after a manager cell mismatch");
  check(state.unmap_calls == 1 && state.close_calls == 1,
        "manager cell mismatch cleanup changed");
  check_text("ERROR manager-cell physical-offset 0x81c0d7e0 "
             "expected=0x81cbb000 actual=0x81cbb001\n");
  check_no_record();
}

static void test_wrong_factory_vptr_never_maps_data(void) {
  reset_fixture();
  store_le32(guard_fixture + FACTORY_VPTR_OFFSET, 0x80626501u);
  check(invoke(2, "manager-vptr-state") == 1,
        "a wrong factory vptr returned success");
  check(state.map_calls == 1, "the data page was mapped after a factory vptr mismatch");
  check(state.unmap_calls == 1 && state.close_calls == 1,
        "factory vptr mismatch cleanup changed");
  check_text("ERROR factory-vptr physical-offset 0x81c0d7bc "
             "expected=0x80626500 actual=0x80626501\n");
  check_no_record();
}

static void test_changed_manager_cell_is_rejected(void) {
  reset_fixture();
  state.mutate_manager_cell_on_data_map = 1;
  check(invoke(2, "manager-vptr-state") == 1,
        "a changed manager cell returned success");
  check(state.map_calls == 2 && state.unmap_calls == 2 &&
            state.close_calls == 1,
        "changed manager cell cleanup changed");
  check_text("ERROR manager-cell-changed physical-offset 0x81c0d7e0 "
             "expected=0x81cbb000 actual=0x81cbb001\n");
  check_no_record();
}

static void test_changed_factory_vptr_is_rejected(void) {
  reset_fixture();
  state.mutate_factory_vptr_on_data_map = 1;
  check(invoke(2, "manager-vptr-state") == 1,
        "a changed factory vptr returned success");
  check(state.map_calls == 2 && state.unmap_calls == 2 &&
            state.close_calls == 1,
        "changed factory vptr cleanup changed");
  check_text("ERROR factory-vptr-changed physical-offset 0x81c0d7bc "
             "expected=0x80626500 actual=0x80626501\n");
  check_no_record();
}

static void test_changed_vptr_is_rejected(void) {
  reset_fixture();
  vptr_writer_armed = 1;
  check(invoke(2, "manager-vptr-state") == 1,
        "a changed manager vptr word returned success");
  check(state.map_calls == 2 && state.unmap_calls == 2 &&
            state.close_calls == 1,
        "changed manager vptr cleanup changed");
  check_text("ERROR manager-vptr-changed physical-offset 0x81cbb000 "
             "expected=0x80626500 actual=0x11223344\n");
  check_no_record();
}

static void test_map_failures(void) {
  reset_fixture();
  state.fail_map_at = 1;
  check(invoke(2, "manager-vptr-state") == 1,
        "guard map failure returned success");
  check(state.map_calls == 1 && state.unmap_calls == 0 &&
            state.close_calls == 1,
        "guard map failure cleanup changed");
  check_text("ERROR mmap2 physical-offset 0x81c0d000 errno=0x0000000c\n");
  check_no_record();

  reset_fixture();
  state.fail_map_at = 2;
  check(invoke(2, "manager-vptr-state") == 1,
        "data map failure returned success");
  check(state.map_calls == 2 && state.unmap_calls == 1 &&
            state.close_calls == 1,
        "data map failure did not unmap the guard page");
  check_text("ERROR mmap2 physical-offset 0x81cbb000 errno=0x0000000c\n");
  check_no_record();
}

static void test_unmap_failures(void) {
  reset_fixture();
  state.fail_unmap_at = 1;
  check(invoke(2, "manager-vptr-state") == 1,
        "data unmap failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "data unmap failure did not finish cleanup");
  check_no_record();

  reset_fixture();
  state.fail_unmap_at = 2;
  check(invoke(2, "manager-vptr-state") == 1,
        "guard unmap failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "guard unmap failure cleanup changed");
  check_no_record();
}

static void test_close_failure(void) {
  reset_fixture();
  state.fail_close = 1;
  check(invoke(2, "manager-vptr-state") == 1, "close failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1,
        "close failure did not retain prior cleanup");
  check_text("ERROR close /dev/mem errno=0x00000009\n");
  check_no_record();
}

static void test_write_failures(void) {
  reset_fixture();
  state.fail_write = 1;
  check(invoke(2, "manager-vptr-state") == 1, "write failure returned success");
  check(state.unmap_calls == 2 && state.close_calls == 1 &&
            state.write_calls == 1 && state.written_length == 0,
        "write failure cleanup changed");
  check_no_record();

  reset_fixture();
  state.zero_write = 1;
  check(invoke(2, "manager-vptr-state") == 1,
        "zero-byte write returned success");
  check(state.write_calls == 1 && state.written_length == 0 &&
            state.unmap_calls == 2 && state.close_calls == 1,
        "zero-byte write cleanup changed");

  reset_fixture();
  state.short_write = 1;
  check(invoke(2, "manager-vptr-state") == 0,
        "short writes were not completed");
  check(state.write_calls > 1, "the short-write retry path was not exercised");
  check_success_contract();
  check_text("physical-offset 0x81cbb000 bytes: 00 65 62 80\n");
}

static void test_argument_rejection(void) {
  reset_fixture();
  check(invoke(1, "manager-vptr-state") == 2,
        "a missing mode argument was accepted");
  check(state.open_calls == 0, "a missing mode opened /dev/mem");

  reset_fixture();
  check(invoke(3, "manager-vptr-state") == 2,
        "an extra argument was accepted");
  check(state.open_calls == 0, "an extra argument opened /dev/mem");

  reset_fixture();
  check(invoke(2, "manager-vptr") == 2, "an unknown mode was accepted");
  check(state.open_calls == 0, "an unknown mode opened /dev/mem");

  reset_fixture();
  check(invoke(2, "manager-vptr-state 0x81cbb000") == 2,
        "an arbitrary address argument was accepted");
  check(state.open_calls == 0, "an arbitrary address argument opened /dev/mem");
}

int main(void) {
  test_manager_vptr_record();
  test_unexpected_stable_vptr_is_observed();
  test_wrong_manager_cell_never_maps_data();
  test_wrong_factory_vptr_never_maps_data();
  test_changed_manager_cell_is_rejected();
  test_changed_factory_vptr_is_rejected();
  test_changed_vptr_is_rejected();
  test_map_failures();
  test_unmap_failures();
  test_close_failure();
  test_write_failures();
  test_argument_rejection();
  puts("manager-vptr-state host tests: PASS");
  return 0;
}
