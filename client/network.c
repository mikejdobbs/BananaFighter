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
#include <sys/socket.h>
#include <unistd.h>
#include "client.h"

int fd = -1, me = -1, tick, winner, hasq, mycmd;
int nf, nl, ns, nb, nsh, nex, nai;
char phase = 'L';
Fig F[MAX_PLAYERS];
Lob U[MAX_PLAYERS];
Shot S[MAXPROJ], BM[MAXPROJ];
Ship SHP[MAX_PLAYERS];
Boom EX[MAXBOOM];
Extra Q;
char aiName[MAXAI][16];

/* prediction state */
Body pb;
int predicting, pch;
unsigned sent, ack;
static int hist[HIST];

static char rb[4096];
static int rl;

static int num(char **s) { return (int)strtol(*s, s, 10); }

static int look(int x)                  /* direction of the nearest visible opponent */
{
    int b = -1;
    for (int i = 0; i < nf; i++)
        if (F[i].slot != me && F[i].lives > 0 && F[i].x >= 0 &&
            (b < 0 || abs(F[i].x - x) < abs(F[b].x - x)))
            b = i;
    return b < 0 ? 0 : F[b].x >= x ? 1 : -1;
}

/* The CMD_WIN inputs ending at input number i, oldest first: what the server sees as history. */
static void window(unsigned i, int *w)
{
    for (int k = 0; k < CMD_WIN; k++) {
        int idx = (int)i - (CMD_WIN - 1 - k);
        w[k] = idx >= 0 ? hist[idx % HIST] : 0;
    }
}

/* Adopt the server's state, then replay inputs it hasn't consumed yet. */
static void reconcile(void)
{
    const Fig *f = NULL;
    for (int i = 0; i < nf; i++)
        if (F[i].slot == me) f = &F[i];
    if (!f || !hasq || f->lives <= 0 || winner) { predicting = 0; return; }

    Body b = { .x = f->x, .y = f->y, .vx = Q.vx, .vy = Q.vy, .face = f->face,
               .move = f->st >= 0 ? f->st : -1, .frame = Q.frame, .hit = Q.hit,
               .stun = Q.stun, .block = f->st == -3, .flying = f->st == -4, .invis = Q.invis,
               .grace = Q.grace, .meter = f->meter, .fuel = Q.fuel };
    unsigned a = ack > sent ? sent : ack;
    if (sent - a > HIST - CMD_WIN) a = sent - (HIST - CMD_WIN);
    for (unsigned i = a; i < sent; i++) {
        int w[CMD_WIN], lk = look(b.x);
        window(i, w);
        body_step(&b, &chars[f->ch], hist[i % HIST], lk, detect_cmd(w, CMD_WIN, lk ? lk : b.face));
    }
    pb = b;
    pch = f->ch;
    predicting = 1;
}

static void parse(char *s)
{
    char t = *s++;

    if (t == 'Y') { me = num(&s); return; }
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
    if (phase != t) { sent = 0; predicting = 0; audio_reset(); }   /* new phase: restart numbering and sounds */
    phase = t;
    nf = nl = ns = nb = nsh = nex = hasq = 0;
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

int pump(void)                                      /* 0 = server closed the connection */
{
    for (;;) {
        ssize_t n = recv(fd, rb + rl, sizeof rb - rl - 1, 0);
        if (n == 0) return 0;
        if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        rl += n;
        char *nlp;
        while ((nlp = memchr(rb, '\n', rl))) {
            *nlp = 0;
            parse(rb);
            int used = nlp - rb + 1;
            memmove(rb, rb + used, rl - used);
            rl -= used;
        }
        if (rl >= (int)sizeof rb - 1) rl = 0;
    }
}

void sendf(const char *fmt, ...)
{
    char b[48];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    send(fd, b, n, 0);
}

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

/* One fixed simulation step: send the input, predict its result locally.
   While the options menu is open the fighter stands still (neutral input) but the match goes on. */
void client_tick(void)
{
    if (phase != 'S' || !predicting) return;
    int mask = options_open ? 0 : keymask() | padmask();
    int w[CMD_WIN], lk = look(pb.x);
    hist[sent % HIST] = mask;
    window(sent, w);
    mycmd = detect_cmd(w, CMD_WIN, lk ? lk : pb.face);
    sendf("I %d\n", mask);
    body_step(&pb, &chars[pch], mask, lk, mycmd);
    sent++;
}
