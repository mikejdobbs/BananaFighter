/*
 * network.c
 * Client networking and prediction:
 * - TCP: connect, read line-oriented protocol (Y/A/U/L/S), and send commands
 * - UDP: keepalive/hello, input streaming with sequence numbers, and snapshot drain
 * - Client-side prediction: reconcile to snapshots and replay unacked inputs
 */

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include "crypto_session.h"
#include "client.h"

int networkSocketFd = -1;  /* TCP socket to the server */
int me = -1;               /* My player slot */
int tick, winner;          /* Current tick and winner (slot+1, or 0) */
int hasq;                  /* Whether the latest snapshot had a Q block */
int mycmd;                 /* Last detected local command */

/* Parsed counts and state from the latest snapshot */
int nf, nl, ns, nb, nsh, nex, nai;  /* fighters, lobby, shots, bombs, ships, explosions, AIs */
char phase = 'L';                    /* 'L' lobby or 'S' match */
Fig F[MAX_PLAYERS];                  /* Fighters */
Lob U[MAX_PLAYERS];                  /* Lobby entries */
Shot S[MAXPROJ], BM[MAXPROJ];        /* Shots and bombs */
Ship SHP[MAX_PLAYERS];               /* Ships */
Boom EX[MAXBOOM];                    /* Explosions */
Extra Q;                             /* Viewer-only extra state for prediction */
char aiName[MAXAI][16];              /* AI plugin names announced by the server */

/* Client-side prediction: predicted body state, flags, and input history */
Body pb;
int predicting, pch;
unsigned sent, ack;
static int hist[HIST];

static char rb[4096];
static int rl;

static CryptoSession crypto;

static int udp_fd = -1;
static unsigned long long token;
static long long last_hello;
static int last_tick = -1;

/* Helper: parse integer from string and advance pointer */
static int num(char **s) { return (int)strtol(*s, s, 10); }
static void udp_open(void);
static void udp_hello(void);
static int pump_tcp(void);
static long long now_ms(void);

static int tcp_send_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    while (len > 0) {
        ssize_t r = send(fd, p, len, 0);
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            continue;
        if (r <= 0)
            return -1;
        p += (size_t)r;
        len -= (size_t)r;
    }
    return 0;
}

static int awaiting_lobby_banner;

int network_init(void) { return crypto_session_init(); }

void network_set_session_password(const char *password)
{
    crypto_session_set_password(password);
}

static int ensure_crypto_handshake(void)
{
    if (crypto.established)
        return 1;

    /* Cleartext K exchange over blocking I/O so the full line is sent/received
       reliably on remote links (non-blocking send often fails with EAGAIN). */
    int fd_flags = fcntl(networkSocketFd, F_GETFL);
    if (fd_flags >= 0)
        fcntl(networkSocketFd, F_SETFL, fd_flags & ~O_NONBLOCK);
    struct timeval rcv_to = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(networkSocketFd, SOL_SOCKET, SO_RCVTIMEO, &rcv_to, sizeof rcv_to);

    char k_line[128];
    int got_k = 0;
    int ok = 0;
    long long deadline = now_ms() + 5000;

    for (;;) {
        if (now_ms() >= deadline) {
            fprintf(stderr,
                    "timed out waiting for server K handshake line (wrong port, "
                    "firewall, or outdated server binary?)\n");
            break;
        }
        char *nlp;
        while ((nlp = memchr(rb, '\n', (size_t)rl))) {
            *nlp = 0;
            if (rb[0] == 'K' && rb[1] == ' ') {
                snprintf(k_line, sizeof k_line, "%s\n", rb);
                got_k = 1;
            }
            int used = (int)(nlp - rb) + 1;
            memmove(rb, rb + used, (size_t)(rl - used));
            rl -= used;
            if (got_k)
                break;
        }
        if (got_k)
            break;
        ssize_t n = recv(networkSocketFd, rb + rl, sizeof rb - (size_t)rl - 1, 0);
        if (n == 0) {
            fprintf(stderr, "server closed connection during K handshake\n");
            break;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            perror("recv during K handshake");
            break;
        }
        rl += (int)n;
    }

    if (got_k) {
        crypto_session_begin(&crypto);
        char out[128];
        int kn = crypto_session_format_k_line(out, sizeof out, &crypto);
        if (kn < 0 || tcp_send_all(networkSocketFd, out, (size_t)kn) != 0)
            fprintf(stderr, "could not send client K handshake line\n");
        else if (crypto_session_finish_handshake(&crypto, k_line) != 0)
            fprintf(stderr, "invalid server K handshake line\n");
        else
            ok = 1;
    }

    rcv_to.tv_sec = 0;
    rcv_to.tv_usec = 0;
    setsockopt(networkSocketFd, SOL_SOCKET, SO_RCVTIMEO, &rcv_to, sizeof rcv_to);
    if (fd_flags >= 0)
        fcntl(networkSocketFd, F_SETFL, fd_flags | O_NONBLOCK);
    return ok;
}

int network_handshake(void)
{
    rl = 0;
    awaiting_lobby_banner = 1;
    if (!ensure_crypto_handshake()) {
        awaiting_lobby_banner = 0;
        return 0;
    }

    /* Server sends Y/A/U encrypted right after our K line; read them before SDL init. */
    long long deadline = now_ms() + 5000;
    while (me < 0 || !token) {
        if (now_ms() >= deadline) {
            fprintf(stderr,
                    "timed out waiting for encrypted lobby banner (Y/U lines)\n");
            awaiting_lobby_banner = 0;
            return 0;
        }
        if (!pump_tcp()) {
            awaiting_lobby_banner = 0;
            return 0;
        }
        if (me >= 0 && token) {
            awaiting_lobby_banner = 0;
            return 1;
        }
        struct pollfd pfd = {networkSocketFd, POLLIN, 0};
        poll(&pfd, 1, 50);
    }
    awaiting_lobby_banner = 0;
    return 1;
}

/* Direction to the nearest visible non-self opponent at position x: -1 left, +1 right, 0 none */
static int look(int x)
{
    int b = -1;
    for (int i = 0; i < nf; i++)
        if (F[i].slot != me && F[i].lives > 0 && F[i].x >= 0 &&
            (b < 0 || abs(F[i].x - x) < abs(F[b].x - x)))
            b = i;
    return b < 0 ? 0 : F[b].x >= x ? 1 : -1;
}

/* Whether the nearest visible non-self opponent is within throw range of x */
static int near(int x)
{
    int b = -1;
    for (int i = 0; i < nf; i++)
        if (F[i].slot != me && F[i].lives > 0 && F[i].x >= 0 &&
            (b < 0 || abs(F[i].x - x) < abs(F[b].x - x)))
            b = i;
    return b >= 0 && abs(F[b].x - x) <= THROW_RANGE;
}

/* Fill w[] with the last CMD_WIN inputs ending at index i (oldest first) */
static void window(unsigned i, int *w)
{
    for (int k = 0; k < CMD_WIN; k++) {
        int idx = (int)i - (CMD_WIN - 1 - k);
        w[k] = idx >= 0 ? hist[idx % HIST] : 0;
    }
}

/* Adopt the server snapshot, then replay unacknowledged inputs to predict current state */
static void reconcile(void)
{
    const Fig *f = NULL;
    for (int i = 0; i < nf; i++)
        if (F[i].slot == me) f = &F[i];
    if (!f || !hasq || f->lives <= 0 || winner) { predicting = 0; return; }

    Body b = { .x = f->x, .y = f->y, .vx = Q.vx, .vy = Q.vy, .face = f->face,
               .move = f->st >= 0 ? f->st : -1, .frame = Q.frame, .hit = Q.hit,
               .stun = Q.stun, .block = f->st == -3, .flying = f->st == -4,
               .crouch = f->st == -5 || f->st == -7,
               .down = f->st == -7 || f->st == -8, .invis = Q.invis,
               .grace = Q.grace, .meter = f->meter, .fuel = Q.fuel };
    unsigned a = ack > sent ? sent : ack;
    if (sent - a > HIST - CMD_WIN) a = sent - (HIST - CMD_WIN);
    for (unsigned i = a; i < sent; i++) {
        int w[CMD_WIN], lk = look(b.x);
        window(i, w);
        body_step(&b, &chars[f->ch], hist[i % HIST], lk, detect_cmd(w, CMD_WIN, lk ? lk : b.face), near(b.x));
    }
    pb = b;
    pch = f->ch;
    predicting = 1;
}

/* Parse one protocol line (Y/A/U/L/S...) and update client state accordingly */
static void parse(char *s)
{
    char t = *s++;

    if (t == 'Y') { me = num(&s); return; }

    if (t == 'U') {                                     /* "U token": credential for the UDP channel */
        token = strtoull(s, NULL, 16);
        udp_open();
        udp_hello();
        return;
    }

    if (t == 'A') {                                     /* "A ai0 ai1 ..." */
        nai = 0;
        while (nai < MAXAI) {
            while (*s == ' ') s++;
            int n = 0;
            while (s[n] && s[n] != ' ') n++;
            if (!n) break;
            snprintf(aiName[nai++], sizeof aiName[0], "%.*s", n, s);
            s += n;
        }
        return;
    }
    if (t != 'L' && t != 'S') return;

    /* Phase transition: reset numbering and sounds */
    if (phase != t) { sent = 0; predicting = 0; audio_reset(); last_tick = -1; }
    phase = t;

    /* Reset per-snapshot counters */
    nf = 0; nl = 0; ns = 0; nb = 0; nsh = 0; nex = 0; hasq = 0;

    if (t == 'S') { tick = num(&s); winner = num(&s); ack = (unsigned)num(&s); }

    for (;;) {
        while (*s == ' ') s++;
        if (!*s) break;
        char k = *s++;
        if (k == 'U' && nl < MAX_PLAYERS) {
            U[nl].slot = num(&s); U[nl].ch = num(&s); U[nl].ready = num(&s); U[nl].bot = num(&s); nl++;
        } else if (k == 'P' && nf < MAX_PLAYERS) {
            Fig *f = &F[nf++];
            f->slot = num(&s); f->ch = num(&s); f->x = num(&s); f->y = num(&s);
            f->hp = num(&s); f->lives = num(&s); f->meter = num(&s);
            f->face = num(&s); f->st = num(&s); f->invis = num(&s);
            if (f->ch < 0 || f->ch >= NCHARS) f->ch = 0;
            if (f->slot < 0 || f->slot >= MAX_PLAYERS) f->slot = 0;
        } else if (k == 'Q') {
            Q.vx = num(&s); Q.vy = num(&s); Q.frame = num(&s); Q.stun = num(&s);
            Q.grace = num(&s); Q.invis = num(&s); Q.hit = num(&s); Q.fuel = num(&s);
            hasq = 1;
        } else if (k == 'J' && ns < MAXPROJ) {
            S[ns].owner = num(&s); S[ns].x = num(&s); S[ns].y = num(&s); ns++;
        } else if (k == 'B' && nb < MAXPROJ) {
            BM[nb].owner = num(&s); BM[nb].x = num(&s); BM[nb].y = num(&s); nb++;
        } else if (k == 'Z' && nsh < MAX_PLAYERS) {
            SHP[nsh].owner = num(&s); SHP[nsh].x = num(&s); SHP[nsh].vx = num(&s); nsh++;
        } else if (k == 'X' && nex < MAXBOOM) {
            EX[nex].x = num(&s); EX[nex].y = num(&s); EX[nex].t = num(&s); nex++;
        } else break;
    }
    if (t == 'S') { reconcile(); audio_snapshot(); }
}

/* ---- UDP gameplay channel: opened once the server's "U token" arrives over TCP ---- */
static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void udp_open(void)
{
    struct sockaddr_storage peer_addr;
    socklen_t peer_len = sizeof peer_addr;

    /* Close previous socket if any */
    if (udp_fd >= 0) { close(udp_fd); udp_fd = -1; }

    /* Discover the TCP peer address to connect a UDP socket to the same host */
    if (getpeername(networkSocketFd, (struct sockaddr *)&peer_addr, &peer_len) < 0)
        return;

    int s = socket(peer_addr.ss_family, SOCK_DGRAM, 0);
    if (s < 0)
        return;
    if (connect(s, (struct sockaddr *)&peer_addr, peer_len) < 0) {
        close(s);
        return;
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    udp_fd = s;
}

static void udp_send(const char *b, int n)
{
    if (udp_fd < 0 || n <= 0 || !crypto.established)
        return;
    unsigned char wire[2048];
    size_t wire_len;
    if (crypto_session_udp_seal(&crypto, wire, sizeof wire, b, (size_t)n,
                                &wire_len) != 0)
        return;
    send(udp_fd, wire, wire_len, 0);
}

/* Send a UDP keepalive/hello (H slot token) to register our address */
static void udp_hello(void)
{
    char b[64];
    udp_send(b, snprintf(b, sizeof b, "H %d %llx", me, token));
}

static void udp_drain(void)
{
    /* Drain up to a small burst of UDP packets, applying only newer S snapshots */
    char pkt[2048];
    for (int iter = 0; udp_fd >= 0 && iter < 64; iter++) {
        unsigned char wire[2048];
        ssize_t n = recv(udp_fd, wire, sizeof wire, 0);
        if (n < 0) return;
        size_t plain_len;
        if (crypto_session_udp_open(&crypto, wire, (size_t)n, pkt, sizeof pkt - 1,
                                    &plain_len) != 0)
            continue;
        pkt[plain_len] = 0;
        char *nl = strchr(pkt, '\n');
        if (nl) *nl = 0;
        if (pkt[0] != 'S') continue;
        char *p = pkt + 1;
        int t = num(&p);
        if (t <= last_tick && phase == 'S') continue;   /* stale or duplicate datagram */
        parse(pkt);
        last_tick = t;
    }
}

/* Read and parse as many complete TCP lines as available; return 0 on disconnect */
static int tcp_line_cb(void *ctx, char *line)
{
    (void)ctx;
    parse(line);
    return 0;
}

static int pump_tcp(void)
{
    if (!crypto.established && !ensure_crypto_handshake())
        return 0;

    for (;;) {
        unsigned char chunk[CRYPTO_SESSION_TCP_IO_MAX];
        ssize_t n = recv(networkSocketFd, chunk, sizeof chunk, 0);
        if (n == 0) {
            if (awaiting_lobby_banner)
                fprintf(stderr, "server closed connection during encrypted handshake\n");
            return 0;
        }
        if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        if (crypto_session_tcp_feed(&crypto, chunk, (size_t)n, tcp_line_cb, NULL) != 0) {
            if (awaiting_lobby_banner)
                fprintf(stderr,
                        "could not decrypt server lobby data — use the same "
                        "BANANAFIGHTER_KEY or -k on client and server\n");
            return 0;
        }
    }
}

int pump(void)                                      /* 0 = server closed the connection */
{
    int ok = pump_tcp();

    /* Periodic UDP keepalive; also opens the server's return path */
    if (udp_fd >= 0 && token) {
        long long t = now_ms();
        if (t - last_hello >= 500) { udp_hello(); last_hello = t; }
    }
    udp_drain();
    return ok;
}

/* Send a formatted command over TCP */
void sendf(const char *fmt, ...)
{
    char b[48];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (n > 0 && crypto.established)
        crypto_session_tcp_send(networkSocketFd, &crypto, b, (size_t)n);
}

/* Resolve and connect a non-blocking TCP socket with TCP_NODELAY; returns fd or -1 */
int dial(const char *host, const char *port)
{
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM }, *res, *ai;
    int s = -1, one = 1;

    if (getaddrinfo(host, port, &hints, &res)) return -1;
    for (ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s >= 0 && connect(s, ai->ai_addr, ai->ai_addrlen) == 0) break;
        if (s >= 0) close(s);
        s = -1;
    }
    freeaddrinfo(res);
    if (s >= 0) {
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    }
    return s;
}

/* One fixed simulation step: send input window over UDP and advance local prediction */
void client_tick(void)
{
    if (phase != 'S' || !predicting) return;
    int mask = options_open ? 0 : keymask() | padmask();
    int w[CMD_WIN], lk = look(pb.x);
    hist[sent % HIST] = mask;
    window(sent, w);
    mycmd = detect_cmd(w, CMD_WIN, lk ? lk : pb.face);
    {   /* "I slot token seq m0 m1 ...": every input the server hasn't acked, newest last */
        char b[512];
        unsigned a = ack > sent ? sent : ack;
        if (sent + 1 - a > 32) a = sent + 1 - 32;
        int n = snprintf(b, sizeof b, "I %d %llx %u", me, token, a);
        for (unsigned i = a; i <= sent && n < (int)sizeof b - 16; i++)
            n += snprintf(b + n, sizeof b - n, " %d", hist[i % HIST]);
        udp_send(b, n);
    }
    body_step(&pb, &chars[pch], mask, lk, mycmd, near(pb.x));
    sent++;
}

