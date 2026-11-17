#include "lan.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "platform.h"

static bf_socket_t sock = BF_INVALID_SOCKET;
static LanServer list[MAX_LAN_SERVERS];
static int nlist;

void lan_start(void)
{
    if (sock != BF_INVALID_SOCKET)
        return;
    nlist = 0;
    if (platform_net_init() != 0)
        return;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == BF_INVALID_SOCKET)
        return;
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char *)&one, sizeof one);
#endif
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = htons(DISCOVER_PORT),
                               .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("lan discover bind");
        bf_close(sock);
        sock = BF_INVALID_SOCKET;
        return;
    }
    bf_set_nonblock(sock, 1);
}

void lan_stop(void)
{
    if (sock != BF_INVALID_SOCKET) {
        bf_close(sock);
        sock = BF_INVALID_SOCKET;
    }
    nlist = 0;
}

static void note_server(const char *host, int port)
{
    long long t = bf_now_ms();
    for (int i = 0; i < nlist; i++) {
        if (list[i].port == port && !strcmp(list[i].host, host)) {
            list[i].seen_ms = t;
            return;
        }
    }
    if (nlist >= MAX_LAN_SERVERS)
        return;
    snprintf(list[nlist].host, sizeof list[nlist].host, "%s", host);
    list[nlist].port = port;
    list[nlist].seen_ms = t;
    nlist++;
}

void lan_pump(void)
{
    if (sock == BF_INVALID_SOCKET)
        return;

    for (;;) {
        char buf[128];
        struct sockaddr_in from;
        bf_socklen_t fromlen = sizeof from;
        bf_ssize_t n = recvfrom(sock, buf, (int)sizeof buf - 1, 0,
                                (struct sockaddr *)&from, &fromlen);
        if (n < 0)
            break;
        buf[n] = 0;
        char *nl = strchr(buf, '\n');
        if (nl)
            *nl = 0;

        /* BFDISC <tcp_port> */
        if (strncmp(buf, DISCOVER_MAGIC, strlen(DISCOVER_MAGIC)) != 0)
            continue;
        char *p = buf + strlen(DISCOVER_MAGIC);
        while (*p == ' ')
            p++;
        int port = (int)strtol(p, NULL, 10);
        if (port <= 0 || port > 65535)
            continue;

        char host[64];
        if (!inet_ntop(AF_INET, &from.sin_addr, host, sizeof host))
            continue;
        note_server(host, port);
    }

    long long t = bf_now_ms();
    for (int i = 0; i < nlist;) {
        if (t - list[i].seen_ms > 3500) {
            list[i] = list[nlist - 1];
            nlist--;
        } else {
            i++;
        }
    }
}

int lan_count(void) { return nlist; }

const LanServer *lan_at(int i)
{
    return (i >= 0 && i < nlist) ? &list[i] : NULL;
}
