/* sim.h - deterministic per-fighter step shared by server and client */

#ifndef SIM_H
#define SIM_H

#include "config.h"

typedef struct { int x, y, vx, vy, face, move, frame, hit, stun, block, flying, crouch, invis, grace, meter, fuel; } Body;

enum { EV_SPAWN = 1, EV_MELEE = 2 };    /* effects the caller (server) must apply */

#define CMD_WIN    30           /* ticks of direction history used to read motion commands */
#define CMD_RECENT 12           /* a motion must have finished this recently when the button is pressed */

/* static inline: each program uses a different subset, and inline functions never warn when unused */
static inline int lo(int a, int b) { return a < b ? a : b; }
static inline int hi(int a, int b) { return a > b ? a : b; }

/* Does the window w[0..n-1] (oldest..newest) contain the motion in seq?
   Letters: d = down, D = down+forward, f = forward, b = back, B = down+back.
   Each tick advances at most one step; extra ticks in between are forgiven. */
static inline int motion(const int *w, int n, int face, const char *seq)
{
    int fw = face > 0 ? IN_R : IN_L, bk = face > 0 ? IN_L : IN_R, st = 0, len = 0, last = -1;
    while (seq[len]) len++;
    for (int i = 0; i < n && st < len; i++) {
        int f = !!(w[i] & fw), b = !!(w[i] & bk), d = !!(w[i] & IN_D), ok;
        switch (seq[st]) {
            case 'd': ok = d; break;
            case 'D': ok = d && f; break;
            case 'f': ok = f; break;
            case 'b': ok = b; break;
            default:  ok = d && b; break;
        }
        if (ok) { st++; last = i; }
    }
    return st == len && last >= n - 1 - CMD_RECENT;
}

/* The longer motion wins, because it contains the shorter one. */
static inline int detect_cmd(const int *w, int n, int face)
{
    if (motion(w, n, face, "bBdDf")) return CMD_HCF;
    if (motion(w, n, face, "dDf")) return CMD_QCF;
    return CMD_NONE;
}

/* Which animation row (0..10: LP MP HP LK MK HK cast special super air-punch air-kick) a move uses. */
static inline int move_row(const Move *m)
{
    if (m->anim) return m->anim - 1;
    if (m->air) return (m->input & IN_P) ? 9 : 10;
    if (!m->cmd)
        return m->input == IN_LP ? 0 : m->input == IN_MP ? 1 : m->input == IN_HP ? 2 :
               m->input == IN_LK ? 3 : m->input == IN_MK ? 4 : 5;
    if (m->ship || m->cost >= 60) return 8;
    return m->proj_speed ? 6 : 7;
}

/* in: button/direction mask. cmd: motion command read from the recent input history.
   nearfoe: within throw range of the nearest opponent (lets a plain heavy punch become a throw). */
static inline int body_step(Body *body, const Character *character, int in, int look, int cmd, int nearfoe)
{
    int ev = 0;

    body->block = body->flying = body->crouch = 0;
   
    /* Count down for invisability */
    if (body->invis) body->invis--;
    
    /* Count down for invulnerability for new players */
    if (body->grace) body->grace--;

    if (body->stun) { /* Stuned players are limited */
        body->stun--;
        /* Slow player's walk if stunned */
        if (body->y == 0) body->vx = body->vx * 3 / 4;
    } else if (body->move >= 0) { /* Continue moves already in progress */
        const Move *m = &character->moves[body->move];
        int f = body->frame++;
        if (f == m->startup && (m->proj_speed || m->ship)) ev |= EV_SPAWN;
        else if (!m->proj_speed && !m->ship && m->dmg && !body->hit
                 && f >= m->startup && f < m->startup + m->active) ev |= EV_MELEE;
        if (f >= m->startup + m->active + m->recover) body->move = -1;
    } else { /* Read movement and attack input*/
        if (look) /* face enemy if present */
            body->face = look;
        
        /* Handle Ducking */
        int away = body->face > 0 ? !!(in & IN_L) : !!(in & IN_R); /* see if holding away to block */
        if (body->y == 0 && (in & IN_D)) {             /* ducking: no walking, smaller hurtbox */
            body->crouch = 1;
            body->vx = 0;
        }
        
        /* Handle Blocking */
        if (body->y == 0 && away) {             /* hold away to block */
            body->block = 1;
            body->vx = (body->face > 0 ? -character->speed : character->speed) * 2 / 3;
        } else {
            body->vx = ((in & IN_R) ? character->speed : 0) - ((in & IN_L) ? character->speed : 0);
            if (in & IN_U) { /* Jump! */
                if (body->y == 0) body->vy = character->jump;
                else if (character->fly && body->fuel > 0) {         /* tail flight: hover and climb */
                    body->flying = 1;
                    body->fuel--;
                    if (body->y >= FLY_CEIL) { if (body->vy > 0) body->vy = 0; }
                    else if (body->vy < FLY_VMAX) body->vy = lo(FLY_VMAX, body->vy + character->fly);
                }
            }
            
            /* Commands first, so QCF + punch is a fireball and not a normal punch. Ground moves
               need the ground and jump attacks need the air, so a jump kick can't be done standing. */

            int airborne = body->y > 0;
            
            for (int pass = 0; pass < 2 && body->move < 0; pass++)
                for (int k = 0; k < MAXMOVES && character->moves[k].name; k++) {
                    const Move *m = &character->moves[k];
                    if ((pass == 0) != (m->cmd != 0) || (m->air != 0) != airborne) continue;
                    if (!(in & m->input) || body->meter < m->cost || (m->cmd && m->cmd != cmd)) continue;
                    if (m->throw && !nearfoe) continue;    /* a throw only comes out within grab range */
                    body->move = k; body->frame = 0; body->hit = 0; body->flying = 0;
                    body->meter -= m->cost;
                    body->invis = m->invis;
                    if (body->y == 0) body->vx = 0;
                    break;
                }
        }
    }

    body->x = hi(character->w / 2, lo(STAGE_W - character->w / 2, body->x + body->vx));
    body->y += body->vy;
    body->vy -= GRAVITY;
    if (body->y <= 0) {
        body->y = body->vy = 0;
        if (body->move >= 0) {
            body->vx = 0;
            if (character->moves[body->move].air) body->move = -1;    /* landing ends a jump attack */
        }
        if (character->fuel) body->fuel = lo(character->fuel, body->fuel + FUEL_REGEN);
    }
    return ev;
}

#endif
