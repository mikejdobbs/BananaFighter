#define _POSIX_C_SOURCE 200809L
#include <SDL3/SDL.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "sim.h"                /* shared with the server */

#include "client.h"

static int load_snd(int i)
{
    char path[512];
    const char *base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%ssounds/%s.wav", base ? base : "", sndfile[i]);
    SDL_AudioSpec spec;
    Uint8 *buf;
    Uint32 len;
    if (!SDL_LoadWAV(path, &spec, &buf, &len)) {
        snprintf(path, sizeof path, "sounds/%s.wav", sndfile[i]);
        if (!SDL_LoadWAV(path, &spec, &buf, &len)) return 0;
    }
    SDL_AudioSpec want = { SDL_AUDIO_S16, 1, AUDIO_RATE };
    if (spec.format != want.format || spec.channels != 1 || spec.freq != AUDIO_RATE) {
        Uint8 *cv;
        int cl;
        if (!SDL_ConvertAudioSamples(&spec, buf, (int)len, &want, &cv, &cl)) { SDL_free(buf); return 0; }
        SDL_free(buf);
        buf = cv;
        len = (Uint32)cl;
    }
    snd[i].pcm = (Sint16 *)buf;
    snd[i].n = len / 2;
    return 1;
}

static void audio_init(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { fprintf(stderr, "no audio: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, AUDIO_RATE };
    astream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!astream) { fprintf(stderr, "no audio device: %s\n", SDL_GetError()); return; }
    int got = 0;
    for (int i = 0; i < SN_COUNT; i++) got += load_snd(i);
    if (got < SN_COUNT) fprintf(stderr, "loaded %d of %d sounds (run make_sounds.py)\n", got, SN_COUNT);
    SDL_ResumeAudioStreamDevice(astream);
}

static void sfx(int s, float vol)
{
    static int rr = 1;
    if (!astream || !snd[s].pcm) return;
    int v = 1;
    while (v < NVOX && vox[v].on) v++;
    if (v == NVOX) { v = rr; rr = rr % (NVOX - 1) + 1; }       /* all busy: steal one */
    vox[v] = (Voice){ .s = s, .on = 1, .vol = vol };
}

static void music(int s)
{
    if (s == cur_music) return;
    cur_music = s;
    vox[0] = (Voice){ .s = s, .on = astream && snd[s].pcm, .loop = 1, .vol = MUSIC_VOL };
}

/* Keep about 125 ms of mixed audio queued. Called once per frame from the main thread. */
static void audio_pump(void)
{
    if (!astream) return;
    while (SDL_GetAudioStreamQueued(astream) < AUDIO_RATE / 8 * 2) {
        Sint16 out[512];
        for (int i = 0; i < 512; i++) {
            float acc = 0;
            for (int v = 0; v < NVOX; v++) {
                Voice *x = &vox[v];
                if (!x->on) continue;
                const Snd *sn = &snd[x->s];
                acc += sn->pcm[x->pos] * x->vol;
                if (++x->pos >= sn->n) { x->pos = 0; if (!x->loop) x->on = 0; }
            }
            acc *= mastervol;
            out[i] = (Sint16)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
        }
        SDL_PutAudioStreamData(astream, out, sizeof out);
    }
}

/* A fighter just started an attack: pick the swing sound from its strength. */
static void swing_sound(const Move *m)
{
    int row = move_row(m);
    int lvl = row < 6 ? row % 3 : row == 6 ? 1 : row == 9 ? 0 : row == 10 ? 1 : 2;
    sfx(SN_SWING1 + lvl, row == 8 ? 1.0f : 0.8f);
}

/* ================================================================== gamepads */

static SDL_Gamepad  *pads[MAXPADS];
static SDL_Joystick *joys[MAXPADS];
static int           npads, njoys;
static int           pprev[SDL_GAMEPAD_BUTTON_COUNT];

static void pad_open(SDL_JoystickID id)
{
    if (npads >= MAXPADS || SDL_GetGamepadFromID(id)) return;
    SDL_Gamepad *g = SDL_OpenGamepad(id);
    if (g) pads[npads++] = g;
}

static void pad_close(SDL_JoystickID id)
{
    for (int i = 0; i < npads; i++)
        if (SDL_GetGamepadID(pads[i]) == id) {
            SDL_CloseGamepad(pads[i]);
            pads[i] = pads[--npads];
            return;
        }
}

static void joy_open(SDL_JoystickID id)
{
    if (njoys >= MAXPADS || SDL_IsGamepad(id) || SDL_GetJoystickFromID(id)) return;
    SDL_Joystick *j = SDL_OpenJoystick(id);
    if (j) joys[njoys++] = j;
}

static void joy_close(SDL_JoystickID id)
{
    for (int i = 0; i < njoys; i++)
        if (SDL_GetJoystickID(joys[i]) == id) {
            SDL_CloseJoystick(joys[i]);
            joys[i] = joys[--njoys];
            return;
        }
}

static void pads_init(void)
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) { fprintf(stderr, "no gamepad support: %s\n", SDL_GetError()); return; }
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; i < n; i++) pad_open(ids[i]);
    SDL_free(ids);
    ids = SDL_GetJoysticks(&n);
    for (int i = 0; i < n; i++) joy_open(ids[i]);
    SDL_free(ids);
}

/* Direction and attack bits from every connected pad and joystick. */
static int padmask(void)
{
    int m = 0;
    for (int i = 0; i < npads; i++) {
        SDL_Gamepad *g = pads[i];
        int ax = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), ay = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT)  || ax < -PAD_DEAD) m |= IN_L;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || ax >  PAD_DEAD) m |= IN_R;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP)    || ay < -PAD_DEAD) m |= IN_U;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN)  || ay >  PAD_DEAD) m |= IN_D;
        for (size_t k = 0; k < sizeof padkeys / sizeof padkeys[0]; k++)
            if (SDL_GetGamepadButton(g, padkeys[k].b)) m |= padkeys[k].bit;
        if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > PAD_TRIG) m |= IN_HK;
    }
    for (int i = 0; i < njoys; i++) {
        SDL_Joystick *j = joys[i];
        Uint8 hat = SDL_GetNumJoystickHats(j) > 0 ? SDL_GetJoystickHat(j, 0) : 0;
        int ax = SDL_GetNumJoystickAxes(j) > 0 ? SDL_GetJoystickAxis(j, 0) : 0;
        int ay = SDL_GetNumJoystickAxes(j) > 1 ? SDL_GetJoystickAxis(j, 1) : 0;
        if ((hat & SDL_HAT_LEFT)  || ax < -PAD_DEAD) m |= IN_L;
        if ((hat & SDL_HAT_RIGHT) || ax >  PAD_DEAD) m |= IN_R;
        if ((hat & SDL_HAT_UP)    || ay < -PAD_DEAD) m |= IN_U;
        if ((hat & SDL_HAT_DOWN)  || ay >  PAD_DEAD) m |= IN_D;
        for (int b = 0; b < 6 && b < SDL_GetNumJoystickButtons(j); b++)
            if (SDL_GetJoystickButton(j, b)) m |= joybuttons[b];
    }
    return m;
}

/* True once when a gamepad button goes down (call once per frame per button). */
static int pad_edge(SDL_GamepadButton b)
{
    int d = 0;
    for (int i = 0; i < npads; i++)
        if (SDL_GetGamepadButton(pads[i], b)) d = 1;
    int e = d && !pprev[b];
    pprev[b] = d;
    return e;
}

static int keymask(void)
{
    const bool *ks = SDL_GetKeyboardState(NULL);
    int mask = 0;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (ks[keys[i].sc]) mask |= keys[i].bit;
    return mask;
}

/* ================================================================== simulation glue */

static int look(int x)                  /* direction of the nearest visible opponent */
{
    int b = -1;
    for (int i = 0; i < nf; i++)
        if (F[i].slot != me && F[i].lives > 0 && F[i].x >= 0 &&
            (b < 0 || abs(F[i].x - x) < abs(F[b].x - x)))
            b = i;
    return b < 0 ? 0 : F[b].x >= x ? 1 : -1;
}

/* The CMD_WIN inputs ending at input number i, oldest first: what the server sees as history. */
static void window(unsigned i, int *w)
{
    for (int k = 0; k < CMD_WIN; k++) {
        int idx = (int)i - (CMD_WIN - 1 - k);
        w[k] = idx >= 0 ? hist[idx % HIST] : 0;
    }
}

/* Adopt the server's state, then replay inputs it hasn't consumed yet. */
static void reconcile(void)
{
    const Fig *f = NULL;
    for (int i = 0; i < nf; i++)
        if (F[i].slot == me) f = &F[i];
    if (!f || !hasq || f->lives <= 0 || winner) { predicting = 0; return; }

    Body b = { .x = f->x, .y = f->y, .vx = Q.vx, .vy = Q.vy, .face = f->face,
               .move = f->st >= 0 ? f->st : -1, .frame = Q.frame, .hit = Q.hit,
               .stun = Q.stun, .block = f->st == -3, .flying = f->st == -4, .invis = Q.invis,
               .grace = Q.grace, .meter = f->meter, .fuel = Q.fuel };
    unsigned a = ack > sent ? sent : ack;
    if (sent - a > HIST - CMD_WIN) a = sent - (HIST - CMD_WIN);
    for (unsigned i = a; i < sent; i++) {
        int w[CMD_WIN], lk = look(b.x);
        window(i, w);
        body_step(&b, &chars[f->ch], hist[i % HIST], lk, detect_cmd(w, CMD_WIN, lk ? lk : b.face));
    }
    pb = b;
    pch = f->ch;
    predicting = 1;
}

/* Sounds that come from the authoritative snapshot: hits, blocks, KOs, shots, bombs, ships. */
static int php[MAX_PLAYERS], plives[MAX_PLAYERS], pns, pnb, pnsh, sfresh = 1;
static void audio_snapshot(void)
{
    for (int i = 0; i < nf; i++) {
        const Fig *f = &F[i];
        if (!sfresh) {
            int d = php[f->slot] - f->hp;
            if (d > 0) {
                if (f->st == -3) sfx(SN_BLOCK, 0.8f);
                else sfx(d < 40 ? SN_HIT1 : d < 90 ? SN_HIT2 : SN_HIT3, 1.0f);
            }
            if (f->lives < plives[f->slot]) sfx(SN_KO, 1.0f);
        }
        php[f->slot] = f->hp;
        plives[f->slot] = f->lives;
    }
    if (!sfresh) {
        if (ns > pns) sfx(SN_SHOT, 0.7f);
        if (nb > pnb) sfx(SN_BOMB, 0.6f);
        if (nsh > pnsh) sfx(SN_SHIP, 0.8f);
        for (int i = 0; i < nex; i++)
            if (EX[i].t >= BOOM_TICKS) sfx(SN_BOOM, 1.0f);
    }
    pns = ns; pnb = nb; pnsh = nsh;
    sfresh = 0;
}

static void parse(char *s)
{
    char t = *s++;

    if (t == 'Y') { me = num(&s); return; }
    if (t == 'A') {                                     /* "A ai0 ai1 ..." */
        nai = 0;
        while (nai < MAXAI) {
            while (*s == ' ') s++;
            int n = 0;
            while (s[n] && s[n] != ' ') n++;
            if (!n) break;
            snprintf(aiName[nai++], sizeof aiName[0], "%.*s", n, s);
            s += n;
        }
        return;
    }
    if (t != 'L' && t != 'S') return;
    if (phase != t) { sent = 0; predicting = 0; sfresh = 1; }   /* new phase: restart numbering and sounds */
    phase = t;
    nf = nl = ns = nb = nsh = nex = hasq = 0;
    if (t == 'S') { tick = num(&s); winner = num(&s); ack = (unsigned)num(&s); }

    for (;;) {
        while (*s == ' ') s++;
        if (!*s) break;
        char k = *s++;
        if (k == 'U' && nl < MAX_PLAYERS) {
            U[nl].slot = num(&s); U[nl].ch = num(&s); U[nl].ready = num(&s); U[nl].bot = num(&s); nl++;
        } else if (k == 'P' && nf < MAX_PLAYERS) {
            Fig *f = &F[nf++];
            f->slot = num(&s); f->ch = num(&s); f->x = num(&s); f->y = num(&s);
            f->hp = num(&s); f->lives = num(&s); f->meter = num(&s);
            f->face = num(&s); f->st = num(&s); f->invis = num(&s);
            if (f->ch < 0 || f->ch >= NCHARS) f->ch = 0;
            if (f->slot < 0 || f->slot >= MAX_PLAYERS) f->slot = 0;
        } else if (k == 'Q') {
            Q.vx = num(&s); Q.vy = num(&s); Q.frame = num(&s); Q.stun = num(&s);
            Q.grace = num(&s); Q.invis = num(&s); Q.hit = num(&s); Q.fuel = num(&s);
            hasq = 1;
        } else if (k == 'J' && ns < MAXPROJ) {
            S[ns].owner = num(&s); S[ns].x = num(&s); S[ns].y = num(&s); ns++;
        } else if (k == 'B' && nb < MAXPROJ) {
            BM[nb].owner = num(&s); BM[nb].x = num(&s); BM[nb].y = num(&s); nb++;
        } else if (k == 'Z' && nsh < MAX_PLAYERS) {
            SHP[nsh].owner = num(&s); SHP[nsh].x = num(&s); SHP[nsh].vx = num(&s); nsh++;
        } else if (k == 'X' && nex < MAXBOOM) {
            EX[nex].x = num(&s); EX[nex].y = num(&s); EX[nex].t = num(&s); nex++;
        } else break;
    }
    if (t == 'S') { reconcile(); audio_snapshot(); }
}

static int pump(void)                               /* 0 = server closed the connection */
{
    for (;;) {
        ssize_t n = recv(fd, rb + rl, sizeof rb - rl - 1, 0);
        if (n == 0) return 0;
        if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        rl += n;
        char *nlp;
        while ((nlp = memchr(rb, '\n', rl))) {
            *nlp = 0;
            parse(rb);
            int used = nlp - rb + 1;
            memmove(rb, rb + used, rl - used);
            rl -= used;
        }
        if (rl >= (int)sizeof rb - 1) rl = 0;
    }
}

static void sendf(const char *fmt, ...)
{
    char b[48];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    send(fd, b, n, 0);
}

static int dial(const char *host, const char *port)
{
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM }, *res, *ai;
    int s = -1, one = 1;

    if (getaddrinfo(host, port, &hints, &res)) return -1;
    for (ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s >= 0 && connect(s, ai->ai_addr, ai->ai_addrlen) == 0) break;
        if (s >= 0) close(s);
        s = -1;
    }
    freeaddrinfo(res);
    if (s >= 0) {
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    }
    return s;
}

/* One fixed simulation step: send the input, predict its result locally.
   While the options menu is open the fighter stands still (neutral input) but the match goes on. */
static void client_tick(void)
{
    if (phase != 'S' || !predicting) return;
    int mask = options_open ? 0 : keymask() | padmask();
    int w[CMD_WIN], lk = look(pb.x);
    hist[sent % HIST] = mask;
    window(sent, w);
    mycmd = detect_cmd(w, CMD_WIN, lk ? lk : pb.face);
    sendf("I %d\n", mask);
    body_step(&pb, &chars[pch], mask, lk, mycmd);
    sent++;
}

/* ================================================================== images */

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

static void load_sprites(void)
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
        f.st = pb.stun ? -2 : pb.block ? -3 : pb.move >= 0 ? pb.move : pb.flying ? -4 : -1;
    }
    return f;
}

/* Per-tick bookkeeping: animation state, plus the sounds that follow a fighter's own motion. */
static void anim_tick(void)
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
    float w = c->w * SCALE, h = c->h * SCALE;
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
        float h = c->h * SCALE;
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

static void draw_match(void)
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

enum { OPT_RESUME, OPT_SOUND, OPT_BOXES, OPT_QUIT, OPT_COUNT };

static void option_activate(int *quit)
{
    switch (options_item) {
    case OPT_RESUME: options_open = 0; break;
    case OPT_SOUND:  mastervol = mastervol > 0 ? 0 : SOUND_ON; break;
    case OPT_BOXES:  show_boxes ^= 1; break;
    case OPT_QUIT:   *quit = 1; break;
    }
}

static void options_input(int edge, int confirm, int *quit)
{
    if (edge & IN_U) options_item = (options_item + OPT_COUNT - 1) % OPT_COUNT;
    if (edge & IN_D) options_item = (options_item + 1) % OPT_COUNT;
    if (confirm) option_activate(quit);
}

static void draw_options(void)
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

        box(x, y, width - 10, 31, 32, 35, 52, 255);
        box(x, y, 4, 31, c[0], c[1], c[2], 255);
        text(x + 10, y + 4, "%cP%d  %-8.8s  %s", p->slot == me ? '>' : ' ', p->slot,
             chars[ch].name, p->ready ? "READY" : "PICKING");
        if (p->bot)
            text(x + 10, y + 17, "BOT: %s", p->bot - 1 < nai ? aiName[p->bot - 1] : "?");
        else
            text(x + 10, y + 17, p->slot == me ? "YOU" : "HUMAN");
    }
}

/* Street Fighter II style: a grid of portraits and a big animated preview. */
static void draw_select(int cur, int asel)
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

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    char port[8];
    snprintf(port, sizeof port, "%d", argc > 2 ? atoi(argv[2]) : PORT);

    if ((fd = dial(host, port)) < 0) { fprintf(stderr, "cannot connect to %s:%s\n", host, port); return 1; }
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win;
    if (!SDL_CreateWindowAndRenderer("fighter", SW, SH, 0, &win, &ren)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderVSync(ren, 1);
    load_sprites();
    pads_init();
    audio_init();

    const Uint64 dt = SDL_NS_PER_SECOND / TICK_HZ;
    Uint64 acc = 0, prev = SDL_GetTicksNS();
    int cur = 0, asel = 0, quit = 0, prev_mask = 0;     /* cursor character, the next bot's AI */

    while (!quit) {
        int key_ready = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit = 1;
            else if (e.type == SDL_EVENT_GAMEPAD_ADDED) pad_open(e.gdevice.which);
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) pad_close(e.gdevice.which);
            else if (e.type == SDL_EVENT_JOYSTICK_ADDED) joy_open(e.jdevice.which);
            else if (e.type == SDL_EVENT_JOYSTICK_REMOVED) joy_close(e.jdevice.which);
            if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) continue;
            SDL_Keycode k = e.key.key;
            if (k == SDLK_ESCAPE) {                 /* Escape opens and closes the options menu */
                options_open ^= 1;
                options_item = OPT_RESUME;
                continue;
            }
            if (k == SDLK_F1) show_boxes ^= 1;
            if (k == SDLK_M) mastervol = mastervol > 0 ? 0 : SOUND_ON;
            if (options_open) {                     /* the menu swallows everything else */
                if (k == SDLK_RETURN) option_activate(&quit);
                continue;
            }
            if (phase != 'L') continue;
            if (k == SDLK_RETURN) key_ready = 1;
            if (k == SDLK_LEFTBRACKET && nai)  asel = (asel + nai - 1) % nai;
            if (k == SDLK_RIGHTBRACKET && nai) asel = (asel + 1) % nai;
            if (k == SDLK_T) { sendf("B %d %d\n", cur, asel); sfx(SN_BOT, 0.8f); }   /* B is a kick key */
            if (k == SDLK_BACKSPACE)
                for (int i = nl - 1; i >= 0; i--)
                    if (U[i].bot) { sendf("X %d\n", U[i].slot); break; }
            if (k == SDLK_DELETE)
                for (int i = 0; i < nl; i++)
                    if (U[i].bot) sendf("X %d\n", U[i].slot);
        }

        if (!pump()) { fprintf(stderr, "server closed the connection\n"); break; }

        /* newly pressed directions/buttons from the keyboard and every pad */
        int now = keymask() | padmask(), edge = now & ~prev_mask;
        prev_mask = now;
        int p_start = pad_edge(SDL_GAMEPAD_BUTTON_START), p_back = pad_edge(SDL_GAMEPAD_BUTTON_BACK);
        int p_lb = pad_edge(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), p_rb = pad_edge(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

        int menu_frame = options_open;              /* a frame that began in the menu never also acts on the select screen */
        if (options_open) {
            if (p_start) options_open = 0;
            else options_input(edge, !!(edge & IN_LK), &quit);
        } else if (phase == 'S' && p_start) {       /* Start opens the menu during a fight */
            options_open = 1;
            options_item = OPT_RESUME;
            menu_frame = 1;
        }

        if (phase == 'L' && !options_open && !menu_frame) {
            const Lob *mine = NULL;
            for (int i = 0; i < nl; i++)
                if (U[i].slot == me) mine = &U[i];
            int locked = mine && mine->ready;
            if (!locked) {                          /* a locked-in fighter can't move the cursor */
                int old = cur;
                if (edge & IN_L) cur = (cur + NCHARS - 1) % NCHARS;
                if (edge & IN_R) cur = (cur + 1) % NCHARS;
                if (edge & IN_U) cur = (cur + NCHARS - SEL_COLS) % NCHARS;
                if (edge & IN_D) cur = (cur + SEL_COLS) % NCHARS;
                if (cur != old) { sendf("J %d\n", cur); sfx(SN_MOVE, 0.6f); }
            }
            if ((edge & (IN_P | IN_K)) || key_ready || p_start) {
                if (!locked) sendf("J %d\n", cur);
                sendf("R\n");
                sfx(locked ? SN_SELECT : SN_READY, 0.9f);
            }
            if (p_back) { sendf("B %d %d\n", cur, asel); sfx(SN_BOT, 0.8f); }
            if (p_lb && nai) asel = (asel + nai - 1) % nai;
            if (p_rb && nai) asel = (asel + 1) % nai;
        }

        Uint64 t = SDL_GetTicksNS();
        acc += t - prev;
        prev = t;
        if (acc > 5 * dt) acc = 5 * dt;             /* don't spiral after a stall */
        for (; acc >= dt; acc -= dt) { client_tick(); anim_tick(); }

        music(phase == 'S' ? SN_MUSIC_FIGHT : SN_MUSIC_MENU);
        audio_pump();

        SDL_SetRenderDrawColor(ren, 20, 20, 30, 255);
        SDL_RenderClear(ren);
        if (phase == 'S') draw_match(); else draw_select(cur, asel);
        if (options_open) draw_options();
        SDL_RenderPresent(ren);
    }
    close(fd);
    SDL_Quit();
    return 0;
}
