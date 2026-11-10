/*
 * Experimental target diagnostic imported from MiniMax4d2deb0.
 * Host argument/error paths and the ARM build have been validated. There is
 * no demonstrated end-to-end RAW capture from this program. See README.md.
 *
 * Acquire: category0x80/attribute0x018c with five u32 request words.
 * Reply: category0x81/attribute0x018c, five u32 words; matching size/bank
 * does not establish request association, ownership or permission to free.
 * Still: category0x83/attribute0x0201 with the reviewed48-byte body.
 * Encoded event: category0x81/attribute0x0201 names a context, not a proved
 * stable sensor descriptor or retained buffer. Passive observation is racy.
 * Release: category0x81/attribute0x018d has no remote acknowledgement.
 *
 * The capture-record interpretation, candidate region, sample heuristic,
 * setting effects, retries, target copy and release/error lifetime remain
 * experimental. A timeout can leave an unknown operation pending. No outage
 * is attributed to an allocation leak or mapping strategy by this source.
 */
#include <stdint.h>
#include <stddef.h>

#include "still_request.h"
#include "mcb_frame.h"

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t i32;
typedef int64_t i64;
typedef unsigned long usize;

#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_munmap 91u
#define SYS_ioctl 54u
#define SYS_nanosleep 162u
#define SYS_mmap2 192u
#define SYS_clock_gettime 263u
#define SYS_fork 2u
#define SYS_execve 11u
#define SYS_wait4 114u
#define SYS_fsync 118u
#define SYS_exit 1u
#define CLOCK_MONOTONIC 1
#define O_RDONLY 0u
#define O_WRONLY 1u
#define O_CREAT 0100u
#define O_TRUNC 01000u
#define O_RDWR 2u
#define PROT_READ 1u
#define MAP_SHARED 1u

#define SHARED_PHYS_BASE 0x02010000u
#define SHARED_MAP_BYTES 0x2000u

#define IPCC_WRITE_AVAILABLE_CMD 0xc0087403u
#define IPCC_WRITE_PKT_CMD 0xc0107407u

/* The bridge categories and attributes this program uses. */
#define CATEGORY_READ 0x0080u
#define CATEGORY_RESULT 0x0081u
#define CATEGORY_STILL 0x0083u
#define ATTRIBUTE_IMAGE_BUFFER 0x018cu
#define ATTRIBUTE_RELEASE 0x018du
#define ATTRIBUTE_STILL 0x0201u

/* The destination CPU of a frame: the RTOS.  This Linux side is CPU 3,
 * which is the route byte the driver stamps in front of the frame. */
#define DESTINATION_CPU 0u

#define RING_CAPACITY 395u
#define OUTPUT_CAP (4u * 1024u)
#define DEFAULT_TIMEOUT_MS 20000u
#define ACQUIRE_WAIT_MS 5000u
#define LEASE_WORDS 5u

/* The RTOS's runtime image region: the only range this repository has
 * already mapped on the camera. */
#define RTOS_IMAGE_FIRST 0x80000000u
#define RTOS_IMAGE_LAST 0x83000000u

/* The capture pool the RTOS grants from: the base and the size the RTOS's
 * own answer reports.  A granted address outside this range is refused
 * rather than released. */
#define POOL_FIRST 0xa0000000u
#define POOL_LAST 0xb8000000u

/* The offset of the plane word inside the capture object. */
#define PLANE_WORD_OFFSET 0x84u

/* The region the RTOS publishes its image descriptor into.  The capture
 * library maps physical 0x85400000 read-write; the descriptor itself is at
 * 0x85400000 + 0x1274. */
/* The RTOS's log ring: a static array of 0xa4-byte entries, the index at
 * 0x81474d6c and the formatted text at +0xc of each entry. */
#define LOG_RING_BASE 0x8144b96cu
#define LOG_RING_INDEX 0x81474d6cu
#define LOG_ENTRY_SIZE 0xa4u
#define LOG_TEXT_OFFSET 0x0cu
#define LOG_TEXT_SIZE 0x80u
#define LOG_ENTRIES_TO_CHECK 96u

/* The region the scan walks: the RTOS's own image, and the region it
 * shares with the Linux side, which is where its log entries may live. */
#define SEARCH_FIRST 0x80000000u
#define SEARCH_LAST 0x86000000u

#define DESCRIPTOR_FIRST 0x85400000u
#define DESCRIPTOR_LAST 0x85410000u
#define DESCRIPTOR_ADDRESS 0x85401274u

typedef struct {
  u32 write_cursor;
  u32 data;
  const char *name;
} ring_desc;

static const ring_desc RINGS[4] = {
    {0x4cu, 0xecu, "A"},
    {0x54u, 0x277u, "B"},
    {0x94u, 0xecfu, "C"},
    {0xa4u, 0x11e5u, "D"},
};
#define RING_COUNT 4

/*
 * The syscall layer.  On the camera this is the ARM supervisor call.  The
 * host test harness replaces it, so that the argument handling and the exit
 * paths are exercised against the real c_entry without a device.
 */
#ifdef IPCC_RAW_GRAB_HOST_TEST
extern long ipcc_raw_grab_host_syscall(long nr, long a0, long a1, long a2,
                                        long a3, long a4, long a5);
#define syscall6 ipcc_raw_grab_host_syscall
#else
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
#endif

static int failed(long result) { return (usize)result >= (usize)-4095; }

static char *output;
static usize output_length;

/*
 * The output buffer is mapped only after the arguments have been read, so a
 * message printed before that point has nowhere to go.  Those messages go
 * here instead and are written to the error stream line by line.  The
 * previous shape wrote them through a null pointer, which killed the run
 * before it could report anything.
 */
#define FALLBACK_CAP 1024u
static char fallback_output[FALLBACK_CAP];
static usize fallback_length;

/* When the run started, so that a run cannot go on for ever. */
static u32 run_started;

static u32 now_microseconds(void);

/* True once the run has been going for longer than the limit.  A program
 * that hangs on the camera keeps /dev/d5_ipcc open, and the stock
 * application cannot take its own pictures while that is true. */
static int past_deadline(u32 limit_seconds) {
  if (limit_seconds == 0u) {
    return 0;
  }
  return ((now_microseconds() - run_started) / 1000000u) > limit_seconds;
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

static void flush_output(void) {
  if (output_length != 0u) {
    (void)write_all(1, output, output_length);
    output_length = 0u;
  }
}

static void log_char(char c);

static void put_char(char c) {
  if (output == 0) {
    /* Nothing is mapped yet: collect the line and write it to the error
     * stream, so a bad argument is reported without a null write and
     * without any mapping or device I/O. */
    if (fallback_length + 1u >= FALLBACK_CAP) {
      (void)write_all(2, fallback_output, fallback_length);
      fallback_length = 0u;
    }
    fallback_output[fallback_length++] = c;
    if (c == '\n') {
      (void)write_all(2, fallback_output, fallback_length);
      fallback_length = 0u;
    }
    return;
  }
  if (output_length + 1u >= OUTPUT_CAP) {
    flush_output();
  }
  output[output_length++] = c;
  log_char(c);
}

/*
 * A copy of everything the program says, kept in a file on the card.
 *
 * The console output of a run disappears with the process, and a run that
 * takes the camera down takes its console with it: what is left is a camera
 * that has rebooted and no record of how far the program got.  A file on
 * the card survives the reboot, so the last line of the log says where the
 * program was when the camera went.
 */
static long log_fd = -1;
static char log_line[1024];
static usize log_line_length;

static void log_flush_line(void) {
  if (log_fd >= 0 && log_line_length != 0u) {
    (void)write_all((int)log_fd, log_line, log_line_length);
    /* The camera can reboot in the middle of a run, and the log is then the
     * only record of how far the program got, so it is on the disk before
     * the next line is written. */
    (void)syscall6(SYS_fsync, log_fd, 0, 0, 0, 0, 0);
    log_line_length = 0u;
  }
}

static void log_char(char c) {
  if (log_fd < 0) {
    return;
  }
  if (log_line_length + 1u >= sizeof(log_line)) {
    log_flush_line();
  }
  log_line[log_line_length++] = c;
  if (c == '\n') {
    log_flush_line();
  }
}

static void put_text(const char *text) {
  while (*text) {
    put_char(*text++);
  }
}

static void put_u64(u64 value) {
  char digits[21];
  int count = 0;
  do {
    digits[count++] = (char)('0' + value % 10u);
    value /= 10u;
  } while (value && count < 20);
  while (count) {
    put_char(digits[--count]);
  }
}

static void put_u32(u32 value) { put_u64((u64)value); }

static void put_i32(i32 value) {
  if (value < 0) {
    put_char('-');
    put_u64((u64)(-(i64)value));
  } else {
    put_u64((u64)value);
  }
}

static void put_hex_byte(u32 value) {
  static const char table[] = "0123456789abcdef";
  put_char(table[(value >> 4) & 0xfu]);
  put_char(table[value & 0xfu]);
}

static void put_hex(const u8 *bytes, u32 count) {
  u32 index;
  for (index = 0u; index < count; ++index) {
    put_hex_byte(bytes[index]);
  }
}

static void put_line(void) {
  put_char('\n');
  flush_output();
}

struct timespec32 {
  long seconds;
  long nanoseconds;
};

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

static int parse_number(const char *text, u64 *value) {
  u64 result = 0;
  int base = 10;
  if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    text += 2;
  }
  if (!*text) {
    return 0;
  }
  for (; *text; ++text) {
    u64 digit;
    if (*text >= '0' && *text <= '9') {
      digit = (u64)(*text - '0');
    } else if (base == 16 && *text >= 'a' && *text <= 'f') {
      digit = (u64)(*text - 'a' + 10);
    } else if (base == 16 && *text >= 'A' && *text <= 'F') {
      digit = (u64)(*text - 'A' + 10);
    } else {
      return 0;
    }
    result = result * (u64)base + digit;
  }
  *value = result;
  return 1;
}

/*
 * The two command words the reviewed still-request component names: the
 * legacy plain path and the shared path of the 22 October 2026 run.  The
 * encoder is wire construction and does not validate a command against the
 * firmware, so this is a local policy: a word outside this set has no
 * reviewed body layout and is refused before the request is built.
 */
static int still_command_is_reviewed(u32 command)
{
  return command == STILL_REQUEST_COMMAND_PLAIN_PATH ||
         command == STILL_REQUEST_COMMAND_SHARED_PATH;
}

static int same(const char *a, const char *b) {
  usize i = 0;
  while (a[i] && b[i] && a[i] == b[i]) {
    ++i;
  }
  return a[i] == 0 && b[i] == 0;
}

void *memmove(void *destination, const void *source, size_t length);
void *memcpy(void *destination, const void *source, size_t length);

void *memmove(void *destination, const void *source, size_t length)
{
  unsigned char *to = (unsigned char *)destination;
  const unsigned char *from = (const unsigned char *)source;
  size_t index;

  if (to == from || length == 0u) {
    return destination;
  }
  if (to < from) {
    for (index = 0; index < length; ++index) {
      to[index] = from[index];
    }
  } else {
    for (index = length; index > 0u; --index) {
      to[index - 1u] = from[index - 1u];
    }
  }
  return destination;
}

void *memcpy(void *destination, const void *source, size_t length)
{
  return memmove(destination, source, length);
}

/* ------------------------------------------------------------------ */
/* The ring watcher: read-only, and it never consumes anything.        */
/* ------------------------------------------------------------------ */

/*
 * One record in a ring is
 *
 *   ff 7f | inner length u16 LE | the inner frame
 *
 * and the inner frame is the one the frame helper describes:
 *
 *   00 03 | category u16 LE | attribute u16 LE | body length u16 LE |
 *   body | two trailer bytes
 *
 * A record can straddle the end of a ring, so the bytes of a record are
 * never read as one run of memory.  Every byte is taken with modular
 * indexing inside that ring's own 395 bytes, and every frame is copied
 * into this program's own scratch before a callback is given it.  A
 * pointer into the ring is not handed out: the body of a record that
 * wraps is not contiguous in memory, and reading it as if it were reads
 * the ring that follows instead of the record's own bytes.
 */
#define RECORD_MARKER0 ((u8)0xffu)
#define RECORD_MARKER1 ((u8)0x7fu)
#define RECORD_HEADER 4u

typedef struct {
  u32 last_write[RING_COUNT];
  const unsigned char *base;
  /* Counts observed parse outcomes. Full laps and racing overwrites are
   * undetectable here; these counters are not complete loss accounting. */
  u32 records_ok;      /* frames delivered and accepted */
  u32 records_other;   /* valid frames for another attribute */
  u32 records_bad;     /* malformed records or frames */
  u32 frames_rejected; /* frames the callback refused */
  u32 bytes_discarded; /* bytes dropped with a malformed record */
  u32 cursor_errors;   /* cursor values outside the ring refused */
  u32 incomplete;      /* records not yet written out in full */
} watcher;

static void watcher_init(watcher *w, const unsigned char *base)
{
  int ring;
  w->base = base;
  w->records_ok = 0u;
  w->records_other = 0u;
  w->records_bad = 0u;
  w->frames_rejected = 0u;
  w->bytes_discarded = 0u;
  w->cursor_errors = 0u;
  w->incomplete = 0u;
  for (ring = 0; ring < RING_COUNT; ++ring) {
    w->last_write[ring] =
        *(const volatile u32 *)(const void *)(base + RINGS[ring].write_cursor);
  }
}

/* One byte of a ring.  The index is taken modulo the ring's size, so
 * the read never leaves that ring's own bytes. */
static u8 ring_byte(const watcher *w, int ring, u32 index)
{
  return w->base[RINGS[ring].data + (index % RING_CAPACITY)];
}

/* Copies COUNT bytes out of a ring into BUFFER, taking the index modulo
 * the ring's size again where the record wraps. */
static void ring_take(const watcher *w, int ring, u32 index, u8 *buffer, u32 count)
{
  u32 start = index % RING_CAPACITY;
  u32 first = RING_CAPACITY - start;
  if (first > count) {
    first = count;
  }
  (void)memcpy(buffer, w->base + RINGS[ring].data + start, first);
  if (count > first) {
    (void)memcpy(buffer + first, w->base + RINGS[ring].data, count - first);
  }
}

/*
 * Walks every record published since the last call.  A record is
 * [ff 7f][length u16][frame]. Several complete records within an observed
 * delta can be decoded, but racing overwrites and full laps remain unknown.
 *
 * A frame is given to the callback only when the whole record is
 * present, the record carries the outer marker, the inner frame is a
 * complete frame for the route 00 03, and its category and attribute
 * are the ones asked for.  The route prefix and the declared body
 * length are checked by the frame helper, which refuses a prefix that
 * is not 00 03 and a body length that does not match the frame.  A
 * frame the callback refuses is counted as refused and is not counted
 * as seen.
 */
static void watcher_poll(watcher *w, u32 category, u32 attribute,
                         int (*on_frame)(const u8 *, u32),
                         u32 *seen)
{
  u8 storage[MCB_FRAME_MAX_SIZE];
  int ring;

  for (ring = 0; ring < RING_COUNT; ++ring) {
    u32 write_cursor =
        *(const volatile u32 *)(const void *)(w->base + RINGS[ring].write_cursor);
    u32 index;
    u32 remaining;

    if (write_cursor >= RING_CAPACITY) {
      /* A cursor outside the ring cannot be turned into a ring
       * position.  The ring is left alone and the refusal is counted,
       * rather than read from a position that does not exist. */
      w->cursor_errors += 1u;
      put_text("RING ");
      put_text(RINGS[ring].name);
      put_text(" cursor out of range: ");
      put_u32(write_cursor);
      put_line();
      continue;
    }
    if (w->last_write[ring] >= RING_CAPACITY) {
      /* A bad initial sample is not a cursor baseline. Establish a new
       * baseline without deriving events from the invalid value. */
      w->cursor_errors += 1u;
      w->last_write[ring] = write_cursor;
      continue;
    }
    if (write_cursor == w->last_write[ring]) {
      continue;
    }
    remaining = (write_cursor + RING_CAPACITY - w->last_write[ring]) % RING_CAPACITY;
    index = w->last_write[ring];
    while (remaining >= RECORD_HEADER) {
      u8 marker0 = ring_byte(w, ring, index);
      u8 marker1 = ring_byte(w, ring, index + 1u);
      u32 inner_len = (u32)ring_byte(w, ring, index + 2u) |
                      ((u32)ring_byte(w, ring, index + 3u) << 8);
      u32 total;

      if (marker0 != RECORD_MARKER0 || marker1 != RECORD_MARKER1) {
        /* The record's own length is unknown, so the rest of the ring
         * cannot be walked.  The bytes are counted as lost, the loss
         * is reported, and the ring is picked up again at the
         * producer's cursor. */
        w->records_bad += 1u;
        w->bytes_discarded += remaining;
        put_text("RING ");
        put_text(RINGS[ring].name);
        put_text(" bad marker at ");
        put_u32(index);
        put_text(", ");
        put_u32(remaining);
        put_text(" bytes discarded\n");
        index = write_cursor;
        remaining = 0u;
        break;
      }
      if (inner_len < (u32)MCB_FRAME_MIN_SIZE || inner_len > (u32)MCB_FRAME_MAX_SIZE) {
        /* The record cannot hold a frame, so its length is not
         * trustworthy either.  The rest of the ring is counted as
         * lost and reported. */
        w->records_bad += 1u;
        w->bytes_discarded += remaining;
        put_text("RING ");
        put_text(RINGS[ring].name);
        put_text(" bad length at ");
        put_u32(index);
        put_text(", ");
        put_u32(remaining);
        put_text(" bytes discarded\n");
        index = write_cursor;
        remaining = 0u;
        break;
      }
      total = RECORD_HEADER + inner_len;
      if (total > remaining) {
        /* The producer has not written the whole record out yet.  The
         * cursor is left at the record's first byte, so the record is
         * read again, whole, on the next poll. */
        break;
      }
      ring_take(w, ring, index + RECORD_HEADER, storage, inner_len);
      {
        mcb_frame_message message;
        mcb_frame_status status =
            mcb_frame_copy(storage, (size_t)inner_len, storage, sizeof(storage),
                           &message);
        if (status != MCB_FRAME_OK) {
          /* The record carries something that is not a frame for this
           * route: a bad prefix, or a body length that does not match
           * the record.  The record's own length is known, so the walk
           * goes on and nothing is lost. */
          w->records_bad += 1u;
        } else if (message.category != (uint16_t)category ||
                   message.attribute != (uint16_t)attribute) {
          /* A valid frame for somebody else. */
          w->records_other += 1u;
        } else if (on_frame(storage, inner_len) != 0) {
          w->records_ok += 1u;
          if (seen != 0) {
            *seen += 1u;
          }
        } else {
          w->frames_rejected += 1u;
        }
      }
      index = (index + total) % RING_CAPACITY;
      remaining -= total;
    }
    if (remaining != 0u) {
      /* Fewer bytes than a record header, or a record that is not
       * written out in full.  The cursor stays at the first byte of
       * what is left, so nothing is dropped and the next poll reads
       * it again. */
      w->incomplete += 1u;
    }
    w->last_write[ring] = index;
  }
}

/* What the watcher did with what it found, so that a run says what it
 * did not get. */
static void watcher_report(const watcher *w)
{
  put_text("RING records=");
  put_u32(w->records_ok);
  put_text(" other=");
  put_u32(w->records_other);
  put_text(" bad=");
  put_u32(w->records_bad);
  put_text(" rejected=");
  put_u32(w->frames_rejected);
  put_text(" discarded=");
  put_u32(w->bytes_discarded);
  put_text(" cursor_errors=");
  put_u32(w->cursor_errors);
  put_text(" incomplete=");
  put_u32(w->incomplete);
  put_line();
}

/* ------------------------------------------------------------------ */
/* The lease state we are watching for.                                */
/* ------------------------------------------------------------------ */

static u32 requested_size;
static u32 requested_bank;
static u32 granted_address;
static u32 lease_handle;
static u32 seen_replies;

/* The addresses the RTOS granted in replies that are not ours.  A grant
 * that matches one of these belongs to whoever asked for it, and must never
 * be released by us. */
#define OTHER_ADDRESSES 16u
static u32 other_addresses[OTHER_ADDRESSES];
static u32 other_count;
static u32 seen_our_replies;

/* The addresses that appeared in the capture object we read.  An address
 * that shows up there belongs to the capture, not to us, and must not be
 * released by us. */
#define CAPTURE_ADDRESSES 8u
static u32 capture_addresses[CAPTURE_ADDRESSES];
static u32 capture_count;

static void remember_capture_address(u32 address)
{
  u32 index;
  if (address == 0u) {
    return;
  }
  for (index = 0u; index < capture_count; ++index) {
    if (capture_addresses[index] == address) {
      return;
    }
  }
  if (capture_count < CAPTURE_ADDRESSES) {
    capture_addresses[capture_count++] = address;
  }
}

static int address_is_ours(u32 address)
{
  u32 index;
  for (index = 0u; index < other_count; ++index) {
    if (other_addresses[index] == address) {
      return 0;
    }
  }
  for (index = 0u; index < capture_count; ++index) {
    if (capture_addresses[index] == address) {
      return 0;
    }
  }
  return 1;
}

static u32 load_u32_le(const u8 *bytes)
{
  return (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) |
         ((u32)bytes[3] << 24);
}

/*
 * The watcher hands a frame over only when the frame is whole and is
 * for this category and attribute.  A callback returns 1 when it
 * accepted the frame and 0 when it refused it, and the watcher counts
 * a frame as seen only when it was accepted.  The frame is this
 * program's own copy and is only good for the length of the call.
 */
static int on_lease_answer(const u8 *payload, u32 inner_len)
{
  u32 body_length = (u32)payload[6] | ((u32)payload[7] << 8);
  u32 words[LEASE_WORDS];
  u32 index;
  int ours;

  if (body_length != 20u || 8u + 20u > inner_len) {
    put_text("REPLY short body ");
    put_u32(body_length);
    put_line();
    return 0;
  }
  for (index = 0u; index < LEASE_WORDS; ++index) {
    words[index] = load_u32_le(payload + 8 + index * 4u);
  }
  ours = (words[2] == requested_size && words[3] == requested_bank);
  put_text(ours ? "REPLY OURS w0=" : "REPLY other w0=");
  put_u32(words[0]);
  put_text(" w1=");
  put_u32(words[1]);
  put_text(" w2=");
  put_u32(words[2]);
  put_text(" w3=");
  put_u32(words[3]);
  put_text(" w4=");
  put_u32(words[4]);
  put_line();
  if (ours) {
    lease_handle = words[0];
    granted_address = words[1];
    seen_our_replies += 1u;
  } else if (words[1] != 0u && other_count < OTHER_ADDRESSES) {
    other_addresses[other_count++] = words[1];
  }
  return 1;
}

static u32 completion_address;
static u32 seen_completions;

/* The capture system's own image-buffer grant.  In the capture mode we ask
 * for no buffer ourselves, so every reply on that attribute is the RTOS's
 * own request for the buffer it is about to write the capture into. */
static u32 capture_lease_address;
static u32 capture_lease_size;
static u32 seen_capture_leases;

static int on_capture_lease(const u8 *payload, u32 inner_len)
{
  u32 body_length = (u32)payload[6] | ((u32)payload[7] << 8);
  u32 words[LEASE_WORDS];
  u32 index;

  if (body_length != 20u || 8u + 20u > inner_len) {
    put_text("LEASE short body ");
    put_u32(body_length);
    put_line();
    return 0;
  }
  for (index = 0u; index < LEASE_WORDS; ++index) {
    words[index] = load_u32_le(payload + 8 + index * 4u);
  }
  capture_lease_address = words[1];
  capture_lease_size = words[2];
  seen_capture_leases += 1u;
  put_text("LEASE granted=");
  put_u32(words[1]);
  put_text(" size=");
  put_u32(words[2]);
  put_text(" bank=");
  put_u32(words[3]);
  put_text(" count=");
  put_u32(seen_capture_leases);
  put_line();
  return 1;
}

static int on_completion(const u8 *payload, u32 inner_len)
{
  u32 body_length = (u32)payload[6] | ((u32)payload[7] << 8);
  if (body_length != 4u || 8u + 4u > inner_len) {
    put_text("COMPLETION short body ");
    put_u32(body_length);
    put_line();
    return 0;
  }
  completion_address = load_u32_le(payload + 8);
  put_text("COMPLETION address=");
  put_u32(completion_address);
  put_text(" count=");
  /* The watcher counts this frame after the callback returns, so the
   * count of the completions seen so far is one more than its own. */
  put_u32(seen_completions + 1u);
  put_line();
  return 1;
}

/* The sensor region of one capture, held in this program's own memory.  The
 * capture's block is sixty-four mebibytes and the sensor region is sixteen
 * of them; this is the caller's copy. */
#define SENSOR_REGION_CAPACITY 0x1000000u
static u8 sensor_region[SENSOR_REGION_CAPACITY];

/* ------------------------------------------------------------------ */
/* Searching the RTOS's own memory for a string, read-only.            */
/* ------------------------------------------------------------------ */


/*
 * Scans one page of the RTOS's own image region for an ASCII needle and
 * prints the context of every hit, then unmaps the page again.
 *
 * The page is mapped read-only and released straight away.  Holding many
 * mappings of the RTOS's region at once took the camera offline, so the scan
 * is deliberately one page at a time.
 */
static void scan_page_for_text(u32 page, u32 length, const char *needle,
                               u32 needle_length)
{
  long mem_fd;
  long mapping;
  const unsigned char *at;
  u32 index;

  mem_fd = (int)syscall6(SYS_open, (long)"/dev/mem", (long)O_RDONLY, 0, 0, 0, 0);
  if (failed(mem_fd)) {
    return;
  }
  mapping = syscall6(SYS_mmap2, 0, (long)length, PROT_READ, MAP_SHARED, mem_fd,
                      (long)(page >> 12));
  (void)syscall6(SYS_close, mem_fd, 0, 0, 0, 0, 0);
  if (failed(mapping)) {
    return;
  }
  at = (const unsigned char *)(usize)mapping;
  for (index = 0u; index + needle_length <= length; ++index) {
    u32 k;
    for (k = 0u; k < needle_length; ++k) {
      if (at[index + k] != (unsigned char)needle[k]) {
        break;
      }
    }
    if (k == needle_length) {
      u32 show;
      put_text("HIT 0x");
      put_hex_byte((page + index) >> 24);
      put_hex_byte((page + index) >> 16);
      put_hex_byte((page + index) >> 8);
      put_hex_byte(page + index);
      put_text(" : ");
      for (show = index; show < index + 160u && show < length; ++show) {
        unsigned char c = at[show];
        if (c >= 32u && c < 127u) {
          put_char((char)c);
        } else {
          put_char('.');
        }
      }
      put_line();
    }
  }
  (void)syscall6(SYS_munmap, mapping, (long)length, 0, 0, 0, 0);
}

static int read_rtos_memory(u32 address, u32 length, u32 *words, u32 max_words);

/*
 * Scans the capture's own buffer for the sensor plane's signature.
 *
 * The camera is set to RAW_BIT_14BIT with RAW_PACK_PACK, so a sensor sample
 * is a small number in a sixteen-bit word: the top two bits are clear and
 * the word is not zero.  Ordinary data - pointers, headers, compressed
 * output - has those bits set about a quarter of the time.  A page of
 * sensor data therefore stands out sharply.
 *
 * The buffer is read one page at a time and released again straight away.
 */
/* Dumps the first bytes of the capture's own buffer, so that its content
 * can be compared with a sensor RAW of the same scene. */
static void dump_buffer_head(u32 buffer, u32 length)
{
  u32 words[128];
  long mem_fd;
  long mapping;
  const unsigned char *at;
  u32 index;
  u32 count;

  if (length > 512u) {
    length = 512u;
  }
  /* The capture's buffer is in the capture pool, which is outside the RTOS
   * image region, so it is mapped here rather than read through the guarded
   * reader.  One page, released again straight away. */
  mem_fd = (int)syscall6(SYS_open, (long)"/dev/mem", (long)O_RDONLY, 0, 0, 0, 0);
  if (failed(mem_fd)) {
    put_text("BUFFER head unreadable\n");
    return;
  }
  mapping = syscall6(SYS_mmap2, 0, 4096, PROT_READ, MAP_SHARED, mem_fd,
                      (long)(buffer >> 12));
  (void)syscall6(SYS_close, mem_fd, 0, 0, 0, 0, 0);
  if (failed(mapping)) {
    put_text("BUFFER head unreadable\n");
    return;
  }
  at = (const unsigned char *)(usize)mapping;
  count = length / 4u;
  for (index = 0u; index < count; ++index) {
    words[index] = load_u32_le(at + index * 4u);
  }
  (void)syscall6(SYS_munmap, mapping, 4096, 0, 0, 0, 0);
  put_text("BUFFER ");
  put_u32(buffer);
  put_text(" : ");
  for (index = 0u; index * 4u < length; ++index) {
    put_hex_byte(words[index] >> 24);
    put_hex_byte(words[index] >> 16);
    put_hex_byte(words[index] >> 8);
    put_hex_byte(words[index]);
  }
  put_line();
}

/*
 * Copies a region of the capture's own buffer into this program's memory.
 *
 * The region is copied with a single mapping of the whole thing, which is
 * the shape the capture library itself uses: it maps its 416 MB pool in one
 * go.  The earlier per-page loop made 4096 open/mmap/munmap trips over the
 * device, which is both slow and, as the runs that took the camera with
 * them showed, the sort of thing that upsets it.
 *
 * If the one big mapping is refused, the copy falls back to the old one
 * page at a time, so the region can still be had.
 *
 * Returns the number of bytes copied.
 */
static u32 copy_region_to_memory(u32 buffer, u32 length)
{
  u32 started = now_microseconds();
  long mem_fd;
  long mapping;
  u32 mapped_length;
  u32 copied = 0u;
  u32 page;

  /* One mapping, of the whole region. */
  mapped_length = (length + 4095u) & ~4095u;
  mem_fd = (int)syscall6(SYS_open, (long)"/dev/mem", (long)O_RDONLY, 0, 0, 0, 0);
  if (!failed(mem_fd)) {
    mapping = syscall6(SYS_mmap2, 0, (long)mapped_length, PROT_READ, MAP_SHARED,
                        mem_fd, (long)(buffer >> 12));
    (void)syscall6(SYS_close, mem_fd, 0, 0, 0, 0, 0);
    if (!failed(mapping)) {
      (void)memcpy(sensor_region, (const void *)(usize)mapping, length);
      (void)syscall6(SYS_munmap, mapping, (long)mapped_length, 0, 0, 0, 0);
      copied = length;
      put_text("GRAB copy took ");
      put_u32((now_microseconds() - started) / 1000u);
      put_text(" ms for ");
      put_u32(copied / 1024u);
      put_text(" KiB in one mapping\n");
      return copied;
    }
    put_text("GRAB the one mapping was refused, falling back to one page at a time\n");
  }

  /* The old way: one page at a time, each released straight away. */
  for (page = buffer; page < buffer + length; page += 4096u) {
    u32 chunk = 4096u;
    if (page + chunk > buffer + length) {
      chunk = buffer + length - page;
    }
    if (copied + chunk > SENSOR_REGION_CAPACITY) {
      break;
    }
    mem_fd = (int)syscall6(SYS_open, (long)"/dev/mem", (long)O_RDONLY, 0, 0, 0, 0);
    if (failed(mem_fd)) {
      continue;
    }
    mapping = syscall6(SYS_mmap2, 0, 4096, PROT_READ, MAP_SHARED, mem_fd,
                        (long)(page >> 12));
    (void)syscall6(SYS_close, mem_fd, 0, 0, 0, 0, 0);
    if (failed(mapping)) {
      continue;
    }
    (void)memcpy(sensor_region + copied, (const void *)(usize)mapping, chunk);
    (void)syscall6(SYS_munmap, mapping, 4096, 0, 0, 0, 0);
    copied += chunk;
  }
  put_text("GRAB copy took ");
  put_u32((now_microseconds() - started) / 1000u);
  put_text(" ms for ");
  put_u32(copied / 1024u);
  put_text(" KiB page at a time\n");
  return copied;
}

/* Diagnostic statistic: inspect the low16 bits of each four-byte step
 * across the whole static candidate array. This is not a pixel-format,
 * sensor-provenance or complete-copy validator; extent/packing remain open.
 */
static u32 candidate_low_word_score(void)
{
  u32 index;
  u32 words = SENSOR_REGION_CAPACITY / 4u;
  u32 small = 0u;

  for (index = 0u; index < words; ++index) {
    u32 word = load_u32_le(sensor_region + index * 4u);
    if (word != 0u && (word & 0xc000u) == 0u) {
      small += 1u;
    }
  }
  return (small * 100u) / (words == 0u ? 1u : words);
}

/* Opens the on-card copy of the program's output. */
static void open_log(const char *path)
{
  long fd;

  if (path == 0 || path[0] == '\0') {
    return;
  }
  fd = (long)syscall6(SYS_open, (long)path, (long)(O_WRONLY | O_CREAT | O_TRUNC),
                       0644, 0, 0, 0);
  if (failed(fd)) {
    put_text("LOG cannot open ");
    put_text(path);
    put_line();
    return;
  }
  log_fd = (long)fd;
}

/* Writes the sensor region to a file, from this program's own memory. */
static void write_region(u32 length, const char *path)
{
  long fd;

  fd = (long)syscall6(SYS_open, (long)path, 0x241u, 0644, 0, 0, 0);
  if (failed(fd)) {
    put_text("FILE open failed: ");
    put_text(path);
    put_line();
    return;
  }
  (void)write_all((int)fd, (const char *)sensor_region, length);
  (void)syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  put_text("FILE ");
  put_text(path);
  put_text(" ");
  put_u32(length);
  put_text(" bytes\n");
}

/* Tracks this process's enable request, not setting ownership or an
 * acknowledged change. Original-value restoration remains unresolved. */
static int ssif_raw_requested;

/* The capture system's own setting switch, through the stock settings tool:
 * 124 is SSIF_RAW, and 1 is on. */
static void set_ssif_raw(int on)
{
  long pid;

  if (!on && !ssif_raw_requested) {
    /* This run did not turn the switch on, so it does not turn it off. */
    return;
  }
  pid = (long)syscall6(SYS_fork, 0, 0, 0, 0, 0, 0);
  if (pid == 0) {
    const char *argv[6];
    argv[0] = "/usr/bin/st";
    argv[1] = "cap";
    argv[2] = "capdtm";
    argv[3] = "setusr";
    argv[4] = "124";
    argv[5] = on ? "0x7c0001" : "0x7c0000";
    (void)syscall6(SYS_execve, (long)"/usr/bin/st", (long)argv, 0, 0, 0, 0);
    (void)syscall6(SYS_exit, 1, 0, 0, 0, 0, 0);
  }
  if (pid > 0) {
    int status = 0;
    (void)syscall6(SYS_wait4, pid, (long)&status, 0, 0, 0, 0);
  }
  put_text("SSIF_RAW ");
  put_text(on ? "on" : "off");
  put_line();
  ssif_raw_requested = on ? 1 : 0;
}

/* Attempts disable after this process requested enable. This does not
 * preserve an unknown original value or establish a remote acknowledgement. */
static void restore_ssif_raw(void)
{
  if (ssif_raw_requested) {
    set_ssif_raw(0);
  }
}
static void copy_region(u32 buffer, u32 length, const char *path)
{
  long fd;
  u32 page;
  u32 copied = 0u;

  fd = (long)syscall6(SYS_open, (long)path, 0x241u, 0644, 0, 0, 0);
  if (failed(fd)) {
    put_text("COPY open failed\n");
    return;
  }
  for (page = buffer; page < buffer + length; page += 4096u) {
    long mem_fd;
    long mapping;
    u32 chunk = 4096u;
    if (page + chunk > buffer + length) {
      chunk = buffer + length - page;
    }
    mem_fd = (int)syscall6(SYS_open, (long)"/dev/mem", (long)O_RDONLY, 0, 0, 0, 0);
    if (failed(mem_fd)) {
      continue;
    }
    mapping = syscall6(SYS_mmap2, 0, 4096, PROT_READ, MAP_SHARED, mem_fd,
                        (long)(page >> 12));
    (void)syscall6(SYS_close, mem_fd, 0, 0, 0, 0, 0);
    if (failed(mapping)) {
      continue;
    }
    (void)write_all((int)fd, (const char *)(usize)mapping, chunk);
    (void)syscall6(SYS_munmap, mapping, 4096, 0, 0, 0, 0);
    copied += chunk;
  }
  (void)syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  put_text("COPIED ");
  put_u32(copied);
  put_text(" bytes of ");
  put_u32(buffer);
  put_text(" to ");
  put_text(path);
  put_line();
}

static void scan_for_plane(u32 buffer, u32 length)
{
  u32 page;
  u32 best_count = 0u;
  u32 best_page = 0u;

  put_text("PLANE scan of ");
  put_u32(buffer);
  put_text(" +");
  put_u32(length);
  put_line();
  for (page = buffer; page < buffer + length; page += 4096u) {
    long mem_fd;
    long mapping;
    const unsigned char *at;
    u32 index;
    u32 small = 0u;
    u32 big = 0u;
    u32 nonzero = 0u;

    mem_fd = (int)syscall6(SYS_open, (long)"/dev/mem", (long)O_RDONLY, 0, 0, 0, 0);
    if (failed(mem_fd)) {
      continue;
    }
    mapping = syscall6(SYS_mmap2, 0, 4096, PROT_READ, MAP_SHARED, mem_fd,
                        (long)(page >> 12));
    (void)syscall6(SYS_close, mem_fd, 0, 0, 0, 0, 0);
    if (failed(mapping)) {
      continue;
    }
    at = (const unsigned char *)(usize)mapping;
    for (index = 0u; index + 2u <= 4096u; index += 2u) {
      u32 word = (u32)at[index] | ((u32)at[index + 1u] << 8);
      if (word == 0u) {
        continue;
      }
      nonzero += 1u;
      if ((word & 0xc000u) == 0u) {
        small += 1u;
      } else {
        big += 1u;
      }
    }
    (void)syscall6(SYS_munmap, mapping, 4096, 0, 0, 0, 0);
    if (small > 1024u) {
      put_text("PLANE page 0x");
      put_hex_byte(page >> 24);
      put_hex_byte(page >> 16);
      put_hex_byte(page >> 8);
      put_hex_byte(page);
      put_text(" small=");
      put_u32(small);
      put_text(" big=");
      put_u32(big);
      put_text(" nonzero=");
      put_u32(nonzero);
      put_line();
    }
    if (small > best_count) {
      best_count = small;
      best_page = page;
    }
  }
  put_text("PLANE best page 0x");
  put_hex_byte(best_page >> 24);
  put_hex_byte(best_page >> 16);
  put_hex_byte(best_page >> 8);
  put_hex_byte(best_page);
  put_text(" small=");
  put_u32(best_count);
  put_line();
}

/*
 * Reads the RTOS's own log ring and prints the entries whose text carries
 * the needle, newest first.  The ring is a static array, so this is a
 * handful of one-page reads rather than a scan.
 */
static void read_log_ring(const char *needle, u32 log_dump_count)
{
  u32 needle_length = 0u;
  u32 index_words[1];
  u32 index;
  u32 entry;

  while (needle[needle_length] != 0) {
    needle_length += 1u;
  }
  if (read_rtos_memory(LOG_RING_INDEX, 4u, index_words, 1u) == 0) {
    put_text("LOG index=");
    put_u32(index_words[0]);
    put_line();
    index = index_words[0];
  } else {
    put_text("LOG index unreadable\n");
    return;
  }
  for (entry = 0u; entry < log_dump_count; ++entry) {
    u32 slot = index - entry - 1u;
    u32 text_words[32];
    u32 text_address = LOG_RING_BASE + slot * LOG_ENTRY_SIZE + LOG_TEXT_OFFSET;
    u32 k;
    if (read_rtos_memory(text_address, LOG_TEXT_SIZE, text_words, 32u) != 0) {
      continue;
    }
    for (k = 0u; k < LOG_TEXT_SIZE; ++k) {
      const unsigned char *at = (const unsigned char *)(usize)text_words;
      u32 m;
      int match = 1;
      for (m = 0u; m < needle_length; ++m) {
        if (at[k + m] != (unsigned char)needle[m]) {
          match = 0;
          break;
        }
      }
      if ((match || needle_length == 0u) && k + needle_length <= LOG_TEXT_SIZE) {
        u32 show;
        put_text("LOG entry ");
        put_u32(slot);
        put_text(" : ");
        for (show = k; show < k + 120u && show < LOG_TEXT_SIZE; ++show) {
          unsigned char c = at[show];
          if (c >= 32u && c < 127u) {
            put_char((char)c);
          } else {
            put_char('.');
          }
        }
        put_line();
        break;
      }
    }
  }
}

static int search_text(const char *needle)
{
  u32 needle_length = 0u;
  u32 page;

  while (needle[needle_length] != 0) {
    needle_length += 1u;
  }
  if (needle_length == 0u) {
    put_text("SEARCH empty needle\n");
    return -1;
  }
  for (page = SEARCH_FIRST; page < SEARCH_LAST; page += 4096u) {
    u32 length = 4096u;
    if (page + length > SEARCH_LAST) {
      length = SEARCH_LAST - page;
    }
    scan_page_for_text(page, length, needle, needle_length);
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* Reading the RTOS's own capture object, read-only.                   */
/* ------------------------------------------------------------------ */

/*
 * Reads LENGTH bytes at ADDRESS through /dev/mem, read-only, and only inside
 * the RTOS's runtime image region.  The capture pool is deliberately out of
 * range: mapping it took the camera offline.
 */
static int read_rtos_memory(u32 address, u32 length, u32 *words, u32 max_words)
{
  long mem_fd;
  long mapping;
  u32 page_base = address & ~4095u;
  u32 offset = address - page_base;
  u32 map_length = (offset + length + 4095u) & ~4095u;
  const unsigned char *at;
  u32 index;

  if (address < RTOS_IMAGE_FIRST || address >= RTOS_IMAGE_LAST ||
      address + length > RTOS_IMAGE_LAST) {
    /* The RTOS publishes its image descriptor into a second region, which
     * the Linux side maps as well: physical 0x85400000.  One page of it is
     * read, read-only, exactly as the capture library maps it. */
    if (address >= DESCRIPTOR_FIRST && address + length <= DESCRIPTOR_LAST) {
      put_text("(descriptor region)\n");
    } else {
      put_text("OBJECT refused: address outside the RTOS image region\n");
      return -1;
    }
  }
  if (length == 0u || length > 4096u) {
    put_text("OBJECT refused: bad length\n");
    return -1;
  }
  mem_fd = (int)syscall6(SYS_open, (long)"/dev/mem", (long)O_RDONLY, 0, 0, 0, 0);
  if (failed(mem_fd)) {
    put_text("OBJECT open /dev/mem failed\n");
    return -1;
  }
  mapping = syscall6(SYS_mmap2, 0, (long)map_length, PROT_READ, MAP_SHARED, mem_fd,
                      (long)(page_base >> 12));
  (void)syscall6(SYS_close, mem_fd, 0, 0, 0, 0, 0);
  if (failed(mapping)) {
    put_text("OBJECT mmap failed\n");
    return -1;
  }
  at = (const unsigned char *)(usize)mapping + offset;
  put_text("OBJECT ");
  put_u32(address);
  put_text(" len ");
  put_u32(length);
  put_line();
  for (index = 0u; index < length; ++index) {
    put_hex_byte(at[index]);
    if ((index + 1u) % 32u == 0u) {
      put_line();
    }
  }
  if ((length % 32u) != 0u) {
    put_line();
  }
  for (index = 0u; index < max_words && (index * 4u + 4u) <= length; ++index) {
    words[index] = load_u32_le(at + index * 4u);
  }
  (void)syscall6(SYS_munmap, mapping, (long)map_length, 0, 0, 0, 0);
  return 0;
}

/*
 * Reports the plane word at +0x84, every word equal to the granted address,
 * and every word that looks like a capture-pool address.
 */
static void report_object(const u32 *words, u32 count)
{
  u32 index;
  int found_plane = 0;

  for (index = 0u; index < count; ++index) {
    u32 offset = index * 4u;
    if (offset == PLANE_WORD_OFFSET) {
      put_text("PLANE +0x84 value=");
      put_u32(words[index]);
      put_line();
      found_plane = 1;
    }
    if (granted_address != 0u && words[index] == granted_address) {
      put_text("MATCH offset=0x");
      put_hex_byte((offset >> 8) & 0xffu);
      put_hex_byte(offset & 0xffu);
      put_text(" value=");
      put_u32(words[index]);
      put_line();
    }
    if (words[index] >= POOL_FIRST && words[index] < POOL_LAST) {
      remember_capture_address(words[index]);
      put_text("POOL offset=0x");
      put_hex_byte((offset >> 8) & 0xffu);
      put_hex_byte(offset & 0xffu);
      put_text(" value=");
      put_u32(words[index]);
      put_line();
    }
  }
  if (!found_plane) {
    put_text("PLANE +0x84 not read: raise --object-bytes to 136\n");
  }
  if (granted_address != 0u) {
    put_text("VERDICT granted=");
    put_u32(granted_address);
    put_text(" plane=");
    put_u32((PLANE_WORD_OFFSET / 4u) < count ? words[PLANE_WORD_OFFSET / 4u] : 0u);
    put_text(granted_address ==
                     ((PLANE_WORD_OFFSET / 4u) < count ? words[PLANE_WORD_OFFSET / 4u] : 0u)
                 ? " SAME\n"
                 : " DIFFERENT\n");
  }
}

/*
 * Reads the object the capture record points at and reports the plane word
 * at +0x84 of it, together with whether that word lies inside the block the
 * capture system leased.
 */
static u32 follow_record(u32 record_address, u32 object_bytes)
{
  u32 record_words[128];
  u32 record_count = object_bytes / 4u;
  u32 image_words[128];
  u32 image_count = object_bytes / 4u;
  u32 image_address;
  u32 plane;

  /* The record has to be read far enough to hold the block word at +0x38,
   * and the image far enough to hold the plane word at +0x84.  A smaller
   * --object-bytes would leave those words unset, and an unset word used as
   * a physical address is a mapping of whatever the stack held. */
  if (object_bytes < 0x88u) {
    put_text("RECORD needs --object-bytes of at least 136\n");
    return 0u;
  }

  if (record_count > 128u) {
    record_count = 128u;
  }
  if (image_count > 128u) {
    image_count = 128u;
  }
  if (read_rtos_memory(record_address, object_bytes, record_words,
                       record_count) != 0) {
    put_text("RECORD unreadable\n");
    return 0u;
  }
  image_address = record_words[0];
  put_text("RECORD +0x00 image=");
  put_u32(image_address);
  put_text(" +0x38 lease=");
  put_u32(record_words[0x38 / 4u]);
  put_line();
  if (image_address < RTOS_IMAGE_FIRST || image_address >= RTOS_IMAGE_LAST) {
    put_text("IMAGE pointer outside the RTOS image region, not followed\n");
    return record_words[0x38 / 4u];
  }
  if (read_rtos_memory(image_address, object_bytes, image_words,
                       image_count) != 0) {
    put_text("IMAGE unreadable\n");
    return record_words[0x38 / 4u];
  }
  plane = image_words[PLANE_WORD_OFFSET / 4u];
  put_text("IMAGE +0x84 plane=");
  put_u32(plane);
  put_line();
  if (capture_lease_address != 0u && capture_lease_size != 0u) {
    u32 low = capture_lease_address;
    u32 high = capture_lease_address + capture_lease_size;
    put_text("CONTAINMENT plane in [lease, lease+size) = ");
    put_u32(low);
    put_text(",");
    put_u32(high);
    put_text(" : ");
    put_text((plane >= low && plane < high) ? "YES" : "NO");
    put_line();
  }
  {
    u32 index;
    for (index = 0u; index < image_count; ++index) {
      u32 offset = index * 4u;
      if (image_words[index] == capture_lease_address && capture_lease_address != 0u) {
        put_text("IMAGE MATCH lease at offset=0x");
        put_hex_byte((offset >> 8) & 0xffu);
        put_hex_byte(offset & 0xffu);
        put_line();
      }
      if (image_words[index] >= POOL_FIRST && image_words[index] < POOL_LAST) {
        put_text("IMAGE POOL offset=0x");
        put_hex_byte((offset >> 8) & 0xffu);
        put_hex_byte(offset & 0xffu);
        put_text(" value=");
        put_u32(image_words[index]);
        put_line();
      }
    }
  }
  return record_words[0x38 / 4u];
}


/* ------------------------------------------------------------------ */

static u32 write_frame(int fd, u32 category, u32 attribute,
                       const u8 *body, u32 body_length, const char *label)
{
  u32 frame_words[64];
  u8 *frame = (u8 *)frame_words;
  size_t frame_size = 0u;
  u32 block[4];
  u32 available[2];
  long result;

  if (mcb_frame_encode_linux((uint16_t)category, (uint16_t)attribute, body,
                             (size_t)body_length, frame, sizeof(frame_words),
                             &frame_size) != MCB_FRAME_OK) {
    put_text(label);
    put_text(" encode refused\n");
    return 0u;
  }
  put_text(label);
  put_text(" bytes=");
  put_u32((u32)frame_size);
  put_text(" ");
  put_hex(frame, (u32)frame_size);
  put_line();

  available[0] = DESTINATION_CPU;
  available[1] = 0xffffffffu;
  result = syscall6(SYS_ioctl, fd, (long)IPCC_WRITE_AVAILABLE_CMD,
                     (long)available, 0, 0, 0);
  if (failed(result)) {
    put_text(label);
    put_text(" availability query failed\n");
    return 0u;
  }
  if (available[1] != 0xffffffffu && available[1] < (u32)frame_size) {
    put_text(label);
    put_text(" not enough free space\n");
    return 0u;
  }
  block[0] = DESTINATION_CPU;
  block[1] = (u32)frame_size;
  block[2] = 0xffffffffu;
  block[3] = (u32)(usize)frame;
  result = syscall6(SYS_ioctl, fd, (long)IPCC_WRITE_PKT_CMD, (long)block, 0, 0, 0);
  put_text(label);
  put_text(" rc=");
  put_i32((i32)result);
  put_text(" reported=");
  put_u32(block[2]);
  put_line();
  return (failed(result) || block[2] == 0xffffffffu) ? 0u : 1u;
}

#ifdef IPCC_RAW_GRAB_HOST_TEST
/* The host harness calls c_entry more than once in one process, so the
 * program's static state has to be put back the way a fresh run would find
 * it.  This is the harness's only way in: the camera build does not have
 * it. */
void ipcc_raw_grab_host_reset(void)
{
  output = 0;
  output_length = 0u;
  fallback_length = 0u;
  log_fd = -1;
  log_line_length = 0u;
  run_started = 0u;
  requested_size = 0u;
  requested_bank = 0u;
  granted_address = 0u;
  lease_handle = 0u;
  seen_replies = 0u;
  seen_our_replies = 0u;
  other_count = 0u;
  capture_count = 0u;
  completion_address = 0u;
  seen_completions = 0u;
  capture_lease_address = 0u;
  capture_lease_size = 0u;
  seen_capture_leases = 0u;
  ssif_raw_requested = 0;
}

/*
 * The host harness drives the real watcher over a synthetic shared
 * window.  The window is the shape the camera's shared window has:
 * the four ring regions and the four producer cursor words at the
 * offsets the ring table names.  The harness writes the cursor word
 * itself and puts the record's bytes in the ring, so the watcher sees
 * exactly what it sees on the camera.  There is no second parser here.
 */
static watcher host_watcher;

void ipcc_raw_grab_host_watcher_reset(const unsigned char *window)
{
  watcher_init(&host_watcher, window);
}

void ipcc_raw_grab_host_watcher_poll(u32 category, u32 attribute,
                                     int (*handler)(const u8 *, u32),
                                     u32 *seen)
{
  watcher_poll(&host_watcher, category, attribute, handler, seen);
}

u32 ipcc_raw_grab_host_ring_data_offset(int ring)
{
  return RINGS[ring].data;
}

u32 ipcc_raw_grab_host_ring_cursor_offset(int ring)
{
  return RINGS[ring].write_cursor;
}

u32 ipcc_raw_grab_host_ring_capacity(void)
{
  return RING_CAPACITY;
}

u32 ipcc_raw_grab_host_window_bytes(void)
{
  return SHARED_MAP_BYTES;
}

u32 ipcc_raw_grab_host_stat_records_ok(void)
{
  return host_watcher.records_ok;
}

u32 ipcc_raw_grab_host_stat_records_other(void)
{
  return host_watcher.records_other;
}

u32 ipcc_raw_grab_host_stat_records_bad(void)
{
  return host_watcher.records_bad;
}

u32 ipcc_raw_grab_host_stat_frames_rejected(void)
{
  return host_watcher.frames_rejected;
}

u32 ipcc_raw_grab_host_stat_bytes_discarded(void)
{
  return host_watcher.bytes_discarded;
}

u32 ipcc_raw_grab_host_stat_cursor_errors(void)
{
  return host_watcher.cursor_errors;
}

u32 ipcc_raw_grab_host_stat_incomplete(void)
{
  return host_watcher.incomplete;
}
#endif

/*
 * The dry run: the frame the live path would put on the wire, printed once,
 * and nothing else.  It is a separate function so that its frame buffer is
 * its own stack frame, and it takes only bytes that were already built: the
 * body comes from the preflight, the frame comes from the same encoder the
 * live path uses.  It opens nothing, maps nothing and writes nothing but
 * the text it prints.
 */
static int dry_run_report(u32 command, u32 remaining, u64 free_space,
                          const u8 *body)
{
  u8 frame[64];
  size_t frame_size = 0u;

  if (mcb_frame_encode_linux((uint16_t)CATEGORY_STILL, (uint16_t)ATTRIBUTE_STILL,
                             body, (size_t)STILL_REQUEST_PAYLOAD_SIZE, frame,
                             sizeof(frame), &frame_size) != MCB_FRAME_OK) {
    put_text("ERROR dry run encode refused\n");
    return 2;
  }
  put_text("DRYRUN command=0x");
  put_hex_byte((command >> 24) & 0xffu);
  put_hex_byte((command >> 16) & 0xffu);
  put_hex_byte((command >> 8) & 0xffu);
  put_hex_byte(command & 0xffu);
  put_text(" cap=");
  put_u32(STILL_REQUEST_CAP_DEFAULT);
  put_text(" max=");
  put_u32(STILL_REQUEST_MAX_DEFAULT);
  put_text(" remaining=");
  put_u32(remaining);
  put_text(" free=");
  put_u64(free_space);
  put_text(" body=");
  put_u32((u32)STILL_REQUEST_PAYLOAD_SIZE);
  put_line();
  put_text("DRYRUN frame bytes=");
  put_u32((u32)frame_size);
  put_line();
  put_text("DRYRUN frame ");
  put_hex(frame, (u32)frame_size);
  put_line();
  return 0;
}

int c_entry(usize *initial_stack)
{
  int argc = (int)initial_stack[0];
  char **argv = (char **)(initial_stack + 1);
  long buffer;
  long mapping;
  int fd;
  u32 timeout_ms = DEFAULT_TIMEOUT_MS;
  u32 period_us = 500u;
  u32 settle_ms = 0u;
  u32 object_bytes = 256u;
  u32 size = 0x00100000u;
  u32 bank = 2u;
  u32 resends = 2u;
  u32 command = STILL_REQUEST_COMMAND_SHARED_PATH;
  u32 remaining = 2518u;
  u64 free_space = 7687503872u;
  int mode = 0;            /* 0 acquire, 1 compare, 2 control */
  int dry_run = 0;
  int do_release = 1;
  int release_only = 0;
  int still_mode;
  int read_descriptor = 0;
  int scan_plane = 0;
  int dump_buffer = 0;
  int no_copy = 0;
  const char *copy_path = 0;
  const char *output_path = "/tmp/sensor.raw";
  u32 bench_address = 0u;
  u32 bench_length = 0xfff200u;
  const char *log_path = 0;
  u32 max_seconds = 120u;
  u32 copy_length = 0xfff200u;
  const char *search_needle = 0;
  u32 log_dump_count = 0u;
  u32 release_address = 0u;
  int i;
  still_request_options options;
  still_request_storage_info storage;
  u8 still_body[STILL_REQUEST_PAYLOAD_SIZE];
  u8 lease_body[20];
  watcher watch;
  u32 start;
  u32 object_words[128];
  u32 object_count;

  for (i = 1; i < argc; ++i) {
    u64 value = 0u;
    if ((same(argv[i], "--mode") || same(argv[i], "-m")) && i + 1 < argc) {
      const char *name = argv[++i];
      if (same(name, "acquire")) {
        mode = 0;
      } else if (same(name, "compare")) {
        mode = 1;
      } else if (same(name, "control")) {
        mode = 2;
      } else if (same(name, "capture")) {
        mode = 3;
      } else if (same(name, "grab")) {
        mode = 4;
      } else if (same(name, "bench")) {
        mode = 5;
      } else if (same(name, "still")) {
        mode = 6;
      } else {
        put_text("ERROR bad mode\n");
        return 2;
      }
    } else if ((same(argv[i], "--size") || same(argv[i], "-s")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value == 0u || value > 0x10000000u) {
        put_text("ERROR bad size\n");
        return 2;
      }
      size = (u32)value;
    } else if ((same(argv[i], "--bank") || same(argv[i], "-b")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value > 0xffffu) {
        put_text("ERROR bad bank\n");
        return 2;
      }
      bank = (u32)value;
    } else if ((same(argv[i], "--cmd") || same(argv[i], "--command")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value > 0xffffffffu) {
        put_text("ERROR bad command\n");
        return 2;
      }
      command = (u32)value;
    } else if ((same(argv[i], "--rem") || same(argv[i], "--remaining")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value > 0xffffffffu) {
        put_text("ERROR bad remaining\n");
        return 2;
      }
      remaining = (u32)value;
    } else if ((same(argv[i], "--free")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value)) {
        put_text("ERROR bad free\n");
        return 2;
      }
      free_space = value;
    } else if ((same(argv[i], "--log-dump")) && i + 1 < argc) {
      /* Zero means "do not dump the ring"; the runner passes it by default. */
      if (!parse_number(argv[++i], &value) || value > 200u) {
        put_text("ERROR bad log-dump\n");
        return 2;
      }
      log_dump_count = (u32)value;
    } else if ((same(argv[i], "--copy-plane")) && i + 1 < argc) {
      copy_path = argv[++i];
    } else if ((same(argv[i], "--out")) && i + 1 < argc) {
      output_path = argv[++i];
    } else if ((same(argv[i], "--log")) && i + 1 < argc) {
      log_path = argv[++i];
    } else if ((same(argv[i], "--max-seconds")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value == 0u || value > 3600u) {
        put_text("ERROR bad max-seconds\n");
        return 2;
      }
      max_seconds = (u32)value;
    } else if ((same(argv[i], "--bench-address")) && i + 1 < argc) {
      /* Zero is the runner's default and means "no bench address".  Only
       * the bench mode needs one, and it checks for itself before it
       * touches the device. */
      if (!parse_number(argv[++i], &value) || value > 0xffffffffu) {
        put_text("ERROR bad bench-address\n");
        return 2;
      }
      bench_address = (u32)value;
    } else if ((same(argv[i], "--bench-length")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value == 0u) {
        put_text("ERROR bad bench-length\n");
        return 2;
      }
      bench_length = (u32)value;
    } else if (same(argv[i], "--dump-buffer")) {
      dump_buffer = 1;
    } else if (same(argv[i], "--dry-run")) {
      /* Print the frame that would go out and stop there. */
      dry_run = 1;
    } else if (same(argv[i], "--no-copy")) {
      no_copy = 1;
    } else if (same(argv[i], "--scan-plane")) {
      scan_plane = 1;
    } else if ((same(argv[i], "--search-text")) && i + 1 < argc) {
      search_needle = argv[++i];
    } else if (same(argv[i], "--read-descriptor")) {
      read_descriptor = 1;
    } else if (same(argv[i], "--no-release")) {
      do_release = 0;
    } else if ((same(argv[i], "--release-address")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value == 0u || value > 0xffffffffu) {
        put_text("ERROR bad release-address\n");
        return 2;
      }
      release_address = (u32)value;
      release_only = 1;
    } else if ((same(argv[i], "--resends")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value > 20u) {
        put_text("ERROR bad resends\n");
        return 2;
      }
      resends = (u32)value;
    } else if ((same(argv[i], "--obj") || same(argv[i], "--object-bytes")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value == 0u || value > 4096u) {
        put_text("ERROR bad object-bytes\n");
        return 2;
      }
      object_bytes = (u32)value;
    } else if ((same(argv[i], "--set") || same(argv[i], "--settle-ms")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value > 120000u) {
        put_text("ERROR bad settle-ms\n");
        return 2;
      }
      settle_ms = (u32)value;
    } else if ((same(argv[i], "--tmo") || same(argv[i], "--timeout-ms")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value > 120000u) {
        put_text("ERROR bad timeout-ms\n");
        return 2;
      }
      timeout_ms = (u32)value;
    } else if ((same(argv[i], "--per") || same(argv[i], "--period-us")) && i + 1 < argc) {
      if (!parse_number(argv[++i], &value) || value > 100000u) {
        put_text("ERROR bad period-us\n");
        return 2;
      }
      period_us = (u32)value;
    } else {
      put_text("usage: ipcc-raw-grab --mode <grab|bench|capture|acquire|compare|control> "
               "[--size <bytes>] [--bank <n>] [--command <hex>] [--settle-ms <n>] "
               "[--object-bytes <n>] [--timeout-ms <n>] [--period-us <n>] "
               "[--dry-run] [--no-release]\n");
      return 2;
    }
  }

  /* The bench mode copies from an address the caller names, so it cannot run
   * without one.  It is checked here, before the output buffer is mapped and
   * before the device is opened, so a missing address costs nothing. */
  if (mode == 3 && object_bytes < 0x88u) {
    put_text("ERROR capture needs --object-bytes of at least 136\n");
    return 2;
  }
  if (mode == 5 && bench_address == 0u) {
    put_text("ERROR bench needs --bench-address\n");
    return 2;
  }

  /* The modes that send a still request.  The acquire and the bench modes
   * send none, and a release on its own sends none. */
  still_mode = (mode == 1 || mode == 2 || mode == 3 || mode == 4 || mode == 6);

  /*
   * The still request is built here, before the output buffer is mapped and
   * before anything is asked of the device.  The encoder is a pure function
   * of the arguments, so building the body here costs nothing and means that
   * a request that cannot be built never takes a lease and never reaches
   * the device.  The body built here is the body the live path sends.
   */
  if (still_mode && !release_only) {
    if (!still_command_is_reviewed(command)) {
      put_text("ERROR bad still command\n");
      return 2;
    }
    options.command = command;
    options.cap = STILL_REQUEST_CAP_DEFAULT;
    options.max = STILL_REQUEST_MAX_DEFAULT;
    storage.remaining_count = remaining;
    storage.free_space_bytes = free_space;
    if (still_request_encode_ex(still_body, sizeof(still_body), &storage,
                                 &options) != 0) {
      put_text("ERROR still request refused\n");
      return 2;
    }
  }

  /*
   * A dry run stops here.  It prints the frame that would go out and it
   * touches nothing: no mapping, no device, no log file, no output file and
   * no setting.  The frame it prints is the frame the live path would send,
   * built by the same encoder from the same arguments.
   */
  if (dry_run) {
    if (release_only) {
      put_text("ERROR dry run does not apply to --release-address\n");
      return 2;
    }
    if (!still_mode) {
      put_text("ERROR dry run needs a still mode\n");
      return 2;
    }
    return dry_run_report(command, remaining, free_space, still_body);
  }

  buffer = syscall6(SYS_mmap2, 0, (long)OUTPUT_CAP, PROT_READ | 2u, 2u | 0x20u, -1, 0);
  if (failed(buffer)) {
    (void)write_all(2, "ERROR output buffer\n", 20);
    return 1;
  }
  output = (char *)(usize)buffer;
  output_length = 0u;
  requested_size = size;
  requested_bank = bank;

  open_log(log_path);
  run_started = now_microseconds();

  put_text("MODE ");
  put_u32((u32)mode);
  put_text(" size=");
  put_u32(size);
  put_text(" bank=");
  put_u32(bank);
  put_line();

  mapping = syscall6(SYS_mmap2, 0, (long)SHARED_MAP_BYTES, PROT_READ, MAP_SHARED,
                      -1, (long)(SHARED_PHYS_BASE >> 12));
  if (failed(mapping)) {
    /* Fall back to /dev/mem for the shared window. */
    long mem_fd = (int)syscall6(SYS_open, (long)"/dev/mem", (long)O_RDONLY, 0, 0, 0, 0);
    if (failed(mem_fd)) {
      put_text("ERROR open /dev/mem\n");
      return 1;
    }
    mapping = syscall6(SYS_mmap2, 0, (long)SHARED_MAP_BYTES, PROT_READ, MAP_SHARED,
                        mem_fd, (long)(SHARED_PHYS_BASE >> 12));
    (void)syscall6(SYS_close, mem_fd, 0, 0, 0, 0, 0);
  }
  if (failed(mapping)) {
    put_text("ERROR mmap shared window\n");
    return 1;
  }

  fd = (int)syscall6(SYS_open, (long)"/dev/d5_ipcc", (long)O_RDWR, 0, 0, 0, 0);
  if (failed(fd)) {
    (void)syscall6(SYS_munmap, mapping, (long)SHARED_MAP_BYTES, 0, 0, 0, 0);
    put_text("ERROR open /dev/d5_ipcc\n");
    return 1;
  }

  /* Step one: the acquire.  The body is the capture library's own twenty
   * byte record: unused, the address the RTOS fills in, the requested byte
   * count, the bank, and the free run the RTOS fills in.
   *
   * The bench takes no lease: it only measures a copy, and a lease that is
   * never given back is a leak.  The still mode takes none either: it is
   * the experiment that tells the capture apart from everything else. */
  if (mode != 2 && mode != 3 && mode != 5 && mode != 6 && !release_only) {
    u32 index;
    u32 words[LEASE_WORDS];
    words[0] = 0u;
    words[1] = 0u;
    words[2] = size;
    words[3] = bank;
    words[4] = 0u;
    for (index = 0u; index < LEASE_WORDS; ++index) {
      lease_body[index * 4u + 0u] = (u8)(words[index] & 0xffu);
      lease_body[index * 4u + 1u] = (u8)((words[index] >> 8) & 0xffu);
      lease_body[index * 4u + 2u] = (u8)((words[index] >> 16) & 0xffu);
      lease_body[index * 4u + 3u] = (u8)((words[index] >> 24) & 0xffu);
    }
    watcher_init(&watch, (const unsigned char *)(usize)mapping);
    {
      u32 attempt = 0u;
      for (;;) {
        if (write_frame(fd, CATEGORY_READ, ATTRIBUTE_IMAGE_BUFFER, lease_body,
                        (u32)sizeof(lease_body), "ACQUIRE") == 0u) {
          put_text("ACQUIRE not sent\n");
          goto done;
        }
        /* Wait for the reply that carries our own size and bank.  The RTOS
         * answers in milliseconds. */
        start = now_microseconds();
        while (seen_our_replies == 0u) {
          u32 elapsed = now_microseconds() - start;
          if (elapsed >= ACQUIRE_WAIT_MS * 1000u) {
            break;
          }
          watcher_poll(&watch, CATEGORY_RESULT, ATTRIBUTE_IMAGE_BUFFER,
                       on_lease_answer, &seen_replies);
          if (seen_our_replies == 0u && period_us) {
            sleep_microseconds(period_us);
          }
        }
        if (granted_address != 0u || attempt >= resends) {
          break;
        }
        /* Experimental retry. A missing reply does not establish allocation
         * failure or idempotence; this path needs an ownership contract. */
        attempt += 1u;
        put_text("ACQUIRE not granted yet, asking again ");
        put_u32(attempt);
        put_line();
        sleep_microseconds(1000000u);
      }
    }
    if (granted_address == 0u) {
      put_text("NO GRANT\n");
    }
    put_text("GRANTED address=");
    put_u32(granted_address);
    put_text(" handle=");
    put_u32(lease_handle);
    put_line();
  } else {
    watcher_init(&watch, (const unsigned char *)(usize)mapping);
  }

  /* A release on its own, for a block this run did not take. */
  if (release_only) {
    u8 release_body[4];
    release_body[0] = (u8)(release_address & 0xffu);
    release_body[1] = (u8)((release_address >> 8) & 0xffu);
    release_body[2] = (u8)((release_address >> 16) & 0xffu);
    release_body[3] = (u8)((release_address >> 24) & 0xffu);
    put_text("RELEASE-ONLY address=");
    put_u32(release_address);
    put_line();
    if (write_frame(fd, CATEGORY_RESULT, ATTRIBUTE_RELEASE, release_body,
                    (u32)sizeof(release_body), "RELEASE") == 0u) {
      put_text("RELEASE not sent\n");
    }
    goto done;
  }


  /* How fast can the sensor region be taken out?  This copies the region the
   * last capture used, without taking a picture, so the cost of a grab can
   * be measured on its own. */
  if (mode == 5) {
    u32 buffer = bench_address;
    u32 length = bench_length;
    u32 copied;

    if (buffer == 0u) {
      put_text("BENCH needs --bench-address\n");
      goto done;
    }
    put_text("BENCH grabbing ");
    put_u32(length / 1024u);
    put_text(" KiB from ");
    put_u32(buffer);
    put_line();
    copied = copy_region_to_memory(buffer, length);
    put_text("BENCH copied ");
    put_u32(copied);
    put_text(" bytes, low-word score ");
    put_u32(candidate_low_word_score());
    put_text(" per cent\n");
    goto done;
  }

  /* Step two: the still, and the completion that names the capture object. */
  if (mode == 4) {
    /* Keep the sensor data in the capture's block, so that the region is
     * still ours to read after the capture has finished. */
    set_ssif_raw(1);
  }
  if (mode == 1 || mode == 2 || mode == 3 || mode == 4 || mode == 6) {
    /* The body was built in the preflight, before anything was taken from
     * the device, and it is the body that goes out. */
    if (write_frame(fd, CATEGORY_STILL, ATTRIBUTE_STILL, still_body,
                    (u32)STILL_REQUEST_PAYLOAD_SIZE, "STILL") == 0u) {
      put_text("STILL not sent\n");
      goto done;
    }
    start = now_microseconds();
    while (seen_completions == 0u) {
      u32 elapsed = now_microseconds() - start;
      if (elapsed >= timeout_ms * 1000u) {
        put_text("NO COMPLETION\n");
        break;
      }
      if (past_deadline(max_seconds)) {
        put_text("DEADLINE reached, giving up on the completion\n");
        break;
      }
      watcher_poll(&watch, CATEGORY_RESULT, ATTRIBUTE_STILL, on_completion,
                   &seen_completions);
      if (mode == 3) {
        /* We asked for no buffer, so every reply on the image-buffer
         * attribute is the capture system's own request. */
        watcher_poll(&watch, CATEGORY_RESULT, ATTRIBUTE_IMAGE_BUFFER,
                     on_capture_lease, &seen_replies);
      }
      if (seen_completions == 0u && period_us) {
        sleep_microseconds(period_us);
      }
    }
    if (settle_ms) {
      u32 waited = 0u;
      while (waited < settle_ms) {
        u32 slice = (settle_ms - waited > 200u) ? 200u : (settle_ms - waited);
        sleep_microseconds(slice * 1000u);
        waited += slice;
      }
      put_text("SETTLED ");
      put_u32(settle_ms);
      put_line();
    }
    if (completion_address != 0u) {
      object_count = object_bytes / 4u;
      if (object_count > 128u) {
        object_count = 128u;
      }
      if (mode == 6) {
        /* The bare capture: the still went out, the completion came back,
         * and nothing of the RTOS's own memory is read.  If the camera
         * survives this, the capture is sound and it is the reading of the
         * RTOS's memory that upsets it. */
        put_text("STILL-ONLY completion address=");
        put_u32(completion_address);
        put_line();
      } else if (mode == 3) {
        u32 lease = follow_record(completion_address, object_bytes);
        if (copy_path != 0 && lease != 0u) {
          copy_region(lease, copy_length, copy_path);
        }
        if (dump_buffer && lease != 0u) {
          dump_buffer_head(lease, 512u);
        }
        if (scan_plane && lease != 0u) {
          scan_for_plane(lease, 0x4000000u);
        }
      } else if (mode == 4) {
        /* The deliverable's flow: the capture system gives us the record, we
         * take the sensor region out of the capture's own block into our own
         * memory, and we write it. */
        u32 record_words[64];
        u32 buffer;
        u32 region_end;
        u32 length;
        u32 copied;
        u32 fraction;

        if (read_rtos_memory(completion_address, 256u, record_words, 64u) != 0) {
          put_text("GRAB cannot read the capture record\n");
          set_ssif_raw(0);
          goto done;
        }
        buffer = record_words[0x38 / 4u];
        region_end = record_words[0x3c / 4u];
        put_text("GRAB block=");
        put_u32(buffer);
        put_text(" region_end=");
        put_u32(region_end);
        put_line();
        if (buffer == 0u || region_end <= buffer) {
          put_text("GRAB the record does not name a sensor region\n");
          set_ssif_raw(0);
          goto done;
        }
        length = region_end - buffer;
        if (length > SENSOR_REGION_CAPACITY) {
          length = SENSOR_REGION_CAPACITY;
        }
        if (no_copy) {
          /* The record is read and the region is named, but the region is
           * not touched.  This is the step that tells the capture apart
           * from the copy. */
          put_text("GRAB no-copy, the region is ");
          put_u32(length);
          put_text(" bytes\n");
          set_ssif_raw(0);
          goto done;
        }
        if (past_deadline(max_seconds)) {
          put_text("GRAB the run is out of time, not copying\n");
          set_ssif_raw(0);
          goto done;
        }
        copied = copy_region_to_memory(buffer, length);
        put_text("GRAB copied ");
        put_u32(copied);
        put_text(" bytes into our own memory\n");
        fraction = candidate_low_word_score();
        put_text("GRAB low-word score words: ");
        put_u32(fraction);
        put_text(" per cent\n");
        if (copied != length) {
          put_text("GRAB the copy is short, not writing a file\n");
          set_ssif_raw(0);
          goto done;
        }
        if (fraction < 60u) {
          put_text("GRAB the copy does not look like sensor data, not writing a file\n");
          set_ssif_raw(0);
          goto done;
        }
        write_region(copied, output_path);
        set_ssif_raw(0);
      } else if (read_rtos_memory(completion_address, object_bytes, object_words,
                                  object_count) == 0) {
        report_object(object_words, object_count);
      }
    }
  }

  /* The descriptor the RTOS published, read through our own mapping. */
  if (read_descriptor) {
    u32 words[40];
    u32 index;
    u32 slot;
    /* The RTOS stages ten descriptors in that region, one per topic, and
     * writes the one it published into its own slot. */
    for (slot = 0u; slot < 10u; ++slot) {
      if (read_rtos_memory(DESCRIPTOR_ADDRESS + slot * 0x88u, 0x88u, words,
                           40u) != 0) {
        continue;
      }
      put_text("DESCRIPTOR slot ");
      put_u32(slot);
      put_text(" +0x44 physical=");
      put_u32(words[0x44 / 4u]);
      put_text(" +0x54 virtual=");
      put_u32(words[0x54 / 4u]);
      put_text(" +0x58 virtual=");
      put_u32(words[0x58 / 4u]);
      put_text(" +0x40 type=");
      put_u32(words[0x40 / 4u]);
      put_line();
    }
    if (read_rtos_memory(DESCRIPTOR_ADDRESS, 0x88u, words, 40u) == 0) {
      for (index = 0u; index < 0x88u / 4u; ++index) {
        u32 offset = index * 4u;
        if (words[index] >= POOL_FIRST && words[index] < POOL_LAST) {
          put_text("DESCRIPTOR POOL +0x");
          put_hex_byte((offset >> 8) & 0xffu);
          put_hex_byte(offset & 0xffu);
          put_text(" = 0x");
          put_hex_byte((words[index] >> 24) & 0xffu);
          put_hex_byte((words[index] >> 16) & 0xffu);
          put_hex_byte((words[index] >> 8) & 0xffu);
          put_hex_byte(words[index] & 0xffu);
          put_line();
        }
      }
    }
    goto done;
  }

  /* The RTOS's own log ring, and a broader search of its memory. */
  if (log_dump_count != 0u) {
    if (search_needle != 0) {
      read_log_ring(search_needle, log_dump_count);
    } else {
      read_log_ring("", log_dump_count);
    }
  }
  if (search_needle != 0) {
    (void)search_text(search_needle);
    goto done;
  }

  /* Step three: give the block back.  The address is the one the RTOS
   * granted in a reply that carried our own size and bank; nothing else is
   * ever released. */
  if (mode != 2 && mode != 3 && do_release) {
    if (granted_address == 0u) {
      put_text("RELEASE skipped: nothing granted to us\n");
    } else if (granted_address < POOL_FIRST || granted_address >= POOL_LAST) {
      put_text("RELEASE refused: address outside the pool range\n");
    } else if (!address_is_ours(granted_address)) {
      put_text("RELEASE refused: the address belongs to the capture or to "
               "another request\n");
    } else {
      u8 release_body[4];
      u32 address = granted_address;
      release_body[0] = (u8)(address & 0xffu);
      release_body[1] = (u8)((address >> 8) & 0xffu);
      release_body[2] = (u8)((address >> 16) & 0xffu);
      release_body[3] = (u8)((address >> 24) & 0xffu);
      if (write_frame(fd, CATEGORY_RESULT, ATTRIBUTE_RELEASE, release_body,
                      (u32)sizeof(release_body), "RELEASE") == 0u) {
        put_text("RELEASE not sent\n");
      }
    }
  }

done:
  /* What the watcher made of the ring, so that a run says what it did
   * not get as well as what it did. */
  watcher_report(&watch);
  /* Attempt disable only after a local enable request. Original state
   * and acknowledgement remain unproved. */
  restore_ssif_raw();
  (void)syscall6(SYS_munmap, mapping, (long)SHARED_MAP_BYTES, 0, 0, 0, 0);
  (void)syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  put_text("DONE granted=");
  put_u32(granted_address);
  put_text(" replies=");
  put_u32(seen_replies);
  put_text(" completions=");
  put_u32(seen_completions);
  put_line();
  flush_output();
  return 0;
}
