#ifndef SERVER_H
#define SERVER_H

/* Must come before any system header so the feature-test macro takes effect. */
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
#include "crypto_session.h"

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
    int cmd;                /* motion command (CMD_QCF/HCF/FDF) the bot "performs" with its button */
    unsigned rng;           /* this bot's own random-number state, so bots don't act in lockstep */

    /* Humans: partial-line buffer for cleartext K handshake and decrypted lines. */
    char buf[96];           /* cleartext K line (longer than lobby lines) and scratch */
    int len;                /* how many bytes of buf are in use */

    CryptoSession crypto;   /* session keys after K exchange */
    int crypto_ready;       /* 1 once handshake completes */

    /* Humans: UDP gameplay channel. */
    unsigned long long token;       /* secret issued over TCP; must appear in every UDP packet */
    struct sockaddr_in udp_addr;    /* where to send snapshots (learned from authenticated packets) */
    int udp_ok;                     /* 1 once a valid UDP packet has arrived */
    uint32_t next_seq;                   /* next input sequence number we expect */
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

/* state.c: global game state */
extern Player players[MAX_PLAYERS];   /* one entry per slot: humans, bots and empty slots */
extern Proj   projectiles[MAXPROJ];   /* pool of live shots and bombs */
extern Ship   ships[MAX_PLAYERS];     /* indexed by the owner's slot: one ship per caller */
extern Boom   explosions[MAXBOOM];    /* pool of live explosion effects */
extern int    udp_fd;                 /* the gameplay datagram socket */
extern int    srv_phase;              /* LOBBY (the fighter-select screen) or PLAY (a match) */
extern int    srv_tick;               /* ticks since the current match started */
extern int    over;                   /* 0 while fighting; counts up once the match ends, until ROUND_WAIT */
extern int    srv_winner;             /* slot + 1 of the winner, or 0 if undecided or a draw */

/* util.c */
long long          now_ms(void);
unsigned long long make_token(void);
int  fighting(const Player *p);
int  player_is_person_or_bot(const Player *p);
int  overlap(int ax0, int ax1, int ay0, int ay1, int bx0, int bx1, int by0, int by1);
int  nearest(int i);

/* state.c */
void clear_fx(void);
void place(Player *player, int x);
void check_start(void);
void end_match(void);
void drop(int i);
void add_bot(int character, int ai);
void del_bot(int i);

/* combat.c */
void spawn(Player *p, int owner, const Move *m);
void call_ship(Player *p, int owner, const Move *m);
void melee(int i, const Move *m);
void update_projectiles(void);
void update_ships(void);

/* sim.c */
void step(void);

/* net.c */
void send_state(int v);
void read_client_in_lobby(int i);
void read_udp(void);
void check_udp_timeouts(void);
void accept_client_to_lobby(int listen_fd);

/* Optional lobby password; NULL if unset. Set before accepting clients. */
void server_set_session_password(const char *password);

#endif
