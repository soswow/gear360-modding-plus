/*
 * Minimal UDP output for the freestanding observation tools.
 *
 * udp_open("a.b.c.d:port") creates a datagram socket aimed at that address;
 * udp_send() sends a buffer as datagrams of at most 1200 bytes. Sending never
 * blocks the caller on storage and is fire-and-forget: lost datagrams are not
 * retried. The including file must define UDP_SYSCALL6(nr, a0..a5) to its own
 * six-argument system-call helper before including this header.
 */
#ifndef UDPOUT_H
#define UDPOUT_H

#include <stdint.h>

#define UDP_SYS_socket 281u
#define UDP_SYS_sendto 290u
#define UDP_AF_INET 2
#define UDP_SOCK_DGRAM 2

struct udp_sockaddr_in {
  uint16_t family;
  uint16_t port_be;
  uint32_t addr_be;
  uint8_t zero[8];
};

static long udp_fd = -1;
static struct udp_sockaddr_in udp_dest;

/* Parses "a.b.c.d:port"; returns 0 on success. */
static int udp_open(const char *spec) {
  uint32_t parts[5] = {0, 0, 0, 0, 0};
  int part = 0;
  for (const char *t = spec; *t; t++) {
    if (*t >= '0' && *t <= '9') {
      parts[part] = parts[part] * 10u + (uint32_t)(*t - '0');
    } else if ((*t == '.' && part < 3) || (*t == ':' && part == 3)) {
      part++;
    } else {
      return -1;
    }
  }
  if (part != 4 || parts[4] == 0 || parts[4] > 65535u) return -1;
  for (int i = 0; i < 4; i++)
    if (parts[i] > 255u) return -1;
  udp_dest.family = UDP_AF_INET;
  udp_dest.port_be = (uint16_t)(((parts[4] & 0xffu) << 8) | (parts[4] >> 8));
  /* The address bytes in network order, stored little-endian. */
  udp_dest.addr_be = parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
  for (int i = 0; i < 8; i++) udp_dest.zero[i] = 0;
  long fd = UDP_SYSCALL6(UDP_SYS_socket, UDP_AF_INET, UDP_SOCK_DGRAM, 0, 0, 0, 0);
  if ((unsigned long)fd >= (unsigned long)-4095) return -1;
  udp_fd = fd;
  return 0;
}

static void udp_send(const void *data, uint32_t length) {
  if (udp_fd < 0) return;
  const char *p = (const char *)data;
  while (length) {
    uint32_t n = length > 1200u ? 1200u : length;
    UDP_SYSCALL6(UDP_SYS_sendto, udp_fd, (long)p, (long)n, 0, (long)&udp_dest, (long)sizeof udp_dest);
    p += n;
    length -= n;
  }
}

#endif
