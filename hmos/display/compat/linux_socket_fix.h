/* negroni: the SDK's bionic-style <linux/socket.h> redefines struct sockaddr_storage next to musl's
 * <sys/socket.h>; keep musl's and give <linux/netlink.h> the one typedef it needs. */
#pragma once
#define _UAPI_LINUX_SOCKET_H
#define _K_SS_MAXSIZE 128
typedef unsigned short __kernel_sa_family_t;
