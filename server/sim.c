#include "server.h"

/* Build the AI plugin's view and return the input bits to press. */
static int think(int i)
{
    Player *player = &players[i];
    int target = nearest(i);
    player->cmd = 0;

    if (target < 0)
        return 0;

    AiView aiView = {
        .me = &chars[player->s_character],
        .foe = &chars[players[target].s_character],
        .self = &player->body,
        .tgt = &players[target].body,
        .rng = &player->rng,
        .shot = 9999,
    };
    aiView.dx = players[target].body.x - player->body.x;

    if (players[target].body.invis)
        aiView.dx += ai_rand(&aiView, 300) - 150; /* bots can't see invisible fighters */
    aiView.dist = abs(aiView.dx);

    for (int j = 0; j < MAXPROJ; j++) /* shots and bombs both count as threats */
        if (projectiles[j].on && projectiles[j].owner != i &&
            (projectiles[j].bomb ||
             (projectiles[j].vx > 0 ? projectiles[j].x < player->body.x
                                     : projectiles[j].x > player->body.x)))
            aiView.shot = lo(aiView.shot, abs(projectiles[j].x - player->body.x));

    int input_from_ai = ais[player->ai].think(&aiView);
    player->cmd = aiView.cmd;
    return input_from_ai;
}

/* Consume one queued human input (and drop excess latency); advances server acks. */
static void take_input(Player *player)
{
    while (player->q_num > INPUT_QLAG) { /* bound latency: drop the oldest */
        player->q_head = (player->q_head + 1) % INPUT_QMAX;
        player->q_num--;
        player->num_ack_by_server++;
    }
    if (player->q_num) {
        player->in = player->q[player->q_head];
        player->q_head = (player->q_head + 1) % INPUT_QMAX;
        player->q_num--;
        player->num_ack_by_server++;
    }
}

/* True when carry physics owns this body (carrier or arm-slam victim). */
static int locked_out(int i)
{
    if (players[i].carry_t > 0)
        return 1;
    /* Arm-slam victim only; free to float during sky-uppercut juggle. */
    for (int a = 0; a < MAX_PLAYERS; a++)
        if (players[a].carry_t > 0 && players[a].carry_slot == i &&
            players[a].carry_hits_left <= 0)
            return 1;
    return 0;
}

/* One tick of fighter control: input, motion detect, body_step, spawn/melee. */
static void update_player(int i)
{
    Player *player = &players[i];
    const Character *character = &chars[player->s_character];
    int foe = nearest(i);
    int look = foe >= 0 ? (players[foe].body.x >= player->body.x ? 1 : -1) : 0;
    int nearfoe = foe >= 0 && abs(players[foe].body.x - player->body.x) <= THROW_RANGE;
    int cmd;

    /* Carry pair: update_mike_uppercut owns leap / arm-hold / slam physics. */
    if (locked_out(i)) {
        if (!player->bot)
            take_input(player); /* discard mash, keep acks moving */
        player->in = 0;
        return;
    }

    if (!player->bot)
        take_input(player);
    else if (player->think_cd-- <= 0) { /* bots hold input between thoughts */
        player->in = think(i);
        player->think_cd = AI_THINK_TICKS;
    }

    player->in_history[player->in_history_n++ % 32] = player->in;

    if (player->bot) {
        cmd = player->cmd;
    } else {
        /* Motion commands from the last CMD_WIN consumed inputs (oldest first). */
        int input_window[CMD_WIN];
        for (int slot = 0; slot < CMD_WIN; slot++) {
            int hist_idx = (int)player->in_history_n - (CMD_WIN - slot);
            input_window[slot] =
                hist_idx >= 0 ? player->in_history[hist_idx % 32] : 0;
        }
        cmd = detect_cmd(input_window, CMD_WIN, look ? look : player->body.face);
    }

    int ev = body_step(&player->body, character, player->in, look, cmd, nearfoe);

    if (ev & EV_SPAWN) {
        const Move *move = &character->moves[player->body.move];
        /* Pack pitches are fired in update_stephen_pack_flights, not here. */
        if (!move->pack) {
            if (move->ship)
                call_ship(player, i, move);
            else
                spawn(player, i, move);
        }
    }

    if (ev & EV_MELEE)
        melee(i, &character->moves[player->body.move]);
}

/* One server tick: lobby / intro / play update, then win check. */
void step(void)
{
    if (srv_phase == LOBBY) {
        check_start();
        return;
    }

    if (over) {
        if (++over > ROUND_WAIT)
            end_match();
        return;
    }

    /* Hold everyone still until the fight announcer finishes. */
    if (fight_intro_holding()) {
        for (int i = 0; i < MAX_PLAYERS; i++)
            if (fighting(&players[i]) && !players[i].bot)
                take_input(&players[i]); /* discard mash, keep acks moving */
        srv_tick++;
        fight_intro_check_done();
        return;
    }

    for (int i = 0; i < MAX_PLAYERS; i++)
        if (fighting(&players[i]))
            update_player(i);

    update_mike_uppercut();
    update_stephen_pack_flights();
    update_ships();
    update_projectiles();

    int n = 0, last = -1;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (fighting(&players[i])) {
            n++;
            last = i;
        }

    if (n <= 1) {
        over = 1;
        srv_winner = n ? last + 1 : 0; /* winner: slot + 1, 0 = none */
    }
    srv_tick++;
}
