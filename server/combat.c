#include "server.h"

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
void spawn(Player *p, int owner, const Move *m)
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
void call_ship(Player *p, int owner, const Move *m)
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

void melee(int i, const Move *m)
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

void update_projectiles(void)
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

void update_ships(void)
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
