#include "server.h"

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
    int nearfoe = t >= 0 && abs(players[t].body.x - player->body.x) <= THROW_RANGE;

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

    int ev = body_step(&player->body, character, player->in, look, cmd, nearfoe);

    if (ev & EV_SPAWN) {
        const Move *m = &character->moves[player->body.move];
        if (m->ship) call_ship(player, i, m); else spawn(player, i, m);
    }

    if (ev & EV_MELEE)
        melee(i, &character->moves[player->body.move]);
}

void step(void)
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
