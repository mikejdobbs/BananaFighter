/* sim.h - deterministic per-fighter step shared by server and client */

#ifndef SIM_H
#define SIM_H

#include "config.h"

typedef struct { int x, y, vx, vy, face, move, frame, hit, stun, block, flying, crouch, down, invis, grace, meter, fuel; } Body;

enum { EV_SPAWN = 1, EV_MELEE = 2 };    /* effects the caller (server) must apply */

#define CMD_WIN    30           /* ticks of direction history used to read motion commands */
#define CMD_RECENT 12           /* a motion must have finished this recently when the button is pressed */

/* static inline: each program uses a different subset, and inline functions never warn when unused */
static inline int lo(int a, int b) { return a < b ? a : b; }
static inline int hi(int a, int b) { return a > b ? a : b; }

static inline int jump_vel(const Character *c)
{
    return c->jump * JUMP_NUM / JUMP_DEN;
}

/* One motion step. Extra letters for the dragon-punch (straight, down, straight):
   s = toward only, v = down only, S = toward (down may still be held).
   u = up (not down). */
static inline int step_ok(int bits, int face, char kind)
{
    int fw = face > 0 ? IN_R : IN_L, bk = face > 0 ? IN_L : IN_R;
    int f = !!(bits & fw), b = !!(bits & bk), d = !!(bits & IN_D);
    int u = !!(bits & IN_U);
    switch (kind) {
    case 'd': return d;
    case 'D': return d && f;
    case 'f': return f;
    case 'b': return b;
    case 'u': return u && !d;
    case 's': return f && !d && !b;
    case 'v': return d && !f && !b;
    case 'S': return f && !b;
    default:  return d && b;
    }
}

/* Does the window contain seq, finished within the last `recent` ticks?
   Every start index is tried, so an older completed motion cannot hide a new one. */
static inline int motion(const int *w, int n, int face, const char *seq, int recent)
{
    int len = 0;
    while (seq[len]) len++;
    for (int start = 0; start < n; start++) {
        int st = 0, last = -1;
        for (int i = start; i < n && st < len; i++) {
            if (step_ok(w[i], face, seq[st])) {
                st++;
                last = i;
            }
        }
        if (st == len && last >= n - 1 - recent)
            return 1;
    }
    return 0;
}

/* The longer motion wins, because it contains the shorter one. */
static inline int detect_cmd(const int *w, int n, int face)
{
    /* Full circle (SPD): toward, down, back, up — either direction. Before HCF. */
    if (motion(w, n, face, "fdbu", CMD_RECENT + 10) ||
        motion(w, n, face, "fubd", CMD_RECENT + 10))
        return CMD_360;
    if (motion(w, n, face, "bBdDf", CMD_RECENT)) return CMD_HCF;
    /* Forward, down, forward (dragon punch / "straight down straight").
       Toward-only first so a plain QCF (d,df,f) does not count as FDF.
       Accept pure down (v) or down-forward (D) in the middle — players often
       roll through the corner (f,df,f) instead of tapping clean down.
       Final toward may keep down held (S). Either facing. Before QCF. */
    if (motion(w, n, face, "svS", CMD_RECENT + 8) || motion(w, n, -face, "svS", CMD_RECENT + 8) ||
        motion(w, n, face, "sDS", CMD_RECENT + 8) || motion(w, n, -face, "sDS", CMD_RECENT + 8))
        return CMD_FDF;
    if (motion(w, n, face, "dDf", CMD_RECENT)) return CMD_QCF;
    return CMD_NONE;
}

/* Motion steps for UI arrows (toward = right on select / HUD). */
enum { ARR_DOWN = 0, ARR_TOWARD, ARR_AWAY, ARR_DOWN_TOWARD, ARR_DOWN_AWAY, ARR_UP };

static inline int cmd_arrow_steps(int cmd, int *steps, int max)
{
    switch (cmd) {
    case CMD_360:
        if (max < 4) return 0;
        steps[0] = ARR_TOWARD;
        steps[1] = ARR_DOWN;
        steps[2] = ARR_AWAY;
        steps[3] = ARR_UP;
        return 4;
    case CMD_QCF:
        if (max < 3) return 0;
        steps[0] = ARR_DOWN;
        steps[1] = ARR_DOWN_TOWARD;
        steps[2] = ARR_TOWARD;
        return 3;
    case CMD_HCF:
        if (max < 5) return 0;
        steps[0] = ARR_AWAY;
        steps[1] = ARR_DOWN_AWAY;
        steps[2] = ARR_DOWN;
        steps[3] = ARR_DOWN_TOWARD;
        steps[4] = ARR_TOWARD;
        return 5;
    case CMD_FDF:
        if (max < 3) return 0;
        steps[0] = ARR_TOWARD;
        steps[1] = ARR_DOWN;
        steps[2] = ARR_TOWARD;
        return 3;
    default:
        return 0;
    }
}

/* Which animation row (0..22) a move uses; the sheet row is 7 + this.
   0..10 existing; 11 crouching block (drawn from snapshot state -6, not a move);
   12..17 crouching LP MP HP LK MK HK; 18..21 jump LP HP LK HK; 22 ground throw (grab row).
   Medium jump attacks, and the old shared P/K jump moves, use 9 and 10. */
static inline int move_row(const Move *m)
{
    if (m->anim) return m->anim - 1;
    if (m->throw && !m->air) return 22;
    if (m->air) {
        switch (m->input) {
        case IN_LP: return 18;
        case IN_HP: return 19;
        case IN_LK: return 20;
        case IN_HK: return 21;
        default:    return (m->input & IN_P) ? 9 : 10;
        }
    }
    if (!m->cmd) {
        int strength = m->input == IN_LP ? 0 : m->input == IN_MP ? 1 : m->input == IN_HP ? 2 :
                       m->input == IN_LK ? 3 : m->input == IN_MK ? 4 : 5;
        return (m->crouch ? 12 : 0) + strength;
    }
    if (m->pack || m->ship || m->rain || m->hat || m->cost >= 60) return 8;
    return m->proj_speed ? 6 : 7;
}

/* in: button/direction mask. cmd: motion command read from the recent input history.
   nearfoe: within throw range of the nearest opponent (lets a plain heavy punch become a throw). */
static inline int body_step(Body *body, const Character *character,
                            int in, int look, int cmd, int nearfoe)
{
    int ev = 0;

    int knocked = body->down && body->stun; /* sweep trip: stay down until get-up */
    body->block = body->flying = body->crouch = 0;

    /* Count down for invisibility */
    if (body->invis) body->invis--;

    /* Count down for invulnerability for new players */
    if (body->grace) body->grace--;

    if (body->stun) { /* Stunned players are limited */
        body->stun--;
        if (knocked && body->stun > GETUP_TICKS) {
            body->crouch = 1; /* still on the ground */
            body->vx = 0;
        } else if (body->y == 0) {
            /* Slow player's walk if stunned */
            body->vx = body->vx * 3 / 4;
        }
        if (!body->stun)
            body->down = 0;

    } else if (body->move >= 0) { /* Continue moves already in progress */
        const Move *m = &character->moves[body->move];

        /* A crouching attack stays crouched for its whole duration,
           even if the player releases down. */
        body->crouch = (body->y == 0 && m->crouch);

        int f = body->frame++;
        /* Lion ride: stay on the ground and dash. Hits are rechecked every active tick. */
        if (m->ride) {
            body->crouch = 0;
            body->y = 0;
            body->vy = 0;
            body->vx = (f < m->startup + m->active) ? body->face * m->ride : 0;
        }
        /* Rocket-pack supers: lift off, then the stick steers. Gravity is skipped
           while the pack is active. Pitches stay in update_stephen_pack_flights. */
        if (m->pack) {
            body->crouch = 0;
            body->flying = 1;
            if (f == 0)
                body->vy = 14;
            else if (f >= m->startup && f < m->startup + m->active) {
                int fly = character->speed > 0 ? character->speed + 1 : 7;
                body->vx = ((in & IN_R) ? fly : 0) - ((in & IN_L) ? fly : 0);
                body->vy = ((in & IN_U) ? fly : 0) - ((in & IN_D) ? fly : 0);
                if (look)
                    body->face = look;
            }
        }
        /* leap_vy moves (uppercut) already left the ground on the start tick.
           Other leaps rise when they become active. A ride never jumps. */
        if (m->leap && !m->ride && !m->leap_vy && !m->crouch && f == m->startup && body->y == 0) {
            int vy = jump_vel(character);
            if (vy > 0) body->vy = vy;
        }
        int leap_from = m->leap_vy ? 0 : m->startup;
        if (!m->ride && m->leap && f >= leap_from && f < m->startup + m->active + m->recover)
            body->vx = body->face * m->leap;
        /* pack pitches are fired by update_stephen_pack_flights, not EV_SPAWN.
           A ride keeps swinging for the whole dash so newcomers in the path get hit. */
        if (m->ride && f >= m->startup && f < m->startup + m->active)
            ev |= EV_MELEE;
        else if (f == m->startup && !m->pack && (m->proj_speed || m->ship || m->rain || m->hat))
            ev |= EV_SPAWN;
        else if (!m->proj_speed && !m->ship && !m->pack && !m->rain && !m->hat && (m->dmg || m->boom)
                 && !body->hit && f >= m->startup && f < m->startup + m->active)
            ev |= EV_MELEE;
        if (f >= m->startup + m->active + m->recover) body->move = -1;

    } else { /* Read movement and attack input */
        if (look) /* face enemy if present */
            body->face = look;

        int away = body->face > 0 ? !!(in & IN_L) : !!(in & IN_R);
        int airborne = body->y > 0;
        int ducking = !airborne && (in & IN_D);   /* holding down on the ground */
        int want_jump = !airborne && (in & IN_U); /* jump beats standing block (back + up) */
        int blocking = !airborne && away && !want_jump;

        /* Ducking and blocking are independent: down + away is a crouching block. */
        if (ducking) body->crouch = 1;
        if (blocking) body->block = 1;

        if ((ducking && !want_jump) || blocking) {
            /* No walk/jump while ducking or blocking. A standing block backs away slowly. */
            if (blocking && !ducking)
                body->vx = (body->face > 0 ? -character->speed : character->speed) * 2 / 3;
            else
                body->vx = 0;
        } else {
            /* Walk, jump (including backward), or tail flight */
            body->vx = ((in & IN_R) ? character->speed : 0) - ((in & IN_L) ? character->speed : 0);
            if (in & IN_U) {
                if (body->y == 0) body->vy = jump_vel(character);
                else if (character->fly && body->fuel > 0) { /* tail flight: hover and climb */
                    body->flying = 1;
                    body->fuel--;
                    if (body->y >= FLY_CEIL) { if (body->vy > 0) body->vy = 0; }
                    else if (body->vy < FLY_VMAX) body->vy = lo(FLY_VMAX, body->vy + character->fly);
                }
            }
        }

        /* Attack selection runs for every idle posture: standing, ducking and blocking.
           Close standing heavy punch throws. Any motion command wins, so
           down-forward strong punch fires the special instead of grabbing. */
        if (!airborne && !ducking && nearfoe && (in & IN_HP) && !cmd) {
            for (int k = 0; k < MAXMOVES && character->moves[k].name && body->move < 0; k++) {
                const Move *m = &character->moves[k];
                if (!m->throw || m->air || m->cmd || m->crouch) continue;
                if (!(in & m->input) || body->meter < m->cost) continue;
                body->move = k; body->frame = 0; body->hit = 0; body->flying = 0;
                body->block = 0;
                body->crouch = 0;
                body->meter -= m->cost;
                body->invis = m->invis;
                if (body->y == 0) body->vx = 0;
            }
        }
        for (int pass = 0; pass < 2 && body->move < 0; pass++)
            for (int k = 0; k < MAXMOVES && character->moves[k].name; k++) {
                const Move *m = &character->moves[k];
                if ((pass == 0) != (m->cmd != 0) || (m->air != 0) != airborne) continue;

                /* Ordinary ground attacks must match the posture. Specials are exempt. */
                if (!airborne && !m->cmd && (m->crouch != 0) != (ducking != 0)) continue;

                if (!(in & m->input) || body->meter < m->cost) continue;
                /* cmd2 is optional. An unset cmd2 is 0, which is also CMD_NONE,
                   so it must not count as a match when no motion was entered. */
                if (m->cmd && cmd != m->cmd && !(m->cmd2 && cmd == m->cmd2)) continue;
                /* Plain grabs need range. Command throws (Toadie gulp, etc.) still
                   start out of range so the motion is not eaten — they whiff on active. */
                if (m->throw && !m->cmd && !nearfoe) continue;

                body->move = k; body->frame = 0; body->hit = 0; body->flying = 0;
                body->block = 0;                          /* an attack replaces blocking */
                body->crouch = (!airborne && m->crouch);  /* posture comes from the move */
                body->meter -= m->cost;
                body->invis = m->invis;
                if (m->invuln)
                    body->grace = m->startup + m->active + m->recover;
                /* spin_slam leaps only after a successful grab (server melee). */
                if (m->ride) {
                    body->y = 0;
                    body->vy = 0;
                    body->vx = body->face * m->ride;
                } else if (m->leap_vy && !m->spin_slam) {
                    body->vy = m->leap_vy; /* leave the ground on this tick */
                    if (m->leap) body->vx = body->face * m->leap;
                } else if (body->y == 0) body->vx = 0;
                break;
            }
    }

    /* Apply movement, clamped to the stage */
    body->x = hi(character->w / 2, lo(STAGE_W - character->w / 2, body->x + body->vx));
    /* A ride that has reached the wall it is running toward starts recovery. */
    if (body->move >= 0) {
        const Move *rm = &character->moves[body->move];
        if (rm->ride && body->frame > rm->startup &&
            body->frame < rm->startup + rm->active) {
            int half = character->w / 2;
            int at_edge = (body->face > 0 && body->x >= STAGE_W - half) ||
                          (body->face < 0 && body->x <= half);
            if (at_edge) {
                body->frame = rm->startup + rm->active;
                body->vx = 0;
            }
        }
    }
    body->y += body->vy;
    /* Pack flight ignores gravity; the stick sets vx/vy. Recover falls normally. */
    {
        int pack_air = 0;
        if (body->move >= 0) {
            const Move *lm = &character->moves[body->move];
            /* frame was already incremented in the move branch */
            pack_air = lm->pack && body->frame <= lm->startup + lm->active;
        }
        if (pack_air) {
            int half = character->w / 2;
            if (body->x < half + 4)
                body->x = half + 4;
            else if (body->x > STAGE_W - half - 4)
                body->x = STAGE_W - half - 4;
            if (body->y < PACK_Y_MIN)
                body->y = PACK_Y_MIN;
            else if (body->y > FLY_CEIL)
                body->y = FLY_CEIL;
        } else
            body->vy -= GRAVITY;
    }

    /* Landing. Pack flight stays inside the altitude band above. */
    if (body->y <= 0) {
        body->y = body->vy = 0;
        if (body->move >= 0) {
            const Move *lm = &character->moves[body->move];
            if (!lm->leap && !(lm->ride && body->frame <= lm->startup + lm->active))
                body->vx = 0;
            if (lm->air) body->move = -1; /* landing ends a jump attack */
        }
        if (character->fuel) body->fuel = lo(character->fuel, body->fuel + FUEL_REGEN);
    }

    return ev;
}

#endif
