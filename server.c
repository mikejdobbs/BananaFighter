#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "ai.h"

/*
 * Transport split
 *   TCP (PORT): lobby, handshake and all lobby commands (B, X, J, R), plus "L" lobby state.
 *   UDP (PORT): in-match only. Client -> server: inputs. Server -> client: "S" snapshots.
 *
 * TCP handshake lines sent on connect:
 *   "Y slot name0 name1 ..."
 *   "A ai0 ai1 ..."
 *   "U token"                     token is hex; the client must echo it in every UDP packet
 *
 * UDP, client -> server (text, one datagram each):
 *   "H slot token"                hello / keepalive: registers the client's UDP address
 *   "I slot token seq m0 m1 ..."  inputs numbered seq, seq+1, ...; send every input not yet acked
 *                                 (seq starts at 0 each match; the first "S" snapshot of a match starts it)
 * UDP, server -> client:
 *   "S tick winner ack ..."       full snapshot (same format as before); the client ignores any
 *                                 snapshot whose tick is not newer than the last one applied
 */

#define UDP_PKT_MAX     1400        /* stay under a typical MTU to avoid fragmentation */
#define UDP_MAX_INPUTS  32          /* inputs accepted per datagram */
#define UDP_TIMEOUT_MS  5000        /* in a match, drop a human we have not heard from over UDP */

enum { LOBBY, PLAY };               /* LOBBY is the fighter-select screen on the client */

typedef struct {
    Body body;
    int fd, ready, in_match, s_character, hp, lives, spawn;
    int in, num_ack_by_server, q_head, q_num, q[INPUT_QMAX];   /* input queue; ack = inputs consumed */
    int in_history[32]; unsigned in_history_n;                 /* recent consumed inputs, for motion commands */

    /* Bots: no socket and no input queue. Their input comes from a plugin in ais[]. */
    int bot;                /* 1 = computer-controlled slot, 0 = human or empty */
    int ai;                 /* index into ais[]: which AI plugin drives this bot */
    int think_cd;           /* ticks until the bot decides again; it holds its input until then */
    int cmd;                /* motion command (CMD_QCF/CMD_HCF) the bot "performs" with its button */
    unsigned rng;           /* this bot's own random-number state, so bots don't act in lockstep */

    /* Humans: partial-line buffer for the TCP stream (unused for bots). */
    char buf[64];           /* received bytes not yet forming a complete "\n"-terminated line */
    int len;                /* how many bytes of buf are in use */

    /* Humans: UDP gameplay channel. */
    unsigned long long token;       /* secret issued over TCP; must appear in every UDP packet */
    struct sockaddr_in udp_addr;    /* where to send snapshots (learned from authenticated packets) */
    int udp_ok;                     /* 1 once a valid UDP packet has arrived */
    int next_seq;                   /* next input sequence number we expect */
    int last_in;                    /* last input received, used to fill gaps */
    long long last_udp_ms;          /* time of the last valid UDP packet */
} Player;

/* A shot or a bomb in flight. Slots are reused: `on` says whether an entry is live. */
typedef struct {
    int on;             /* 1 = this pool entry is in use */
    int owner;          /* slot of the fighter who fired it (never hurt by it) */
    int x, y;           /* position in stage units; y is height above the ground */
    int vx, vy;         /* velocity per tick (vy > 0 is upward) */
    int grav;           /* downward acceleration per tick: 0 for shots, BOMB_GRAV for bombs */
    int bomb;           /* 1 = explodes with area damage on touching a fighter or the ground */
    int life;           /* ticks left before it fizzles out */
    const Move *m;      /* the move that fired it: source of damage and knockback */
} Proj;

/* A called ship crossing the stage and dropping bombs. At most one per owner. */
typedef struct {
    int on;             /* 1 = flying */
    int owner;          /* slot of the fighter who called it */
    int x, vx;          /* position; velocity per tick (its sign is the flight direction) */
    int drops;          /* bombs still to drop */
    int cd;             /* ticks until the next bomb */
    const Move *m;      /* the calling move: dmg and kb apply to each bomb */
} Ship;

/* An explosion effect. Damage is dealt when the bomb detonates; this only tells clients
   where and when to draw it. */
typedef struct {
    int on;             /* 1 = visible */
    int x, y;           /* where it went off (y >= 0) */
    int t;              /* ticks left; starts at BOOM_TICKS, so clients can spot a fresh one */
} Boom;

static Player players[MAX_PLAYERS];   /* one entry per slot: humans, bots and empty slots */
static Proj   projectiles[MAXPROJ];   /* pool of live shots and bombs */
static Ship   ships[MAX_PLAYERS];     /* indexed by the owner's slot: one ship per caller */
static Boom   explosions[MAXBOOM];    /* pool of live explosion effects */

static int    udp_fd = -1;    /* the gameplay datagram socket */
static int    phase;          /* LOBBY (the fighter-select screen) or PLAY (a match) */
static int    tick;           /* ticks since the current match started */
static int    over;           /* 0 while fighting; counts up once the match ends, until ROUND_WAIT */
static int    winner;         /* slot + 1 of the winner, or 0 if undecided or a draw */

static long long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

static unsigned long long make_token(void)
{
    unsigned long long t = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&t, sizeof t, 1, f) != 1) t = 0;
        fclose(f);
    }
    if (!t) t = ((unsigned long long)rand() << 32) ^ (unsigned long long)rand() ^ (unsigned long long)now_ms();
    return t ? t : 1;
}

static int fighting(const Player *p) { return p->in_match && p->lives > 0; }
static int player_is_person_or_bot(const Player *p) { return p->fd >= 0 || p->bot; }   /* slot in use */

static int overlap(int ax0, int ax1, int ay0, int ay1, int bx0, int bx1, int by0, int by1)
{
    return lo(ax1, bx1) >= hi(ax0, bx0) && lo(ay1, by1) >= hi(ay0, by0);
}

static int nearest(int i)
{
    int b = -1;
    for (int k = 0; k < MAX_PLAYERS; k++)
        if (k != i && fighting(&players[k]) &&
            (b < 0 || abs(players[k].body.x - players[i].body.x) < abs(players[b].body.x - players[i].body.x)))
            b = k;
    return b;
}

static void clear_fx(void)
{
    memset(projectiles, 0, sizeof projectiles);
    memset(ships, 0, sizeof ships);
    memset(explosions, 0, sizeof explosions);
}

static void place(Player *player, int x)
{
    memset(&player->body, 0, sizeof player->body);
    player->body.x = x;
    player->body.face = 1;
    player->body.move = -1;
    player->body.fuel = chars[player->s_character].fuel;
    player->hp = chars[player->s_character].hp;
}

static void check_start(void)
{
    int c = 0, r = 0, h = 0, k = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (player_is_person_or_bot(&players[i])) { c++; r += players[i].ready; h += players[i].fd >= 0; }
    if (h == 0 || c < MIN_PLAYERS || r != c) return;   /* needs a human, and everyone ready */
    long long t0 = now_ms();
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *p = &players[i];
        if (!player_is_person_or_bot(p)) continue;
        p->in_match = 1;
        p->lives = chars[p->s_character].lives;
        p->in = p->num_ack_by_server = p->q_head = p->q_num = p->think_cd = p->cmd = 0;
        p->next_seq = 0;                    /* the client numbers its inputs from 0 each match */
        p->last_in = 0;
        p->last_udp_ms = t0;
        memset(p->in_history, 0, sizeof p->in_history);
        p->in_history_n = 0;
        p->spawn = STAGE_W * (++k) / (c + 1);
        place(p, p->spawn);
    }
    clear_fx();
    phase = PLAY; tick = over = winner = 0;
    fprintf(stderr, "match start: %d fighters\n", c);
}

static void end_match(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) { players[i].in_match = 0; players[i].ready = players[i].bot; }
    clear_fx();
    phase = LOBBY;
    over = 0;
}

static void drop(int i)
{
    if (players[i].fd >= 0)
        close(players[i].fd);

    memset(&players[i], 0, sizeof players[i]);      /* also wipes the token and UDP state */
    players[i].fd = -1;
    ships[i].on = 0;
    for (int j = 0; j < MAXPROJ; j++)
        if (projectiles[j].owner == i) projectiles[j].on = 0;
    fprintf(stderr, "slot %d left\n", i);
}

static void ko(Player *v)
{
    if (--v->lives > 0) {
        place(v, v->spawn);
        v->body.grace = RESPAWN_GRACE;
    }
}

static int hurt_top(const Player *v, const Character *vc)
{
    int h = vc->h;
    if (v->body.crouch && v->body.y == 0) h = h / 2;    /* crouching: half-height hurtbox dodges high attacks */
    return v->body.y + h;
}

static void hurt(Player *a, Player *v, const Move *m, int dir)
{
    Body *vb = &v->body;
    if (vb->grace) return;
    vb->invis = 0;                                  /* getting hit reveals you */
    a->body.meter = lo(METER_MAX, a->body.meter + m->dmg / 4);
    vb->meter  = lo(METER_MAX, vb->meter + m->dmg / 8);
    if (vb->block && !m->throw) {                   /* chip damage only; grabs beat blocks */
        v->hp -= m->dmg * BLOCK_PCT / 100;
        vb->vx = dir * m->kb / 8;
    } else {
        v->hp -= m->dmg;
        vb->stun = m->throw ? THROW_STUN : HITSTUN;
        vb->vx = dir * m->kb / 4;
        if (m->throw) vb->vy = THROW_LIFT;
        vb->move = -1;
    }
    if (v->hp <= 0) ko(v);
}

/* Fire one projectile, or a fan of m->spread of them. */
static void spawn(Player *p, int owner, const Move *m)
{
    int n = m->spread > 1 ? m->spread : 1;
    for (int s = 0; s < n; s++) {
        int vy = (2 * s - (n - 1)) * m->spread_vy / 2;
        for (int i = 0; i < MAXPROJ; i++) {
            if (projectiles[i].on) continue;
            projectiles[i] = (Proj){ .on = 1, .owner = owner, .x = p->body.x + p->body.face * chars[p->s_character].w / 2,
                                     .y = p->body.y + m->y + m->h / 2, .vx = p->body.face * m->proj_speed,
                                     .vy = vy, .life = m->proj_life, .m = m };
            break;
        }
    }
}

/* A ship flies in from the edge behind the caller and carpet-bombs the stage. */
static void call_ship(Player *p, int owner, const Move *m)
{
    Ship *s = &ships[owner];
    if (s->on) return;                              /* one ship per caller at a time */
    int dir = p->body.face;
    *s = (Ship){ .on = 1, .owner = owner, .x = dir > 0 ? -SHIP_MARGIN : STAGE_W + SHIP_MARGIN,
                 .vx = dir * SHIP_SPEED, .drops = m->ship, .cd = 10, .m = m };
}

static void drop_bomb(const Ship *s)
{
    for (int i = 0; i < MAXPROJ; i++) {
        if (projectiles[i].on) continue;
        projectiles[i] = (Proj){ .on = 1, .owner = s->owner, .x = s->x, .y = SHIP_Y - 20,
                                 .vx = s->vx / 3, .grav = BOMB_GRAV, .bomb = 1, .life = 400, .m = s->m };
        return;
    }
}

/* Area damage around a bomb; the owner is immune. */
static void explode(const Proj *j)
{
    int cy = j->y < 0 ? 0 : j->y;

    for (int k = 0; k < MAXBOOM; k++)
        if (!explosions[k].on) { explosions[k] = (Boom){ 1, j->x, cy, BOOM_TICKS }; break; }

    for (int k = 0; k < MAX_PLAYERS; k++) {
        Player *v = &players[k];
        const Character *vc = &chars[v->s_character];
        if (k == j->owner || !fighting(v))
            continue;

        if (overlap(j->x - BOMB_R, j->x + BOMB_R, cy - BOMB_R, cy + BOMB_R,
                    v->body.x - vc->w / 2, v->body.x + vc->w / 2, v->body.y, hurt_top(v, vc)))
            hurt(&players[j->owner], v, j->m, v->body.x >= j->x ? 1 : -1);
    }
}

static void melee(int i, const Move *m)
{
    Player *p = &players[i];
    const Character *c = &chars[p->s_character];
    int x0 = p->body.x + p->body.face * c->w / 2, x1 = x0 + p->body.face * m->reach, best = -1;

    for (int k = 0; k < MAX_PLAYERS; k++) {
        Player *v = &players[k];
        const Character *vc = &chars[v->s_character];
        if (k == i || !fighting(v) || v->body.grace) continue;
        if (!overlap(lo(x0, x1), hi(x0, x1), p->body.y + m->y, p->body.y + m->y + m->h,
                     v->body.x - vc->w / 2, v->body.x + vc->w / 2, v->body.y, hurt_top(v, vc)))
            continue;
        if (!m->throw) {
            hurt(p, v, m, p->body.face);
            p->body.hit = 1;
        } else if (best < 0 || abs(v->body.x - p->body.x) < abs(players[best].body.x - p->body.x)) {
            best = k;
        }
    }
    if (best >= 0) {                                /* a grab holds only the closest victim */
        hurt(p, &players[best], m, p->body.face);
        p->body.hit = 1;
    }
}

/* Add a bot using the character and AI the client chose (not the chooser's own character). */
static void add_bot(int character, int ai)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (player_is_person_or_bot(&players[i])) continue;
        players[i] = (Player){ .fd = -1, .bot = 1, .ready = 1,
                               .s_character = (character >= 0 && character < NCHARS) ? character : 0,
                               .ai = (ai >= 0 && ai < NAIS) ? ai : 0, .rng = 2463534242u + 7919u * i };
        players[i].body.move = -1;
        fprintf(stderr, "slot %d: bot %s (%s)\n", i, ais[players[i].ai].name, chars[players[i].s_character].name);
        return;
    }
}

static void del_bot(int i)
{
    if (i < 0 || i >= MAX_PLAYERS || !players[i].bot) return;
    memset(&players[i], 0, sizeof players[i]);
    players[i].fd = -1;
}

/* Build the plugin's view of the world and ask it what to press. */
static int think(int i)
{
    Player *p = &players[i];
    int t = nearest(i);
    p->cmd = 0;
    if (t < 0) return 0;
    AiView v = { .me = &chars[p->s_character], .foe = &chars[players[t].s_character], .self = &p->body, .tgt = &players[t].body,
                 .rng = &p->rng, .shot = 9999 };
    v.dx = players[t].body.x - p->body.x;
    if (players[t].body.invis) v.dx += ai_rand(&v, 300) - 150;   /* bots can't see invisible fighters */
    v.dist = abs(v.dx);
    for (int j = 0; j < MAXPROJ; j++)                   /* shots and bombs both count as threats */
        if (projectiles[j].on && projectiles[j].owner != i &&
            (projectiles[j].bomb || (projectiles[j].vx > 0 ? projectiles[j].x < p->body.x : projectiles[j].x > p->body.x)))
            v.shot = lo(v.shot, abs(projectiles[j].x - p->body.x));
    int in = ais[p->ai].think(&v);
    p->cmd = v.cmd;
    return in;
}

static void take_input(Player *p)
{
    while (p->q_num > INPUT_QLAG) {                    /* bound latency: drop the oldest */
        p->q_head = (p->q_head + 1) % INPUT_QMAX; p->q_num--; p->num_ack_by_server++;
    }
    if (p->q_num) {                                    /* empty queue: repeat the last input */
        p->in = p->q[p->q_head];
        p->q_head = (p->q_head + 1) % INPUT_QMAX; p->q_num--; p->num_ack_by_server++;
    }
}

static void update_player(int i)
{
    Player *player = &players[i];
    const Character *character = &chars[player->s_character];
    int t = nearest(i), look = t >= 0 ? (players[t].body.x >= player->body.x ? 1 : -1) : 0, cmd;

    if (!player->bot)
        take_input(player);
    else if (player->think_cd-- <= 0) {                  /* bots hold their input between thoughts */
        player->in = think(i);
        player->think_cd = AI_THINK_TICKS;
    }

    player->in_history[player->in_history_n++ % 32] = player->in;

    if (player->bot) {
        cmd = player->cmd;
    } else { /* read a motion from the last CMD_WIN inputs */
        int w[CMD_WIN];
        for (int k = 0; k < CMD_WIN; k++) {
            int idx = (int)player->in_history_n - (CMD_WIN - k);
            w[k] = idx >= 0 ? player->in_history[idx % 32] : 0;
        }
        cmd = detect_cmd(w, CMD_WIN, look ? look : player->body.face);
    }

    int ev = body_step(&player->body, character, player->in, look, cmd);

    if (ev & EV_SPAWN) {
        const Move *m = &character->moves[player->body.move];
        if (m->ship) call_ship(player, i, m); else spawn(player, i, m);
    }

    if (ev & EV_MELEE)
        melee(i, &character->moves[player->body.move]);
}

static void update_projectiles(void)
{
    for (int i = 0; i < MAXPROJ; i++) {
        Proj *j = &projectiles[i];
        if (!j->on) continue;
        j->x += j->vx;
        j->y += j->vy;
        if (j->grav) j->vy -= j->grav;
        if (--j->life <= 0 || j->x < 0 || j->x > STAGE_W || j->y > 2000) { j->on = 0; continue; }
        if (j->y <= 0) {                            /* reached the ground */
            if (j->bomb) explode(j);
            j->on = 0;
            continue;
        }
        for (int k = 0; k < MAX_PLAYERS && j->on; k++) {
            Player *v = &players[k];
            const Character *vc = &chars[v->s_character];
            if (k == j->owner || !fighting(v)) continue;
            if (overlap(j->x - PROJ_HALF, j->x + PROJ_HALF, j->y - PROJ_HALF, j->y + PROJ_HALF,
                        v->body.x - vc->w / 2, v->body.x + vc->w / 2, v->body.y, hurt_top(v, vc))) {
                if (j->bomb) explode(j);
                else hurt(&players[j->owner], v, j->m, j->vx > 0 ? 1 : -1);
                j->on = 0;
            }
        }
    }
}

static void update_ships(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Ship *s = &ships[i];
        if (!s->on) continue;
        s->x += s->vx;
        if (s->x < -SHIP_MARGIN - 50 || s->x > STAGE_W + SHIP_MARGIN + 50)
            { s->on = 0; continue; }

        if (s->drops > 0 && s->x >= 0 && s->x <= STAGE_W && --s->cd <= 0) {
            drop_bomb(s);
            s->drops--;
            s->cd = SHIP_DROP;
        }
    }

    for (int k = 0; k < MAXBOOM; k++)
        if (explosions[k].on && --explosions[k].t <= 0)
            explosions[k].on = 0;
}

static void step(void)
{
    if (phase == LOBBY)
        { check_start(); return; }

    if (over)
        { if (++over > ROUND_WAIT) end_match(); return; }

    for (int i = 0; i < MAX_PLAYERS; i++)
        if (fighting(&players[i]))
            update_player(i);

    update_ships();
    update_projectiles();
    int n = 0, last = -1;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (fighting(&players[i])) { n++; last = i; }
    if (n <= 1) { over = 1; winner = n ? last + 1 : 0; }   /* winner: slot + 1, 0 = none */
    tick++;
}

/* Select screen (TCP): "L {U slot char ready bot}..."   (bot = 0 for humans, else AI index + 1)
   Match (UDP): "S tick winner ack {P slot ch x y hp lives meter face state invis}... Q vx vy frame
           stun grace invis_ticks hit fuel {J owner x y}... {B owner x y}... {Z owner x vx}... {X x y t}..."
   state: >= 0 attacking (move index), -1 idle, -2 stunned, -3 blocking, -4 flying, -5 crouching.
   J = shot, B = bomb, Z = ship, X = explosion. Q (viewer's own extra state) lets the client
   resume simulation. Invisible opponents have x, y and state hidden. */
static void send_state(int v)
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

static void read_client(int i)
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
static void read_udp(void)
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
static void check_udp_timeouts(void)
{
    long long t = now_ms();
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (players[i].fd >= 0 && players[i].in_match && t - players[i].last_udp_ms > UDP_TIMEOUT_MS) {
            fprintf(stderr, "slot %d: no UDP for %d ms\n", i, UDP_TIMEOUT_MS);
            drop(i);
        }
}

static void accept_client(int listen_fd)
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

int main(void)
{
    int reuse_addr = 1;
    struct sockaddr_in listen_addr = { .sin_family = AF_INET, .sin_port = htons(PORT),
                                       .sin_addr.s_addr = htonl(INADDR_ANY) };
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof reuse_addr);
    if (bind(listen_fd, (struct sockaddr *)&listen_addr, sizeof listen_addr) < 0) { perror("bind tcp"); return 1; }
    if (listen(listen_fd, 8) < 0) { perror("listen"); return 1; }

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);            /* same port number, different protocol */
    if (udp_fd < 0) { perror("socket udp"); return 1; }
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof reuse_addr);
    if (bind(udp_fd, (struct sockaddr *)&listen_addr, sizeof listen_addr) < 0) { perror("bind udp"); return 1; }
    fcntl(udp_fd, F_SETFL, fcntl(udp_fd, F_GETFL) | O_NONBLOCK);

    for (int i = 0; i < MAX_PLAYERS; i++)
        players[i].fd = -1;

    long long next_tick_ms = now_ms();
    for (;;) {

        struct pollfd poll_fds[MAX_PLAYERS + 2] = { { listen_fd, POLLIN, 0 } };
        for (int i = 0; i < MAX_PLAYERS; i++)
            poll_fds[i + 1] = (struct pollfd){ players[i].fd, POLLIN, 0 };
        
        poll_fds[MAX_PLAYERS + 1] = (struct pollfd){ udp_fd, POLLIN, 0 };

        // sleep until something happens or socket timesout
        long long timeout_ms = next_tick_ms - now_ms();
        if (poll(poll_fds, MAX_PLAYERS + 2, timeout_ms > 0 ? (int)timeout_ms : 0) > 0) {
            if (poll_fds[0].revents & POLLIN) accept_client(listen_fd);
            for (int i = 0; i < MAX_PLAYERS; i++)
                if ((poll_fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) && players[i].fd == poll_fds[i + 1].fd)
                    read_client(i);
            if (poll_fds[MAX_PLAYERS + 1].revents & POLLIN) read_udp();
        }
        
        if (now_ms() >= next_tick_ms) {
            step();
            check_udp_timeouts();
            for (int i = 0; i < MAX_PLAYERS; i++)
                if (players[i].fd >= 0) send_state(i);
            next_tick_ms += 1000 / TICK_HZ;
        }
    }
}

