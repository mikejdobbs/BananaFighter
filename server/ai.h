/* ai.h - bot AI plugins, dwm style.
   Plugins choose an input mask. ai_special() also sets v->cmd, the motion command the bot
   "performs": bots don't enter motions, they just report the command to the simulation. */
#ifndef AI_H
#define AI_H

#include "sim.h"

#define AI_THINK_TICKS 5

typedef struct {
	const Character *me, *foe;
	const Body *self, *tgt;
	int dx, dist, shot;
	int cmd;                        /* output: motion command to perform with the returned button */
	unsigned *rng;
} AiView;

typedef int (*AiFn)(AiView *);
typedef struct { const char *name; AiFn think; } Ai;

static inline int ai_rand(AiView *v, int n)
{
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

/* strongest ground normal that reaches the target (air moves and commands are skipped) */
static inline int ai_melee(const AiView *v)
{
	int in = 0, dmg = 0;
	for (int k = 0; k < MAXMOVES && v->me->moves[k].name; k++) {
		const Move *m = &v->me->moves[k];
		if (m->air || m->cmd || m->proj_speed || m->ship || !m->dmg || !ai_in_reach(v, m)) continue;
		if (m->dmg > dmg) { dmg = m->dmg; in = m->input; }
	}
	return in;
}

static inline int ai_special(AiView *v)
{
	for (int k = 0; k < MAXMOVES && v->me->moves[k].name; k++) {
		const Move *m = &v->me->moves[k];
		if (m->cmd && v->self->meter >= m->cost && (m->proj_speed || m->ship || v->dist < 220)) {
			v->cmd = m->cmd;
			return m->input;
		}
	}
	return 0;
}

static inline int ai_fly(const AiView *v)
{
	return v->me->fly && v->self->y > 0 && v->self->y < 220 && v->self->fuel > 15 ? IN_U : 0;
}

static inline int ai_rusher(AiView *v)
{
	int a = ai_melee(v), sp = ai_special(v), in = ai_walk(v, 1);
	if (a) return a;
	if (sp && v->dist > 170 && !ai_rand(v, 18)) return sp;
	if (!ai_rand(v, 40)) in |= IN_U;
	return in | ai_fly(v);
}

static inline int ai_zoner(AiView *v)
{
	int a = ai_melee(v), sp = ai_special(v), cornered = v->self->x < 100 || v->self->x > STAGE_W - 100;
	if (sp && !ai_rand(v, 12)) return sp;
	if (a && v->dist < 120) return a;
	if (v->dist < 250) return (cornered ? ai_walk(v, 1) : ai_walk(v, 0)) | ai_fly(v);
	return ai_walk(v, 1);
}

static inline int ai_turtle(AiView *v)
{
	int a = ai_melee(v);
	/* holding away from the opponent blocks, so retreating from a threat guards */
	if (v->shot < 220 || (v->tgt->move >= 0 && v->dist < 250)) return ai_walk(v, 0);
	if (a) return a;
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
