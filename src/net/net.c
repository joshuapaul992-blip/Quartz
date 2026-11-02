/* net.c - Quartz networking via Watt-32.
 *
 * sock_init() loads the installed packet driver, runs DHCP (per WATTCP.CFG),
 * and brings up the TCP/IP stack. We then read the assigned address info from
 * Watt-32's globals. Must be called in text mode before entering graphics. */
#include "net.h"
#include <tcp.h>
#include <string.h>

int qz_net_init(qz_netinfo *out)
{
    int rc;
    memset(out, 0, sizeof *out);

    rc = sock_init();
    if (rc != 0) {
        const char *e = sock_init_err(rc);
        out->up = 0;
        strncpy(out->err, e ? e : "sock_init failed", sizeof out->err - 1);
        return rc;
    }

    out->up = 1;
    _inet_ntoa(out->ip, my_ip_addr);
    _inet_ntoa(out->mask, sin_mask);
    if (last_nameserver > 0)
        _inet_ntoa(out->dns, def_nameservers[0]);
    else
        strcpy(out->dns, "(none)");
    out->gw[0] = 0;
    return 0;
}

int qz_net_resolve(const char *host, char *ipbuf)
{
    DWORD ip = resolve(host);
    if (ip == 0) { strcpy(ipbuf, "(failed)"); return -1; }
    _inet_ntoa(ipbuf, ip);
    return 0;
}
