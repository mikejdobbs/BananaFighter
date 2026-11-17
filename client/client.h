/* client.h - shared constants, types and state for the client's source files:
   audio.c, input.c, network.c, render.c and main.c */
#ifndef CLIENT_H
#define CLIENT_H

/* Feature-test macros must precede SDL / libc headers so network.c sees
   getaddrinfo, struct timeval, etc. on glibc. */
#ifndef _WIN32
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE 1
#  endif
#endif

#include <SDL3/SDL.h>
#include "platform.h"
#include "sim.h" /* shared with the server */
#include "settings.h"

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
#define FX_CELL 96   /* fx.png: bomb, then three explosion frames */
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
#define ROW_SHIP 30 /* called-ship frames (2) near the bottom of each fighter sheet */
#define ROW_SHOT 31 /* projectile icon (1 cell) on each fighter sheet */
#define IDLE_FR 6 /* frames in each looping row */
#define WALK_FR 8
#define FLY_FR 4
#define SHIP_FR 2
#define IDLE_TICKS 9    /* ticks per idle frame */
#define WALK_TICKS 3    /* ticks per walk frame */
#define WALK_HOLD 6     /* ticks without movement before the walk cycle stops */
#define LAND_TICKS 12   /* how long the landing crouch / recover shows */
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
  int kind;  /* BOOM_NORMAL or BOOM_BARF */
  int owner; /* slot; barf spray originates here */
} Boom;
typedef struct {
  int on, life, grounded; /* grounded = puddle smeared on the floor */
  float x, y, vx, vy;
  float r;           /* blob / puddle size in screen px */
  Uint8 cr, cg, cb;  /* green vomit color */
} Gib;
typedef struct {
  int vx, vy, frame, stun, grace, invis, hit, fuel;
} Extra;
typedef struct {
  int st, t, lx, ly, dy, still, walk, land, kd;
} Anim; /* kd: ticks since a sweep knockdown started, or -1 */

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
  SN_LAUGH,
  SN_MOVE,
  SN_SELECT,
  SN_READY,
  SN_BOT,
  SN_FIGHT,
  SN_MUSIC_MENU,
  SN_MUSIC_FIGHT_STADIUM,
  SN_MUSIC_FIGHT_FIRST, /* chars[0] home stage; +1..NCHARS-1 follow roster order */
  SN_MUSIC_FIGHT_LAST = SN_MUSIC_FIGHT_FIRST + NCHARS - 1,
  SN_COUNT
};

enum {
  OPT_RESUME,         /* Resume (pause) or Back (settings/controls) */
  OPT_OPEN_SETTINGS,  /* Pause root only: enter settings */
  OPT_OPEN_CONTROLS,  /* Settings only: enter key binds */
  OPT_MUSIC,
  OPT_SFX,
  OPT_FULLSCREEN,
  OPT_BOXES,
  OPT_BIND0, /* BIND_COUNT rows: OPT_BIND0 .. OPT_BIND0+BIND_COUNT-1 */
  OPT_RESET = OPT_BIND0 + BIND_COUNT,
  OPT_QUIT,
  OPT_COUNT
};

/* options_submenu levels */
enum { OPTMENU_PAUSE = 0, OPTMENU_SETTINGS = 1, OPTMENU_CONTROLS = 2 };

/* Pre-match UI: title → connect (optional) → play (lobby/match). */
enum { SCR_TITLE, SCR_CONNECT, SCR_PLAY };
enum {
  TITLE_SINGLE,
  TITLE_HOST,
  TITLE_CONNECT,
  TITLE_OPTIONS,
  TITLE_QUIT,
  TITLE_COUNT
};

/* game state, filled in by network.c from the server's snapshots */
extern bf_socket_t networkSocketFd;
extern int me, tick, winner, hasq, mycmd;
extern int nf, nl, ns, nb, nsh, nex, nai;
extern char phase;
extern Fig F[MAX_PLAYERS];
extern Lob U[MAX_PLAYERS];
extern Shot S[MAXPROJ], BM[MAXPROJ];
extern Ship SHP[MAX_PLAYERS];
extern Boom EX[MAXBOOM];
extern Gib GIB[MAXGIB];
extern Extra Q;
void spawn_barf(const Boom *b);
void barf_fx_reset(void); /* clear gib puddles + spawn dedup (phase change) */
void gib_tick(void);
extern char aiName[MAXAI][16]; /* AI plugin names announced by the server */

/* prediction state (network.c) */
extern Body pb; /* predicted state of my fighter */
extern int predicting, pch;
extern unsigned sent, ack; /* inputs sent / inputs the server has consumed */

/* render.c */
extern int show_boxes, options_open, options_item; /* the Escape menu */
extern int options_rebinding; /* BIND_* while waiting for a key, else -1 */
extern int options_scroll;    /* first visible options row */
extern int options_in_play;   /* 1 when opened from a match/lobby */
extern int options_submenu;   /* OPTMENU_PAUSE / SETTINGS / CONTROLS */
extern unsigned aclk;         /* animation clock, one per tick */
extern SDL_Renderer *renderer;
void load_sprites(void);
void anim_tick(void);
void draw_match(void);
void draw_select(int cursorIndex, int aiSelectionIndex);
int match_stage_for_music(void); /* -1 stadium, else 0..NCHARS-1 once match stage latched */
void draw_title(int item);
void draw_connect(int list_sel, const char *address, const char *status);
void draw_options(void);
void options_input(int inputEdge, int confirm, int *leaveTitle);
void option_activate(int *leaveTitle);
void options_back(void); /* Esc/Start: leave controls/settings or close menu */
int options_item_hidden(int item); /* 1 if skipped on this platform */
void options_ensure_scroll(void);
int options_hit_test(float x, float y); /* row index or -1 */

/* audio.c */
extern float mastervol; /* 0 = muted (M key); else 1 */
void audio_init(void);
void audio_pump(void);
void audio_set_suspended(int suspended); /* pause device while backgrounded */
void audio_snapshot(void); /* sounds that come from a new authoritative snapshot */
void audio_reset(void);    /* a new phase: don't compare against the old snapshot */
void sfx(int s, float vol);
void music(int s);
void music_refresh_vol(void); /* re-apply music voice volume from settings */
int music_for_stage(int stage); /* -1 stadium, else 0..NCHARS-1 → SN_MUSIC_FIGHT_* */
void swing_sound(const Move *m);
void fight_intro_play(void);     /* start match announcer; gates fight_intro_blocking */
int  fight_intro_blocking(void); /* 1 until that announcer voice finishes */

/* input.c */
void pads_init(void);
void pad_open(SDL_JoystickID id);
void pad_close(SDL_JoystickID id);
void joy_open(SDL_JoystickID id);
void joy_close(SDL_JoystickID id);
int pads_connected(void); /* gamepads + raw joysticks currently open */
int padmask(void);
int keymask(void);
int touchmask(void); /* on-screen virtual pad bits */
int pad_edge(SDL_GamepadButton b);
int input_rebind_key(SDL_Scancode sc); /* apply during rebind; 1 if consumed */
/* Finger / mouse events already converted to render (logical) coords. */
void touch_handle_event(const SDL_Event *e, int screen);
void touch_clear(void);
int touch_ui_confirm(void);  /* edge: tap confirmed a menu action */
int touch_ui_back(void);     /* edge: tap back / close */
int touch_ui_options(void);  /* edge: open/close options */
int touch_title_item(void);  /* >=0 when a title row was just tapped */
int touch_connect_sel(void); /* >=0 when a LAN row was just tapped */
int touch_options_item(void);/* >=0 when an options row was just tapped */
int touch_want_overlay(void); /* draw virtual pad (no physical controller) */
void draw_touch_controls(int screen);

/* network.c */
int network_init(void);
void network_set_session_password(const char *password);
void network_disconnect(void); /* close sockets and clear lobby/match state */
bf_socket_t dial(const char *host, const char *port);
int network_handshake(void); /* after dial; 0 on failure */
const char *network_handshake_error(void); /* detail after handshake failure */
int pump(void); /* 0 = server closed the connection */
void sendf(const char *fmt, ...);
void client_tick(void);

#endif
