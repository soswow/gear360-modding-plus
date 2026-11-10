/*
 * Read-only capture timeline observer for the Gear 360 RTOS capture pool.
 *
 * Maps RTOS memory through /dev/mem with PROT_READ only and records, over a
 * bounded time window, changes in
 *   - the CCaptureMemoryManager per-block records (manager + 0xb0 + index * 16),
 *   - sparse pixel samples across the 0xA6000000 capture pool, and
 *   - the VD-falling callback counter at 0x81952d38.
 * It never writes device memory, never maps MMIO, and never follows a pointer
 * that was not supplied on the command line and re-checked against RAM.
 *
 * Modes:
 *   probe                         print the manager pointer and fixed guards
 *   timeline <manager> <ms> <us>  record changes for <ms> milliseconds,
 *                                 polling every <us> microseconds
 * Text output goes to stdout after all device mappings are released.
 */
#include <stdint.h>
#include <stddef.h>

typedef uint32_t u32;
typedef unsigned long usize;

#define PAGE 4096u
#define PROT_READ 1u
#define PROT_WRITE 2u
#define MAP_SHARED 1u
#define MAP_PRIVATE 2u
#define MAP_ANONYMOUS 0x20u
#define SYS_exit 1u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_munmap 91u
#define SYS_nanosleep 162u
#define SYS_mmap2 192u
#define SYS_clock_gettime 263u
#define CLOCK_MONOTONIC 1

#define MANAGER_CELL 0x81c0d7e0u
#define MANAGER_VTABLE 0x8062b790u
#define POOL_BASE 0xa6000000u
#define POOL_BYTES 0x1a000000u
#define BLOCK_BYTES 0x4000u
#define BLOCK_COUNT (POOL_BYTES / BLOCK_BYTES)
#define RECORD_OFFSET 0xb0u
#define RECORD_BYTES 16u
#define BASE_FIELD_OFFSET 0x680b0u
#define GLOBALS_PAGE 0x81952000u
#define VD_COUNTER_OFFSET 0xd38u
#define DUMP_FLAGS_OFFSET 0x1c4u
#define SAMPLE_STRIDE 0x40000u /* one sample point every 256 KiB */
#define SAMPLE_COUNT (POOL_BYTES / SAMPLE_STRIDE)
#define SAMPLE_OFFSET 0x2000u  /* inside the block, away from plane starts */
#define SAMPLE_WORDS 8u
#define OUTPUT_CAP (8u * 1024u * 1024u)

_Static_assert(RECORD_OFFSET + BLOCK_COUNT * RECORD_BYTES == BASE_FIELD_OFFSET,
               "record table ends at the base field");
_Static_assert(BLOCK_COUNT == 26624u, "pool block count");

struct timespec32 {
  long seconds;
  long nanoseconds;
};

static inline long syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
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

static int failed(long result) { return (usize)result >= (usize)-4095; }

void *memcpy(void *destination, const void *source, usize count) {
  unsigned char *d = (unsigned char *)destination;
  const unsigned char *s = (const unsigned char *)source;
  for (usize i = 0; i < count; ++i) {
    d[i] = s[i];
  }
  return destination;
}

void *memset(void *destination, int value, usize count) {
  unsigned char *d = (unsigned char *)destination;
  for (usize i = 0; i < count; ++i) {
    d[i] = (unsigned char)value;
  }
  return destination;
}

static char *output;
static usize output_length;
static int output_truncated;

static void put_char(char c) {
  if (output_length < OUTPUT_CAP) {
    output[output_length++] = c;
  } else {
    output_truncated = 1;
  }
}

static void put_text(const char *text) {
  while (*text) {
    put_char(*text++);
  }
}

static void put_hex(u32 value) {
  static const char digits[] = "0123456789abcdef";
  for (int shift = 28; shift >= 0; shift -= 4) {
    put_char(digits[(value >> shift) & 15u]);
  }
}

static void put_decimal(u32 value) {
  char buffer[24];
  int length = 0;
  do {
    buffer[length++] = (char)('0' + value % 10u);
    value /= 10u;
  } while (value && length < (int)sizeof(buffer));
  while (length) {
    put_char(buffer[--length]);
  }
}

static long write_all(int fd, const char *data, usize length) {
  while (length) {
    long written = syscall6(SYS_write, fd, (long)data, (long)length, 0, 0, 0);
    if (failed(written) || written == 0) {
      return failed(written) ? written : -5;
    }
    data += (usize)written;
    length -= (usize)written;
  }
  return 0;
}

static void fatal_text(const char *text) {
  usize length = 0;
  while (text[length]) {
    ++length;
  }
  (void)write_all(2, text, length);
}

static long map_readonly(int fd, u32 physical, u32 length) {
  return syscall6(SYS_mmap2, 0, (long)length, PROT_READ, MAP_SHARED, fd, (long)(physical >> 12));
}

static long map_anonymous(u32 length) {
  return syscall6(SYS_mmap2, 0, (long)length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                  -1, 0);
}

static u32 load(long mapping, u32 offset) {
  return *(volatile const u32 *)((usize)mapping + offset);
}

/* Wraps after about 71 minutes; only differences of nearby samples are used. */
static u32 now_microseconds(void) {
  struct timespec32 time = {0, 0};
  (void)syscall6(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&time, 0, 0, 0, 0);
  return (u32)time.seconds * 1000000u + (u32)time.nanoseconds / 1000u;
}

static void sleep_microseconds(u32 microseconds) {
  struct timespec32 request = {(long)(microseconds / 1000000u),
                               (long)((microseconds % 1000000u) * 1000u)};
  (void)syscall6(SYS_nanosleep, (long)&request, 0, 0, 0, 0, 0);
}

static int same(const char *a, const char *b) {
  usize i = 0;
  while (a[i] && b[i] && a[i] == b[i]) {
    ++i;
  }
  return a[i] == 0 && b[i] == 0;
}

static int parse_number(const char *text, u32 *value) {
  u32 result = 0;
  int base = 10;
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    text += 2;
  }
  if (!*text) {
    return 0;
  }
  for (; *text; ++text) {
    u32 digit;
    if (*text >= '0' && *text <= '9') {
      digit = (u32)(*text - '0');
    } else if (base == 16 && *text >= 'a' && *text <= 'f') {
      digit = (u32)(*text - 'a' + 10);
    } else if (base == 16 && *text >= 'A' && *text <= 'F') {
      digit = (u32)(*text - 'A' + 10);
    } else {
      return 0;
    }
    result = result * (u32)base + digit;
  }
  *value = result;
  return 1;
}

/* Sum of a few words at one sample point; enough to detect DMA overwrites. */
static u32 sample_checksum(long pool, u32 point) {
  u32 offset = point * SAMPLE_STRIDE + SAMPLE_OFFSET;
  u32 sum = 0x811c9dc5u;
  for (u32 i = 0; i < SAMPLE_WORDS; ++i) {
    sum = (sum ^ load(pool, offset + i * 4u)) * 0x01000193u;
  }
  return sum;
}

static int run_probe(int fd) {
  long globals = map_readonly(fd, GLOBALS_PAGE, PAGE);
  long cell_page = map_readonly(fd, MANAGER_CELL & ~(PAGE - 1u), PAGE);
  if (failed(globals) || failed(cell_page)) {
    fatal_text("ERROR mmap globals\n");
    return 1;
  }
  u32 manager = load(cell_page, MANAGER_CELL & (PAGE - 1u));
  put_text("MANAGER_CELL ");
  put_hex(manager);
  put_text("\nVD_COUNTER ");
  put_hex(load(globals, VD_COUNTER_OFFSET));
  put_text("\nDUMP_FLAGS");
  for (u32 i = 0; i < 4; ++i) {
    put_char(' ');
    put_hex(load(globals, DUMP_FLAGS_OFFSET + i * 4u));
  }
  put_char('\n');
  /* The manager pointer is only dereferenced if it lies in RTOS BSS. */
  if (manager >= 0x81448000u && manager < 0x81e00000u && (manager & 3u) == 0) {
    long head = map_readonly(fd, manager & ~(PAGE - 1u), PAGE);
    u32 field = manager + BASE_FIELD_OFFSET;
    long tail = map_readonly(fd, field & ~(PAGE - 1u), 2 * PAGE);
    if (!failed(head) && !failed(tail)) {
      u32 tail_offset = field & (PAGE - 1u);
      put_text("MANAGER_VTABLE ");
      put_hex(load(head, manager & (PAGE - 1u)));
      put_text("\nPOOL_BASE ");
      put_hex(load(tail, tail_offset));
      put_text("\nPOOL_SIZE ");
      put_hex(load(tail, tail_offset + 4u));
      put_text("\nLAST_INDEX ");
      put_hex(load(tail, tail_offset + 12u));
      put_char('\n');
      (void)syscall6(SYS_munmap, head, PAGE, 0, 0, 0, 0);
      (void)syscall6(SYS_munmap, tail, 2 * PAGE, 0, 0, 0, 0);
    } else {
      put_text("ERROR mmap manager\n");
    }
  } else {
    put_text("MANAGER_OUT_OF_RANGE\n");
  }
  (void)syscall6(SYS_munmap, globals, PAGE, 0, 0, 0, 0);
  (void)syscall6(SYS_munmap, cell_page, PAGE, 0, 0, 0, 0);
  return 0;
}

static void emit_record_run(u32 time, u32 first, u32 last, const u32 *old_record,
                            const u32 *new_record) {
  put_text("T ");
  put_decimal(time);
  put_text(" REC ");
  put_decimal(first);
  put_char(' ');
  put_decimal(last);
  for (u32 i = 0; i < 4; ++i) {
    put_char(' ');
    put_hex(old_record[i]);
  }
  put_text(" ->");
  for (u32 i = 0; i < 4; ++i) {
    put_char(' ');
    put_hex(new_record[i]);
  }
  put_char('\n');
}

static int records_equal(const u32 *a, const u32 *b) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int run_timeline(int fd, u32 manager, u32 duration_ms, u32 period_us) {
  if (manager < 0x81448000u || manager >= 0x81e00000u || (manager & 3u) != 0) {
    fatal_text("ERROR manager argument outside RTOS BSS\n");
    return 2;
  }
  u32 table_start = manager + RECORD_OFFSET;
  u32 map_start = table_start & ~(PAGE - 1u);
  /* Records end exactly at the base field; map through base, size and last index too. */
  u32 map_end = table_start + BLOCK_COUNT * RECORD_BYTES + 16u;
  u32 map_length = ((map_end - map_start) + PAGE - 1u) & ~(PAGE - 1u);
  long cell_page = map_readonly(fd, MANAGER_CELL & ~(PAGE - 1u), PAGE);
  long head = map_readonly(fd, manager & ~(PAGE - 1u), PAGE);
  long table = map_readonly(fd, map_start, map_length);
  long globals = map_readonly(fd, GLOBALS_PAGE, PAGE);
  long pool = map_readonly(fd, POOL_BASE, POOL_BYTES);
  if (failed(cell_page) || failed(head) || failed(table) || failed(globals) || failed(pool)) {
    fatal_text("ERROR mmap timeline regions\n");
    return 1;
  }
  u32 table_offset = table_start - map_start;
  u32 base_offset = manager + BASE_FIELD_OFFSET - map_start;

  /* Guards: the supplied manager must still be the live one, with the expected vtable and base. */
  u32 cell = load(cell_page, MANAGER_CELL & (PAGE - 1u));
  u32 vtable = load(head, manager & (PAGE - 1u));
  u32 base = load(table, base_offset);
  if (cell != manager || vtable != MANAGER_VTABLE || base != POOL_BASE) {
    put_text("GUARD_FAIL cell ");
    put_hex(cell);
    put_text(" vtable ");
    put_hex(vtable);
    put_text(" base ");
    put_hex(base);
    put_char('\n');
    return 3;
  }

  long previous_records = map_anonymous(BLOCK_COUNT * RECORD_BYTES);
  long previous_samples = map_anonymous(SAMPLE_COUNT * 4u);
  if (failed(previous_records) || failed(previous_samples)) {
    fatal_text("ERROR mmap snapshot\n");
    return 1;
  }
  u32 *records = (u32 *)(usize)previous_records;
  u32 *samples = (u32 *)(usize)previous_samples;

  u32 start = now_microseconds();
  for (u32 i = 0; i < BLOCK_COUNT * 4u; ++i) {
    records[i] = load(table, table_offset + i * 4u);
  }
  for (u32 point = 0; point < SAMPLE_COUNT; ++point) {
    samples[point] = sample_checksum(pool, point);
  }
  put_text("START ");
  put_decimal(start);
  put_text(" manager ");
  put_hex(manager);
  put_text(" last_index ");
  put_hex(load(table, base_offset + 12u));
  put_text(" vd ");
  put_hex(load(globals, VD_COUNTER_OFFSET));
  put_char('\n');

  /* Baseline: describe every run of identical records so the initial occupancy is known. */
  {
    u32 run_start = 0;
    for (u32 index = 1; index <= BLOCK_COUNT; ++index) {
      if (index == BLOCK_COUNT || !records_equal(&records[index * 4u], &records[run_start * 4u])) {
        put_text("BASE ");
        put_decimal(run_start);
        put_char(' ');
        put_decimal(index - 1u);
        for (u32 w = 0; w < 4; ++w) {
          put_char(' ');
          put_hex(records[run_start * 4u + w]);
        }
        put_char('\n');
        run_start = index;
      }
    }
  }

  u32 iterations = 0;
  u32 last_vd = load(globals, VD_COUNTER_OFFSET);
  u32 duration_us = duration_ms * 1000u;
  for (;;) {
    u32 time = now_microseconds();
    if (time - start >= duration_us || output_truncated) {
      break;
    }
    ++iterations;
    /* Record changes, coalesced into runs with identical old and new contents. */
    u32 run_first = 0, run_last = 0;
    int run_open = 0;
    u32 run_old[4], run_new[4];
    for (u32 index = 0; index < BLOCK_COUNT; ++index) {
      u32 current[4];
      for (u32 w = 0; w < 4; ++w) {
        current[w] = load(table, table_offset + (index * 4u + w) * 4u);
      }
      u32 *saved = &records[index * 4u];
      int changed = !records_equal(current, saved);
      if (changed && run_open && index == run_last + 1u && records_equal(saved, run_old) &&
          records_equal(current, run_new)) {
        run_last = index;
      } else {
        if (run_open) {
          emit_record_run(time - start, run_first, run_last, run_old, run_new);
          run_open = 0;
        }
        if (changed) {
          run_open = 1;
          run_first = run_last = index;
          for (u32 w = 0; w < 4; ++w) {
            run_old[w] = saved[w];
            run_new[w] = current[w];
          }
        }
      }
      if (changed) {
        for (u32 w = 0; w < 4; ++w) {
          saved[w] = current[w];
        }
      }
    }
    if (run_open) {
      emit_record_run(time - start, run_first, run_last, run_old, run_new);
    }
    for (u32 point = 0; point < SAMPLE_COUNT; ++point) {
      u32 sum = sample_checksum(pool, point);
      if (sum != samples[point]) {
        samples[point] = sum;
        put_text("T ");
        put_decimal(time - start);
        put_text(" PIX ");
        put_decimal(point);
        put_char(' ');
        put_hex(sum);
        put_char('\n');
      }
    }
    u32 vd = load(globals, VD_COUNTER_OFFSET);
    if ((iterations & 63u) == 0 && vd != last_vd) {
      put_text("T ");
      put_decimal(time - start);
      put_text(" VD ");
      put_hex(vd);
      put_char('\n');
      last_vd = vd;
    }
    if (period_us) {
      sleep_microseconds(period_us);
    }
  }
  u32 end = now_microseconds();
  u32 final_cell = load(cell_page, MANAGER_CELL & (PAGE - 1u));
  put_text("END ");
  put_decimal(end - start);
  put_text(" iterations ");
  put_decimal(iterations);
  put_text(" cell_after ");
  put_hex(final_cell);
  put_text(output_truncated ? " TRUNCATED\n" : "\n");

  (void)syscall6(SYS_munmap, pool, POOL_BYTES, 0, 0, 0, 0);
  (void)syscall6(SYS_munmap, table, map_length, 0, 0, 0, 0);
  (void)syscall6(SYS_munmap, head, PAGE, 0, 0, 0, 0);
  (void)syscall6(SYS_munmap, cell_page, PAGE, 0, 0, 0, 0);
  (void)syscall6(SYS_munmap, globals, PAGE, 0, 0, 0, 0);
  return final_cell == manager ? 0 : 4;
}

int c_entry(usize *initial_stack) {
  int argc = (int)initial_stack[0];
  char **argv = (char **)(initial_stack + 1);
  long buffer = map_anonymous(OUTPUT_CAP);
  if (failed(buffer)) {
    fatal_text("ERROR output buffer\n");
    return 1;
  }
  output = (char *)(usize)buffer;
  int probe = argc == 2 && same(argv[1], "probe");
  int timeline = argc == 5 && same(argv[1], "timeline");
  if (!probe && !timeline) {
    fatal_text("usage: capture-timeline probe | timeline <manager> <ms> <period-us>\n");
    return 2;
  }
  u32 manager = 0, duration_ms = 0, period_us = 0;
  if (timeline && (!parse_number(argv[2], &manager) || !parse_number(argv[3], &duration_ms) ||
                   !parse_number(argv[4], &period_us) || duration_ms > 120000u)) {
    fatal_text("ERROR bad timeline arguments (duration at most 120000 ms)\n");
    return 2;
  }
  long fd = syscall6(SYS_open, (long)"/dev/mem", 0, 0, 0, 0, 0);
  if (failed(fd)) {
    fatal_text("ERROR open /dev/mem\n");
    return 1;
  }
  int status = probe ? run_probe((int)fd) : run_timeline((int)fd, manager, duration_ms, period_us);
  (void)syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  /* Device memory is unmapped and the descriptor closed before any output is written. */
  (void)write_all(1, output, output_length);
  return status;
}