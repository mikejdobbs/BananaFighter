#include "server.h"

Player players[MAX_PLAYERS];
Proj   projectiles[MAXPROJ];
Ship   ships[MAX_PLAYERS];
Boom   explosions[MAXBOOM];

int    udp_fd = -1;
int    srv_phase;
int    srv_tick;
int    over;
int    srv_winner;

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
        human_count += (players[player_index].fd >= 0);
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

    fprintf(stderr, "match start: %d fighters\n", fighter_count);
}

void end_match(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) { players[i].in_match = 0; players[i].ready = players[i].bot; }
    clear_fx();
    srv_phase = LOBBY;
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
