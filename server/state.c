#include "server.h"

#include <stdio.h>
#include <time.h>

/* Match-start PRNG for resolving Random lobby picks. */
static unsigned match_rng = 2463534242u;

static unsigned match_rand(void)
{
    match_rng ^= match_rng << 13;
    match_rng ^= match_rng >> 17;
    match_rng ^= match_rng << 5;
    return match_rng;
}

/* Roll a concrete fighter for a Random lobby pick (0..NCHARS-1). */
static int roll_character(int slot)
{
    if (NCHARS <= 1)
        return 0;
    match_rng ^= (unsigned)slot * 0x9E3779B9u + (unsigned)time(NULL) + (unsigned)srv_tick;
    return (int)(match_rand() % (unsigned)NCHARS);
}

Player players[MAX_PLAYERS];
Proj   projectiles[MAXPROJ];
Ship   ships[MAX_PLAYERS];
Boom   explosions[MAXBOOM];
Swarm  swarm;

bf_socket_t udp_fd = BF_INVALID_SOCKET;
int    srv_phase;
int    srv_tick;
int    over;
int    srv_winner;
static int fight_intro; /* 1 from match start until the announcer finishes */

/* Dedicated servers cannot hear the client mixer; freeze for the WAV length. */
static int fight_intro_ticks_from_wav(void)
{
    static int cached = -1;
    if (cached >= 0)
        return cached;

    const char *paths[] = { "sounds/fight.wav", NULL };
    cached = 0;
    for (int p = 0; paths[p]; p++) {
        FILE *f = fopen(paths[p], "rb");
        if (!f)
            continue;
        unsigned char hdr[12];
        if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
            fclose(f);
            continue;
        }
        unsigned rate = 22050, channels = 1, bits = 16, data_bytes = 0;
        for (;;) {
            unsigned char ch[8];
            if (fread(ch, 1, 8, f) != 8)
                break;
            unsigned sz = (unsigned)ch[4] | ((unsigned)ch[5] << 8) |
                          ((unsigned)ch[6] << 16) | ((unsigned)ch[7] << 24);
            if (!memcmp(ch, "fmt ", 4)) {
                unsigned char fmt[16];
                if (sz < 16 || fread(fmt, 1, 16, f) != 16)
                    break;
                channels = (unsigned)fmt[2] | ((unsigned)fmt[3] << 8);
                rate = (unsigned)fmt[4] | ((unsigned)fmt[5] << 8) |
                       ((unsigned)fmt[6] << 16) | ((unsigned)fmt[7] << 24);
                bits = (unsigned)fmt[14] | ((unsigned)fmt[15] << 8);
                if (sz > 16)
                    fseek(f, (long)sz - 16, SEEK_CUR);
            } else if (!memcmp(ch, "data", 4)) {
                data_bytes = sz;
                break;
            } else {
                fseek(f, (long)sz, SEEK_CUR);
            }
        }
        fclose(f);
        if (!data_bytes || !rate || !channels || !bits)
            continue;
        unsigned bps = channels * (bits / 8);
        if (!bps)
            continue;
        unsigned samples = data_bytes / bps;
        cached = (int)((samples * (unsigned)TICK_HZ + rate - 1) / rate);
        if (cached < 1)
            cached = 1;
        return cached;
    }
    cached = TICK_HZ; /* 1s fallback if the asset is missing */
    return cached;
}

int fight_intro_holding(void) { return fight_intro; }

void fight_intro_check_done(void)
{
    if (!fight_intro)
        return;
    if (server_embed_fight_intro_from_client()) {
        /* Local client clears the hold when its fight voice ends. */
        if (!server_embed_fight_intro_held() || srv_tick > TICK_HZ * 5)
            fight_intro = 0;
    } else if (srv_tick >= fight_intro_ticks_from_wav()) {
        fight_intro = 0;
    }
}

void clear_fx(void)
{
    memset(projectiles, 0, sizeof projectiles);
    memset(ships, 0, sizeof ships);
    memset(explosions, 0, sizeof explosions);
    memset(&swarm, 0, sizeof swarm);
}

void place(Player *player, int x)
{
    memset(&player->body, 0, sizeof player->body);
    player->body.x = x;
    player->body.face = 1;
    player->body.move = -1;
    player->body.fuel = chars[player->s_character].fuel;
    player->hp = chars[player->s_character].hp;
    player->carry_t = 0;
    player->carry_move = NULL;
    player->carry_hits_left = 0;
    player->carry_hit_cd = 0;
}

/*
 * Start a match when there are enough fighters, at least one connected
 * human, and every participating human or bot is ready.
 */
void check_start(void)
{
    int fighter_count = 0;
    int ready_count = 0;
    int human_count = 0;

    /* Count fighters, ready fighters, and connected human players. */
    for (int player_index = 0; player_index < MAX_PLAYERS; player_index++) {
        if (!player_is_person_or_bot(&players[player_index]))
            continue;

        fighter_count++;
        ready_count += players[player_index].ready;
        human_count += (players[player_index].fd != BF_INVALID_SOCKET);
    }

    /* Leave the lobby unchanged until all start conditions are met. */
    if (human_count == 0 ||
        fighter_count < MIN_PLAYERS ||
        ready_count != fighter_count) {
        return;
    }

    /* Initialize every fighter's UDP activity timer to the same time. */
    long long match_start_ms = now_ms();
    int spawn_position_index = 0;

    for (int player_index = 0; player_index < MAX_PLAYERS; player_index++) {
        Player *player = &players[player_index];

        /* Skip slots occupied by neither a human nor a bot. */
        if (!player_is_person_or_bot(player))
            continue;

        /* Resolve Random picks now — lobby kept CHAR_RANDOM until this moment. */
        if (player->s_character == CHAR_RANDOM || player->pick_random) {
            player->pick_random = 1;
            player->s_character = roll_character(player_index);
            fprintf(stderr, "slot %d: Random -> %s\n", player_index,
                    chars[player->s_character].name);
        } else {
            player->pick_random = 0;
        }

        /* Enter the match with the selected character's starting lives. */
        player->in_match = 1;
        player->lives = chars[player->s_character].lives;

        /* Reset current input, acknowledgement count, and input queue. */
        player->in = 0;
        player->num_ack_by_server = 0;
        player->q_head = 0;
        player->q_num = 0;

        /* Reset the AI thinking countdown and pending command. */
        player->think_cd = 0;
        player->cmd = 0;

        /*
         * Clients restart input sequence numbering at zero each match.
         * Clear the previous input used when filling sequence gaps.
         */
        player->next_seq = 0;
        player->last_in = 0;

        /* Start the UDP inactivity timer from the beginning of the match. */
        player->last_udp_ms = match_start_ms;

        /* Discard input history from the previous match. */
        memset(player->in_history, 0, sizeof player->in_history);
        player->in_history_n = 0;

        /*
         * Space fighters evenly across the stage, with a margin at each end.
         * For three fighters, positions are approximately 1/4, 1/2, and 3/4
         * of the stage width.
         */
        spawn_position_index++;
        player->spawn =
            STAGE_W * spawn_position_index / (fighter_count + 1);

        /* Place the fighter at its assigned spawn position. */
        place(player, player->spawn);
    }

    /* Remove effects left over from the previous match. */
    clear_fx();

    /* Enter active play and reset match-wide state. */
    srv_phase = PLAY;
    srv_tick = 0;
    over = 0;
    srv_winner = 0;
    fight_intro = 1;
    /* Listen server: hold until the client's fight voice finishes. */
    if (server_embed_fight_intro_from_client())
        server_embed_set_fight_intro_hold(1);

    fprintf(stderr, "match start: %d fighters\n", fighter_count);
}

void end_match(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i].in_match = 0;
        players[i].ready = players[i].bot;
        /* Put Random choosers back on the Random cell for the next lobby. */
        if (players[i].pick_random)
            players[i].s_character = CHAR_RANDOM;
    }
    clear_fx();
    srv_phase = LOBBY;
    over = 0;
    fight_intro = 0;
    if (server_embed_fight_intro_from_client())
        server_embed_set_fight_intro_hold(0);
}

/* Full wipe for embed stop/start so a prior Single/Host match cannot leak
 * winner/phase/bots into the next session. */
void server_reset_world(void)
{
    memset(players, 0, sizeof players);
    for (int i = 0; i < MAX_PLAYERS; i++)
        players[i].fd = BF_INVALID_SOCKET;
    clear_fx();
    srv_phase = LOBBY;
    srv_tick = 0;
    over = 0;
    srv_winner = 0;
    fight_intro = 0;
}

void drop(int i)
{
    if (players[i].fd != BF_INVALID_SOCKET)
        bf_close(players[i].fd);

    for (int a = 0; a < MAX_PLAYERS; a++)
        if (players[a].carry_t > 0 && players[a].carry_slot == i) {
            players[a].carry_t = 0;
            players[a].carry_move = NULL;
            players[a].carry_hits_left = 0;
            players[a].carry_hit_cd = 0;
        }

    memset(&players[i], 0, sizeof players[i]);      /* also wipes the token and UDP state */
    players[i].fd = BF_INVALID_SOCKET;
    ships[i].on = 0;
    
    for (int j = 0; j < MAXPROJ; j++)
        if (projectiles[j].owner == i) projectiles[j].on = 0;
    
    fprintf(stderr, "slot %d left\n", i);
}

/* Add a bot using the character and AI the client chose (not the chooser's own character).
   character == CHAR_RANDOM: bot stays on Random until match start. */
void add_bot(int character, int ai)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (player_is_person_or_bot(&players[i])) continue;
        int ch = 0, rnd = 0;
        if (character == CHAR_RANDOM) {
            ch = CHAR_RANDOM;
            rnd = 1;
        } else if (character >= 0 && character < NCHARS) {
            ch = character;
        }
        players[i] = (Player){ .fd = BF_INVALID_SOCKET, .bot = 1, .ready = 1,
                               .s_character = ch, .pick_random = rnd,
                               .ai = (ai >= 0 && ai < NAIS) ? ai : 0, .rng = 2463534242u + 7919u * i };
        players[i].body.move = -1;
        fprintf(stderr, "slot %d: bot %s (%s)\n", i, ais[players[i].ai].name,
                rnd ? "Random" : chars[players[i].s_character].name);
        return;
    }
}

void del_bot(int i)
{
    if (i < 0 || i >= MAX_PLAYERS || !players[i].bot) return;
    memset(&players[i], 0, sizeof players[i]);
    players[i].fd = BF_INVALID_SOCKET;
}
