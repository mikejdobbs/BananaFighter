#ifndef CLIENT_H
#define CLIENT_H

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

#include "sim.h"

#define SW 1280
#define SH 720
#define GROUND 580
#define SCALE ((float)SW / STAGE_W)
#define HIST 256

#define SPR_CELL 192
#define SPR_CX 78
#define SPR_FIGH 171
#define SHOT_CELL 48
#define FX_CELL 96
#define SHIP_FW 192
#define SHIP_FH 96

enum {
  ROW_IDLE,
  ROW_WALK,
  ROW_JUMP,
  ROW_BLOCK,
  ROW_HURT,
  ROW_FLY,
  ROW_MOVE
};
#define IDLE_FR 6
#define WALK_FR 8
#define FLY_FR 4
#define IDLE_TICKS 9
#define WALK_TICKS 3
#define WALK_HOLD 6
#define LAND_TICKS 6
#define MIN_ACTIVE 6
#define SHIP_DRAW_W 200
#define SHIP_DRAW_H 100

#define SEL_COLS 3
#define CELL_W 150
#define CELL_H 170
#define CELL_GAP 14
#define ROSTER_COLS 5

#define AUDIO_RATE 22050
#define NVOX 24
#define MUSIC_VOL 0.30f
#define SOUND_ON 0.8f

#define MAXPADS 4
#define PAD_DEAD 12000
#define PAD_TRIG 16000

static const struct {
  SDL_Scancode sc;
  int bit;
} keys[] = {
    {SDL_SCANCODE_LEFT, IN_L},  {SDL_SCANCODE_A, IN_L},
    {SDL_SCANCODE_RIGHT, IN_R}, {SDL_SCANCODE_D, IN_R},
    {SDL_SCANCODE_UP, IN_U},    {SDL_SCANCODE_W, IN_U},
    {SDL_SCANCODE_DOWN, IN_D},  {SDL_SCANCODE_S, IN_D},
    {SDL_SCANCODE_U, IN_LP},    {SDL_SCANCODE_Z, IN_LP},
    {SDL_SCANCODE_I, IN_MP},    {SDL_SCANCODE_X, IN_MP},
    {SDL_SCANCODE_O, IN_HP},    {SDL_SCANCODE_C, IN_HP},
    {SDL_SCANCODE_J, IN_LK},    {SDL_SCANCODE_V, IN_LK},
    {SDL_SCANCODE_K, IN_MK},    {SDL_SCANCODE_B, IN_MK},
    {SDL_SCANCODE_L, IN_HK},    {SDL_SCANCODE_N, IN_HK},
};

static const struct {
  SDL_GamepadButton b;
  int bit;
} padkeys[] = {
    {SDL_GAMEPAD_BUTTON_WEST, IN_LP},
    {SDL_GAMEPAD_BUTTON_NORTH, IN_MP},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, IN_HP},
    {SDL_GAMEPAD_BUTTON_SOUTH, IN_LK},
    {SDL_GAMEPAD_BUTTON_EAST, IN_MK},
};

static const int joybuttons[6] = {IN_LP, IN_MP, IN_HP, IN_LK, IN_MK, IN_HK};

static const Uint8 pal[MAX_PLAYERS][3] = {
    {230, 70, 70},   {70, 140, 230}, {80, 200, 100}, {230, 200, 60},
    {180, 90, 220},  {240, 140, 50}, {70, 210, 210}, {230, 110, 170},
    {150, 150, 150}, {140, 90, 50},
};

typedef struct {
  int slot, ch, x, y, hp, lives, meter, face, st, invis;
} Fig;
typedef struct {
  int slot, ch, ready, bot;
} Lob;
typedef struct {
  int owner, x, y;
} Shot;
typedef struct {
  int owner, x, vx;
} Ship;
typedef struct {
  int x, y, t;
} Boom;
typedef struct {
  int vx, vy, frame, stun, grace, invis, hit, fuel;
} Extra;
typedef struct {
  int st, t, lx, ly, dy, still, walk, land;
} Anim;

enum {
  SN_SWING1,
  SN_SWING2,
  SN_SWING3,
  SN_HIT1,
  SN_HIT2,
  SN_HIT3,
  SN_BLOCK,
  SN_SHOT,
  SN_JUMP,
  SN_LAND,
  SN_FLY,
  SN_BOOM,
  SN_BOMB,
  SN_SHIP,
  SN_KO,
  SN_MOVE,
  SN_SELECT,
  SN_READY,
  SN_BOT,
  SN_MUSIC_MENU,
  SN_MUSIC_FIGHT,
  SN_COUNT
};

static const char *sndfile[SN_COUNT] = {
    "swing1",      "swing2",      "swing3",     "hit1",    "hit2",
    "hit3",        "block",       "shot",       "jump",    "land",
    "fly",         "boom",        "bomb",       "ship",    "ko",
    "menu_move",   "menu_select", "menu_ready", "bot_add", "music_menu",
    "music_fight",
};

typedef struct {
  Sint16 *pcm;
  Uint32 n;
} Snd;

typedef struct {
  int s, on, loop;
  Uint32 pos;
  float vol;
} Voice;

extern int fd, me, tick, winner, hasq, show_boxes, mycmd;
extern int nf, nl, ns, nb, nsh, nex;
extern int options_open, options_item;
extern char phase, rb[4096];
extern int rl;
extern Fig F[MAX_PLAYERS];
extern Lob U[MAX_PLAYERS];
extern Shot S[MAXPROJ], BM[MAXPROJ];
extern Ship SHP[MAX_PLAYERS];
extern Boom EX[MAXBOOM];
extern Extra Q;
extern Anim an[MAX_PLAYERS];
extern unsigned aclk;
extern char aiName[MAXAI][16];
extern int nai;
extern SDL_Renderer *ren;
extern SDL_Texture *tex[NCHARS];
extern SDL_Texture *shots, *shipt, *fxt, *bg;
extern Body pb;
extern int predicting, pch;
extern unsigned sent, ack;
extern int hist[HIST];
extern Snd snd[SN_COUNT];
extern Voice vox[NVOX];
extern SDL_AudioStream *astream;
extern float mastervol;
extern int cur_music;

int num(char **s);

void audio_init(void);
void audio_pump(void);
void music(int s);
void sfx(int s, float vol);
void swing_sound(const Move *m);

int padmask(void);
int keymask(void);
int pad_edge(SDL_GamepadButton b);
void pads_init(void);
void option_activate(int *quit);
void options_input(int edge, int confirm, int *quit);

int dial(const char *host, const char *port);
int pump(void);
void sendf(const char *fmt, ...);
void parse(char *s);
void client_tick(void);

int look(int x);
int detect_cmd(int *w, int n, int face);
void body_step(void *b, void *c, int mask, int lk, int cmd);
void window(unsigned i, int *w);

#endif
