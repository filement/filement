#ifndef _IFADDRS_H
#define _IFADDRS_H

#include <sys/socket.h>

struct ifaddrs {
  struct ifaddrs  *ifa_next;
  char            *ifa_name;
  unsigned int     ifa_flags;
  struct sockaddr *ifa_addr;
  struct sockaddr *ifa_netmask;
  union {
    struct sockaddr *ifu_broadaddr;
    struct sockaddr *ifu_dstaddr;
  } ifa_ifu;
#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr   ifa_ifu.ifu_dstaddr
  void            *ifa_data;
};

extern int getifaddrs(struct ifaddrs **ifap);

extern void freeifaddrs(struct ifaddrs *ifa);

#endif
