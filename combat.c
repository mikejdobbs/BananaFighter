#include "game.h"

// --- Assistant additions: prototypes (idempotent) ---
static int canAttemptThrow(Player* a, Player* d);
static void startThrow(Player* a, Player* d);
static int isFacing(Player* a, Player* d);
static void alignForThrow(Player* a, Player* d);
static void onHighKickPressed(Player* a, Player* d);
static void startCrouchSweepThrow(Player* a, Player* d);
static void onCrouchSweepHit(Player* a, Player* d, const HitContext* ctx);

static int canAttemptThrow(Player* a, Player* d) {
    if (!a || !d) return 0;
    if (!a->state.canAct) return 0;
    if (!d->state.isGrounded) return 0;
    if (d->flags.isInvulnerable || d->flags.isThrowImmune) return 0;
    if (!isFacing(a, d)) return 0;
    float dx = fabsf(a->pos.x - d->pos.x);
    float range = a->params.throwRange > 0 ? a->params.throwRange : 0.8f;
    if (dx > range) return 0;
    return 1;
}

static int isFacing(Player* a, Player* d) {
    float dir = (d->pos.x - a->pos.x);
    return (a->facing > 0 && dir >= 0) || (a->facing < 0 && dir <= 0);
}

static void startThrow(Player* a, Player* d) {
    a->state.id = STATE_THROW_START;
    d->state.id = STATE_BEING_THROWN;
    d->flags.isBlocking = 0;
    d->flags.isThrowImmune = 1;
    d->timers.throwImmuneEnd = currentTime() + THROW_IMMUNITY_WINDOW;
    alignForThrow(a, d);
    playAnim(a, ANIM_THROW_START);
}

static void alignForThrow(Player* a, Player* d) {
    float offset = 0.4f * (a->facing >= 0 ? 1.0f : -1.0f);
    d->pos.x = a->pos.x + offset;
}

void onHighPunchPressed(Player* a, Player* d) {
    if (canAttemptThrow(a, d)) {
        startThrow(a, d);
        return;
    }
    startHighPunch(a);
}

void onHighKickPressed(Player* a, Player* d) {
    if (a->state.isCrouching) {
        startCrouchSweepThrow(a, d);
        return;
    }
    startStandingHighKick(a, d);
}

static void startCrouchSweepThrow(Player* a, Player* d) {
    a->state.id = STATE_CROUCH_SWEEP_START;
    playAnim(a, ANIM_CROUCH_SWEEP);
    Hitbox hb = makeSweepHitbox(a);
    hb.type = HitType_Special;
    hb.flags.unblockable = 1;
    hb.onHit = onCrouchSweepHit;
    activateHitbox(a, &hb);
}

static void onCrouchSweepHit(Player* a, Player* d, const HitContext* ctx) {
    d->state.id = STATE_AIRBORNE;
    d->vel.y = (a->params.sweepLaunchVy != 0.0f) ? a->params.sweepLaunchVy : 6.0f;
    d->vel.x += ((a->params.sweepKnockbackVx != 0.0f) ? a->params.sweepKnockbackVx : 1.2f) * (a->facing >= 0 ? 1.0f : -1.0f);
    d->flags.isThrowImmune = 1;
    d->timers.throwImmuneEnd = currentTime() + AIR_THROW_IMMUNITY_WINDOW;
    playAnim(d, ANIM_LAUNCHED);
}

void resolveHit(Player* att, Player* def, const Hit* hit) {
    if (hit->type == HitType_Throw || hit->flags.unblockable) {
        // Skip block logic for throws/unblockables
    } else {
        if (def->flags.isBlocking) {
            // existing block logic
        }
    }
    // ... existing hit resolution logic ...
}
