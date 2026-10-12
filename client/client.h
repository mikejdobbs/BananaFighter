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

#include "sim.h" /* shared with the server */

/* ---- customize here, dwm style ---- */
#define SW 1280
#define SH 720
#define GROUND 580
#define SCALE ((float)SW / STAGE_W)
#define HIST 256 /* recent inputs kept for replay and motion commands */

/* sprite sheet layout (see make_sprites.py): 8 columns of SPR_CELL squares */
#define SPR_CELL 192 /* cell size in pixels */
#define SPR_CX 78    /* figure centre inside a cell (facing right) */
#define SPR_FIGH                                                               \
  171 /* figure height in the cell: scaled to the hitbox height */
#define SHOT_CELL 48 /* shots.bmp: one cell per character */
#define FX_CELL 96   /* fx.bmp: bomb, then three explosion frames */
#define SHIP_FW 192  /* ship.bmp: two frames of SHIP_FW x SHIP_FH */
#define SHIP_FH 96

enum {
  ROW_IDLE,
  ROW_WALK,
  ROW_JUMP,
  ROW_BLOCK,
  ROW_HURT,
  ROW_FLY,
  ROW_MOVE
};                /* 11 move rows follow */
#define IDLE_FR 6 /* frames in each looping row */
#define WALK_FR 8
#define FLY_FR 4
#define IDLE_TICKS 9    /* ticks per idle frame */
#define WALK_TICKS 3    /* ticks per walk frame */
#define WALK_HOLD 6     /* ticks without movement before the walk cycle stops */
#define LAND_TICKS 6    /* how long the landing crouch shows */
#define MIN_ACTIVE 6    /* show the attack's impact frames at least this long */
#define SHIP_DRAW_W 200 /* on-screen size of a ship */
#define SHIP_DRAW_H 100

/* fighter select screen */
#define SEL_COLS 3
#define CELL_W 150
#define CELL_H 170
#define CELL_GAP 14
#define ROSTER_COLS 5 /* player cards per row in the bottom roster */

/* audio: every sound is a mono 16-bit WAV at AUDIO_RATE (see make_sounds.py) */
#define AUDIO_RATE 22050
#define NVOX 24
#define MUSIC_VOL 0.30f
#define SOUND_ON 0.8f /* master volume when sound is on */

/* gamepads and joysticks */
#define MAXPADS 4
#define PAD_DEAD 12000 /* stick dead zone */
#define PAD_TRIG 16000 /* trigger pressed threshold */

/* Street Fighter II layout: directions on the left hand, punches U I O, kicks J
   K L. Second bindings keep arrows + Z X C / V B N available. */
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

/* Gamepad buttons (Xbox naming): top row X Y RB = light/medium/heavy punch,
   bottom row A B RT = light/medium/heavy kick. The d-pad and left stick move.
 */
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

/* Raw joysticks that SDL has no gamepad mapping for: hat or first two axes
   move, buttons 0-5 are LP MP HP LK MK HK. */
static const int joybuttons[6] = {IN_LP, IN_MP, IN_HP, IN_LK, IN_MK, IN_HK};

static const Uint8 pal[MAX_PLAYERS][3] = {
    {230, 70, 70},   {70, 140, 230}, {80, 200, 100}, {230, 200, 60},
    {180, 90, 220},  {240, 140, 50}, {70, 210, 210}, {230, 110, 170},
    {150, 150, 150}, {140, 90, 50},
};
/* ------------------------------------ */

typedef struct {
  int slot, ch, x, y, hp, lives, meter, face, st, invis;
} Fig;
typedef struct {
  int slot, ch, ready, bot;
} Lob;
typedef struct {
  int owner, x, y;
} Shot; /* shots and bombs */
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

static int fd = -1, me = -1, tick, winner, hasq, show_boxes, mycmd;
static int nf, nl, ns, nb, nsh, nex;
static int options_open, options_item; /* the Escape menu */
static char phase = 'L', rb[4096];
static int rl;
static Fig F[MAX_PLAYERS];
static Lob U[MAX_PLAYERS];
static Shot S[MAXPROJ], BM[MAXPROJ];
static Ship SHP[MAX_PLAYERS];
static Boom EX[MAXBOOM];
static Extra Q;
static Anim an[MAX_PLAYERS];   /* per-slot animation state */
static unsigned aclk;          /* animation clock, one per tick */
static char aiName[MAXAI][16]; /* AI plugin names announced by the server */
static int nai;
static SDL_Renderer *ren;
static SDL_Texture *tex[NCHARS]; /* NULL = fall back to coloured boxes */
static SDL_Texture *shots, *shipt, *fxt, *bg;

/* prediction state */
static Body pb; /* predicted state of my fighter */
static int predicting, pch;
static unsigned sent, ack; /* inputs sent / inputs the server has consumed */
static int hist[HIST];

static int num(char **s) { return (int)strtol(*s, s, 10); }

/* ================================================================== audio */
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
static Snd snd[SN_COUNT];
static Voice vox[NVOX]; /* voice 0 is the music */
static SDL_AudioStream *astream;
static float mastervol = SOUND_ON;
static int cur_music = -1;

#endif
