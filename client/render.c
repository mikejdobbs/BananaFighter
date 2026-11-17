/*
 * render.c
 * Rendering and UI:
 * - Draw fighters, projectiles, ships, explosions, and UI overlays
 * - Select screen grid, roster, and options menu
 * Uses SDL textures when available; falls back to simple boxes otherwise.
 */

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "client.h"
#include "lan.h"

/* Per-slot UI colors used for markers and fallback boxes */
static const Uint8 pal[MAX_PLAYERS][3] = {
    {230, 70, 70},   {70, 140, 230}, {80, 200, 100}, {230, 200, 60},
    {180, 90, 220},  {240, 140, 50}, {70, 210, 210}, {230, 110, 170},
    {150, 150, 150}, {140, 90, 50},
};

int show_boxes, options_open, options_item; /* Debug hitboxes and options menu state */
int options_rebinding = -1;                /* BIND_* while waiting for a key, else -1 */
int options_scroll;                        /* first visible options row */
int options_in_play;                       /* 1 when opened from match/lobby */
int options_submenu;                       /* OPTMENU_PAUSE / SETTINGS / CONTROLS */
unsigned aclk;                               /* Animation clock (ticks) */
SDL_Renderer *renderer;                      /* Global renderer */
static Anim an[MAX_PLAYERS];                 /* Per-slot animation state */
static SDL_Texture *tex[NCHARS];             /* Fighter sheets (NULL => fallback boxes) */
static SDL_Texture *fxt, *bg;                /* FX sheet and stage backdrop */
static SDL_Texture *bg_char[NCHARS];         /* Per-fighter home stages */
static SDL_Texture *bg_match;                /* Latched stage for the current fight (NULL = pick) */
static int bg_match_picked;                  /* 1 once a match stage has been chosen */
static int bg_match_stage = -1;              /* -1 stadium, else 0..NCHARS-1 */
static unsigned gib_rng = 2463534242u;

static unsigned gib_rand(void)
{
    gib_rng ^= gib_rng << 13;
    gib_rng ^= gib_rng >> 17;
    gib_rng ^= gib_rng << 5;
    return gib_rng;
}

/* Load a PNG texture from sprites/ with alpha blending enabled; NULL on failure */
static SDL_Texture *load_tex(const char *file)
{
    char path[512];
    const char *base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%ssprites/%s", base ? base : "", file);
    SDL_Surface *s = SDL_LoadPNG(path);
    if (!s) {
        snprintf(path, sizeof path, "sprites/%s", file);
        s = SDL_LoadPNG(path);
    }
    if (!s) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_DestroySurface(s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

/* Load all fighter and effect textures (missing files fall back to boxes) */
void load_sprites(void)
{
    for (int i = 0; i < NCHARS; i++) {
        char fn[48];
        snprintf(fn, sizeof fn, "%s.png", chars[i].name);
        for (char *p = fn; *p; p++) *p = (char)tolower((unsigned char)*p);
        if (!(tex[i] = load_tex(fn)))
            fprintf(stderr, "no sprites/%s: drawing %s as a box\n", fn, chars[i].name);
    }
    fxt = load_tex("fx.png");
    bg  = load_tex("background.png");
    for (int i = 0; i < NCHARS; i++) {
        char fn[64];
        snprintf(fn, sizeof fn, "background_%s.png", chars[i].name);
        for (char *p = fn; *p; p++) *p = (char)tolower((unsigned char)*p);
        bg_char[i] = load_tex(fn);
        if (!bg_char[i])
            bg_char[i] = bg; /* missing home stage: reuse the stadium */
    }
    bg_match = NULL;
    bg_match_picked = 0;
    bg_match_stage = -1;
}

/* Stretch a stage texture over the playfield sky band */
static void draw_stage(SDL_Texture *stage)
{
    if (!stage) return;
    SDL_FRect d = { 0, 0, SW, GROUND };
    SDL_RenderTexture(renderer, stage, NULL, &d);
}

/* Pick a random loaded stage once per match (stadium + home stages).
 * Sets bg_match_stage: -1 = stadium, else fighter index 0..NCHARS-1. */
static SDL_Texture *pick_match_stage(void)
{
    int ids[1 + NCHARS];
    int n = 0;
    if (bg) ids[n++] = -1;
    for (int i = 0; i < NCHARS; i++)
        if (bg_char[i]) ids[n++] = i;
    if (!n) {
        bg_match_stage = -1;
        return NULL;
    }
    bg_match_stage = ids[SDL_rand(n)];
    if (bg_match_stage < 0)
        return bg;
    return bg_char[bg_match_stage] ? bg_char[bg_match_stage] : bg;
}

int match_stage_for_music(void)
{
    return bg_match_stage;
}

/* True if this move is a spin_slam carry (sky uppercut / arm slam). */
static int is_spin_slam_st(int ch, int st)
{
    if (st < 0 || st >= MAXMOVES || ch < 0 || ch >= NCHARS)
        return 0;
    const Move *m = &chars[ch].moves[st];
    return m->name && m->spin_slam;
}

/* True if this move is a rocket-pack wander flight (server-steered). */
static int is_pack_st(int ch, int st)
{
    if (st < 0 || st >= MAXMOVES || ch < 0 || ch >= NCHARS)
        return 0;
    const Move *m = &chars[ch].moves[st];
    return m->name && m->pack;
}

/* Effective fighter state: predicted for self, snapshot for others */
static Fig eff(int i)
{
    Fig f = F[i];
    if (predicting && f.slot == me) {
        /* Prediction has no carry physics — trust server for the whole slam. */
        if (is_spin_slam_st(f.ch, f.st) ||
            (pb.move >= 0 && chars[pch].moves[pb.move].spin_slam))
            return f;
        /* Pack flight wander is server-owned; prediction would desync position. */
        if (is_pack_st(f.ch, f.st) ||
            (pb.move >= 0 && chars[pch].moves[pb.move].pack))
            return f;
        /* Chicken-bucket vanish is server-owned; prediction would desync invis. */
        if (f.invis)
            return f;
        f.x = pb.x; f.y = pb.y; f.face = pb.face; f.invis = pb.invis > 0;
        f.st = (pb.stun && pb.crouch) ? -7
             : (pb.stun && pb.down) ? -8
             : pb.stun ? -2
             : (pb.block && pb.crouch) ? -6
             : pb.block ? -3
             : pb.move >= 0 ? pb.move
             : pb.flying ? -4
             : pb.crouch ? -5
             : -1;
    }
    return f;
}

/* Update per-slot animation timers and play movement-related sounds */
void anim_tick(void)
{
    aclk++;
    gib_tick();
    for (int i = 0; i < nf; i++) {
        Fig f = eff(i);
        Anim *a = &an[f.slot];
        if (f.x < 0) continue;                      /* hidden */
        /* One clock for the whole sweep: fall, then knees, then feet.
           Leaving the fall (server hitstun or idle) jumps straight to the kneel. */
        if (f.st >= 0) {
            a->kd = -1;
        } else if (f.st == -7) {
            if (a->st != -7) a->kd = 0;
            else if (a->kd < 10000) a->kd++;
        } else if (f.st == -8 && a->kd <= 0) {
            a->kd = KNOCKDOWN_STUN - GETUP_TICKS; /* joined during the get-up */
        } else if (a->kd > 0 && (f.st == -8 || f.st == -2 || f.st == -1)) {
            int lie = KNOCKDOWN_STUN - GETUP_TICKS;
            if (a->st == -7 && a->kd < lie) a->kd = lie;
            else a->kd++;
            if (a->kd > KNOCKDOWN_STUN) a->kd = -1;
        } else {
            a->kd = -1;
        }
        if (f.st != a->st) {
            if (f.st >= 0 && f.st < MAXMOVES && chars[f.ch].moves[f.st].name)
                swing_sound(&chars[f.ch].moves[f.st]);
            a->st = f.st;
            a->t = 0;
        } else a->t++;
        if (a->ly == 0 && f.y > 0 && f.st != -2) sfx(SN_JUMP, 0.5f);
        if (a->ly > 0 && f.y == 0 && f.st != -2) sfx(SN_LAND, 0.5f);
        if (f.st == -4 && aclk % 6 == 0) sfx(SN_FLY, 0.35f);
        if (f.x != a->lx) { a->still = 0; a->walk++; } else if (a->still < 1000) a->still++;
        if (a->still >= WALK_HOLD) a->walk = 0;
        if (a->ly > 0 && f.y == 0) a->land = LAND_TICKS;
        else if (a->land > 0) a->land--;
        if (f.y != a->ly) a->dy = f.y - a->ly;
        a->lx = f.x;
        a->ly = f.y;
    }
}

/* Six-phase move animation (windup/impact/follow-through) derived from timings */
static int move_frame(const Move *m, int t)
{
    int S = m->startup > 0 ? m->startup : 1, R = m->recover > 0 ? m->recover : 1;
    int A = m->active > MIN_ACTIVE ? m->active : MIN_ACTIVE;
    if (t < S) return t * 2 / S;
    t -= S;
    if (t < A) return 2 + t * 2 / A;
    t -= A;
    return 4 + (t * 2 / R > 1 ? 1 : t * 2 / R);
}

/* Choose sprite row/column for a fighter based on state and animation */
static void pick(const Fig *f, int *row, int *col)
{
    const Character *c = &chars[f->ch];
    const Anim *a = &an[f->slot];

    if (a->kd >= 0 && (f->st == -7 || f->st == -8 || f->st == -2 || f->st == -1)) {
        int lie = KNOCKDOWN_STUN - GETUP_TICKS;
        int knees = lie + GETUP_TICKS * 3 / 5;
        *row = ROW_HURT;
        if (a->kd < 7) *col = 3;             /* foot swept out */
        else if (a->kd < 16) *col = 4;       /* tipping onto the back */
        else if (a->kd < lie) *col = 5;      /* flat on the back */
        else if (a->kd < knees) *col = 6;    /* up on the knees */
        else *col = 7;                       /* up on the feet */
    }
    else if (f->st == -2) { *row = ROW_HURT; *col = a->t < 3 ? 0 : a->t < 8 ? 1 : 2; }
    else if (f->y == 0 && a->still < WALK_HOLD && (f->st == -3 || f->st == -6)) {
        *row = ROW_WALK;
        *col = (a->walk / WALK_TICKS) % WALK_FR;
    }
    else if (f->st == -3) { *row = ROW_BLOCK; *col = a->t < 4 ? 0 : 1; }
    else if (f->st == -6) { *row = ROW_MOVE + 11; *col = a->t < 4 ? 0 : 1; }
    else if (f->st == -4) { *row = ROW_FLY; *col = (aclk / 2) % FLY_FR; }
    else if (f->st == -5) { *row = ROW_CROUCH; *col = a->t < 4 ? 0 : 1; }
    else if (f->st >= 0 && f->st < MAXMOVES && c->moves[f->st].name) {
        const Move *m = &c->moves[f->st];
        *row = ROW_MOVE + move_row(m);
        /* Flying kicks pick a cell from height. An uppercut stays on the
           raised-fist cell for the whole jump; recovery plays after landing. */
        if (m->spin_slam) {
            if (m->combo_hits) {
                /* Uppercut until a foe is pinned at arm height for the slam windup. */
                int slam_pose = 0;
                if (f->slot == me && hasq && Q.frame >= 100)
                    slam_pose = 1;
                else {
                    int arm_x = f->x + f->face * 40;
                    for (int i = 0; i < nf; i++) {
                        if (F[i].slot == f->slot || F[i].lives <= 0 || F[i].x < 0)
                            continue;
                        int dx = F[i].x - arm_x;
                        if (dx < 0) dx = -dx;
                        if (dx <= 48 && F[i].y >= 30 && F[i].y <= 50) {
                            slam_pose = 1;
                            break;
                        }
                    }
                }
                if (slam_pose) {
                    *row = ROW_MOVE + 22; /* arm slam / throw */
                    *col = 3;
                } else {
                    /* Startup, then four chamber→punch beats (server frame 50+col). */
                    *row = ROW_MOVE + move_row(m);
                    int S = m->startup > 0 ? m->startup : 1;
                    int t = a->t;
                    if (t < S) {
                        *col = t * 2 / S;
                    } else if (f->slot == me && hasq && Q.frame >= 50 && Q.frame < 100) {
                        *col = Q.frame - 50;
                        if (*col < 0) *col = 0;
                        if (*col > 3) *col = 3;
                    } else {
                        static const int hit_cols[4] = {1, 2, 3, 2};
                        int u = t - S;
                        int gap = COMBO_HIT_GAP > 0 ? COMBO_HIT_GAP : 6;
                        int punch_i = (u / gap) % 4;
                        int phase = u % gap;
                        *col = (phase < gap / 2) ? 0 : hit_cols[punch_i];
                    }
                }
            } else {
                *row = ROW_MOVE + 22;
                if (f->y > 80) *col = 3;
                else if (f->y > 0) *col = 2;
                else *col = 1;
            }
        } else if (m->bucket_burst) {
            /* Eat windup → gorging → bloated blast pose. */
            int S = m->startup > 0 ? m->startup : 1;
            int t = a->t;
            if (t < S / 3) *col = 0;
            else if (t < (S * 2) / 3) *col = 1;
            else if (t < S) *col = 2;
            else *col = 3;
        } else if (m->leap_vy && f->y > 0) *col = 2;
        else if (m->leap && f->y > 40) *col = 4;
        else if (m->leap && f->y > 12) *col = 3;
        else if (m->leap && f->y > 0) *col = 2;
        else if (m->crouch && !m->air) {
            /* Short crouch normals: hold the hit cell so punches/kicks read. */
            int S = m->startup > 0 ? m->startup : 1;
            int R = m->recover > 0 ? m->recover : 1;
            int A = m->active > 0 ? m->active : 1;
            int t = a->t;
            if (t < S / 2) *col = 0;
            else if (t < S + A + R / 2) *col = 2;
            else *col = 4;
        } else *col = move_frame(m, a->t);
    }
    else if (f->y > 0) {
        *row = ROW_JUMP;
        *col = a->dy > 2 ? 0 : a->dy < -5 ? 3 : a->dy < -1 ? 2 : 1;
    }
    else if (a->land > 0) { *row = ROW_JUMP; *col = a->land > LAND_TICKS / 2 ? 4 : 5; }
    else if (a->still < WALK_HOLD) { *row = ROW_WALK; *col = (a->walk / WALK_TICKS) % WALK_FR; }
    else { *row = ROW_IDLE; *col = (aclk / IDLE_TICKS) % IDLE_FR; }
}

/* Draw a filled rectangle in screen space */
static void box(float x, float y, float w, float h, int r, int g, int b, int a)
{
    SDL_FRect q = { x, y, w, h };
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderFillRect(renderer, &q);
}

/* Draw debug text using SDL's debug text renderer */
static void text(float x, float y, const char *fmt, ...)
{
    char b[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
    SDL_RenderDebugText(renderer, x, y, b);
}

#define ARR_SZ 7.0f
#define ARR_GAP 3.0f

static void arrow_line(float x1, float y1, float x2, float y2)
{
    SDL_RenderLine(renderer, x1, y1, x2, y2);
}

/* One motion-input arrow glyph; returns width in pixels. */
static float draw_arrow_glyph(float x, float y, int kind)
{
    float cx = x + ARR_SZ * 0.5f, cy = y + ARR_SZ * 0.5f;
    SDL_SetRenderDrawColor(renderer, 250, 220, 80, 255);
    switch (kind) {
    case ARR_TOWARD:
        arrow_line(cx - 3, cy, cx + 3, cy);
        arrow_line(cx + 3, cy, cx, cy - 3);
        arrow_line(cx + 3, cy, cx, cy + 3);
        break;
    case ARR_AWAY:
        arrow_line(cx + 3, cy, cx - 3, cy);
        arrow_line(cx - 3, cy, cx, cy - 3);
        arrow_line(cx - 3, cy, cx, cy + 3);
        break;
    case ARR_DOWN:
        arrow_line(cx, cy - 3, cx, cy + 3);
        arrow_line(cx, cy + 3, cx - 3, cy);
        arrow_line(cx, cy + 3, cx + 3, cy);
        break;
    case ARR_DOWN_TOWARD:
        arrow_line(cx - 2, cy - 2, cx + 2, cy + 2);
        arrow_line(cx + 2, cy + 2, cx - 1, cy + 2);
        arrow_line(cx + 2, cy + 2, cx + 2, cy - 1);
        break;
    case ARR_DOWN_AWAY:
        arrow_line(cx + 2, cy - 2, cx - 2, cy + 2);
        arrow_line(cx - 2, cy + 2, cx + 1, cy + 2);
        arrow_line(cx - 2, cy + 2, cx - 2, cy - 1);
        break;
    case ARR_UP:
        arrow_line(cx, cy + 3, cx, cy - 3);
        arrow_line(cx, cy - 3, cx - 3, cy);
        arrow_line(cx, cy - 3, cx + 3, cy);
        break;
    default:
        break;
    }
    return ARR_SZ + ARR_GAP;
}

/* face < 0: character faces left, so "toward" is the left arrow. */
static int arrow_for_face(int kind, int face)
{
    if (face >= 0)
        return kind;
    switch (kind) {
    case ARR_TOWARD: return ARR_AWAY;
    case ARR_AWAY: return ARR_TOWARD;
    case ARR_DOWN_TOWARD: return ARR_DOWN_AWAY;
    case ARR_DOWN_AWAY: return ARR_DOWN_TOWARD;
    default: return kind;
    }
}

static float draw_cmd_arrows(float x, float y, int cmd, int face)
{
    int steps[8], n = cmd_arrow_steps(cmd, steps, 8);
    if (!n)
        return 0;
    float ox = x;
    for (int i = 0; i < n; i++)
        x += draw_arrow_glyph(x, y, arrow_for_face(steps[i], face));
    return x - ox;
}

/* Draw a fighter using a sprite sheet */
static void draw_sprite(const Fig *f, SDL_Texture *t)
{
    const Character *c = &chars[f->ch];
    int row, col;
    pick(f, &row, &col);

    float cs = c->h * SCALE * SPR_CELL / SPR_FIGH;      /* on-screen cell size */
    float xc = f->x * SCALE;
    SDL_FRect src = { col * SPR_CELL, row * SPR_CELL, SPR_CELL, SPR_CELL };
    SDL_FRect dst = { xc - cs * (f->face > 0 ? SPR_CX : SPR_CELL - SPR_CX) / SPR_CELL,
                      GROUND - f->y * SCALE - cs, cs, cs };

    SDL_FlipMode flip = f->face < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

    SDL_SetTextureAlphaMod(t, f->invis ? 90 : 255);
    if (f->st == -2 || f->st == -7 || f->st == -8) SDL_SetTextureColorMod(t, 255, 140, 140);
    else SDL_SetTextureColorMod(t, 255, 255, 255);

    /* Pack ship cell: left half = bat jetpack (back), right half = pitching machine (hands).
       Always sample those source halves; SDL flip mirrors them when facing left. */
    if (is_pack_st(f->ch, f->st)) {
        int fr = (aclk / 6) % SHIP_FR;
        float half = SPR_CELL * 0.5f;
        float pw = cs * 0.36f, ph = cs * 0.48f;
        float back_ox = f->face > 0 ? -cs * 0.22f : cs * 0.22f;
        float oy = cs * 0.30f;
        SDL_FRect bsrc = { fr * SPR_CELL, ROW_SHIP * SPR_CELL, half, (float)SPR_CELL };
        SDL_FRect bdst = { xc + back_ox - pw / 2, dst.y + oy, pw, ph };
        SDL_RenderTextureRotated(renderer, t, &bsrc, &bdst, 0, NULL, flip);
    }

    SDL_RenderTextureRotated(renderer, t, &src, &dst, 0, NULL, flip);

    /* Held pitching machine in front; bore tip matches PACK_MUZZLE_* in combat.c. */
    if (is_pack_st(f->ch, f->st)) {
        int fr = (aclk / 6) % SHIP_FR;
        float half = SPR_CELL * 0.5f;
        float pw = cs * 0.36f, ph = cs * 0.48f;
        float hand_ox = f->face > 0 ? cs * 0.28f : -cs * 0.28f;
        float oy = cs * 0.30f;
        SDL_FRect msrc = { fr * SPR_CELL + half, ROW_SHIP * SPR_CELL, half, (float)SPR_CELL };
        SDL_FRect mdst = { xc + hand_ox - pw / 2, dst.y + oy, pw, ph };
        SDL_RenderTextureRotated(renderer, t, &msrc, &mdst, 0, NULL, flip);
    }

    const Uint8 *pc = pal[f->slot % MAX_PLAYERS];                   /* slot marker above the head */
    float mark_y = (f->st == -7 || f->st == -8) ? GROUND - f->y * SCALE - 10 : dst.y + cs * 0.02f;
    box(xc - 7, mark_y, 14, 6, pc[0], pc[1], pc[2], f->invis ? 90 : 255);
}

/* Draw a fighter as a colored box (fallback when no texture) */
static void draw_box(const Fig *f)                  /* fallback when a sheet is missing */
{
    const Character *c = &chars[f->ch];
    float w, h, x, y;
    if (f->st == -7 || f->st == -8) {                /* lying down / getting up */
        w = c->h * SCALE * (f->st == -8 ? 0.55f : 0.65f);
        h = c->w * SCALE;
        x = f->x * SCALE - w / 2;
        y = GROUND - f->y * SCALE - h;
    } else {
        w = c->w * SCALE;
        h = c->h * SCALE * (f->st == -5 ? 0.5f : 1.0f);
        x = f->x * SCALE - w / 2;
        y = GROUND - f->y * SCALE - h;
    }
    int a = f->invis ? 90 : 255;
    const Uint8 *col = pal[f->slot % MAX_PLAYERS];

    box(x, y, w, h, col[0], col[1], col[2], a);
    if (f->st != -7 && f->st != -8)
        box(f->face > 0 ? x + w * 0.65f : x + w * 0.15f, y + h * 0.12f, w * 0.2f, h * 0.08f, 255, 255, 255, a);
    if (f->st == -2 || f->st == -7 || f->st == -8) box(x, y, w, h, 255, 255, 255, 140);
    if (f->st >= 0 && f->st < MAXMOVES && c->moves[f->st].name) {
        const Move *m = &c->moves[f->st];
        if (!m->proj_speed && m->reach)
            box(f->face > 0 ? x + w : x - m->reach * SCALE,
                GROUND - (f->y + m->y + m->h) * SCALE, m->reach * SCALE, m->h * SCALE,
                255, 230, 80, 160);
    }
}

/* Draw a fighter with shadow, sprite/box, and optional hitbox overlay */
static void draw_fighter(const Fig *f)
{
    const Character *c = &chars[f->ch];
    if (f->x < 0 || f->lives <= 0) return;          /* hidden by the server, or eliminated: KO'd out of the match */
    if (f->invis) return;                           /* fully gone (e.g. chicken-bucket vanish) */
    float sw = c->w * SCALE;
    float lift = f->y * SCALE;                      /* shadow shrinks and fades as you rise */
    float shw = sw * (1.0f - lift / 600.0f);
    if (shw < sw * 0.4f) shw = sw * 0.4f;
    box(f->x * SCALE - shw / 2, GROUND - 3, shw, 6, 0, 0, 0, 80 * (lift > 300 ? 0.5f : 1.0f));
    if (tex[f->ch]) draw_sprite(f, tex[f->ch]); else draw_box(f);
    if (show_boxes) {                               /* hitbox overlay */
        float h = c->h * SCALE * (f->st == -5 || f->st == -7 ? 0.5f : 1.0f);
        box(f->x * SCALE - sw / 2, GROUND - f->y * SCALE - h, sw, h, 255, 255, 255, 50);
    }
}

/* Draw a projectile (J) */
static void draw_shot(const Shot *s)
{
    int ch = 0, face = 1;
    for (int i = 0; i < nf; i++)
        if (F[i].slot == s->owner) { ch = F[i].ch; face = F[i].face; }
    int spin = 0;                                   /* does this character's projectile spin? */
    for (int k = 0; k < MAXMOVES && chars[ch].moves[k].name; k++)
        if (chars[ch].moves[k].proj_speed) { spin = chars[ch].moves[k].spin; break; }
    float sz = PROJ_HALF * 3.8f, x = s->x * SCALE, y = GROUND - s->y * SCALE;

    if (tex[ch]) {
        SDL_FRect src = { 0, ROW_SHOT * SPR_CELL, SPR_CELL, SPR_CELL };
        SDL_FRect dst = { x - sz / 2, y - sz / 2, sz, sz };
        double ang = spin ? (double)aclk * 18 * face : 0;
        SDL_RenderTextureRotated(renderer, tex[ch], &src, &dst, ang, NULL,
                                 !spin && face < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    } else {
        const Uint8 *col = pal[s->owner % MAX_PLAYERS];
        box(x - PROJ_HALF, y - PROJ_HALF, PROJ_HALF * 2, PROJ_HALF * 2, col[0], col[1], col[2], 255);
    }
}

/* A ship or pack move with spin drops the fighter's shot icon (Stephen's baseballs)
   instead of the generic bomb. It still explodes on impact. */
static int bomb_is_ball(const Shot *s)
{
    int ch = -1;
    for (int i = 0; i < nf; i++)
        if (F[i].slot == s->owner) { ch = F[i].ch; break; }
    if (ch < 0)
        return 0;
    for (int k = 0; k < MAXMOVES && chars[ch].moves[k].name; k++) {
        const Move *m = &chars[ch].moves[k];
        if (m->spin && (m->ship || m->pack))
            return 1;
    }
    return 0;
}

/* Draw a bomb (B) */
static void draw_bomb(const Shot *s)
{
    float x = s->x * SCALE, y = GROUND - s->y * SCALE;
    if (bomb_is_ball(s)) {
        int ch = 0;
        for (int i = 0; i < nf; i++)
            if (F[i].slot == s->owner) { ch = F[i].ch; break; }
        if (tex[ch]) {
            float sz = PROJ_HALF * 3.8f;
            SDL_FRect src = { 0, ROW_SHOT * SPR_CELL, SPR_CELL, SPR_CELL };
            SDL_FRect dst = { x - sz / 2, y - sz / 2, sz, sz };
            SDL_RenderTextureRotated(renderer, tex[ch], &src, &dst, (double)aclk * 18, NULL,
                                     SDL_FLIP_NONE);
            return;
        }
    }
    if (fxt) {
        SDL_FRect src = { 0, 0, FX_CELL, FX_CELL }, dst = { x - 20, y - 20, 40, 40 };
        SDL_RenderTexture(renderer, fxt, &src, &dst);
    } else {
        box(x - 6, y - 10, 12, 20, 40, 40, 50, 255);
    }
}

/* ~50° down from horizontal: outward + downward (stage y is up). */
#define BARF_DEG 50.0f
#define BARF_COS 0.6428f /* cos(50°) */
#define BARF_SIN 0.7660f /* sin(50°) */

/* Dedup for spawn_barf across duplicate snapshot sightings of the same boom. */
static int barf_last_x = -99999, barf_last_y = -99999, barf_last_owner = -1;

/* True if this roster character has a bucket_burst (JV chicken barf) move. */
static int char_can_barf(int ch)
{
    if (ch < 0 || ch >= NCHARS)
        return 0;
    for (int m = 0; m < MAXMOVES && chars[ch].moves[m].name; m++)
        if (chars[ch].moves[m].bucket_burst)
            return 1;
    return 0;
}

/* Snapshot slot → barfer character; -1 if unknown / not a barfer. */
static int barf_owner_ch(int owner_slot)
{
    for (int i = 0; i < nf; i++)
        if (F[i].slot == owner_slot)
            return char_can_barf(F[i].ch) ? F[i].ch : -1;
    return -1;
}

/* Draw an explosion (X). Barf: short diagonal mouth sprays; blobs in draw_gibs. */
static void draw_boom(const Boom *b)
{
    float x = b->x * SCALE, y = GROUND - b->y * SCALE;
    /* Treat non-JV "barf" kinds as normal booms so green jets never leak. */
    if (b->kind == BOOM_BARF && barf_owner_ch(b->owner) >= 0) {
        /* Brief 50° green jets from the mouth while the boom is young. */
        if (b->t < BARF_BOOM_TICKS - 8) return;
        int a = 180;
        float step = 14.0f * SCALE;
        for (int side = -1; side <= 1; side += 2) {
            for (int i = 0; i < 6; i++) {
                float px = x + side * (i + 1) * step * BARF_COS;
                float py = y + (i + 1) * step * BARF_SIN; /* screen down */
                float sz = 10.0f + i * 2.0f;
                box(px - sz / 2, py - sz / 2, sz, sz * 0.7f, 80, 190, 45, a - i * 20);
            }
        }
        return;
    }
    int f = b->t > 9 ? 1 : b->t > 4 ? 2 : 3; /* t counts down from BOOM_TICKS */
    float sz = BOMB_R * 2 * SCALE * (f == 1 ? 0.7f : 1.0f);
    if (fxt) {
        SDL_FRect src = { f * FX_CELL, 0, FX_CELL, FX_CELL }, dst = { x - sz / 2, y - sz * 0.8f, sz, sz };
        SDL_RenderTexture(renderer, fxt, &src, &dst);
    } else {
        box(x - sz / 2, y - sz / 2, sz, sz / 2, 255, 150, 40, 160);
    }
}

/* Drop client-only barf debris when leaving/entering a match (or disconnect). */
void barf_fx_reset(void)
{
    memset(GIB, 0, sizeof GIB);
    barf_last_x = barf_last_y = -99999;
    barf_last_owner = -1;
}

/* Green vomit streams left and right from the mouth at ~50° to the ground. */
void spawn_barf(const Boom *b)
{
    /* Only JV (bucket_burst) may create vomit; ignores bogus BOOM_BARF packets. */
    if (barf_owner_ch(b->owner) < 0)
        return;
    /* Avoid double-spawn if the same fresh boom is seen twice. */
    if (b->x == barf_last_x && b->y == barf_last_y && b->owner == barf_last_owner)
        return;
    barf_last_x = b->x;
    barf_last_y = b->y;
    barf_last_owner = b->owner;

    static const Uint8 cols[][3] = {
        {70, 180, 45}, {50, 160, 35}, {110, 200, 55},
        {40, 140, 30}, {90, 190, 50}, {130, 210, 70},
        {60, 150, 40}, {100, 175, 48},
    };
    /* Boom y is already at mouth height (move.y + h/2). */
    float mouth_x = (float)b->x;
    float mouth_y = (float)b->y;
    for (int n = 0; n < 44; n++) {
        int slot = -1;
        for (int i = 0; i < MAXGIB; i++)
            if (!GIB[i].on) { slot = i; break; }
        if (slot < 0) break;
        int dir = (n & 1) ? 1 : -1; /* left and right */
        float deg_jit = ((int)(gib_rand() % 17) - 8) * (3.14159265f / 180.0f);
        float ang = BARF_DEG * (3.14159265f / 180.0f) + deg_jit;
        float c = cosf(ang), s = sinf(ang);
        float spd = 7.5f + (gib_rand() % 100) / 10.0f;
        /* Small stagger so the mouth→ground path fills immediately. */
        float along = (n / 2) * 0.08f;
        const Uint8 *col = cols[gib_rand() % 8];
        float y0 = mouth_y - along * 18.0f * s;
        if (y0 < 4) y0 = 4;
        GIB[slot] = (Gib){
            .on = 1,
            .grounded = 0,
            .life = 80 + (int)(gib_rand() % 40),
            .x = mouth_x + dir * (8.0f + along * 18.0f * c),
            .y = y0,
            .vx = dir * spd * c,
            .vy = -spd * s, /* negative = toward the ground */
            .r = 6.0f + (gib_rand() % 90) / 10.0f,
            .cr = col[0], .cg = col[1], .cb = col[2],
        };
    }
}

void gib_tick(void)
{
    /* No JV (or other bucket_burst char) in this match → no vomit puddles. */
    int any_barfer = 0;
    for (int i = 0; i < nf; i++)
        if (char_can_barf(F[i].ch)) {
            any_barfer = 1;
            break;
        }
    if (!any_barfer) {
        for (int i = 0; i < MAXGIB; i++)
            if (GIB[i].on) {
                barf_fx_reset();
                break;
            }
        return;
    }

    for (int i = 0; i < MAXGIB; i++) {
        Gib *g = &GIB[i];
        if (!g->on) continue;
        if (!g->grounded) {
            g->x += g->vx;
            g->y += g->vy;
            g->vy -= 0.35f; /* keep pulling down along the vomit arc */
            g->vx *= 0.998f;
            if (g->y <= 2) {
                g->y = 1;
                g->vy = 0;
                g->vx *= 0.2f;
                g->grounded = 1;
                g->r *= 2.1f; /* splat / puddle on impact */
                if (g->life < 50) g->life = 50 + (int)(gib_rand() % 25);
            }
        } else {
            g->vx *= 0.8f;
            g->x += g->vx;
            if (fabsf(g->vx) < 0.05f) g->vx = 0;
        }
        if (--g->life <= 0) g->on = 0;
    }
}

static void draw_gibs(void)
{
    for (int i = 0; i < MAXGIB; i++) {
        const Gib *g = &GIB[i];
        if (!g->on) continue;
        float sx = g->x * SCALE, sy = GROUND - g->y * SCALE;
        int alpha = g->life > 20 ? 235 : g->life * 11;
        if (g->grounded) {
            /* Flat puddle where the stream hits the floor */
            float w = g->r * 2.4f, h = g->r * 0.4f;
            box(sx - w / 2, GROUND - h - 1, w, h, g->cr, g->cg, g->cb, alpha);
            box(sx - w * 0.4f, GROUND - h * 1.35f, w * 0.8f, h * 0.5f,
                (Uint8)(g->cr < 230 ? g->cr + 25 : 255),
                (Uint8)(g->cg < 230 ? g->cg + 20 : 255), g->cb, alpha * 3 / 4);
        } else {
            /* Elongated splat along the 50° flight path (screen: down-right / down-left) */
            float len = g->r * 1.8f;
            float thick = g->r * 0.55f;
            /* Approximate diagonal by two stepped boxes along the velocity */
            float dx = (g->vx >= 0 ? 1.0f : -1.0f) * len * BARF_COS * 0.35f;
            float dy = len * BARF_SIN * 0.35f; /* screen down */
            box(sx - thick / 2, sy - thick / 2, thick, thick, g->cr, g->cg, g->cb, alpha);
            box(sx + dx - thick / 2, sy + dy - thick / 2, thick * 0.85f, thick * 0.85f,
                g->cr, g->cg, g->cb, alpha);
            box(sx + dx * 2 - thick / 3, sy + dy * 2 - thick / 3, thick * 0.65f, thick * 0.65f,
                g->cr, g->cg, g->cb, alpha * 4 / 5);
        }
    }
}

/* Draw a ship (Z): art comes from the caller's fighter sheet (ROW_SHIP) */
static void draw_ship(const Ship *s)
{
    int ch = 0;
    for (int i = 0; i < nf; i++)
        if (F[i].slot == s->owner) { ch = F[i].ch; break; }
    float x = s->x * SCALE, y = GROUND - SHIP_Y * SCALE;
    if (tex[ch]) {
        int fr = (aclk / 8) % SHIP_FR;
        SDL_FRect src = { fr * SPR_CELL, ROW_SHIP * SPR_CELL, SPR_CELL, SPR_CELL };
        SDL_FRect dst = { x - SHIP_DRAW_W / 2, y - SHIP_DRAW_H / 2, SHIP_DRAW_W, SHIP_DRAW_H };
        SDL_RenderTextureRotated(renderer, tex[ch], &src, &dst, 0, NULL,
                                 s->vx < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    } else {
        box(x - SHIP_DRAW_W / 2, y - SHIP_DRAW_H / 4, SHIP_DRAW_W, SHIP_DRAW_H / 2, 140, 145, 160, 255);
    }
}

/* Draw the active match: background, actors, HUD, and winner banner */
void draw_match(void)
{
    if (!bg_match_picked) {
        bg_match = pick_match_stage();
        bg_match_picked = 1;
    }
    draw_stage(bg_match ? bg_match : bg);
    box(0, GROUND, SW, SH - GROUND, 40, 36, 48, 255);
    box(0, GROUND, SW, 4, 90, 82, 100, 255);
    for (int i = 0; i < nsh; i++) draw_ship(&SHP[i]);
    /* Higher altitude first so a carried victim draws with the right overlap. */
    {
        int ord[MAX_PLAYERS], n = 0;
        for (int i = 0; i < nf && n < MAX_PLAYERS; i++)
            ord[n++] = i;
        for (int a = 0; a < n; a++)
            for (int b = a + 1; b < n; b++) {
                Fig fa = eff(ord[a]), fb = eff(ord[b]);
                if (fb.y > fa.y) {
                    int t = ord[a];
                    ord[a] = ord[b];
                    ord[b] = t;
                }
            }
        for (int i = 0; i < n; i++) {
            Fig f = eff(ord[i]);
            draw_fighter(&f);
        }
    }
    for (int i = 0; i < ns; i++) draw_shot(&S[i]);
    for (int i = 0; i < nb; i++) draw_bomb(&BM[i]);
    for (int i = 0; i < nex; i++) draw_boom(&EX[i]);
    draw_gibs();
    for (int i = 0; i < nf; i++) {
        const Fig *f = &F[i];
        int mx = chars[f->ch].hp, hp = f->hp < 0 ? 0 : f->hp, x = 10 + i * 118;
        text(x, 8, "%c%d %.8s x%d", f->slot == me ? '>' : ' ', f->slot, chars[f->ch].name, f->lives);
        box(x, 20, 108, 8, 60, 20, 20, 255);
        box(x, 20, 108.0f * hp / mx, 8, 80, 220, 80, 255);
        box(x, 30, 108, 4, 50, 40, 10, 255);
        box(x, 30, 108.0f * f->meter / METER_MAX, 4, 250, 200, 40, 255);
        if (f->slot == me && chars[f->ch].fuel) {   /* my tail-flight fuel */
            int fuel = predicting ? pb.fuel : Q.fuel;
            box(x, 36, 108, 4, 20, 40, 70, 255);
            box(x, 36, 108.0f * fuel / chars[f->ch].fuel, 4, 90, 170, 255, 255);
        }
    }
    text(10, SH - 32, "motion:");
    {
        int face = 1;
        for (int i = 0; i < nf; i++)
            if (F[i].slot == me)
                face = predicting ? pb.face : F[i].face;
        if (mycmd)
            draw_cmd_arrows(72, SH - 34, mycmd, face);
        else
            text(72, SH - 32, "-");
    }
    text(200, SH - 32, "Esc / menu: options");
    text(10, SH - 16, "predicting: %s   unacked inputs: %u", predicting ? "on" : "off", sent - ack);
    if (winner) {
        char b[32];
        snprintf(b, sizeof b, winner - 1 == me ? "YOU WIN" : "P%d WINS", winner - 1);
        SDL_SetRenderScale(renderer, 3, 3);
        text((SW / 3.0f - strlen(b) * 8) / 2, 60, "%s", b);
        SDL_SetRenderScale(renderer, 1, 1);
    }
}

/* =============================== Title / Connect screens =============================== */

/* One fighter cell for the title face-off (attack pose, optional horizontal flip) */
static void title_fighter(int ch, float x, float y, float size, int flip)
{
    if (ch < 0 || ch >= NCHARS) ch = 0;
    if (!tex[ch]) {
        const Uint8 *c = pal[ch % MAX_PLAYERS];
        box(x + size * 0.25f, y + size * 0.1f, size * 0.5f, size * 0.85f, c[0], c[1], c[2], 255);
        return;
    }
    int row = ROW_MOVE + 2;                         /* fierce punch row */
    int col = 1 + (int)((aclk / 6) % 3);            /* windup / impact / follow */
    SDL_FRect src = { col * SPR_CELL, row * SPR_CELL, SPR_CELL, SPR_CELL };
    SDL_FRect dst = { x, y, size, size };
    SDL_SetTextureAlphaMod(tex[ch], 255);
    SDL_SetTextureColorMod(tex[ch], 255, 255, 255);
    SDL_RenderTextureRotated(renderer, tex[ch], &src, &dst, 0, NULL,
                             flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

void draw_title(int item)
{
    const char *label[TITLE_COUNT] = {
        "Single Player",
        "Host Game",
        "Connect",
        "Options",
        "Quit",
    };
    /* Stadium backdrop + floor, then a dark wash so the menu stays readable */
    if (bg) {
        draw_stage(bg);
        box(0, GROUND, SW, SH - GROUND, 40, 36, 48, 255);
        box(0, GROUND, SW, 4, 90, 82, 100, 255);
    } else {
        box(0, 0, SW, SH, 12, 14, 28, 255);
    }
    box(0, 0, SW, SH, 8, 10, 22, 140);

    SDL_SetRenderScale(renderer, 3, 3);
    text((SW / 3.0f - 13 * 8) / 2, 16, "BananaFighter");
    SDL_SetRenderScale(renderer, 1, 1);
    {
        char ver[32];
        snprintf(ver, sizeof ver, "v%s", BF_VERSION);
        text((SW - (int)strlen(ver) * 8) / 2.0f, 72, "%s", ver);
    }

    /* Left menu panel */
    const float panel_x = 48, panel_y = 160, panel_w = 400, panel_h = 320;
    box(panel_x, panel_y, panel_w, panel_h, 16, 18, 32, 220);
    box(panel_x, panel_y, 6, panel_h, 210, 190, 70, 255);
    text(panel_x + 28, panel_y + 20, "Pick a mode to start");
    for (int i = 0; i < TITLE_COUNT; i++) {
        float y = panel_y + 56 + i * 48;
        if (i == item)
            box(panel_x + 16, y - 10, panel_w - 32, 36, 65, 70, 100, 255);
        text(panel_x + 36, y, "%c %s", i == item ? '>' : ' ', label[i]);
    }

    /* Face-off on the right: rotating roster pair in fierce-punch poses */
    int left_ch = (int)((aclk / 180) % NCHARS);
    int right_ch = (left_ch + 1) % NCHARS;
    const float fight_size = 380;
    const float fight_y = SH - fight_size - 36;
    title_fighter(left_ch, 520, fight_y, fight_size, 0);
    title_fighter(right_ch, SW - fight_size - 40, fight_y, fight_size, 1);

    text(84, SH - 28, "Up/Down or tap: select   Enter / pad A / tap: choose");
}

void draw_connect(int list_sel, const char *address, const char *status)
{
    box(0, 0, SW, SH, 12, 14, 28, 255);
    SDL_SetRenderScale(renderer, 2, 2);
    text(40, 24, "Connect");
    SDL_SetRenderScale(renderer, 1, 1);

    text(80, 100, "Local servers");
    box(80, 120, SW - 160, 280, 22, 24, 40, 255);

    int n = lan_count();
    if (n <= 0) {
        text(100, 160, "(searching LAN...)");
    } else {
        for (int i = 0; i < n; i++) {
            const LanServer *s = lan_at(i);
            float y = 140 + i * 28;
            if (i == list_sel)
                box(90, y - 6, SW - 180, 24, 65, 70, 100, 255);
            text(110, y, "%c %s:%d", i == list_sel ? '>' : ' ', s->host, s->port);
        }
    }

    text(80, 420, "Address (host or host:port)");
    box(80, 440, SW - 160, 28, 22, 24, 40, 255);
    text(96, 448, "%s_", address && address[0] ? address : "");

    if (status && status[0])
        text(80, 490, "%s", status);
    text(80, SH - 48,
         "Up/Down or tap: pick   Type address   Enter/tap Connect   Esc/tap back");
}

/* =============================== Options menu (Escape / title) =============================== */

#define OPT_PANEL_W 640.0f
#define OPT_PANEL_H 560.0f
#define OPT_PANEL_Y 60.0f
#define OPT_ROW0 70.0f
#define OPT_ROW_H 32.0f
#define OPT_VISIBLE 12

int options_item_hidden(int item)
{
    /* In-play pause root: Resume / Options / Main menu only. */
    if (options_in_play && options_submenu == OPTMENU_PAUSE) {
        return !(item == OPT_RESUME || item == OPT_OPEN_SETTINGS || item == OPT_QUIT);
    }
    /* Controls submenu: Back + key binds. */
    if (options_submenu == OPTMENU_CONTROLS) {
        if (item == OPT_RESUME)
            return 0;
        if (item >= OPT_BIND0 && item < OPT_BIND0 + BIND_COUNT)
            return 0;
        return 1;
    }
    /* Settings (title or from pause): audio/video + Controls entry. */
    if (item == OPT_OPEN_SETTINGS || item == OPT_QUIT)
        return 1;
    if (item >= OPT_BIND0 && item < OPT_BIND0 + BIND_COUNT)
        return 1;
#ifdef BF_IOS
    if (item == OPT_FULLSCREEN)
        return 1;
#endif
    return 0;
}

static int options_step(int cur, int dir)
{
    int n = OPT_COUNT;
    do
        cur = (cur + dir + n) % n;
    while (options_item_hidden(cur));
    return cur;
}

void options_ensure_scroll(void)
{
    int vis = 0, sel_vis = 0;
    for (int i = 0; i < OPT_COUNT; i++) {
        if (options_item_hidden(i))
            continue;
        if (i == options_item)
            sel_vis = vis;
        vis++;
    }
    if (options_scroll > sel_vis)
        options_scroll = sel_vis;
    if (options_scroll < sel_vis - (OPT_VISIBLE - 1))
        options_scroll = sel_vis - (OPT_VISIBLE - 1);
    if (options_scroll < 0)
        options_scroll = 0;
}

static float vol_step(float v, int dir)
{
    v += dir * 0.1f;
    if (v < 0.f)
        v = 0.f;
    if (v > 1.f)
        v = 1.f;
    /* Snap to tenths */
    v = (float)((int)(v * 10.f + 0.5f)) / 10.f;
    return v;
}

static void option_label(int item, char *buf, size_t n)
{
    if (item == OPT_RESUME) {
        if (options_in_play && options_submenu == OPTMENU_PAUSE)
            snprintf(buf, n, "Resume");
        else
            snprintf(buf, n, "Back");
        return;
    }
    if (item == OPT_OPEN_SETTINGS) {
        snprintf(buf, n, "Options");
        return;
    }
    if (item == OPT_OPEN_CONTROLS) {
        snprintf(buf, n, "Controls");
        return;
    }
    if (item == OPT_MUSIC) {
        snprintf(buf, n, "Music volume: %d%%", (int)(g_settings.music_vol * 100 + 0.5f));
        return;
    }
    if (item == OPT_SFX) {
        snprintf(buf, n, "SFX volume: %d%%", (int)(g_settings.sfx_vol * 100 + 0.5f));
        return;
    }
    if (item == OPT_FULLSCREEN) {
        snprintf(buf, n, "Fullscreen: %s", g_settings.fullscreen ? "ON" : "OFF");
        return;
    }
    if (item == OPT_BOXES) {
        snprintf(buf, n, "Hitboxes: %s", show_boxes ? "ON" : "OFF");
        return;
    }
    if (item >= OPT_BIND0 && item < OPT_BIND0 + BIND_COUNT) {
        int b = item - OPT_BIND0;
        SDL_Scancode sc = g_settings.binds[b];
        const char *name = sc == SDL_SCANCODE_UNKNOWN ? "(none)" : SDL_GetScancodeName(sc);
        if (!name || !name[0])
            name = "?";
        if (options_rebinding == b)
            snprintf(buf, n, "%s: ... press a key ...", bind_action_name(b));
        else
            snprintf(buf, n, "%s: %s", bind_action_name(b), name);
        return;
    }
    if (item == OPT_RESET) {
        snprintf(buf, n, "Reset defaults");
        return;
    }
    if (item == OPT_QUIT) {
        snprintf(buf, n, "Main menu");
        return;
    }
    snprintf(buf, n, "?");
}

static void options_enter_level(int level, int select_item)
{
    options_submenu = level;
    options_item = select_item;
    options_scroll = 0;
    options_ensure_scroll();
}

/* Esc / Start / touch menu: step out one level or close. */
void options_back(void)
{
    options_rebinding = -1;
    if (options_submenu == OPTMENU_CONTROLS) {
        options_enter_level(OPTMENU_SETTINGS, OPT_OPEN_CONTROLS);
        return;
    }
    if (options_in_play && options_submenu == OPTMENU_SETTINGS) {
        options_enter_level(OPTMENU_PAUSE, OPT_OPEN_SETTINGS);
        return;
    }
    options_open = 0;
    options_submenu = OPTMENU_PAUSE;
}

/* Activate the selected option (may close menu or leave to title) */
void option_activate(int *leaveTitle)
{
    if (options_rebinding >= 0)
        return;
    if (options_item == OPT_RESUME) {
        if (options_submenu == OPTMENU_CONTROLS) {
            options_enter_level(OPTMENU_SETTINGS, OPT_OPEN_CONTROLS);
            return;
        }
        if (options_in_play && options_submenu == OPTMENU_SETTINGS) {
            options_enter_level(OPTMENU_PAUSE, OPT_OPEN_SETTINGS);
            return;
        }
        options_open = 0;
        options_submenu = OPTMENU_PAUSE;
        return;
    }
    if (options_item == OPT_OPEN_SETTINGS) {
        options_enter_level(OPTMENU_SETTINGS, OPT_RESUME);
        return;
    }
    if (options_item == OPT_OPEN_CONTROLS) {
        options_enter_level(OPTMENU_CONTROLS, OPT_RESUME);
        return;
    }
    if (options_item == OPT_MUSIC) {
        g_settings.music_vol = vol_step(g_settings.music_vol, 1);
        if (g_settings.music_vol < 0.05f && g_settings.music_vol > 0.f)
            g_settings.music_vol = 0.f;
        settings_apply_audio();
        settings_save();
        return;
    }
    if (options_item == OPT_SFX) {
        g_settings.sfx_vol = vol_step(g_settings.sfx_vol, 1);
        settings_apply_audio();
        settings_save();
        sfx(SN_SELECT, 0.8f);
        return;
    }
    if (options_item == OPT_FULLSCREEN) {
#ifndef BF_IOS
        g_settings.fullscreen ^= 1;
        settings_apply_video();
        settings_save();
#endif
        return;
    }
    if (options_item == OPT_BOXES) {
        show_boxes ^= 1;
        g_settings.show_boxes = show_boxes;
        settings_save();
        return;
    }
    if (options_item >= OPT_BIND0 && options_item < OPT_BIND0 + BIND_COUNT) {
        options_rebinding = options_item - OPT_BIND0;
        return;
    }
    if (options_item == OPT_RESET) {
        settings_reset_and_save();
        return;
    }
    if (options_item == OPT_QUIT) {
        if (leaveTitle)
            *leaveTitle = 1;
        options_open = 0;
        return;
    }
}

/* Handle options menu navigation and activation */
void options_input(int edge, int confirm, int *leaveTitle)
{
    if (options_rebinding >= 0)
        return;
    if (edge & IN_U) {
        options_item = options_step(options_item, -1);
        options_ensure_scroll();
    }
    if (edge & IN_D) {
        options_item = options_step(options_item, 1);
        options_ensure_scroll();
    }
    if (edge & IN_L) {
        if (options_item == OPT_MUSIC) {
            g_settings.music_vol = vol_step(g_settings.music_vol, -1);
            settings_apply_audio();
            settings_save();
        } else if (options_item == OPT_SFX) {
            g_settings.sfx_vol = vol_step(g_settings.sfx_vol, -1);
            settings_apply_audio();
            settings_save();
        }
    }
    if (edge & IN_R) {
        if (options_item == OPT_MUSIC) {
            g_settings.music_vol = vol_step(g_settings.music_vol, 1);
            settings_apply_audio();
            settings_save();
        } else if (options_item == OPT_SFX) {
            g_settings.sfx_vol = vol_step(g_settings.sfx_vol, 1);
            settings_apply_audio();
            settings_save();
            sfx(SN_SELECT, 0.6f);
        }
    }
    if (confirm)
        option_activate(leaveTitle);
}

int options_hit_test(float x, float y)
{
    const float w = OPT_PANEL_W, bx = (SW - w) / 2, by = OPT_PANEL_Y;
    int vis = 0, shown = 0;
    if (x < bx + 16 || x > bx + w - 16)
        return -1;
    for (int i = 0; i < OPT_COUNT; i++) {
        if (options_item_hidden(i))
            continue;
        if (vis < options_scroll) {
            vis++;
            continue;
        }
        if (shown >= OPT_VISIBLE)
            break;
        float row = by + OPT_ROW0 + shown * OPT_ROW_H;
        if (y >= row - 6 && y <= row + OPT_ROW_H - 8)
            return i;
        vis++;
        shown++;
    }
    return -1;
}

/* Render the options menu overlay */
void draw_options(void)
{
    const float w = OPT_PANEL_W, h = OPT_PANEL_H, x = (SW - w) / 2, y = OPT_PANEL_Y;
    char label[96];
    int vis = 0, shown = 0;

    options_ensure_scroll();

    box(0, 0, SW, SH, 0, 0, 0, 180);
    box(x - 3, y - 3, w + 6, h + 6, 210, 190, 70, 255);
    box(x, y, w, h, 22, 24, 40, 255);
    SDL_SetRenderScale(renderer, 2, 2);
    text((x + 24) / 2, (y + 16) / 2,
         options_submenu == OPTMENU_CONTROLS ? "CONTROLS" :
         (options_in_play && options_submenu == OPTMENU_PAUSE) ? "PAUSED" : "OPTIONS");
    SDL_SetRenderScale(renderer, 1, 1);

    for (int i = 0; i < OPT_COUNT; i++) {
        if (options_item_hidden(i))
            continue;
        if (vis < options_scroll) {
            vis++;
            continue;
        }
        if (shown >= OPT_VISIBLE)
            break;
        float ry = y + OPT_ROW0 + shown * OPT_ROW_H;
        option_label(i, label, sizeof label);
        if (i == options_item)
            box(x + 16, ry - 6, w - 32, OPT_ROW_H - 4, 65, 70, 100, 255);
        text(x + 30, ry, "%c %s", i == options_item ? '>' : ' ', label);
        vis++;
        shown++;
    }

    if (options_submenu == OPTMENU_CONTROLS) {
        text(x + 24, y + h - 56,
             "Enter: rebind   Esc: back   Arrow keys always move");
        text(x + 24, y + h - 36,
             "Gamepad: d-pad/stick move, X Y RB punch, A B RT kick");
    } else {
        text(x + 24, y + h - 56,
             "Up/Down: select   Left/Right: volume   Enter: choose   Esc: back");
        text(x + 24, y + h - 36,
             "Gamepad: d-pad/stick move, X Y RB punch, A B RT kick");
        text(x + 24, y + h - 20,
             "Fullscreen letterboxes 1280x720 to any display resolution");
    }
}

/* =============================== Fighter select =============================== */

static int sel_scroll; /* first visible row of the character grid */

static int sel_rows(void) { return (NCHARS + SEL_COLS - 1) / SEL_COLS; }
static int sel_max_scroll(void)
{
    int extra = sel_rows() - SEL_VISIBLE_ROWS;
    return extra > 0 ? extra : 0;
}

/* Keep the cursor's row inside the visible window */
static void ensure_sel_scroll(int cur)
{
    int row = cur / SEL_COLS, max = sel_max_scroll();
    if (row < sel_scroll) sel_scroll = row;
    if (row >= sel_scroll + SEL_VISIBLE_ROWS) sel_scroll = row - SEL_VISIBLE_ROWS + 1;
    if (sel_scroll > max) sel_scroll = max;
    if (sel_scroll < 0) sel_scroll = 0;
}

/* Grid layout helpers for select screen cells (y is relative to the scroll window) */
static int cell_x(int i) { return SW - (SEL_COLS * CELL_W + (SEL_COLS - 1) * CELL_GAP) - 60 + (i % SEL_COLS) * (CELL_W + CELL_GAP); }
static int cell_y(int i) { return 130 + (i / SEL_COLS - sel_scroll) * (CELL_H + CELL_GAP); }
static int grid_top(void) { return 130; }
static int grid_h(void) { return SEL_VISIBLE_ROWS * CELL_H + (SEL_VISIBLE_ROWS - 1) * CELL_GAP; }

/* Short label for an input bit (used in move list) */
static const char *btn_label(int in)
{
    return in == IN_LP ? "LP" : in == IN_MP ? "MP" : in == IN_HP ? "HP" : in == IN_LK ? "LK" :
           in == IN_MK ? "MK" : in == IN_HK ? "HK" : in == IN_P ? "P" : in == IN_K ? "K" : "?";
}

/* Draw an idle portrait (animated if requested) */
static void idle_portrait(int ch, float x, float y, float size, int animate)
{
    if (!tex[ch]) {
        const Uint8 *c = pal[ch % MAX_PLAYERS];
        box(x + size * 0.25f, y + size * 0.1f, size * 0.5f, size * 0.85f, c[0], c[1], c[2], 255);
        return;
    }
    SDL_FRect src = { (animate ? (aclk / IDLE_TICKS) % IDLE_FR : 0) * SPR_CELL, ROW_IDLE * SPR_CELL, SPR_CELL, SPR_CELL };
    SDL_FRect dst = { x, y, size, size };
    SDL_SetTextureAlphaMod(tex[ch], 255);
    SDL_SetTextureColorMod(tex[ch], 255, 255, 255);
    SDL_RenderTexture(renderer, tex[ch], &src, &dst);
}

/* Roster along the bottom: each player's slot, fighter, and ready state */
static void draw_roster(void)
{
    const float left = 28, top = SH - 94, width = (SW - 2 * left) / ROSTER_COLS;

    box(16, top - 22, SW - 32, 110, 12, 14, 28, 240);
    text(left, top - 15, "PLAYERS AND SELECTIONS  (%d/%d)", nl, MAX_PLAYERS);
    for (int i = 0; i < nl; i++) {
        const Lob *p = &U[i];
        int ch = p->ch >= 0 && p->ch < NCHARS ? p->ch : 0;
        const Uint8 *c = pal[p->slot % MAX_PLAYERS];
        float x = left + (i % ROSTER_COLS) * width, y = top + (i / ROSTER_COLS) * 36;
        float card_w = width - 10, port = 28;              /* small portrait of their picked fighter */

        box(x, y, card_w, 31, 32, 35, 52, 255);
        box(x, y, 4, 31, c[0], c[1], c[2], 255);
        box(x + card_w - port - 3, y + 1, port, port, 20, 20, 36, 255);
        idle_portrait(ch, x + card_w - port - 3, y + 1, port, 1);
        text(x + 10, y + 4, "%cP%d  %-8.8s  %s", p->slot == me ? '>' : ' ', p->slot,
             chars[ch].name, p->ready ? "READY" : "PICKING");
        if (p->bot)
            text(x + 10, y + 17, "BOT: %s", p->bot - 1 < nai ? aiName[p->bot - 1] : "?");
        else
            text(x + 10, y + 17, p->slot == me ? "YOU" : "HUMAN");
    }
}

/* Main select screen: big preview, move list, grid of portraits, and help */
void draw_select(int cur, int asel)
{
    const Lob *mine = NULL;
    for (int i = 0; i < nl; i++)
        if (U[i].slot == me) mine = &U[i];
    int locked = mine && mine->ready;
    const Character *c = &chars[cur];

    /* Preview this fighter's home stage; clear the match latch for the next fight */
    bg_match_picked = 0;
    bg_match = NULL;
    bg_match_stage = -1;
    draw_stage(bg_char[cur] ? bg_char[cur] : bg);
    box(0, 0, SW, SH, 8, 8, 20, 175);

    SDL_SetRenderScale(renderer, 3, 3);
    text(40 / 3.0f, 36 / 3.0f, "SELECT YOUR FIGHTER");
    SDL_SetRenderScale(renderer, 1, 1);

    /* the big preview of whoever the cursor is on */
    box(40, 100, 420, 430, 20, 20, 40, 190);
    idle_portrait(cur, 50, 110, 400, 1);
    SDL_SetRenderScale(renderer, 2, 2);
    text(60 / 2.0f, 505 / 2.0f, "%s", c->name);
    SDL_SetRenderScale(renderer, 1, 1);
    if (locked) {
        SDL_SetRenderScale(renderer, 2, 2);
        text(250 / 2.0f, 130 / 2.0f, "LOCKED IN");
        SDL_SetRenderScale(renderer, 1, 1);
    }
    text(60, 542, "HP %d   LIVES %d   SPEED %d   JUMP %d", c->hp, c->lives, c->speed, c->jump);
    text(60, 558, "%s", c->fly ? "Tail flight: hold jump in the air" : "");

    /* its moves */
    text(480, 108, "MOVES");
    int line = 0;
    for (int k = 0; k < MAXMOVES && c->moves[k].name; k++) {
        const Move *m = &c->moves[k];
        char cost[16] = "";
        if (m->cost) snprintf(cost, sizeof cost, " [%d]", m->cost);
        {
            float ly = 128 + line++ * 14;
            text(480, ly, "%-11.11s%s", m->name, m->air ? " (air)" : "");
            float ax = 480 + 118;
            if (m->air)
                ax += 32;
            if (m->cmd)
                ax += draw_cmd_arrows(ax, ly - 2, m->cmd, 1);
            if (m->cmd2) {
                text(ax, ly, "/");
                ax += 8;
                ax += draw_cmd_arrows(ax, ly - 2, m->cmd2, 1);
            }
            text(ax, ly, "%s%s", btn_label(m->input), cost);
        }
    }

    /* the scrolling grid of fighter portraits */
    ensure_sel_scroll(cur);
    {
        float gx = cell_x(0) - 4, gy = grid_top() - 4;
        float gw = SEL_COLS * CELL_W + (SEL_COLS - 1) * CELL_GAP + 8, gh = grid_h() + 8;
        SDL_Rect clip = { (int)gx, (int)gy, (int)gw, (int)gh };
        SDL_SetRenderClipRect(renderer, &clip);
        for (int i = 0; i < NCHARS; i++) {
            int row = i / SEL_COLS;
            if (row < sel_scroll || row >= sel_scroll + SEL_VISIBLE_ROWS) continue;
            float x = cell_x(i), y = cell_y(i);
            box(x, y, CELL_W, CELL_H, 30, 30, 48, 235);
            idle_portrait(i, x + 11, y + 4, 128, 0);
            text(x + 8, y + CELL_H - 34, "%.12s", chars[i].name);
            if (i == cur) {                             /* pulsing cursor */
                int pulse = 160 + (int)(95 * ((aclk / 6) % 2));
                box(x - 4, y - 4, CELL_W + 8, 4, pulse, pulse, 40, 255);
                box(x - 4, y + CELL_H, CELL_W + 8, 4, pulse, pulse, 40, 255);
                box(x - 4, y, 4, CELL_H, pulse, pulse, 40, 255);
                box(x + CELL_W, y, 4, CELL_H, pulse, pulse, 40, 255);
            }
            int tag = 0;                                /* who has picked this fighter */
            for (int k = 0; k < nl; k++) {
                if (U[k].ch != i) continue;
                const Uint8 *pc = pal[U[k].slot % MAX_PLAYERS];
                float tx = x + 6 + tag++ * 22, ty = y + CELL_H - 18;
                box(tx, ty, 18, 14, pc[0], pc[1], pc[2], 255);
                text(tx + 5, ty + 3, "%d", U[k].slot);
                if (U[k].ready) box(tx, ty + 14, 18, 2, 255, 255, 255, 255);
            }
        }
        SDL_SetRenderClipRect(renderer, NULL);

        /* scroll cues when more fighters sit above/below the window */
        float mid = gx + gw / 2;
        if (sel_scroll > 0)
            text(mid - 20, gy - 16, "^ more");
        if (sel_scroll < sel_max_scroll())
            text(mid - 20, gy + gh + 2, "v more");
    }

    /* short help under the grid; the full controls are in the options menu */
    float hx = cell_x(0), hy = grid_top() + grid_h() + 22;
    text(hx, hy,      "Move: arrows/WASD/pad   Lock in: attack/Enter/Start");
    text(hx, hy + 16, "T or Back: add a bot as %s (%s)", c->name, nai ? aiName[asel] : "-");
    text(hx, hy + 32, "[ ] or shoulders: bot AI   Backspace/Delete: remove");
    text(hx, hy + 48, "Esc: options menu");

    draw_roster();
}

