/* ai.h - bot AI plugins, dwm style.
   Plugins choose an input mask. ai_special() also sets v->cmd, the motion command the bot
   "performs": bots don't enter motions, they just report the command to the simulation.
   Difficulty (Easy/Normal/Hard) scales think rate, special frequency, and edge pressure. */
#ifndef AI_H
#define AI_H

#include "sim.h"

enum { DIFF_EASY = 0, DIFF_NORMAL = 1, DIFF_HARD = 2 };

/* Ticks between bot decisions; Hard reacts faster, Easy slower. */
static inline int ai_think_ticks(int diff)
{
	if (diff <= DIFF_EASY) return 8;
	if (diff >= DIFF_HARD) return 3;
	return 5;
}

#define AI_THINK_TICKS 5 /* Normal default; prefer ai_think_ticks(diff) */

typedef struct {
	const Character *me, *foe;
	const Body *self, *tgt;
	int dx, dist, shot;
	int diff;                       /* DIFF_EASY / NORMAL / HARD */
	int cmd;                        /* output: motion command to perform with the returned button */
	unsigned *rng;
} AiView;

typedef int (*AiFn)(AiView *);
typedef struct { const char *name; AiFn think; } Ai;

static inline int ai_rand(AiView *v, int n)
{
	if (n <= 1) return 0;
	*v->rng = *v->rng * 1664525u + 1013904223u;
	return (int)((*v->rng >> 16) % (unsigned)n);
}

static inline int ai_walk(const AiView *v, int toward)
{
	int right = (v->dx > 0) == (toward != 0);
	return right ? IN_R : IN_L;
}

static inline int ai_in_reach(const AiView *v, const Move *m)
{
	return v->dist - v->foe->w / 2 <= v->me->w / 2 + m->reach;
}

/* Foe camping near the stage edge — pressure them instead of giving free space. */
static inline int ai_foe_edged(const AiView *v)
{
	return v->tgt->x < 150 || v->tgt->x > STAGE_W - 150;
}

static inline int ai_is_super(const Move *m)
{
	return m->cost > 0 || m->ship || m->hat || m->swarm || m->rain || m->ride;
}

static inline int ai_is_uppercut(const Move *m)
{
	return m->cmd == CMD_FDF && !m->proj_speed && (m->reach || m->leap_vy || m->spin_slam || m->throw);
}

static inline int ai_is_projectile(const Move *m)
{
	return m->proj_speed || m->ship || m->hat || m->swarm || m->rain;
}

/* strongest ground normal that reaches the target (air moves and commands are skipped) */
static inline int ai_melee(const AiView *v)
{
	int in = 0, dmg = 0;
	for (int k = 0; k < MAXMOVES && v->me->moves[k].name; k++) {
		const Move *m = &v->me->moves[k];
		if (m->air || m->cmd || m->crouch || m->proj_speed || m->ship || m->hat || m->swarm || !m->dmg || !ai_in_reach(v, m)) continue;
		if (m->dmg > dmg) { dmg = m->dmg; in = m->input; }
	}
	return in;
}

/* Score eligible cmd moves: supers when metered, FDF uppercuts in range, projectiles farther out. */
static inline int ai_special(AiView *v)
{
	int best_in = 0, best_score = -1, best_cmd = 0;
	for (int k = 0; k < MAXMOVES && v->me->moves[k].name; k++) {
		const Move *m = &v->me->moves[k];
		if (!m->cmd || v->self->meter < m->cost) continue;

		int ranged = ai_is_projectile(m);
		int close_ok = v->dist < 220 || (m->reach && ai_in_reach(v, m));
		if (!ranged && !close_ok) continue;
		/* Melee FDF/HCF need to actually reach. */
		if (!ranged && m->reach && !ai_in_reach(v, m) && !m->boom) continue;

		int score = m->dmg;
		if (ai_is_super(m))
			score += 400;
		if (ai_is_uppercut(m) && ai_in_reach(v, m))
			score += 250;
		else if (ai_is_uppercut(m))
			score += 40;
		if (ranged && v->dist >= 120)
			score += 80;
		if (ranged && v->dist < 100)
			score -= 60; /* prefer uppercut/melee up close over fireball */
		if (m->cmd == CMD_HCF)
			score += 30;
		if (score > best_score) {
			best_score = score;
			best_in = m->input;
			best_cmd = m->cmd;
		}
	}
	if (best_in) {
		v->cmd = best_cmd;
		return best_in;
	}
	return 0;
}

/* True when the special we would fire is an in-range uppercut (prefer over normals). */
static inline int ai_special_is_close_uppercut(AiView *v, int sp)
{
	if (!sp || !v->cmd) return 0;
	for (int k = 0; k < MAXMOVES && v->me->moves[k].name; k++) {
		const Move *m = &v->me->moves[k];
		if (m->cmd == v->cmd && m->input == sp && ai_is_uppercut(m) && ai_in_reach(v, m))
			return 1;
	}
	return 0;
}

static inline int ai_special_is_super(AiView *v, int sp)
{
	if (!sp || !v->cmd) return 0;
	for (int k = 0; k < MAXMOVES && v->me->moves[k].name; k++) {
		const Move *m = &v->me->moves[k];
		if (m->cmd == v->cmd && m->input == sp && ai_is_super(m))
			return 1;
	}
	return 0;
}

/* Chance denominator for attempting a special (lower = more often). */
static inline int ai_special_chance(const AiView *v, int is_super, int edged)
{
	int n;
	if (v->diff >= DIFF_HARD) n = is_super ? 2 : 4;
	else if (v->diff <= DIFF_EASY) n = is_super ? 10 : 20;
	else n = is_super ? 4 : 8;
	if (edged) {
		if (v->diff >= DIFF_HARD) n = 2;
		else if (v->diff == DIFF_NORMAL) n = n > 3 ? n - 2 : 2;
	}
	return n < 2 ? 2 : n;
}

static inline int ai_fly(const AiView *v)
{
	return v->me->fly && v->self->y > 0 && v->self->y < 220 && v->self->fuel > 15 ? IN_U : 0;
}

/* Aggressive AI: walk in, use uppercuts/supers, pressure edged foes. */
static inline int ai_rusher(AiView *v)
{
	int edged = ai_foe_edged(v);
	int a = ai_melee(v), sp = ai_special(v), in = ai_walk(v, 1);
	int is_super = ai_special_is_super(v, sp);
	int upper = ai_special_is_close_uppercut(v, sp);

	/* Prefer in-range uppercut over normals on Normal/Hard. */
	if (sp && upper && v->diff >= DIFF_NORMAL)
		return sp;
	/* Spend meter on supers when available. */
	if (sp && is_super && !ai_rand(v, ai_special_chance(v, 1, edged)))
		return sp;
	if (a) return a;
	if (sp && !ai_rand(v, ai_special_chance(v, 0, edged)))
		return sp;

	/* Edge pressure: stay on them, jump in more often. */
	if (edged) {
		int jump_n = v->diff >= DIFF_HARD ? 12 : (v->diff <= DIFF_EASY ? 40 : 20);
		if (!ai_rand(v, jump_n)) in |= IN_U;
		return in | ai_fly(v);
	}
	if (!ai_rand(v, v->diff >= DIFF_HARD ? 25 : 40)) in |= IN_U;
	return in | ai_fly(v);
}

static inline int ai_zoner(AiView *v)
{
	int edged = ai_foe_edged(v);
	int a = ai_melee(v), sp = ai_special(v);
	int cornered = v->self->x < 100 || v->self->x > STAGE_W - 100;
	int is_super = ai_special_is_super(v, sp);
	int upper = ai_special_is_close_uppercut(v, sp);

	if (sp && upper && v->diff >= DIFF_NORMAL) return sp;
	if (sp && is_super && !ai_rand(v, ai_special_chance(v, 1, edged))) return sp;
	if (sp && !ai_rand(v, ai_special_chance(v, 0, edged))) return sp;
	if (a && v->dist < 120) return a;
	/* Don't give free space when the foe is edged — walk in instead of backing off. */
	if (edged) return ai_walk(v, 1) | ai_fly(v);
	if (v->dist < 250) return (cornered ? ai_walk(v, 1) : ai_walk(v, 0)) | ai_fly(v);
	return ai_walk(v, 1);
}

static inline int ai_turtle(AiView *v)
{
	int edged = ai_foe_edged(v);
	int a = ai_melee(v), sp = ai_special(v);
	/* holding away from the opponent blocks, so retreating from a threat guards */
	if (!edged && (v->shot < 220 || (v->tgt->move >= 0 && v->dist < 250)))
		return ai_walk(v, 0);
	if (sp && ai_special_is_close_uppercut(v, sp) && v->diff >= DIFF_NORMAL) return sp;
	if (sp && ai_special_is_super(v, sp) && !ai_rand(v, ai_special_chance(v, 1, edged))) return sp;
	if (a) return a;
	if (edged) return ai_walk(v, 1);
	return v->dist > 150 ? ai_walk(v, 1) : 0;
}

static const Ai ais[] = {
	{ "rusher", ai_rusher },
	{ "zoner",  ai_zoner },
	{ "turtle", ai_turtle },
};
#define NAIS (int)(sizeof ais / sizeof ais[0])
_Static_assert(sizeof ais / sizeof ais[0] <= MAXAI, "raise MAXAI in config.h");

#endif // !AI_H
