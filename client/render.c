#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "client.h"

static const Uint8 pal[MAX_PLAYERS][3] = {
    {230, 70, 70},   {70, 140, 230}, {80, 200, 100}, {230, 200, 60},
    {180, 90, 220},  {240, 140, 50}, {70, 210, 210}, {230, 110, 170},
    {150, 150, 150}, {140, 90, 50},
};

int show_boxes, options_open, options_item;
unsigned aclk;
SDL_Renderer *ren;
static Anim an[MAX_PLAYERS];   /* per-slot animation state */
static SDL_Texture *tex[NCHARS]; /* NULL = fall back to coloured boxes */
static SDL_Texture *shots, *shipt, *fxt, *bg;

/* The sheets are 32-bit BMPs with a real alpha channel, so edges are smooth: no colour key. */
static SDL_Texture *load_tex(const char *file)
{
    char path[512];
    const char *base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%ssprites/%s", base ? base : "", file);
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) {
        snprintf(path, sizeof path, "sprites/%s", file);
        s = SDL_LoadBMP(path);
    }
    if (!s) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(ren, s);
    SDL_DestroySurface(s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

void load_sprites(void)
{
    for (int i = 0; i < NCHARS; i++) {
        char fn[48];
        snprintf(fn, sizeof fn, "%s.bmp", chars[i].name);
        for (char *p = fn; *p; p++) *p = (char)tolower((unsigned char)*p);
        if (!(tex[i] = load_tex(fn)))
            fprintf(stderr, "no sprites/%s: drawing %s as a box\n", fn, chars[i].name);
    }
    shots = load_tex("shots.bmp");
    shipt = load_tex("ship.bmp");
    fxt   = load_tex("fx.bmp");
    bg    = load_tex("background.bmp");
}

/* My fighter is drawn from the predicted state, everyone else from the snapshot. */
static Fig eff(int i)
{
    Fig f = F[i];
    if (predicting && f.slot == me) {
        f.x = pb.x; f.y = pb.y; f.face = pb.face; f.invis = pb.invis > 0;
        f.st = pb.stun ? -2 : pb.block ? -3 : pb.move >= 0 ? pb.move : pb.flying ? -4 : pb.crouch ? -5 : -1;
    }
    return f;
}

/* Per-tick bookkeeping: animation state, plus the sounds that follow a fighter's own motion. */
void anim_tick(void)
{
    aclk++;
    for (int i = 0; i < nf; i++) {
        Fig f = eff(i);
        Anim *a = &an[f.slot];
        if (f.x < 0) continue;                      /* hidden */
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

/* An attack's six frames: two of windup, two of impact, two of follow-through. */
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

static void pick(const Fig *f, int *row, int *col)
{
    const Character *c = &chars[f->ch];
    const Anim *a = &an[f->slot];

    if (f->st == -2) { *row = ROW_HURT; *col = a->t < 3 ? 0 : a->t < 8 ? 1 : 2; }
    else if (f->st == -3) { *row = ROW_BLOCK; *col = a->t < 4 ? 0 : 1; }
    else if (f->st == -4) { *row = ROW_FLY; *col = (aclk / 2) % FLY_FR; }
    else if (f->st == -5) { *row = ROW_CROUCH; *col = a->t < 4 ? 0 : 1; }
    else if (f->st >= 0 && f->st < MAXMOVES && c->moves[f->st].name) {
        *row = ROW_MOVE + move_row(&c->moves[f->st]);
        *col = move_frame(&c->moves[f->st], a->t);
    }
    else if (f->y > 0) { *row = ROW_JUMP; *col = a->dy > 2 ? 0 : a->dy < -2 ? 2 : 1; }
    else if (a->land > 0) { *row = ROW_JUMP; *col = 3; }
    else if (a->still < WALK_HOLD) { *row = ROW_WALK; *col = (a->walk / WALK_TICKS) % WALK_FR; }
    else { *row = ROW_IDLE; *col = (aclk / IDLE_TICKS) % IDLE_FR; }
}

static void box(float x, float y, float w, float h, int r, int g, int b, int a)
{
    SDL_FRect q = { x, y, w, h };
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_RenderFillRect(ren, &q);
}

static void text(float x, float y, const char *fmt, ...)
{
    char b[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    SDL_SetRenderDrawColor(ren, 240, 240, 240, 255);
    SDL_RenderDebugText(ren, x, y, b);
}

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

    SDL_SetTextureAlphaMod(t, f->invis ? 90 : 255);
    if (f->st == -2) SDL_SetTextureColorMod(t, 255, 140, 140);     /* hurt: red tint */
    else SDL_SetTextureColorMod(t, 255, 255, 255);
    SDL_RenderTextureRotated(ren, t, &src, &dst, 0, NULL,
                             f->face < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);

    const Uint8 *pc = pal[f->slot % MAX_PLAYERS];                   /* slot marker above the head */
    box(xc - 7, dst.y + cs * 0.02f, 14, 6, pc[0], pc[1], pc[2], f->invis ? 90 : 255);
}

static void draw_box(const Fig *f)                  /* fallback when a sheet is missing */
{
    const Character *c = &chars[f->ch];
    float w = c->w * SCALE, h = c->h * SCALE * (f->st == -5 ? 0.5f : 1.0f);
    float x = f->x * SCALE - w / 2, y = GROUND - f->y * SCALE - h;
    int a = f->invis ? 90 : 255;
    const Uint8 *col = pal[f->slot % MAX_PLAYERS];

    box(x, y, w, h, col[0], col[1], col[2], a);
    box(f->face > 0 ? x + w * 0.65f : x + w * 0.15f, y + h * 0.12f, w * 0.2f, h * 0.08f, 255, 255, 255, a);
    if (f->st == -2) box(x, y, w, h, 255, 255, 255, 140);
    if (f->st >= 0 && f->st < MAXMOVES && c->moves[f->st].name) {
        const Move *m = &c->moves[f->st];
        if (!m->proj_speed && m->reach)
            box(f->face > 0 ? x + w : x - m->reach * SCALE,
                GROUND - (f->y + m->y + m->h) * SCALE, m->reach * SCALE, m->h * SCALE,
                255, 230, 80, 160);
    }
}

static void draw_fighter(const Fig *f)
{
    const Character *c = &chars[f->ch];
    if (f->x < 0) return;                           /* hidden by the server (invisible foe) */
    float sw = c->w * SCALE;
    float lift = f->y * SCALE;                      /* shadow shrinks and fades as you rise */
    float shw = sw * (1.0f - lift / 600.0f);
    if (shw < sw * 0.4f) shw = sw * 0.4f;
    box(f->x * SCALE - shw / 2, GROUND - 3, shw, 6, 0, 0, 0, (f->invis ? 30 : 80) * (lift > 300 ? 0.5f : 1.0f));
    if (tex[f->ch]) draw_sprite(f, tex[f->ch]); else draw_box(f);
    if (show_boxes) {                               /* hitbox overlay */
        float h = c->h * SCALE * (f->st == -5 ? 0.5f : 1.0f);
        box(f->x * SCALE - sw / 2, GROUND - f->y * SCALE - h, sw, h, 255, 255, 255, 50);
    }
}

static void draw_shot(const Shot *s)
{
    int ch = 0, face = 1;
    for (int i = 0; i < nf; i++)
        if (F[i].slot == s->owner) { ch = F[i].ch; face = F[i].face; }
    int spin = 0;                                   /* does this character's projectile spin? */
    for (int k = 0; k < MAXMOVES && chars[ch].moves[k].name; k++)
        if (chars[ch].moves[k].proj_speed) { spin = chars[ch].moves[k].spin; break; }
    float sz = PROJ_HALF * 3.8f, x = s->x * SCALE, y = GROUND - s->y * SCALE;

    if (shots) {
        SDL_FRect src = { ch * SHOT_CELL, 0, SHOT_CELL, SHOT_CELL };
        SDL_FRect dst = { x - sz / 2, y - sz / 2, sz, sz };
        double ang = spin ? (double)aclk * 18 * face : 0;
        SDL_RenderTextureRotated(ren, shots, &src, &dst, ang, NULL,
                                 !spin && face < 0 ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    } else {
        const Uint8 *col = pal[s->owner % MAX_PLAYERS];
        box(x - PROJ_HALF, y - PROJ_HALF, PROJ_HALF * 2, PROJ_HALF * 2, col[0], col[1], col[2], 255);
    }
}

static void draw_bomb(const Shot *s)
{
    float x = s->x * SCALE, y = GROUND - s->y * SCALE;
    if (fxt) {
        SDL_FRect src = { 0, 0, FX_CELL, FX_CELL }, dst = { x - 20, y - 20, 40, 40 };
        SDL_RenderTexture(ren, fxt, &src, &dst);
    } else {
        box(x - 6, y - 10, 12, 20, 40, 40, 50, 255);
    }
}

static void draw_boom(const Boom *b)
{
    int f = b->t > 9 ? 1 : b->t > 4 ? 2 : 3;        /* t counts down from BOOM_TICKS */
    float sz = BOMB_R * 2 * SCALE * (f == 1 ? 0.7f : 1.0f), x = b->x * SCALE, y = GROUND - b->y * SCALE;
    if (fxt) {
        SDL_FRect src = { f * FX_CELL, 0, FX_CELL, FX_CELL }, dst = { x - sz / 2, y - sz * 0.8f, sz, sz };
        SDL_RenderTexture(ren, fxt, &src, &dst);
    } else {
        box(x - sz / 2, y - sz / 2, sz, sz / 2, 255, 150, 40, 160);
    }
}

static void draw_ship(const Ship *s)
{
    float x = s->x * SCALE, y = GROUND - SHIP_Y * SCALE;
    if (shipt) {
        SDL_FRect src = { ((aclk / 8) % 2) * SHIP_FW, 0, SHIP_FW, SHIP_FH };
        SDL_FRect dst = { x - SHIP_DRAW_W / 2, y - SHIP_DRAW_H / 2, SHIP_DRAW_W, SHIP_DRAW_H };
        SDL_RenderTexture(ren, shipt, &src, &dst);
    } else {
        box(x - SHIP_DRAW_W / 2, y - SHIP_DRAW_H / 4, SHIP_DRAW_W, SHIP_DRAW_H / 2, 140, 145, 160, 255);
    }
}

void draw_match(void)
{
    if (bg) {
        SDL_FRect d = { 0, 0, SW, GROUND };
        SDL_RenderTexture(ren, bg, NULL, &d);
    }
    box(0, GROUND, SW, SH - GROUND, 40, 36, 48, 255);
    box(0, GROUND, SW, 4, 90, 82, 100, 255);
    for (int i = 0; i < nsh; i++) draw_ship(&SHP[i]);
    for (int i = 0; i < nf; i++) {
        Fig f = eff(i);
        draw_fighter(&f);
    }
    for (int i = 0; i < ns; i++) draw_shot(&S[i]);
    for (int i = 0; i < nb; i++) draw_bomb(&BM[i]);
    for (int i = 0; i < nex; i++) draw_boom(&EX[i]);
    for (int i = 0; i < nf; i++) {
        const Fig *f = &F[i];
        int mx = chars[f->ch].hp, hp = f->hp < 0 ? 0 : f->hp, x = 10 + i * 118;
        text(x, 8, "%c%d %.8s x%d", f->slot == me ? '>' : ' ', f->slot, chars[f->ch].name, f->lives);
        box(x, 20, 108, 8, 60, 20, 20, 255);
        box(x, 20, 108.0f * hp / mx, 8, 80, 220, 80, 255);
        box(x, 30, 108.0f * f->meter / METER_MAX, 4, 250, 200, 40, 255);
        if (f->slot == me && chars[f->ch].fuel) {   /* my tail-flight fuel */
            int fuel = predicting ? pb.fuel : Q.fuel;
            box(x, 36, 108.0f * fuel / chars[f->ch].fuel, 4, 90, 170, 255, 255);
        }
    }
    text(10, SH - 32, "motion: %s   Esc: options", mycmd == CMD_HCF ? "HCF" : mycmd == CMD_QCF ? "QCF" : "-");
    text(10, SH - 16, "predicting: %s   unacked inputs: %u", predicting ? "on" : "off", sent - ack);
    if (winner) {
        char b[32];
        snprintf(b, sizeof b, winner - 1 == me ? "YOU WIN" : "P%d WINS", winner - 1);
        SDL_SetRenderScale(ren, 3, 3);
        text((SW / 3.0f - strlen(b) * 8) / 2, 60, "%s", b);
        SDL_SetRenderScale(ren, 1, 1);
    }
}

/* ================================================================== options menu (Escape) */

void option_activate(int *quit)
{
    switch (options_item) {
    case OPT_RESUME: options_open = 0; break;
    case OPT_SOUND:  mastervol = mastervol > 0 ? 0 : SOUND_ON; break;
    case OPT_BOXES:  show_boxes ^= 1; break;
    case OPT_QUIT:   *quit = 1; break;
    }
}

void options_input(int edge, int confirm, int *quit)
{
    if (edge & IN_U) options_item = (options_item + OPT_COUNT - 1) % OPT_COUNT;
    if (edge & IN_D) options_item = (options_item + 1) % OPT_COUNT;
    if (confirm) option_activate(quit);
}

void draw_options(void)
{
    const float w = 560, h = 400, x = (SW - w) / 2, y = 90;
    const char *label[OPT_COUNT] = {
        "Resume",
        mastervol > 0 ? "Sound: ON" : "Sound: OFF",
        show_boxes ? "Hitboxes: ON" : "Hitboxes: OFF",
        "Quit game",
    };

    box(0, 0, SW, SH, 0, 0, 0, 180);
    box(x - 3, y - 3, w + 6, h + 6, 210, 190, 70, 255);
    box(x, y, w, h, 22, 24, 40, 255);
    SDL_SetRenderScale(ren, 2, 2);
    text((x + 24) / 2, (y + 20) / 2, "OPTIONS");
    SDL_SetRenderScale(ren, 1, 1);

    for (int i = 0; i < OPT_COUNT; i++) {
        float ry = y + 72 + i * 40;
        if (i == options_item) box(x + 16, ry - 8, w - 32, 32, 65, 70, 100, 255);
        text(x + 30, ry, "%c %s", i == options_item ? '>' : ' ', label[i]);
    }

    text(x + 24, y + 252, "Up/Down: select   Enter or pad A: choose   Esc or Start: resume");
    text(x + 24, y + 280, "CONTROLS");
    text(x + 24, y + 298, "Keyboard: arrows/WASD move, U I O punch, J K L kick");
    text(x + 24, y + 314, "Alt keys: Z X C punch, V B N kick.  Hold AWAY to block");
    text(x + 24, y + 330, "Gamepad: d-pad/stick move, X Y RB punch, A B RT kick");
    text(x + 24, y + 346, "QCF+punch = fireball.  HCF+button = super / big special");
    text(x + 24, y + 362, "Squirrel: hold jump in the air to fly.  Jump and press an attack");
}

/* ================================================================== fighter select */

static int cell_x(int i) { return SW - (SEL_COLS * CELL_W + (SEL_COLS - 1) * CELL_GAP) - 60 + (i % SEL_COLS) * (CELL_W + CELL_GAP); }
static int cell_y(int i) { return 130 + (i / SEL_COLS) * (CELL_H + CELL_GAP); }

static const char *btn_label(int in)
{
    return in == IN_LP ? "LP" : in == IN_MP ? "MP" : in == IN_HP ? "HP" : in == IN_LK ? "LK" :
           in == IN_MK ? "MK" : in == IN_HK ? "HK" : in == IN_P ? "P" : in == IN_K ? "K" : "?";
}

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
    SDL_RenderTexture(ren, tex[ch], &src, &dst);
}

/* The roster along the bottom: every player's slot, fighter and ready state. */
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

/* Street Fighter II style: a grid of portraits and a big animated preview. */
void draw_select(int cur, int asel)
{
    const Lob *mine = NULL;
    for (int i = 0; i < nl; i++)
        if (U[i].slot == me) mine = &U[i];
    int locked = mine && mine->ready;
    const Character *c = &chars[cur];

    if (bg) {
        SDL_FRect d = { 0, 0, SW, GROUND };
        SDL_RenderTexture(ren, bg, NULL, &d);
    }
    box(0, 0, SW, SH, 8, 8, 20, 175);

    SDL_SetRenderScale(ren, 3, 3);
    text(40 / 3.0f, 36 / 3.0f, "SELECT YOUR FIGHTER");
    SDL_SetRenderScale(ren, 1, 1);

    /* the big preview of whoever the cursor is on */
    box(40, 100, 420, 430, 20, 20, 40, 190);
    idle_portrait(cur, 50, 110, 400, 1);
    SDL_SetRenderScale(ren, 2, 2);
    text(60 / 2.0f, 505 / 2.0f, "%s", c->name);
    SDL_SetRenderScale(ren, 1, 1);
    if (locked) {
        SDL_SetRenderScale(ren, 2, 2);
        text(250 / 2.0f, 130 / 2.0f, "LOCKED IN");
        SDL_SetRenderScale(ren, 1, 1);
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
        text(480, 128 + line++ * 14, "%-11.11s %s%s%s%s", m->name, m->air ? "(air) " : "",
             m->cmd ? (m->cmd == CMD_QCF ? "QCF+" : "HCF+") : "", btn_label(m->input), cost);
    }

    /* the grid */
    for (int i = 0; i < NCHARS; i++) {
        float x = cell_x(i), y = cell_y(i);
        box(x, y, CELL_W, CELL_H, 30, 30, 48, 235);
        idle_portrait(i, x + 11, y + 4, 128, 0);
        text(x + 8, y + CELL_H - 34, "%.12s", chars[i].name);
        if (i == cur) {                                 /* pulsing cursor */
            int pulse = 160 + (int)(95 * ((aclk / 6) % 2));
            box(x - 4, y - 4, CELL_W + 8, 4, pulse, pulse, 40, 255);
            box(x - 4, y + CELL_H, CELL_W + 8, 4, pulse, pulse, 40, 255);
            box(x - 4, y, 4, CELL_H, pulse, pulse, 40, 255);
            box(x + CELL_W, y, 4, CELL_H, pulse, pulse, 40, 255);
        }
        int tag = 0;                                    /* who has picked this fighter */
        for (int k = 0; k < nl; k++) {
            if (U[k].ch != i) continue;
            const Uint8 *pc = pal[U[k].slot % MAX_PLAYERS];
            float tx = x + 6 + tag++ * 22, ty = y + CELL_H - 18;
            box(tx, ty, 18, 14, pc[0], pc[1], pc[2], 255);
            text(tx + 5, ty + 3, "%d", U[k].slot);
            if (U[k].ready) box(tx, ty + 14, 18, 2, 255, 255, 255, 255);
        }
    }

    /* short help under the grid; the full controls are in the options menu */
    float hx = cell_x(0), hy = cell_y(NCHARS - 1) + CELL_H + CELL_GAP + 8;
    text(hx, hy,      "Move: arrows/WASD/pad   Lock in: attack/Enter/Start");
    text(hx, hy + 16, "T or Back: add a bot as %s (%s)", c->name, nai ? aiName[asel] : "-");
    text(hx, hy + 32, "[ ] or shoulders: bot AI   Backspace/Delete: remove");
    text(hx, hy + 48, "Esc: options menu");

    draw_roster();
}
