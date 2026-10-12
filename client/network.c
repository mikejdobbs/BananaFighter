#include "client.h"

static void sendf(const char *fmt, ...)
{
    char b[48];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    send(fd, b, n, 0);
}

static int dial(const char *host, const char *port)
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

static int pump(void)
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

static void parse(char *s)
{
    char t = *s++;

    if (t == 'Y') { me = num(&s); return; }
    if (t == 'A') {
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
    if (phase != t) { sent = 0; predicting = 0; show_boxes = show_boxes; }
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
    if (t == 'S') { /* state snapshot processed in after-main code */ }
}

static void client_tick(void)
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
