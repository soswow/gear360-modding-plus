/*
 * Live UDP server for the motion history (read-only; never writes a device).
 *
 *   accelserve [port] [seconds]     defaults: 7700, 600 (then it exits)
 *
 * Any datagram received on the port registers its sender as the client; the
 * client must send one at least every 5 s (a keep-alive) or it is dropped.
 * About every 20 ms every new 20-byte sample is sent to the client as
 *   u32 first_ring_index, u32 count, then count * 20 raw bytes (little endian)
 * Sample: word0 timestamp (~1 us units), word1 0, words 2-4 six int16.
 * Fire-and-forget: lost datagrams are not retried (the client sees index gaps).
 */
#include <stddef.h>
#include <stdint.h>

#define SYS_exit 1u
#define SYS_write 4u
#define SYS_open 5u
#define SYS_close 6u
#define SYS_ioctl 54u
#define SYS_nanosleep 162u
#define SYS_mmap2 192u
#define SYS_clock_gettime 263u
#define O_RDONLY 0
#define O_RDWR 2
#define PROT_READ 1
#define MAP_SHARED 1

#define SYS_socket 281u
#define SYS_bind 282u
#define SYS_sendto 290u
#define SYS_recvfrom 292u
#define MSG_DONTWAIT 0x40
#define HISTORY_PHYS 0x9cd00000u

struct timespec32 {
  long sec;
  long nsec;
};

static inline long sys6(long nr, long a0, long a1, long a2, long a3, long a4, long a5) {
  register long r7 __asm__("r7") = nr;
  register long r0 __asm__("r0") = a0;
  register long r1 __asm__("r1") = a1;
  register long r2 __asm__("r2") = a2;
  register long r3 __asm__("r3") = a3;
  register long r4 __asm__("r4") = a4;
  register long r5 __asm__("r5") = a5;
  __asm__ volatile("svc #0"
                   : "+r"(r0)
                   : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
                   : "memory");
  return r0;
}

static int failed(long r) { return (unsigned long)r >= (unsigned long)-4095; }

#define HISTORY_SIZE 8000000u
#define SAMPLE_BYTES 20u
static uint32_t parse_dec(const char *t) {
  uint32_t v = 0;
  while (*t >= '0' && *t <= '9') v = v * 10u + (uint32_t)(*t++ - '0');
  return v;
}



struct sockaddr_in32 {
  uint16_t family;
  uint16_t port_be;
  uint32_t addr_be;
  uint8_t zero[8];
};

static uint32_t now_ms(void) {
  struct timespec32 t;
  sys6(SYS_clock_gettime, 1, (long)&t, 0, 0, 0, 0);
  return (uint32_t)t.sec * 1000u + (uint32_t)t.nsec / 1000000u;
}
static uint32_t read_pos(void) {
  int32_t value = 0;
  long fd = sys6(SYS_open, (long)"/dev/accel", O_RDWR, 0, 0, 0, 0);
  if (failed(fd)) return 0xffffffffu;
  long rc = sys6(SYS_ioctl, fd, (long)0x80046c07u, (long)&value, 0, 0, 0);
  sys6(SYS_close, fd, 0, 0, 0, 0, 0);
  return failed(rc) ? 0xffffffffu : (uint32_t)value;
}

#define MAX_PER_PACKET 50u
/* The driver advances the position before the newest slots are written (live
 * reads showed stale records from the previous lap about every 4th sample), so
 * stay this many samples behind it. */
#define LAG 10u
static uint32_t packet[2 + MAX_PER_PACKET * 5];

void c_entry(long *stack) {
  int argc = (int)stack[0];
  char **argv = (char **)(stack + 1);
  uint32_t port = argc > 1 ? parse_dec(argv[1]) : 7700u;
  uint32_t seconds = argc > 2 ? parse_dec(argv[2]) : 600u;
  long mem = sys6(SYS_open, (long)"/dev/mem", O_RDONLY, 0, 0, 0, 0);
  const volatile uint32_t *hist = 0;
  if (!failed(mem)) {
    long m = sys6(SYS_mmap2, 0, HISTORY_SIZE, PROT_READ, MAP_SHARED, mem, (long)(HISTORY_PHYS >> 12));
    if (!failed(m)) hist = (const volatile uint32_t *)m;
    sys6(SYS_close, mem, 0, 0, 0, 0, 0);
  }
  long sock = sys6(SYS_socket, 2, 2, 0, 0, 0, 0);
  struct sockaddr_in32 me = {2, (uint16_t)(((port & 0xffu) << 8) | (port >> 8)), 0, {0}};
  if (!hist || failed(sock) || failed(sys6(SYS_bind, sock, (long)&me, sizeof me, 0, 0, 0)))
    sys6(SYS_exit, 1, 0, 0, 0, 0, 0);

  const uint32_t slots = HISTORY_SIZE / SAMPLE_BYTES;
  struct sockaddr_in32 client = {0, 0, 0, {0}};
  uint32_t last_hello = 0;
  int have_client = 0;
  uint32_t last = (read_pos() + slots - LAG) % slots;
  uint32_t end = now_ms() + seconds * 1000u;
  while ((int32_t)(now_ms() - end) < 0) {
    char rx[16];
    struct sockaddr_in32 from;
    long fromlen = sizeof from;
    long got = sys6(SYS_recvfrom, sock, (long)rx, sizeof rx, MSG_DONTWAIT, (long)&from, (long)&fromlen);
    while (!failed(got)) {
      client = from;
      have_client = 1;
      last_hello = now_ms();
      fromlen = sizeof from;
      got = sys6(SYS_recvfrom, sock, (long)rx, sizeof rx, MSG_DONTWAIT, (long)&from, (long)&fromlen);
    }
    uint32_t pos = read_pos();
    if (pos != 0xffffffffu) {
      pos = (pos + slots - LAG) % slots;
      uint32_t n = (pos + slots - last) % slots;
      if (n > slots / 2u) n = 0;  /* position went backwards: skip */
      if (have_client && now_ms() - last_hello > 5000u) have_client = 0;
      while (n) {
        uint32_t k = n > MAX_PER_PACKET ? MAX_PER_PACKET : n;
        if (have_client) {
          packet[0] = last;
          packet[1] = k;
          for (uint32_t i = 0; i < k; i++) {
            const volatile uint32_t *s = hist + ((last + i) % slots) * (SAMPLE_BYTES / 4u);
            for (int w = 0; w < 5; w++) packet[2 + i * 5 + w] = s[w];
          }
          sys6(SYS_sendto, sock, (long)packet, (long)(8u + k * SAMPLE_BYTES), 0, (long)&client, sizeof client);
        }
        last = (last + k) % slots;
        n -= k;
      }
      last = pos;
    }
    struct timespec32 d = {0, 20000000};
    sys6(SYS_nanosleep, (long)&d, 0, 0, 0, 0, 0);
  }
  sys6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
