#include "server.h"

Player players[MAX_PLAYERS];
Proj   projectiles[MAXPROJ];
Ship   ships[MAX_PLAYERS];
Boom   explosions[MAXBOOM];

int    udp_fd = -1;
int    phase;
int    tick;
int    over;
int    winner;

void clear_fx(void)
{
    memset(projectiles, 0, sizeof projectiles);
    memset(ships, 0, sizeof ships);
    memset(explosions, 0, sizeof explosions);
}

void place(Player *player, int x)
{
    memset(&player->body, 0, sizeof player->body);
    player->body.x = x;
    player->body.face = 1;
    player->body.move = -1;
    player->body.fuel = chars[player->s_character].fuel;
    player->hp = chars[player->s_character].hp;
}

void check_start(void)
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

void end_match(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) { players[i].in_match = 0; players[i].ready = players[i].bot; }
    clear_fx();
    phase = LOBBY;
    over = 0;
}

void drop(int i)
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

/* Add a bot using the character and AI the client chose (not the chooser's own character). */
void add_bot(int character, int ai)
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

void del_bot(int i)
{
    if (i < 0 || i >= MAX_PLAYERS || !players[i].bot) return;
    memset(&players[i], 0, sizeof players[i]);
    players[i].fd = -1;
}
