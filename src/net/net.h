/* net.h - Quartz networking (Watt-32 TCP/IP over a packet driver). */
#ifndef QUARTZ_NET_H
#define QUARTZ_NET_H

typedef struct {
    int  up;                 /* 1 if the stack initialized (DHCP/config ok) */
    char ip[20];
    char mask[20];
    char gw[20];
    char dns[20];
    char err[80];
} qz_netinfo;

/* Initialize Watt-32. Call this in text mode, before qz_gfx_init(), so the
 * packet driver handshake / DHCP can run (and any messages are visible).
 * Returns 0 on success. Fills *out either way. */
int qz_net_init(qz_netinfo *out);

/* Resolve a hostname to a dotted-quad string (needs DNS). 0 on success. */
int qz_net_resolve(const char *host, char *ipbuf /* >= 20 */);

#endif
