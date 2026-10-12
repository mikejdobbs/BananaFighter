#include "server.h"

/* Select screen (TCP): "L {U slot char ready bot}..."   (bot = 0 for humans, else AI index + 1)
   Match (UDP): "S tick winner ack {P slot ch x y hp lives meter face state invis}... Q vx vy frame
           stun grace invis_ticks hit fuel {J owner x y}... {B owner x y}... {Z owner x vx}... {X x y t}..."
   state: >= 0 attacking (move index), -1 idle, -2 stunned, -3 blocking, -4 flying, -5 crouching.
   J = shot, B = bomb, Z = ship, X = explosion. Q (viewer's own extra state) lets the client
   resume simulation. Invisible opponents have x, y and state hidden. */
void send_state(int v)
{
    char buf[UDP_PKT_MAX];
    int n;

    if (phase == LOBBY) {
        n = snprintf(buf, sizeof buf, "L");
        for (int k = 0; k < MAX_PLAYERS; k++)
            if (player_is_person_or_bot(&players[k]) && n < (int)sizeof buf - 48)
                n += snprintf(buf + n, sizeof buf - n, " U %d %d %d %d", k, players[k].s_character, players[k].ready,
                              players[k].bot ? players[k].ai + 1 : 0);
    } else { // NOT IN LOBBY
        n = snprintf(buf, sizeof buf, "S %d %d %d", tick, winner, players[v].num_ack_by_server);

        // share players
        for (int k = 0; k < MAX_PLAYERS; k++) {
            Player *p = &players[k];
            if (!p->in_match) continue;

            // hide invisible players to everyone but themself
            int hide = p->body.invis && k != v;

            int st = p->body.stun ? -2 : p->body.block ? -3 : p->body.move >= 0 ? p->body.move : p->body.flying ? -4 : p->body.crouch ? -5 : -1;
            n += snprintf(buf + n, sizeof buf - n, " P %d %d %d %d %d %d %d %d %d %d", k, p->s_character,
                          hide ? -1 : p->body.x, hide ? -1 : p->body.y, p->hp, p->lives, p->body.meter,
                          p->body.face, hide ? -1 : st, p->body.invis > 0);
        }

        // show player their stats
        if (players[v].in_match) {
            Body *b = &players[v].body;
            n += snprintf(buf + n, sizeof buf - n, " Q %d %d %d %d %d %d %d %d",
                          b->vx, b->vy, b->frame, b->stun, b->grace, b->invis, b->hit, b->fuel);
        }

        // show projectiles
        for (int k = 0; k < MAXPROJ; k++)
            if (projectiles[k].on && n < (int)sizeof buf - 48)
                n += snprintf(buf + n, sizeof buf - n, " %c %d %d %d", projectiles[k].bomb ? 'B' : 'J',
                              projectiles[k].owner, projectiles[k].x, projectiles[k].y);

        // show ships
        for (int k = 0; k < MAX_PLAYERS; k++)
            if (ships[k].on && n < (int)sizeof buf - 48)
                n += snprintf(buf + n, sizeof buf - n, " Z %d %d %d", k, ships[k].x, ships[k].vx);

        // show explosions
        for (int k = 0; k < MAXBOOM; k++)
            if (explosions[k].on && n < (int)sizeof buf - 48)
                n += snprintf(buf + n, sizeof buf - n, " X %d %d %d", explosions[k].x, explosions[k].y, explosions[k].t);
    }

    if (n > (int)sizeof buf - 2) n = (int)sizeof buf - 2;     /* snprintf reports the untruncated length */
    buf[n++] = '\n';

    if (phase == LOBBY) {                                      /* reliable: TCP */
        ssize_t r = send(players[v].fd, buf, n, 0);
        if (r != n && !(r < 0 && errno == EAGAIN)) drop(v);    /* error or partial write */
    } else if (players[v].udp_ok) {                            /* lossy is fine: UDP */
        sendto(udp_fd, buf, n, 0, (struct sockaddr *)&players[v].udp_addr, sizeof players[v].udp_addr);
    }
}

static void push_input(Player *player, int mask)
{
    if (player->q_num == INPUT_QMAX) {                      /* full: drop the oldest */
        player->q_head = (player->q_head + 1) % INPUT_QMAX; player->q_num--; player->num_ack_by_server++;
    }
    player->q[(player->q_head + player->q_num++) % INPUT_QMAX] = mask;
}

/* Inputs arrive as seq, seq+1, ... and may be duplicated, late or missing. Skip ones already
   seen; for a gap, repeat the last input so the numbering (and the ack) stays aligned. */
static void apply_udp_inputs(Player *p, int seq, const int *m, int count)
{
    for (int j = 0; j < count; j++) {
        int s = seq + j;
        if (s < p->next_seq) continue;                      /* duplicate from redundancy */
        int gap = s - p->next_seq;
        int fill = gap < INPUT_QMAX ? gap : INPUT_QMAX;
        for (int g = 0; g < fill; g++) push_input(p, p->last_in);
        p->num_ack_by_server += gap - fill;                 /* a huge jump: count skipped inputs as consumed */
        push_input(p, m[j]);
        p->last_in = m[j];
        p->next_seq = s + 1;
    }
}

static void handle_line(int player_num, char *line)
{
    Player *player = &players[player_num];
    int v, w;

    if (phase == LOBBY && !player->in_match && sscanf(line, "B %d %d", &v, &w) == 2)
        add_bot(v, w);                              /* B <char> <ai>: any number of bots */
    else if (phase == LOBBY && !player->in_match && sscanf(line, "X %d", &v) == 1)
        del_bot(v);                                 /* X <slot> removes a bot */
    else if (sscanf(line, "J %d", &v) == 1 && !player->in_match && !player->ready)
        player->s_character = (v >= 0 && v < NCHARS) ? v : 0;   /* J <char>: hover/pick; locked-in fighters can't change */
    else if (line[0] == 'R' && phase == LOBBY && !player->in_match)
        player->ready ^= 1;
    /* "I" inputs now arrive over UDP: see read_udp() */
}

void read_client(int i)
{
    Player *player = &players[i];
    ssize_t n = recv(player->fd, player->buf + player->len, sizeof player->buf - player->len - 1, 0);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) { drop(i); return; }
    if (n < 0) return;

    player->len += n;
    char *newline;
    while ((newline = memchr(player->buf, '\n', player->len))) {
        *newline = 0;
        handle_line(i, player->buf);
        int used = (int)(newline - player->buf) + 1;
        memmove(player->buf, player->buf + used, player->len - used);
        player->len -= used;
    }
    if (player->len >= (int)sizeof player->buf - 1) drop(i);      /* line too long */
}

/* Drain the UDP socket. Every packet must name a connected slot and carry that slot's token. */
void read_udp(void)
{
    for (int iter = 0; iter < 256; iter++) {
        char pkt[UDP_PKT_MAX + 1];
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        ssize_t n = recvfrom(udp_fd, pkt, UDP_PKT_MAX, 0, (struct sockaddr *)&from, &fl);
        if (n < 0) return;                                  /* EAGAIN: socket drained */
        pkt[n] = 0;

        char kind;
        int slot, used = 0;
        unsigned long long tok;
        if (sscanf(pkt, "%c %d %llx %n", &kind, &slot, &tok, &used) != 3) continue;
        if (slot < 0 || slot >= MAX_PLAYERS) continue;

        Player *p = &players[slot];
        if (p->fd < 0 || p->token == 0 || tok != p->token) continue;    /* unknown or forged */

        p->udp_addr = from;                                 /* follows NAT rebinding */
        p->udp_ok = 1;
        p->last_udp_ms = now_ms();

        if (kind != 'I' || phase != PLAY || !p->in_match || used == 0) continue;

        char *s = pkt + used, *e;
        int seq = (int)strtol(s, &e, 10), m[UDP_MAX_INPUTS], count = 0;
        if (e == s) continue;
        s = e;
        while (count < UDP_MAX_INPUTS) {
            long val = strtol(s, &e, 10);
            if (e == s) break;
            m[count++] = (int)val;
            s = e;
        }
        apply_udp_inputs(p, seq, m, count);
    }
}

/* In a match a human must keep talking over UDP, or they are dropped. */
void check_udp_timeouts(void)
{
    long long t = now_ms();
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (players[i].fd >= 0 && players[i].in_match && t - players[i].last_udp_ms > UDP_TIMEOUT_MS) {
            fprintf(stderr, "slot %d: no UDP for %d ms\n", i, UDP_TIMEOUT_MS);
            drop(i);
        }
}

void accept_client(int listen_fd)
{
    int fd = accept(listen_fd, NULL, NULL), one = 1, slot = -1;
    if (fd < 0) return;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (!player_is_person_or_bot(&players[i])) { slot = i; break; }
    if (slot < 0) { close(fd); return; }                /* server full */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    memset(&players[slot], 0, sizeof players[slot]);
    players[slot].fd = fd;
    players[slot].body.move = -1;
    players[slot].token = make_token();

    char b[256];                                        /* "Y slot name0 name1 ..." */
    int n = snprintf(b, sizeof b, "Y %d", slot);
    for (int c = 0; c < NCHARS; c++)
        n += snprintf(b + n, sizeof b - n, " %s", chars[c].name);
    b[n++] = '\n';
    send(fd, b, n, 0);
    n = snprintf(b, sizeof b, "A");                     /* "A ai0 ai1 ..." */
    for (int a = 0; a < NAIS; a++)
        n += snprintf(b + n, sizeof b - n, " %s", ais[a].name);
    b[n++] = '\n';
    send(fd, b, n, 0);
    n = snprintf(b, sizeof b, "U %llx\n", players[slot].token);   /* UDP credential */
    send(fd, b, n, 0);
    fprintf(stderr, "slot %d connected\n", slot);
}
