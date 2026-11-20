#include "config.h"
#include "server.h"

/* Lose a life; respawn with grace if any remain. */
static void ko(Player *player)
{
    if (--player->lives > 0) {
        place(player, player->spawn);
        player->body.grace = RESPAWN_GRACE;
    }
}

/* Top of the hurtbox (crouch halves height on the ground). */
static int hurt_top(const Player *victim, const Character *character)
{
    int h = character->h;
    if (victim->body.crouch && victim->body.y == 0)
        h = h / 2; /* crouching: half-height hurtbox dodges high attacks */
    return victim->body.y + h;
}

/* Hitstun length by button strength. */
static int hitstun_for(const Move *move)
{
    switch (move->input) {
    case IN_LP:
    case IN_LK:
        return HITSTUN - 4;
    case IN_MP:
    case IN_MK:
        return HITSTUN;
    case IN_HP:
    case IN_HK:
        return HITSTUN + 4;
    default:
        return HITSTUN;
    }
}

/* Apply damage, meter, stun/knockback; KO if HP hits zero. */
static void hurt(Player *attacker, Player *victim, const Move *move, int dir)
{
    Body *vb = &victim->body;

    if (vb->grace)
        return;

    vb->invis = 0; /* getting hit reveals you */
    attacker->body.meter = lo(METER_MAX, attacker->body.meter + move->dmg / 4);
    vb->meter = lo(METER_MAX, vb->meter + move->dmg / 8);

    if (vb->block && !move->throw) { /* chip damage only; grabs beat blocks */
        victim->hp -= move->dmg * BLOCK_PCT / 100;
        vb->vx = dir * move->kb / 8;
    } else {
        victim->hp -= move->dmg;
        if (move->throw) {
            /* Throws: no hitstun animation; lift up and nudge away */
            vb->stun = THROW_STUN;
            vb->vx = dir * (move->kb > 0 ? move->kb / 3 : THROW_KB);
            vb->vy = THROW_LIFT;
        } else {
            vb->stun = hitstun_for(move);
            vb->vx = dir * move->kb / 4;
            vb->crouch = 0;
            if (move->input == IN_HK && move->crouch) {
                /* Crouching heavy kick: trip in place, then get up */
                vb->stun = KNOCKDOWN_STUN;
                vb->vx = 0;
                vb->vy = 0;
                vb->down = 1;
                vb->crouch = 1;
            } else if (move->knockdown) {
                vb->stun = KNOCKDOWN_STUN;
                vb->vy = 30; /* pop up, then fall */
                vb->down = 1;
                vb->crouch = 1;
            }
        }
        vb->move = -1;
    }
    if (victim->hp <= 0)
        ko(victim);
}

/* Fire one projectile, or a fan of move->spread of them. */
void spawn(Player *player, int owner, const Move *move)
{
    int n = move->spread > 1 ? move->spread : 1;
    for (int s = 0; s < n; s++) {
        int vy = (2 * s - (n - 1)) * move->spread_vy / 2;
        for (int i = 0; i < MAXPROJ; i++) {
            if (projectiles[i].on)
                continue;
            int fwd = move->proj_x > 0 ? move->proj_x : chars[player->s_character].w / 2;
            projectiles[i] = (Proj){
                .on = 1,
                .owner = owner,
                .x = player->body.x + player->body.face * fwd,
                .y = player->body.y + move->y + move->h / 2,
                .vx = player->body.face * move->proj_speed,
                .vy = vy,
                .life = move->proj_life,
                .m = move};
            break;
        }
    }
}

/* Spawn a ship that flies in from behind the caller and carpet-bombs. */
void call_ship(Player *player, int owner, const Move *move)
{
    Ship *ship = &ships[owner];
    if (ship->on)
        return; /* one ship per caller at a time */
    int dir = player->body.face;
    *ship = (Ship){.on = 1,
                   .owner = owner,
                   .x = dir > 0 ? -SHIP_MARGIN : STAGE_W + SHIP_MARGIN,
                   .vx = dir * SHIP_SPEED,
                   .drops = move->ship,
                   .cd = 10,
                   .m = move};
}

/* Drop one bomb from a flying ship. */
static void drop_bomb(const Ship *ship)
{
    for (int i = 0; i < MAXPROJ; i++) {
        if (projectiles[i].on)
            continue;
        projectiles[i] = (Proj){.on = 1,
                                .owner = ship->owner,
                                .x = ship->x,
                                .y = SHIP_Y - 20,
                                .vx = ship->vx / 3,
                                .grav = BOMB_GRAV,
                                .bomb = 1,
                                .life = 400,
                                .m = ship->m};
        return;
    }
}

/* Fire a level shot from a gun ship (no gravity). */
static void fire_ship_shot(const Ship *ship)
{
    int dir = ship->vx > 0 ? 1 : -1;
    for (int i = 0; i < MAXPROJ; i++) {
        if (projectiles[i].on)
            continue;
        projectiles[i] = (Proj){
            .on = 1,
            .owner = ship->owner,
            .x = ship->x,
            .y = SHIP_Y - 40,
            .vx = dir * ship->m->proj_speed,
            .life = ship->m->proj_life > 0 ? ship->m->proj_life : 45,
            .m = ship->m};
        return;
    }
}

/* Muzzle of the held pitching machine (stage units from body origin / feet).
   Matches client draw_sprite pack overlay (see build_stephen_sprites bat_rocket_pack). */
#define PACK_MUZZLE_X 66
#define PACK_MUZZLE_Y 75

/* Spawn an exploding baseball from a rocket-pack pitch. */
static void spawn_pack_ball(Player *player, int owner, const Move *move)
{
    int fwd = move->proj_x > 0 ? move->proj_x : PACK_MUZZLE_X;
    int py = player->body.y + (move->y > 0 ? move->y : PACK_MUZZLE_Y);
    if (py < 30)
        py = 30;
    for (int i = 0; i < MAXPROJ; i++) {
        if (projectiles[i].on)
            continue;
        projectiles[i] = (Proj){
            .on = 1,
            .owner = owner,
            .x = player->body.x + player->body.face * fwd,
            .y = py,
            .vx = player->body.face * (move->proj_speed > 0 ? move->proj_speed : 11),
            .vy = -2, /* slight drop, pitching-machine arc */
            .grav = BOMB_GRAV,
            .bomb = 1,
            .life = move->proj_life > 0 ? move->proj_life : 45,
            .m = move};
        return;
    }
}

/* Pitch exploding balls during a player-steered pack flight (any move with pack=1).
   vx/vy and facing come from body_step; this only keeps the body in bounds and fires. */
void update_stephen_pack_flights(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *player = &players[i];
        if (!fighting(player) || player->body.move < 0)
            continue;
        const Character *character = &chars[player->s_character];
        const Move *move = &character->moves[player->body.move];
        if (!move->name || !move->pack)
            continue;

        int frame = player->body.frame - 1; /* frame that just ran in body_step */
        if (frame < 0)
            frame = 0;
        int flight_end = move->startup + move->active;
        if (frame >= flight_end)
            continue; /* recover: fall with gravity */

        /* Face the nearest living foe so pitches aim usefully.
           body_step already did this from look; repeat so a missing look still aims. */
        int face_dir = 0, best_dist = 1 << 30;
        for (int k = 0; k < MAX_PLAYERS; k++) {
            if (k == i || !fighting(&players[k]))
                continue;
            int d = players[k].body.x - player->body.x;
            if (d < 0)
                d = -d;
            if (d < best_dist) {
                best_dist = d;
                face_dir = players[k].body.x >= player->body.x ? 1 : -1;
            }
        }
        if (best_dist < (1 << 30))
            player->body.face = face_dir;

        /* Clamp inside the stage and flight band. Do not reverse velocity:
           the stick overwrites vx/vy on the next tick. */
        int half = character->w / 2;
        if (player->body.x < half + 4)
            player->body.x = half + 4;
        else if (player->body.x > STAGE_W - half - 4)
            player->body.x = STAGE_W - half - 4;
        if (player->body.y < PACK_Y_MIN)
            player->body.y = PACK_Y_MIN;
        else if (player->body.y > FLY_CEIL)
            player->body.y = FLY_CEIL;

        /* Pitch exploding baseballs on a fixed cadence. */
        if (frame >= move->startup) {
            int t = frame - move->startup;
            int pitch_count = move->ship > 0 ? move->ship : 8;
            if (t % SHIP_DROP == 0 && t / SHIP_DROP < pitch_count)
                spawn_pack_ball(player, i, move);
        }
    }
}

/* Spawn a boom FX and hurt everyone in radius except owner. */
static void area_damage(int owner, int x, int y, const Move *move, int kind)
{
    int cy = y < 0 ? 0 : y;
    int r = move->boom_r > 0 ? move->boom_r : BOMB_R;
    int ticks = kind == BOOM_BARF ? BARF_BOOM_TICKS : BOOM_TICKS;

    for (int k = 0; k < MAXBOOM; k++)
        if (!explosions[k].on) {
            explosions[k] = (Boom){1, x, cy, ticks, kind, owner};
            break;
        }

    for (int k = 0; k < MAX_PLAYERS; k++) {
        Player *victim = &players[k];
        const Character *vc = &chars[victim->s_character];
        if (k == owner || !fighting(victim))
            continue;

        if (overlap(x - r, x + r, cy - r, cy + r,
                    victim->body.x - vc->w / 2, victim->body.x + vc->w / 2, victim->body.y,
                    hurt_top(victim, vc)))
            hurt(&players[owner], victim, move, victim->body.x >= x ? 1 : -1);
    }
}

/* Detonate a bomb projectile (owner immune). */
static void explode(const Proj *projectile)
{
    area_damage(projectile->owner, projectile->x, projectile->y, projectile->m, BOOM_NORMAL);
}

/* Clear spin_slam carry state after an arm slam. */
static void clear_carry(Player *attacker)
{
    attacker->carry_t = 0;
    attacker->carry_move = NULL;
    attacker->carry_hits_left = 0;
    attacker->carry_hit_cd = 0;
    attacker->body.frame = 0;
    attacker->body.move = -1;
}

/* Arm-slam finish: pin victim, deal slam + AoE, clear carry. */
static void finish_arm_slam(int attacker_slot, Player *attacker, Player *victim)
{
    Move slam = *attacker->carry_move;
    slam.throw = 0;
    slam.knockdown = 1;
    slam.dmg = SKY_SLAM_DMG;
    if (slam.boom_r <= 0)
        slam.boom_r = 40;
    victim->body.x = attacker->body.x + attacker->body.face * 36;
    victim->body.y = 0;
    victim->body.grace = 0;
    hurt(attacker, victim, &slam, attacker->body.face);
    victim->body.grace = 3;
    area_damage(attacker_slot, attacker->body.x, 20, &slam, BOOM_NORMAL);
    clear_carry(attacker);
}

/* Resolve active-frame melee: boom, hits, throws, and spin_slam carry start. */
void melee(int player_index, const Move *move)
{
    Player *player = &players[player_index];
    const Character *character = &chars[player->s_character];

    /* Lion ride: hurt each foe in the whip once, and keep checking on later ticks. */
    if (move->ride) {
        int player_x0 = player->body.x + player->body.face * character->w / 2;
        int player_x1 = player_x0 + player->body.face * move->reach;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            Player *nearby = &players[i];
            const Character *nearby_ch = &chars[nearby->s_character];
            if (i == player_index || !fighting(nearby) || nearby->body.grace)
                continue;
            if (player->ride_mask & (1u << i))
                continue;
            if (!overlap(lo(player_x0, player_x1), hi(player_x0, player_x1),
                         player->body.y + move->y,
                         player->body.y + move->y + move->h,
                         nearby->body.x - nearby_ch->w / 2,
                         nearby->body.x + nearby_ch->w / 2,
                         nearby->body.y,
                         hurt_top(nearby, nearby_ch)))
                continue;
            hurt(player, nearby, move, player->body.face);
            player->ride_mask |= (1u << i);
        }
        return;
    }

    /* Point-blank boom (e.g. chicken barf uses BOOM_BARF). */
    if (move->boom) {
        area_damage(player_index, player->body.x,
                    player->body.y + move->y + move->h / 2, move,
                    move->bucket_burst ? BOOM_BARF : BOOM_NORMAL);
        player->body.hit = 1;
        if (!move->throw && move->reach <= 0)
            return;
    }

    int player_x0 = player->body.x + player->body.face * character->w / 2;
    int player_x1 = player_x0 + player->body.face * move->reach;
    int best = -1;
    /* Close standing heavy punch throws. Command specials keep their own hit. */
    int want_throw = !move->cmd && !move->crouch && !move->air && move->input == IN_HP;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *nearby = &players[i];
        const Character *nearby_ch = &chars[nearby->s_character];
        if (i == player_index || !fighting(nearby) || nearby->body.grace)
            continue;

        if (!overlap(lo(player_x0, player_x1), hi(player_x0, player_x1),
                     player->body.y + move->y,
                     player->body.y + move->y + move->h,
                     nearby->body.x - nearby_ch->w / 2,
                     nearby->body.x + nearby_ch->w / 2,
                     nearby->body.y,
                     hurt_top(nearby, nearby_ch)))
            continue;

        if (move->throw || want_throw) {
            /* Grab: remember closest victim for the throw/carry below */
            if (best < 0 || abs(nearby->body.x - player->body.x) <
                               abs(players[best].body.x - player->body.x))
                best = i;
        } else {
            hurt(player, nearby, move, player->body.face);
            player->body.hit = 1;
        }
    }

    /* Sky uppercut (spin_slam + combo_hits): launch everyone in range straight up. */
    if (move->spin_slam && move->combo_hits > 0) {
        Move launch = *move;
        launch.throw = 0;
        launch.dmg = SKY_AIR_DMG; /* uppercut 1 of 5 */
        launch.knockdown = 1;
        launch.kb = 0; /* no sideways knock — straight up */
        for (int i = 0; i < MAX_PLAYERS; i++) {
            Player *nearby = &players[i];
            const Character *nearby_ch = &chars[nearby->s_character];
            if (i == player_index || !fighting(nearby) || nearby->body.grace)
                continue;
            if (!overlap(lo(player_x0, player_x1), hi(player_x0, player_x1),
                         player->body.y + move->y,
                         player->body.y + move->y + move->h,
                         nearby->body.x - nearby_ch->w / 2,
                         nearby->body.x + nearby_ch->w / 2,
                         nearby->body.y,
                         hurt_top(nearby, nearby_ch)))
                continue;
            hurt(player, nearby, &launch, player->body.face);
            nearby->body.grace = 0;
            nearby->body.down = 0;
            nearby->body.crouch = 0;
            nearby->body.vx = 0;
            nearby->body.vy = SKY_LAUNCH_VY;
            if (nearby->body.y > SKY_Y_CAP)
                nearby->body.y = SKY_Y_CAP;
            nearby->body.x = player->body.x + player->body.face * 28;
            player->body.hit = 1;
            if (best < 0 || abs(nearby->body.x - player->body.x) <
                               abs(players[best].body.x - player->body.x))
                best = i;
        }
    }

    if (best < 0)
        return;

    /* Closest grab victim: start carry or apply a normal throw. */
    Move throw_move = *move;
    throw_move.throw = 1; /* ensure unblockable */
    if (move->spin_slam && move->combo_hits > 0) {
        /* Stay grounded; remaining uppercuts + arm slam run in update_mike_uppercut. */
        player->carry_slot = best;
        player->carry_t = SPIN_SLAM_TICKS + 40;
        player->carry_move = move;
        player->carry_hits_left = move->combo_hits - 1;
        player->carry_hit_cd = COMBO_HIT_GAP;
        player->body.vx = 0;
        player->body.vy = 0;
        player->body.y = 0;
        player->body.hit = 1;
        players[best].body.block = 0;
        players[best].body.flying = 0;
    } else {
        hurt(player, &players[best], &throw_move, player->body.face);
        player->body.hit = 1;
    }
}

/* Per-tick follow-through for spin_slam + combo_hits (sky uppercut / arm slam). */
void update_mike_uppercut(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *attacker = &players[i];
        if (attacker->carry_t <= 0 || !attacker->carry_move)
            continue;

        int victim_slot = attacker->carry_slot;
        if (victim_slot < 0 || victim_slot >= MAX_PLAYERS ||
            !fighting(&players[victim_slot]) || !fighting(attacker)) {
            clear_carry(attacker);
            attacker->body.vy = 0;
            if (attacker->body.y < 0)
                attacker->body.y = 0;
            continue;
        }

        Player *victim = &players[victim_slot];
        int uppercut_phase = (attacker->carry_hits_left > 0);

        attacker->body.block = 0;
        attacker->body.crouch = 0;
        attacker->body.flying = 0;

        /* Keep special anim index alive for clients. */
        if (attacker->body.move < 0) {
            for (int k = 0; k < MAXMOVES && chars[attacker->s_character].moves[k].name; k++)
                if (chars[attacker->s_character].moves[k].spin_slam) {
                    attacker->body.move = k;
                    break;
                }
        }

        /* Attacker stays planted for the whole special. */
        attacker->body.y = 0;
        attacker->body.vx = 0;
        attacker->body.vy = 0;

        /* Keep victim stacked in front — no sideways drift, no sky trip. */
        int above = attacker->body.x + attacker->body.face * 28;
        victim->body.vx = 0;
        if (victim->body.x < above - 4)
            victim->body.x += 6;
        else if (victim->body.x > above + 4)
            victim->body.x -= 6;
        else
            victim->body.x = above;
        if (victim->body.y > SKY_Y_CAP) {
            victim->body.y = SKY_Y_CAP;
            if (victim->body.vy > 0)
                victim->body.vy = 0;
        }
        victim->body.block = 0;
        victim->body.flying = 0;
        victim->in = 0;

        if (uppercut_phase) {
            /* Timed air uppercuts: chamber → punch; frame 50+col drives the client sheet. */
            static const int hit_cols[4] = {1, 2, 3, 2};
            int punch_i = 4 - attacker->carry_hits_left; /* 0..3 while hits remain */
            if (punch_i < 0)
                punch_i = 0;
            if (punch_i > 3)
                punch_i = 3;
            if (attacker->carry_hit_cd > COMBO_HIT_GAP / 2)
                attacker->body.frame = 50; /* fists in */
            else
                attacker->body.frame = 50 + hit_cols[punch_i];

            if (--attacker->carry_hit_cd <= 0) {
                Move air = *attacker->carry_move;
                air.throw = 0;
                air.knockdown = 0;
                air.dmg = SKY_AIR_DMG;
                air.kb = 0;
                victim->body.grace = 0;
                hurt(attacker, victim, &air, attacker->body.face);
                victim->body.grace = 0;
                victim->body.vx = 0;
                victim->body.vy = SKY_JUGGLE_VY;
                victim->body.down = 0;
                victim->body.crouch = 0;
                victim->body.stun = hi(victim->body.stun, COMBO_HIT_GAP + 4);
                attacker->carry_hits_left--;
                attacker->carry_hit_cd = COMBO_HIT_GAP;
                if (attacker->carry_hits_left <= 0)
                    attacker->carry_hit_cd = -1; /* arm-slam windup not started yet */
            }

            attacker->carry_t--;
            if (attacker->carry_t > 0)
                continue;

            /* Timeout during uppercuts: still finish with an arm slam. */
            finish_arm_slam(i, attacker, victim);
            continue;
        }

        /* Arm-slam phase: pull to arm height, wind up, then pound down.
           Victim is locked out of body_step — step y here. */
        int arm_x = attacker->body.x + attacker->body.face * 40;
        victim->body.x = arm_x;
        victim->body.vx = 0;
        victim->body.move = -1;
        victim->body.stun = hi(victim->body.stun, 4);
        victim->body.crouch = 0;
        victim->body.down = 0;

        if (victim->body.y > SKY_ARM_Y) {
            victim->body.y -= 8;
            if (victim->body.y < SKY_ARM_Y)
                victim->body.y = SKY_ARM_Y;
            victim->body.vy = 0;
        } else {
            victim->body.y = SKY_ARM_Y;
            victim->body.vy = 0;
        }

        if (attacker->carry_hit_cd < 0)
            attacker->carry_hit_cd = SKY_SLAM_WINDUP;
        attacker->body.frame = 100 + (attacker->carry_hit_cd > 0 ? attacker->carry_hit_cd : 0);

        /* Wait until at arm height before counting down the slap. */
        if (victim->body.y > SKY_ARM_Y) {
            attacker->carry_t--;
            if (attacker->carry_t > 0)
                continue;
            /* fall through to forced slam on timeout */
        } else if (--attacker->carry_hit_cd > 0) {
            attacker->carry_t--;
            if (attacker->carry_t > 0)
                continue; /* hand still coming down */
        }

        finish_arm_slam(i, attacker, victim);
    }
}

/* Advance projectiles: motion, ground/bomb, fireball cancel, fighter hits. */
void update_projectiles(void)
{
    for (int i = 0; i < MAXPROJ; i++) {
        Proj *projectile = &projectiles[i];
        if (!projectile->on)
            continue;

        projectile->x += projectile->vx;
        projectile->y += projectile->vy;
        if (projectile->grav)
            projectile->vy -= projectile->grav;

        if (--projectile->life <= 0 || projectile->x < 0 || projectile->x > STAGE_W ||
            projectile->y > 2000) {
            projectile->on = 0;
            continue;
        }

        if (projectile->y <= 0) {
            if (projectile->bomb)
                explode(projectile);
            projectile->on = 0;
            continue;
        }

        /* Opposing fireballs cancel; bombs and same-owner spreads do not. */
        if (!projectile->bomb) {
            for (int j = i + 1; j < MAXPROJ; j++) {
                Proj *other = &projectiles[j];
                if (!other->on || other->bomb || other->owner == projectile->owner)
                    continue;
                if (!overlap(projectile->x - PROJ_HALF, projectile->x + PROJ_HALF,
                             projectile->y - PROJ_HALF, projectile->y + PROJ_HALF,
                             other->x - PROJ_HALF, other->x + PROJ_HALF,
                             other->y - PROJ_HALF, other->y + PROJ_HALF))
                    continue;
                int cx = (projectile->x + other->x) / 2;
                int cy = (projectile->y + other->y) / 2;
                if (cy < 0)
                    cy = 0;
                for (int k = 0; k < MAXBOOM; k++)
                    if (!explosions[k].on) {
                        explosions[k] = (Boom){1, cx, cy, BOOM_TICKS, BOOM_NORMAL,
                                               projectile->owner};
                        break;
                    }
                projectile->on = other->on = 0;
                break;
            }
            if (!projectile->on)
                continue;
        }

        for (int target_index = 0; target_index < MAX_PLAYERS && projectile->on; target_index++) {
            Player *target = &players[target_index];
            const Character *target_ch = &chars[target->s_character];
            if (target_index == projectile->owner || !fighting(target))
                continue;
            if (overlap(projectile->x - PROJ_HALF, projectile->x + PROJ_HALF,
                        projectile->y - PROJ_HALF, projectile->y + PROJ_HALF,
                        target->body.x - target_ch->w / 2,
                        target->body.x + target_ch->w / 2, target->body.y,
                        hurt_top(target, target_ch))) {
                if (projectile->bomb)
                    explode(projectile);
                else
                    hurt(&players[projectile->owner], target, projectile->m,
                         projectile->vx > 0 ? 1 : -1);
                projectile->on = 0;
            }
        }
    }
}

/* Advance ships (drop bombs / fire shots) and age explosion FX. */
void update_ships(void)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Ship *ship = &ships[i];
        if (!ship->on)
            continue;
        ship->x += ship->vx;
        if (ship->x < -SHIP_MARGIN - 50 || ship->x > STAGE_W + SHIP_MARGIN + 50) {
            ship->on = 0;
            continue;
        }

        if (ship->drops > 0 && ship->x >= 0 && ship->x <= STAGE_W && --ship->cd <= 0) {
            if (ship->m && ship->m->proj_speed > 0)
                fire_ship_shot(ship);
            else
                drop_bomb(ship);
            ship->drops--;
            ship->cd = SHIP_DROP;
        }
    }

    for (int k = 0; k < MAXBOOM; k++)
        if (explosions[k].on && --explosions[k].t <= 0)
            explosions[k].on = 0;
}
