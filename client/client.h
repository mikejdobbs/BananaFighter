/* client.h - shared constants, types and state for the client's source files:
   audio.c, input.c, network.c, render.c and main.c */
#ifndef CLIENT_H
#define CLIENT_H

#include <SDL3/SDL.h>
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
enum {
  ROW_IDLE,
  ROW_WALK,
  ROW_JUMP,
  ROW_BLOCK,
  ROW_HURT,
  ROW_FLY,
  ROW_CROUCH,
  ROW_MOVE
};                /* 11 move rows follow; then crouch block/attacks and jump extras */
#define ROW_SHIP 29 /* called-ship frames (2) at the bottom of each fighter sheet */
#define IDLE_FR 6 /* frames in each looping row */
#define WALK_FR 8
#define FLY_FR 4
#define SHIP_FR 2
#define IDLE_TICKS 9    /* ticks per idle frame */
#define WALK_TICKS 3    /* ticks per walk frame */
#define WALK_HOLD 6     /* ticks without movement before the walk cycle stops */
#define LAND_TICKS 6    /* how long the landing crouch shows */
#define MIN_ACTIVE 6    /* show the attack's impact frames at least this long */
#define SHIP_DRAW_W 200 /* on-screen size of a ship */
#define SHIP_DRAW_H 100

/* fighter select screen */
#define SEL_COLS 3
#define SEL_VISIBLE_ROWS 2 /* rows shown at once; the grid scrolls when there are more */
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
/* key and gamepad bindings are in input.c, player colours in render.c */
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

enum { OPT_RESUME, OPT_SOUND, OPT_BOXES, OPT_QUIT, OPT_COUNT };

/* game state, filled in by network.c from the server's snapshots */
extern int networkSocketFd, me, tick, winner, hasq, mycmd;
extern int nf, nl, ns, nb, nsh, nex, nai;
extern char phase;
extern Fig F[MAX_PLAYERS];
extern Lob U[MAX_PLAYERS];
extern Shot S[MAXPROJ], BM[MAXPROJ];
extern Ship SHP[MAX_PLAYERS];
extern Boom EX[MAXBOOM];
extern Extra Q;
extern char aiName[MAXAI][16]; /* AI plugin names announced by the server */

/* prediction state (network.c) */
extern Body pb; /* predicted state of my fighter */
extern int predicting, pch;
extern unsigned sent, ack; /* inputs sent / inputs the server has consumed */

/* render.c */
extern int show_boxes, options_open, options_item; /* the Escape menu */
extern unsigned aclk;                              /* animation clock, one per tick */
extern SDL_Renderer *renderer;
void load_sprites(void);
void anim_tick(void);
void draw_match(void);
void draw_select(int cursorIndex, int aiSelectionIndex);
void draw_options(void);
void options_input(int inputEdge, int confirm, int *shouldQuit);
void option_activate(int *shouldQuit);

/* audio.c */
extern float mastervol;
void audio_init(void);
void audio_pump(void);
void audio_snapshot(void); /* sounds that come from a new authoritative snapshot */
void audio_reset(void);    /* a new phase: don't compare against the old snapshot */
void sfx(int s, float vol);
void music(int s);
void swing_sound(const Move *m);

/* input.c */
void pads_init(void);
void pad_open(SDL_JoystickID id);
void pad_close(SDL_JoystickID id);
void joy_open(SDL_JoystickID id);
void joy_close(SDL_JoystickID id);
int padmask(void);
int keymask(void);
int pad_edge(SDL_GamepadButton b);

/* network.c */
int dial(const char *host, const char *port);
int pump(void); /* 0 = server closed the connection */
void sendf(const char *fmt, ...);
void client_tick(void);

#endif
