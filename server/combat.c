#include "config.h"
#include "server.h"

static void ko(Player *player) {
    if (--player->lives > 0) {
        place(player, player->spawn);
        player->body.grace = RESPAWN_GRACE;
    }
}

static int hurt_top(const Player *v, const Character *vc) {
    int h = vc->h;
    if (v->body.crouch && v->body.y == 0)
        h = h / 2; /* crouching: half-height hurtbox dodges high attacks */
    return v->body.y + h;
}

static void hurt(Player *a, Player *v, const Move *m, int dir) {
    Body *vb = &v->body;

    if (vb->grace)
        return;

    vb->invis = 0; /* getting hit reveals you */
    a->body.meter = lo(METER_MAX, a->body.meter + m->dmg / 4);
    vb->meter = lo(METER_MAX, vb->meter + m->dmg / 8);

    if (vb->block && !m->throw) { /* chip damage only; grabs beat blocks */
        v->hp -= m->dmg * BLOCK_PCT / 100;
        vb->vx = dir * m->kb / 8;
    } else {
        v->hp -= m->dmg;
        if (m->throw) {
            /* Throws: no hitstun animation, apply strong vertical lift and some horizontal reposition */
            vb->stun = THROW_STUN;
            vb->vx = dir * (m->kb > 0 ? m->kb : THROW_KB);
            vb->vy = THROW_LIFT;
        } else {
            /* Apply standard hitstun and horizontal knockback */
            vb->stun = HITSTUN;
            vb->vx = dir * m->kb / 4;
            vb->crouch = 0;
            /* Crouching heavy kick: trip — fall in place, then get up */
            if (m->input == IN_HK && m->crouch) {
                vb->stun = KNOCKDOWN_STUN;
                vb->vx = 0;
                vb->vy = 0;
                vb->down = 1;
                vb->crouch = 1; /* stay down until get-up window */
            }
        }
        vb->move = -1;
    }
    if (v->hp <= 0)
        ko(v);
}

/* Fire one projectile, or a fan of m->spread of them. */
void spawn(Player *p, int owner, const Move *m) {
    int n = m->spread > 1 ? m->spread : 1;
    for (int s = 0; s < n; s++) {
        int vy = (2 * s - (n - 1)) * m->spread_vy / 2;
        for (int i = 0; i < MAXPROJ; i++) {
            if (projectiles[i].on)
                continue;
            projectiles[i] = (Proj){
                .on = 1,
                .owner = owner,
                .x = p->body.x + p->body.face * chars[p->s_character].w / 2,
                .y = p->body.y + m->y + m->h / 2,
                .vx = p->body.face * m->proj_speed,
                .vy = vy,
                .life = m->proj_life,
                .m = m};
            break;
        }
    }
}

/* A ship flies in from the edge behind the caller and carpet-bombs the stage.
 */
void call_ship(Player *p, int owner, const Move *m) {
    Ship *s = &ships[owner];
    if (s->on)
        return; /* one ship per caller at a time */
    int dir = p->body.face;
    *s = (Ship){.on = 1,
                .owner = owner,
                .x = dir > 0 ? -SHIP_MARGIN : STAGE_W + SHIP_MARGIN,
                .vx = dir * SHIP_SPEED,
                .drops = m->ship,
                .cd = 10,
                .m = m};
}

static void drop_bomb(const Ship *s) {
    for (int i = 0; i < MAXPROJ; i++) {
        if (projectiles[i].on)
            continue;
        projectiles[i] = (Proj){.on = 1,
                                .owner = s->owner,
                                .x = s->x,
                                .y = SHIP_Y - 20,
                                .vx = s->vx / 3,
                                .grav = BOMB_GRAV,
                                .bomb = 1,
                                .life = 400,
                                .m = s->m};
        return;
    }
}

static void area_damage(int owner, int x, int y, const Move *m)
{
    int cy = y < 0 ? 0 : y;

    for (int k = 0; k < MAXBOOM; k++)
        if (!explosions[k].on) {
            explosions[k] = (Boom){1, x, cy, BOOM_TICKS};
            break;
        }

    for (int k = 0; k < MAX_PLAYERS; k++) {
        Player *v = &players[k];
        const Character *vc = &chars[v->s_character];
        if (k == owner || !fighting(v))
            continue;

        if (overlap(x - BOMB_R, x + BOMB_R, cy - BOMB_R, cy + BOMB_R,
                    v->body.x - vc->w / 2, v->body.x + vc->w / 2, v->body.y,
                    hurt_top(v, vc)))
            hurt(&players[owner], v, m, v->body.x >= x ? 1 : -1);
    }
}

/* Area damage around a bomb; the owner is immune. */
static void explode(const Proj *j)
{
    area_damage(j->owner, j->x, j->y, j->m);
}

void melee(int player_index, const Move *move) {
    Player *player = &players[player_index];
    const Character *character = &chars[player->s_character];

    if (move->boom) {
        area_damage(player_index, player->body.x,
                    player->body.y + move->y + move->h / 2, move);
        player->body.hit = 1;
        if (!move->throw && move->reach <= 0)
            return;
    }

    int player_x0 = player->body.x + player->body.face * character->w / 2;
    int player_x1 = player_x0 + player->body.face * move->reach;
    int best = -1;
    int want_throw = (move->input == IN_HP); /* high punch triggers throw when close */

    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *nearby_player = &players[i];
        const Character *nearby_character = &chars[nearby_player->s_character];
        if (i == player_index || !fighting(nearby_player) ||
            nearby_player->body.grace)
            continue;

        if (!overlap(lo(player_x0, player_x1), hi(player_x0, player_x1),
                     player->body.y + move->y,
                     player->body.y + move->y + move->h,
                     nearby_player->body.x - nearby_character->w / 2,
                     nearby_player->body.x + nearby_character->w / 2,
                     nearby_player->body.y,
                     hurt_top(nearby_player, nearby_character)))
            continue;
        
        /* If this move is a designated throw OR we're pressing high punch near an opponent, pick closest and throw */
        if (move->throw || want_throw) {
            if (best < 0 || abs(nearby_player->body.x - player->body.x) <
                               abs(players[best].body.x - player->body.x)) {
                best = i;
            }
        } else {
            /* Non-throw melee should hurt all players in range */
            hurt(player, nearby_player, move, player->body.face);
            player->body.hit = 1;
        }
    }
    if (best >= 0) { /* a grab holds only the closest victim */
        Move throwMove = *move;
        throwMove.throw = 1; /* ensure unblockable */
        /* Treat high punch close as a throw even if original move wasn't flagged */
        hurt(player, &players[best], &throwMove, player->body.face);
        player->body.hit = 1;
    }
}

void update_projectiles(void) {
    for (int i = 0; i < MAXPROJ; i++) {
        Proj *projectile = &projectiles[i];
        if (!projectile->on)
            continue;
        projectile->x += projectile->vx;
        projectile->y += projectile->vy;
        if (projectile->grav)
            projectile->vy -= projectile->grav;
        if (--projectile->life <= 0 || projectile->x < 0 || projectile->x > STAGE_W || projectile->y > 2000) {
            projectile->on = 0;
            continue;
        }
        if (projectile->y <= 0) { /* reached the ground */
            if (projectile->bomb)
                explode(projectile);
            projectile->on = 0;
            continue;
        }
        for (int targetIndex = 0; targetIndex < MAX_PLAYERS && projectile->on; targetIndex++) {
            Player *target = &players[targetIndex];
            const Character *targetChar = &chars[target->s_character];
            if (targetIndex == projectile->owner || !fighting(target))
                continue;
            if (overlap(projectile->x - PROJ_HALF, projectile->x + PROJ_HALF, projectile->y - PROJ_HALF,
                        projectile->y + PROJ_HALF, target->body.x - targetChar->w / 2,
                        target->body.x + targetChar->w / 2, target->body.y, hurt_top(target, targetChar))) {
                if (projectile->bomb)
                    explode(projectile);
                else
                    hurt(&players[projectile->owner], target, projectile->m, projectile->vx > 0 ? 1 : -1);
                projectile->on = 0;
            }
        }
    }
}

void update_ships(void) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Ship *s = &ships[i];
        if (!s->on)
            continue;
        s->x += s->vx;
        if (s->x < -SHIP_MARGIN - 50 || s->x > STAGE_W + SHIP_MARGIN + 50) {
            s->on = 0;
            continue;
        }

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

