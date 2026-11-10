#include <stdint.h>
#include <stddef.h>

typedef unsigned int u32;
typedef unsigned long usize;

#define PAGE_SIZE 4096u
#define PROT_READ 1u
#define MAP_SHARED 1u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_exit 1u
#define SYS_munmap 91u
#define SYS_mmap2 192u
#define READ_BUFFER_SIZE 0xa0u

/*
 * ARM EABI mmap2 uses 4 KiB page-number offsets. Every target mapping is one
 * PROT_READ|MAP_SHARED page from O_RDONLY /dev/mem; offsets are mode constants.
 * Fixed modes include prior profiles plus frontend-meta, active-image,
 * allocator-meta (guarded by the fixed manager cell), two scalar reads, and
 * ipcc-map-state (guarded by a fixed root word), and peer-state (fixed receiver
 * vptr and callback-count guards), plus enc-export-state (a fixed global word
 * with matching consecutive reads), plus svc6-dispatch-state (a fixed root
 * word guarding one fixed four-byte dispatch slot), plus manager-vptr-state
 * (the fixed manager cell and factory vptr guarding one fixed four-byte
 * manager vptr word), plus profile8-fields (one fixed ordinary RAM page
 * whose object vptr word guards two fixed metadata field words). Observed
 * pointers are never followed.
 */
static const char devmem[] = "/dev/mem";
static char output[1024];

#if defined(RAM_HELPER_HOST_TEST)
#if defined(__linux__) && \
    (defined(__arm__) || defined(__aarch64__) || defined(__ARM_EABI__))
#error "RAM_HELPER_HOST_TEST cannot build as a Linux ARM syscall shim"
#endif
extern long ram_helper_test_syscall6(long nr, long a0, long a1, long a2,
                                     long a3, long a4, long a5);
#define syscall6 ram_helper_test_syscall6
/*
 * Host-only seam between the two stability reads of the svc6 dispatch slot.
 * It lets a host test model a concurrent writer between those reads. The
 * target build compiles it out, so both reads stay adjacent there.
 */
#ifdef RAM_HELPER_SLOT_HOOK
#define RAM_HELPER_SLOT_OBSERVATION() RAM_HELPER_SLOT_HOOK()
#else
#define RAM_HELPER_SLOT_OBSERVATION() ((void)0)
#endif
/*
 * Host-only seam between the two stability reads of the manager vptr word.
 * It lets a host test model a concurrent writer between those reads. The
 * target build compiles it out, so both reads stay adjacent there.
 */
#ifdef RAM_HELPER_VPTR_HOOK
#define RAM_HELPER_VPTR_OBSERVATION() RAM_HELPER_VPTR_HOOK()
#else
#define RAM_HELPER_VPTR_OBSERVATION() ((void)0)
#endif
/*
 * Host-only seam between the two stability reads of the profile8 field pair.
 * It lets a host test model a concurrent writer between those reads. The
 * target build compiles it out, so both reads stay adjacent there.
 */
#ifdef RAM_HELPER_PROFILE8_HOOK
#define RAM_HELPER_PROFILE8_OBSERVATION() RAM_HELPER_PROFILE8_HOOK()
#else
#define RAM_HELPER_PROFILE8_OBSERVATION() ((void)0)
#endif
#else
#define RAM_HELPER_SLOT_OBSERVATION() ((void)0)
#define RAM_HELPER_VPTR_OBSERVATION() ((void)0)
#define RAM_HELPER_PROFILE8_OBSERVATION() ((void)0)
static inline long syscall6(long nr, long a0, long a1, long a2,
                            long a3, long a4, long a5) {
  register long r0 __asm__("r0") = a0;
  register long r1 __asm__("r1") = a1;
  register long r2 __asm__("r2") = a2;
  register long r3 __asm__("r3") = a3;
  register long r4 __asm__("r4") = a4;
  register long r5 __asm__("r5") = a5;
  register long r7 __asm__("r7") = nr;
  __asm__ volatile("svc #0"
                   : "+r"(r0)
                   : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5),
                     "r"(r7)
                   : "memory", "cc");
  return r0;
}
#endif

static int failed(long r) {
  return (usize)r >= (usize)-4095;
}

static void append_char(usize *n, char c) {
  if (*n < sizeof(output)) output[(*n)++] = c;
}

static void append_text(usize *n, const char *s) {
  while (*s) append_char(n, *s++);
}

static void append_hex(usize *n, u32 v) {
  static const char digits[] = "0123456789abcdef";
  append_text(n, "0x");
  for (int shift = 28; shift >= 0; shift -= 4)
    append_char(n, digits[(v >> shift) & 15u]);
}

static void append_byte(usize *n, unsigned char b) {
  static const char digits[] = "0123456789abcdef";
  append_char(n, digits[(b >> 4) & 15u]);
  append_char(n, digits[b & 15u]);
}

static void append_bytes_line(usize *n, u32 address,
                              const unsigned char *bytes, usize count) {
  append_text(n, "physical-offset ");
  append_hex(n, address);
  append_text(n, " bytes:");
  for (usize i = 0; i < count; ++i) {
    append_char(n, ' ');
    append_byte(n, bytes[i]);
  }
  append_char(n, '\n');
}

static long write_all(const char *p, usize n) {
  while (n) {
    long r = syscall6(SYS_write, 1, (long)p, (long)n, 0, 0, 0);
    if (failed(r) || r == 0) return failed(r) ? r : -5;
    p += (usize)r;
    n -= (usize)r;
  }
  return 0;
}

static void append_error(usize *n, const char *op, u32 address, long err) {
  append_text(n, "ERROR ");
  append_text(n, op);
  append_text(n, " physical-offset ");
  append_hex(n, address);
  append_text(n, " errno=");
  append_hex(n, (u32)-err);
  append_char(n, '\n');
}

static long map_page(int fd, u32 address) {
  return syscall6(SYS_mmap2, 0, PAGE_SIZE, PROT_READ, MAP_SHARED, fd,
                  (long)(address >> 12));
}

static long unmap_page(long mapped) {
  return syscall6(SYS_munmap, mapped, PAGE_SIZE, 0, 0, 0, 0);
}

static void copy_from_page(unsigned char *dst, long mapped, u32 offset,
                           usize count) {
  volatile const unsigned char *src =
      (volatile const unsigned char *)(usize)mapped + offset;
  for (usize i = 0; i < count; ++i) dst[i] = src[i];
}

static u32 read_u32_volatile(long mapped, u32 offset) {
  volatile const u32 *src =
      (volatile const u32 *)((usize)mapped + offset);
  return *src;
}

static void store_le32(unsigned char *dst, u32 value) {
  dst[0] = (unsigned char)value;
  dst[1] = (unsigned char)(value >> 8);
  dst[2] = (unsigned char)(value >> 16);
  dst[3] = (unsigned char)(value >> 24);
}

static u32 load_le32(const unsigned char *src) {
  return (u32)src[0] | ((u32)src[1] << 8) |
         ((u32)src[2] << 16) | ((u32)src[3] << 24);
}

static void append_value_error(usize *n, const char *name, u32 address,
                               u32 expected, u32 actual) {
  append_text(n, "ERROR ");
  append_text(n, name);
  append_text(n, " physical-offset ");
  append_hex(n, address);
  append_text(n, " expected=");
  append_hex(n, expected);
  append_text(n, " actual=");
  append_hex(n, actual);
  append_char(n, '\n');
}

static int same(const char *a, const char *b) {
  usize i = 0;
  while (a[i] && b[i] && a[i] == b[i]) ++i;
  return a[i] == 0 && b[i] == 0;
}

/* Only these fixed modes and compile-time physical offsets are accepted. */
int c_entry(usize *initial_stack) {
  int argc = (int)initial_stack[0];
  char **argv = (char **)(initial_stack + 1);
  enum { INVALID, SIGNATURE, COPY_SIGNATURE, TEXT_SIGNATURE, GLOBALS,
         SETTINGS10, DEBUG_IMAGE, FRONTEND_META, ACTIVE_IMAGE, LIVE_CELLS,
         ALLOCATOR_META, ENC_RING_STATE, IPCC_MAP_STATE, PEER_STATE,
         ENC_EXPORT_STATE, SVC6_DISPATCH_STATE, MANAGER_VPTR_STATE,
         PROFILE8_FIELDS } mode = INVALID;
  if (argc == 2 && argv[1]) {
    if (same(argv[1], "signature")) mode = SIGNATURE;
    else if (same(argv[1], "copy-signature")) mode = COPY_SIGNATURE;
    else if (same(argv[1], "text-signature")) mode = TEXT_SIGNATURE;
    else if (same(argv[1], "globals")) mode = GLOBALS;
    else if (same(argv[1], "settings10")) mode = SETTINGS10;
    else if (same(argv[1], "debug-image")) mode = DEBUG_IMAGE;
    else if (same(argv[1], "frontend-meta")) mode = FRONTEND_META;
    else if (same(argv[1], "active-image")) mode = ACTIVE_IMAGE;
    else if (same(argv[1], "live-cells")) mode = LIVE_CELLS;
    else if (same(argv[1], "allocator-meta")) mode = ALLOCATOR_META;
    else if (same(argv[1], "enc-ring-state")) mode = ENC_RING_STATE;
    else if (same(argv[1], "ipcc-map-state")) mode = IPCC_MAP_STATE;
    else if (same(argv[1], "peer-state")) mode = PEER_STATE;
    else if (same(argv[1], "enc-export-state")) mode = ENC_EXPORT_STATE;
    else if (same(argv[1], "svc6-dispatch-state")) mode = SVC6_DISPATCH_STATE;
    else if (same(argv[1], "manager-vptr-state")) mode = MANAGER_VPTR_STATE;
    else if (same(argv[1], "profile8-fields")) mode = PROFILE8_FIELDS;
  }
  if (mode == INVALID) {
    static const char usage[] =
        "ERROR: expected exactly one mode: signature | copy-signature | text-signature | globals | settings10 | debug-image | frontend-meta | active-image | live-cells | allocator-meta | enc-ring-state | ipcc-map-state | peer-state | enc-export-state | svc6-dispatch-state | manager-vptr-state | profile8-fields\n";
    (void)write_all(usage, sizeof(usage) - 1);
    return 2;
  }

  long fd = syscall6(SYS_open, (long)devmem, 0, 0, 0, 0, 0);
  if (failed(fd)) {
    usize n = 0;
    append_error(&n, "open /dev/mem", 0, fd);
    (void)write_all(output, n);
    return 1;
  }

  usize n = 0;
  int status = 0;
  int emit_enc_ring_records = 0;
  int emit_ipcc_records = 0;
  int emit_peer_records = 0;
  int emit_export_record = 0;
  int emit_svc6_record = 0;
  int emit_manager_vptr_record = 0;
  int emit_profile8_record = 0;
  unsigned char bytes[READ_BUFFER_SIZE];
  if (mode == SIGNATURE || mode == COPY_SIGNATURE || mode == TEXT_SIGNATURE) {
    u32 address = mode == SIGNATURE ? 0x80000000u :
                  mode == COPY_SIGNATURE ? 0x81006000u : 0x80541000u;
    long page = map_page((int)fd, address);
    if (failed(page)) {
      append_error(&n, "mmap2", address, page);
      status = 1;
    } else {
      copy_from_page(bytes, page, 0, 64);
      long unmap_result = unmap_page(page);
      if (failed(unmap_result)) {
        append_error(&n, "munmap", address, unmap_result);
        status = 1;
      } else {
        append_bytes_line(&n, address, bytes, 64);
      }
    }
  } else if (mode == SETTINGS10 || mode == DEBUG_IMAGE) {
    const u32 address = mode == SETTINGS10 ? 0x81882968u : 0x81883f30u;
    const usize count = mode == SETTINGS10 ? 100u : 0x90u;
    long page = map_page((int)fd, address);
    if (failed(page)) {
      append_error(&n, "mmap2", address, page);
      status = 1;
    } else {
      copy_from_page(bytes, page, address & (PAGE_SIZE - 1), count);
      long unmap_result = unmap_page(page);
      if (failed(unmap_result)) {
        append_error(&n, "munmap", address, unmap_result);
        status = 1;
      } else {
        append_bytes_line(&n, address, bytes, count);
      }
    }
  } else if (mode == FRONTEND_META) {
    const u32 guard_page_address = 0x81952000u;
    const u32 target_page_address = 0x81d7f000u;
    const u32 guard_offset = 0xfc0u;
    const u32 expected_global = 0x81d7f800u;
    const u32 image_address = 0x81d7f800u;
    const u32 tail_address = 0x81d7fa80u;
    long guard_page = map_page((int)fd, guard_page_address);
    if (failed(guard_page)) {
      append_error(&n, "mmap2", guard_page_address, guard_page);
      status = 1;
    } else {
      u32 before = read_u32_volatile(guard_page, guard_offset);
      long target_page = -1;
      if (before != expected_global) {
        append_value_error(&n, "frontend-global", 0x81952fc0u,
                           expected_global, before);
        status = 1;
      } else {
        target_page = map_page((int)fd, target_page_address);
        if (failed(target_page)) {
          append_error(&n, "mmap2", target_page_address, target_page);
          status = 1;
        } else {
          copy_from_page(bytes, target_page, 0x800u, 0x80u);
          copy_from_page(bytes + 0x80, target_page, 0xa80u, 4u);
          u32 vptr = read_u32_volatile(target_page, 0x800u);
          u32 after = read_u32_volatile(guard_page, guard_offset);
          if (vptr != 0x806718a0u) {
            append_value_error(&n, "frontend-vptr", image_address,
                               0x806718a0u, vptr);
            status = 1;
          }
          if (after != expected_global) {
            append_value_error(&n, "frontend-global-changed", 0x81952fc0u,
                               expected_global, after);
            status = 1;
          }
        }
      }
      if (!failed(target_page)) {
        long r = unmap_page(target_page);
        if (failed(r)) {
          append_error(&n, "munmap", target_page_address, r);
          status = 1;
        }
      }
      long r = unmap_page(guard_page);
      if (failed(r)) {
        append_error(&n, "munmap", guard_page_address, r);
        status = 1;
      }
      if (!status) {
        append_bytes_line(&n, image_address, bytes, 0x80u);
        append_bytes_line(&n, tail_address, bytes + 0x80, 4u);
      }
    }
  } else if (mode == ACTIVE_IMAGE) {
    const u32 guard_page_address = 0x81952000u;
    const u32 target_page_address = 0x82246000u;
    const u32 a_offset = 0xe50u;
    const u32 b_offset = 0xfa4u;
    const u32 a_address = 0x81952e50u;
    const u32 b_address = 0x81952fa4u;
    const u32 expected_global = 0x82246000u;
    long guard_page = map_page((int)fd, guard_page_address);
    if (failed(guard_page)) {
      append_error(&n, "mmap2", guard_page_address, guard_page);
      status = 1;
    } else {
      u32 a_before = read_u32_volatile(guard_page, a_offset);
      u32 b_before = read_u32_volatile(guard_page, b_offset);
      long target_page = -1;
      if (a_before != expected_global || b_before != expected_global) {
        if (a_before != expected_global)
          append_value_error(&n, "active-image-A", a_address,
                             expected_global, a_before);
        if (b_before != expected_global)
          append_value_error(&n, "active-image-B", b_address,
                             expected_global, b_before);
        status = 1;
      } else {
        target_page = map_page((int)fd, target_page_address);
        if (failed(target_page)) {
          append_error(&n, "mmap2", target_page_address, target_page);
          status = 1;
        } else {
          copy_from_page(bytes, target_page, 0, 0xa0u);
          u32 vptr = read_u32_volatile(target_page, 0);
          u32 a_after = read_u32_volatile(guard_page, a_offset);
          u32 b_after = read_u32_volatile(guard_page, b_offset);
          if (vptr != 0x8067a158u) {
            append_value_error(&n, "active-image-vptr", target_page_address,
                               0x8067a158u, vptr);
            status = 1;
          }
          if (a_after != expected_global) {
            append_value_error(&n, "active-image-A-changed", a_address,
                               expected_global, a_after);
            status = 1;
          }
          if (b_after != expected_global) {
            append_value_error(&n, "active-image-B-changed", b_address,
                               expected_global, b_after);
            status = 1;
          }
        }
      }
      if (!failed(target_page)) {
        long r = unmap_page(target_page);
        if (failed(r)) {
          append_error(&n, "munmap", target_page_address, r);
          status = 1;
        }
      }
      long r = unmap_page(guard_page);
      if (failed(r)) {
        append_error(&n, "munmap", guard_page_address, r);
        status = 1;
      }
      if (!status) append_bytes_line(&n, target_page_address, bytes, 0xa0u);
    }
  } else if (mode == LIVE_CELLS) {
    const u32 page_addresses[] = {0x81952000u, 0x81c0d000u};
    const u32 cell_addresses[] = {0x81952d38u, 0x81c0d7e0u};
    const u32 cell_offsets[] = {0xd38u, 0x7e0u};
    long pages[2] = {-1, -1};
    u32 values[2];
    for (usize p = 0; p < 2; ++p) {
      pages[p] = map_page((int)fd, page_addresses[p]);
      if (failed(pages[p])) {
        append_error(&n, "mmap2", page_addresses[p], pages[p]);
        status = 1;
        break;
      }
    }
    if (!status) {
      for (usize i = 0; i < 2; ++i) {
        usize page_index = i;
        values[i] = read_u32_volatile(pages[page_index], cell_offsets[i]);
        store_le32(bytes + i * 4, values[i]);
      }
    }
    for (int p = 1; p >= 0; --p) {
      if (!failed(pages[p])) {
        long r = unmap_page(pages[p]);
        if (failed(r)) {
          append_error(&n, "munmap", page_addresses[p], r);
          status = 1;
        }
      }
    }
    if (!status) {
      append_bytes_line(&n, cell_addresses[0], bytes, 4u);
      append_bytes_line(&n, cell_addresses[1], bytes + 4, 4u);
    }
  } else if (mode == ALLOCATOR_META) {
    const u32 manager_cell_page = 0x81c0d000u;
    const u32 manager_cell_offset = 0x7e0u;
    const u32 expected_manager = 0x81cbb000u;
    const u32 metadata_page = 0x81d23000u;
    const u32 metadata_address = 0x81d230b0u;
    const u32 expected_arena_base = 0xa6000000u;
    long manager_page = map_page((int)fd, manager_cell_page);
    if (failed(manager_page)) {
      append_error(&n, "mmap2", manager_cell_page, manager_page);
      status = 1;
    } else {
      u32 before = read_u32_volatile(manager_page, manager_cell_offset);
      long metadata_map = -1;
      if (before != expected_manager) {
        append_value_error(&n, "allocator-manager", 0x81c0d7e0u,
                           expected_manager, before);
        status = 1;
      } else {
        metadata_map = map_page((int)fd, metadata_page);
        if (failed(metadata_map)) {
          append_error(&n, "mmap2", metadata_page, metadata_map);
          status = 1;
        } else {
          copy_from_page(bytes, metadata_map, 0xb0u, 40u);
          u32 arena_base = load_le32(bytes);
          u32 after = read_u32_volatile(manager_page, manager_cell_offset);
          if (arena_base != expected_arena_base) {
            append_value_error(&n, "allocator-arena-base", metadata_address,
                               expected_arena_base, arena_base);
            status = 1;
          }
          if (after != expected_manager) {
            append_value_error(&n, "allocator-manager-changed", 0x81c0d7e0u,
                               expected_manager, after);
            status = 1;
          }
        }
      }
      if (!failed(metadata_map)) {
        long r = unmap_page(metadata_map);
        if (failed(r)) {
          append_error(&n, "munmap", metadata_page, r);
          status = 1;
        }
      }
      long r = unmap_page(manager_page);
      if (failed(r)) {
        append_error(&n, "munmap", manager_cell_page, r);
        status = 1;
      }
      if (!status) append_bytes_line(&n, metadata_address, bytes, 40u);
    }
  } else if (mode == ENC_EXPORT_STATE) {
    const u32 address = 0x81482ea4u;
    long page = map_page((int)fd, address);
    if (failed(page)) {
      append_error(&n, "mmap2", address, page);
      status = 1;
    } else {
      u32 before = read_u32_volatile(page, 0xea4u);
      store_le32(bytes, before);
      u32 after = read_u32_volatile(page, 0xea4u);
      if (after != before) {
        append_value_error(&n, "enc-export-word-changed", address, before,
                           after);
        status = 1;
      }
      long r = unmap_page(page);
      if (failed(r)) {
        append_error(&n, "munmap", address, r);
        status = 1;
      }
      if (!status) emit_export_record = 1;
    }
  } else if (mode == SVC6_DISPATCH_STATE) {
    /*
     * REA/Ghidra on the pinned RTOS image: the vector-table root literal at
     * 80000138 holds 81004000, and the static registration at 8002386c
     * composes base 81004000 with target 80023fc0 and stores it at base+0x18.
     * The root word is only a guard that the data page is the expected one;
     * it does not prove an active CPU vector, handler or SCTLR state.
     */
    const u32 guard_page_address = 0x80000000u;
    const u32 guard_offset = 0x138u;
    const u32 guard_address = 0x80000138u;
    const u32 expected_root = 0x81004000u;
    const u32 data_page_address = 0x81004000u;
    const u32 data_offset = 0x18u;
    const u32 data_address = 0x81004018u;
    long guard_page = map_page((int)fd, guard_page_address);
    long data_page = -1;
    if (failed(guard_page)) {
      append_error(&n, "mmap2", guard_page_address, guard_page);
      status = 1;
    } else {
      u32 before = read_u32_volatile(guard_page, guard_offset);
      if (before != expected_root) {
        append_value_error(&n, "svc6-root", guard_address, expected_root,
                           before);
        status = 1;
      } else {
        data_page = map_page((int)fd, data_page_address);
        if (failed(data_page)) {
          append_error(&n, "mmap2", data_page_address, data_page);
          status = 1;
        } else {
          u32 slot = read_u32_volatile(data_page, data_offset);
          RAM_HELPER_SLOT_OBSERVATION();
          u32 slot_again = read_u32_volatile(data_page, data_offset);
          u32 after = read_u32_volatile(guard_page, guard_offset);
          store_le32(bytes, slot);
          if (slot_again != slot) {
            append_value_error(&n, "svc6-slot-changed", data_address, slot,
                               slot_again);
            status = 1;
          }
          if (after != expected_root) {
            append_value_error(&n, "svc6-root-changed", guard_address,
                               expected_root, after);
            status = 1;
          }
        }
      }
      if (!failed(data_page)) {
        long r = unmap_page(data_page);
        if (failed(r)) {
          append_error(&n, "munmap", data_page_address, r);
          status = 1;
        }
      }
      long r = unmap_page(guard_page);
      if (failed(r)) {
        append_error(&n, "munmap", guard_page_address, r);
        status = 1;
      }
      if (!status) emit_svc6_record = 1;
    }
  } else if (mode == MANAGER_VPTR_STATE) {
    /*
     * REA/Ghidra on the pinned RTOS image (sha256 cfe69efe...): 0x8054a744
     * constructs and returns the fixed factory at 0x81c0d7bc, 0x800a0d04
     * writes that factory's vptr 0x80626500, 0x800a0f74 returns factory+0x24
     * and 0x80086b20 uses it as the memory manager. The manager cell at
     * 0x81c0d7e0 must match the previously observed 0x81cbb000, the same
     * fixed manager the allocator-meta profile already guards. These are fixed source coordinates in the
     * pinned image, not a statement about a live object.
     */
    const u32 guard_page_address = 0x81c0d000u;
    const u32 manager_cell_offset = 0x7e0u; /* physical 0x81c0d7e0 */
    const u32 factory_vptr_offset = 0x7bcu; /* physical 0x81c0d7bc */
    const u32 manager_cell_address = 0x81c0d7e0u;
    const u32 factory_vptr_address = 0x81c0d7bcu;
    const u32 expected_manager = 0x81cbb000u;
    const u32 expected_factory_vptr = 0x80626500u;
    const u32 data_page_address = 0x81cbb000u;
    const u32 data_address = 0x81cbb000u;
    long guard_page = map_page((int)fd, guard_page_address);
    long data_page = -1;
    if (failed(guard_page)) {
      append_error(&n, "mmap2", guard_page_address, guard_page);
      status = 1;
    } else {
      u32 manager_before = read_u32_volatile(guard_page, manager_cell_offset);
      u32 factory_before = read_u32_volatile(guard_page, factory_vptr_offset);
      if (manager_before != expected_manager) {
        append_value_error(&n, "manager-cell", manager_cell_address,
                           expected_manager, manager_before);
        status = 1;
      }
      if (factory_before != expected_factory_vptr) {
        append_value_error(&n, "factory-vptr", factory_vptr_address,
                           expected_factory_vptr, factory_before);
        status = 1;
      }
      if (!status) {
        data_page = map_page((int)fd, data_page_address);
        if (failed(data_page)) {
          append_error(&n, "mmap2", data_page_address, data_page);
          status = 1;
        } else {
          /* The manager vptr word is observed as a value, never followed. */
          u32 vptr = read_u32_volatile(data_page, 0);
          RAM_HELPER_VPTR_OBSERVATION();
          u32 vptr_again = read_u32_volatile(data_page, 0);
          u32 manager_after = read_u32_volatile(guard_page, manager_cell_offset);
          u32 factory_after = read_u32_volatile(guard_page, factory_vptr_offset);
          store_le32(bytes, vptr);
          if (vptr_again != vptr) {
            append_value_error(&n, "manager-vptr-changed", data_address, vptr,
                               vptr_again);
            status = 1;
          }
          if (manager_after != expected_manager) {
            append_value_error(&n, "manager-cell-changed", manager_cell_address,
                               expected_manager, manager_after);
            status = 1;
          }
          if (factory_after != expected_factory_vptr) {
            append_value_error(&n, "factory-vptr-changed", factory_vptr_address,
                               expected_factory_vptr, factory_after);
            status = 1;
          }
        }
      }
      if (!failed(data_page)) {
        long r = unmap_page(data_page);
        if (failed(r)) {
          append_error(&n, "munmap", data_page_address, r);
          status = 1;
        }
      }
      long r = unmap_page(guard_page);
      if (failed(r)) {
        append_error(&n, "munmap", guard_page_address, r);
        status = 1;
      }
      if (!status) emit_manager_vptr_record = 1;
    }
  } else if (mode == PROFILE8_FIELDS) {
    /*
     * Parent-reviewed Ghidra binding in the pinned RTOS image: getter
     * 8020ca7c returns the object word at 8188257c, ctor 8020c9bc stores the
     * vptr 80670310 there, the profile-record array starts at object+4 with
     * 100-byte records, and the two source fields sit at object+8+8*100
     * (818828a4) and object+12+8*100 (818828a8). Guard and fields share the
     * one ordinary RAM page 81882000. These are metadata fields: they are not
     * proven live pixel dimensions, samples, freshness or ownership, and no
     * observed pointer is ever followed.
     */
    const u32 page_address = 0x81882000u;
    const u32 guard_offset = 0x57cu;  /* physical 0x8188257c */
    const u32 field0_offset = 0x8a4u; /* physical 0x818828a4 */
    const u32 field1_offset = 0x8a8u; /* physical 0x818828a8 */
    const u32 guard_address = 0x8188257cu;
    const u32 field0_address = 0x818828a4u;
    const u32 field1_address = 0x818828a8u;
    const u32 expected_guard = 0x80670310u;
    long page = map_page((int)fd, page_address);
    if (failed(page)) {
      append_error(&n, "mmap2", page_address, page);
      status = 1;
    } else {
      /* The guard word must hold before any data word is read. */
      u32 guard_before = read_u32_volatile(page, guard_offset);
      if (guard_before != expected_guard) {
        append_value_error(&n, "profile8-guard", guard_address,
                           expected_guard, guard_before);
        status = 1;
      } else {
        u32 field0_before = read_u32_volatile(page, field0_offset);
        u32 field1_before = read_u32_volatile(page, field1_offset);
        RAM_HELPER_PROFILE8_OBSERVATION();
        u32 field0_after = read_u32_volatile(page, field0_offset);
        u32 field1_after = read_u32_volatile(page, field1_offset);
        /* The guard word must still hold after the data words are read. */
        u32 guard_after = read_u32_volatile(page, guard_offset);
        store_le32(bytes, field0_before);
        store_le32(bytes + 4u, field1_before);
        if (field0_after != field0_before) {
          append_value_error(&n, "profile8-field0-changed", field0_address,
                             field0_before, field0_after);
          status = 1;
        }
        if (field1_after != field1_before) {
          append_value_error(&n, "profile8-field1-changed", field1_address,
                             field1_before, field1_after);
          status = 1;
        }
        if (guard_after != expected_guard) {
          append_value_error(&n, "profile8-guard-changed", guard_address,
                             expected_guard, guard_after);
          status = 1;
        }
      }
      long r = unmap_page(page);
      if (failed(r)) {
        append_error(&n, "munmap", page_address, r);
        status = 1;
      }
      if (!status) emit_profile8_record = 1;
    }
  } else if (mode == PEER_STATE) {
    const u32 receiver_page_address = 0x81476000u;
    const u32 peer_page_address = 0x81483000u;
    const u32 expected_vptr = 0x805ed2c0u;
    long receiver_page = map_page((int)fd, receiver_page_address);
    long peer_page = -1;
    if (failed(receiver_page)) {
      append_error(&n, "mmap2", receiver_page_address, receiver_page);
      status = 1;
    } else {
      u32 before = read_u32_volatile(receiver_page, 0x5e8u);
      if (before != expected_vptr) {
        append_value_error(&n, "receiver-vptr", 0x814765e8u,
                           expected_vptr, before);
        status = 1;
      } else {
        peer_page = map_page((int)fd, peer_page_address);
        if (failed(peer_page)) {
          append_error(&n, "mmap2", peer_page_address, peer_page);
          status = 1;
        } else {
          /* Four fixed callback slots plus count, not pointer traversal. */
          copy_from_page(bytes, receiver_page, 0x60cu, 20u);
          copy_from_page(bytes + 20u, receiver_page, 0x8d4u, 1u);
          copy_from_page(bytes + 21u, peer_page, 0x254u, 1u);
          u32 count = load_le32(bytes + 16u);
          u32 count_after = read_u32_volatile(receiver_page, 0x61cu);
          u32 after = read_u32_volatile(receiver_page, 0x5e8u);
          if (count > 4u) {
            append_value_error(&n, "category10-count-limit", 0x8147661cu,
                               4u, count);
            status = 1;
          }
          if (count_after != count) {
            append_value_error(&n, "category10-count-changed", 0x8147661cu,
                               count, count_after);
            status = 1;
          }
          if (after != expected_vptr) {
            append_value_error(&n, "receiver-vptr-changed", 0x814765e8u,
                               expected_vptr, after);
            status = 1;
          }
        }
      }
      if (!failed(peer_page)) {
        long r = unmap_page(peer_page);
        if (failed(r)) {
          append_error(&n, "munmap", peer_page_address, r);
          status = 1;
        }
      }
      long r = unmap_page(receiver_page);
      if (failed(r)) {
        append_error(&n, "munmap", receiver_page_address, r);
        status = 1;
      }
      if (!status) emit_peer_records = 1;
    }
  } else if (mode == IPCC_MAP_STATE) {
    const u32 guard_page_address = 0x80008000u;
    const u32 guard_address = 0x80008218u;
    const u32 guard_offset = 0x218u;
    const u32 data_page_address = 0x81000000u;
    const u32 expected_root = 0x81000000u;
    long guard_page = map_page((int)fd, guard_page_address);
    long data_page = -1;
    if (failed(guard_page)) {
      append_error(&n, "mmap2", guard_page_address, guard_page);
      status = 1;
    } else {
      u32 before = read_u32_volatile(guard_page, guard_offset);
      if (before != expected_root) {
        append_value_error(&n, "ipcc-root", guard_address, expected_root,
                           before);
        status = 1;
      } else {
        data_page = map_page((int)fd, data_page_address);
        if (failed(data_page)) {
          append_error(&n, "mmap2", data_page_address, data_page);
          status = 1;
        } else {
          copy_from_page(bytes, data_page, 0x80u, 4u);
          copy_from_page(bytes + 4u, data_page, 0x180u, 4u);
          u32 after = read_u32_volatile(guard_page, guard_offset);
          if (after != expected_root) {
            append_value_error(&n, "ipcc-root-changed", guard_address,
                               expected_root, after);
            status = 1;
          }
        }
      }
      if (!failed(data_page)) {
        long r = unmap_page(data_page);
        if (failed(r)) {
          append_error(&n, "munmap", data_page_address, r);
          status = 1;
        }
      }
      long r = unmap_page(guard_page);
      if (failed(r)) {
        append_error(&n, "munmap", guard_page_address, r);
        status = 1;
      }
      if (!status) emit_ipcc_records = 1;
    }
  } else if (mode == ENC_RING_STATE) {
    const u32 cursor_page_address = 0x85401000u;
    const u32 cursor_address = 0x85401c50u;
    const u32 ready_page_address = 0x85434000u;
    const u32 cursor_offset = 0xc50u;
    const u32 ready_offset = 0x2adu;
    unsigned char cursor_after[2];
    long cursor_page = map_page((int)fd, cursor_page_address);
    long ready_page = -1;
    if (failed(cursor_page)) {
      append_error(&n, "mmap2", cursor_page_address, cursor_page);
      status = 1;
    } else {
      copy_from_page(bytes, cursor_page, cursor_offset, 2u);
      ready_page = map_page((int)fd, ready_page_address);
      if (failed(ready_page)) {
        append_error(&n, "mmap2", ready_page_address, ready_page);
        status = 1;
      } else {
        copy_from_page(bytes + 2u, ready_page, ready_offset, 25u);
        copy_from_page(cursor_after, cursor_page, cursor_offset, 2u);
        if (cursor_after[0] != bytes[0] || cursor_after[1] != bytes[1]) {
          u32 before = (u32)bytes[0] | ((u32)bytes[1] << 8);
          u32 after = (u32)cursor_after[0] | ((u32)cursor_after[1] << 8);
          append_value_error(&n, "enc-ring-cursors-changed", cursor_address,
                             before, after);
          status = 1;
        }
      }
    }
    if (!failed(ready_page)) {
      long r = unmap_page(ready_page);
      if (failed(r)) {
        append_error(&n, "munmap", ready_page_address, r);
        status = 1;
      }
    }
    if (!failed(cursor_page)) {
      long r = unmap_page(cursor_page);
      if (failed(r)) {
        append_error(&n, "munmap", cursor_page_address, r);
        status = 1;
      }
    }
    if (!status) emit_enc_ring_records = 1;
  } else {
    static const u32 page_addresses[] = {0x81884000u, 0x81952000u};
    static const u32 cell_addresses[] = {
        0x818843b0u, 0x818843b4u, 0x818843b8u,
        0x81952fc0u, 0x81952fc4u, 0x81952e50u, 0x81952fa4u};
    long pages[2] = {-1, -1};
    unsigned char cells[7][4];
    for (usize p = 0; p < 2; ++p) {
      pages[p] = map_page((int)fd, page_addresses[p]);
      if (failed(pages[p])) {
        append_error(&n, "mmap2", page_addresses[p], pages[p]);
        status = 1;
        break;
      }
    }
    if (!status) {
      for (usize i = 0; i < 7; ++i) {
        usize page_index = (cell_addresses[i] & 0xfffff000u) == page_addresses[0] ? 0 : 1;
        copy_from_page(cells[i], pages[page_index],
                       cell_addresses[i] & (PAGE_SIZE - 1), 4);
      }
      for (int p = 1; p >= 0; --p) {
        long unmap_result = unmap_page(pages[p]);
        if (failed(unmap_result)) {
          append_error(&n, "munmap", page_addresses[p], unmap_result);
          status = 1;
        }
      }
      if (!status) {
        for (usize i = 0; i < 7; ++i)
          append_bytes_line(&n, cell_addresses[i], cells[i], 4);
      }
    } else {
      for (int p = 1; p >= 0; --p)
        if (!failed(pages[p])) (void)unmap_page(pages[p]);
    }
  }

  long close_result = syscall6(SYS_close, fd, 0, 0, 0, 0, 0);
  if (failed(close_result)) {
    append_text(&n, "ERROR close /dev/mem errno=");
    append_hex(&n, (u32)-close_result);
    append_char(&n, '\n');
    status = 1;
  }
  if (mode == ENC_RING_STATE && emit_enc_ring_records && !status) {
    append_bytes_line(&n, 0x85401c50u, bytes, 2u);
    append_bytes_line(&n, 0x854342adu, bytes + 2u, 25u);
  }
  if (mode == IPCC_MAP_STATE && emit_ipcc_records && !status) {
    append_bytes_line(&n, 0x81000080u, bytes, 4u);
    append_bytes_line(&n, 0x81000180u, bytes + 4u, 4u);
  }
  if (mode == PEER_STATE && emit_peer_records && !status) {
    append_bytes_line(&n, 0x8147660cu, bytes, 20u);
    append_bytes_line(&n, 0x814768d4u, bytes + 20u, 1u);
    append_bytes_line(&n, 0x81483254u, bytes + 21u, 1u);
  }
  if (mode == ENC_EXPORT_STATE && emit_export_record && !status)
    append_bytes_line(&n, 0x81482ea4u, bytes, 4u);
  if (mode == SVC6_DISPATCH_STATE && emit_svc6_record && !status)
    append_bytes_line(&n, 0x81004018u, bytes, 4u);
  if (mode == MANAGER_VPTR_STATE && emit_manager_vptr_record && !status)
    append_bytes_line(&n, 0x81cbb000u, bytes, 4u);
  if (mode == PROFILE8_FIELDS && emit_profile8_record && !status)
    append_bytes_line(&n, 0x818828a4u, bytes, 8u);
  if (n && failed(write_all(output, n))) status = 1;
  return status;
}
