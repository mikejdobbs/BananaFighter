/* config.h - edit and recompile, dwm style */
#ifndef CONFIG_H
#define CONFIG_H

#define PORT          7777
#define MAX_UDP_PACKETS_PER_CALL 256 /* Maximum number of UDP packets to process per call. */
#define TICK_HZ       60
#define MAX_UDP_GAP 50  /* max possible gap between UDP framtes */
#define MAX_PLAYERS          10        /* max connected players (humans + bots) */
#define MIN_PLAYERS   2         /* needed to start a match */
#define STAGE_W       1400
#define GRAVITY       2
#define JUMP_NUM      3         /* jump velocity = character jump * JUMP_NUM / JUMP_DEN */
#define JUMP_DEN      2
#define HITSTUN       12
#define THROW_STUN    30        /* ticks a thrown victim is helpless */
#define THROW_LIFT    50        /* upward launch speed of a throw */
#define THROW_KB      12        /* default horizontal knockback for throws */
#define KNOCKDOWN_STUN 78       /* crouch HK sweep: fall, lie, knees, then feet */
#define GETUP_TICKS   42        /* last ticks: on the knees, then up onto the feet */
#define THROW_RANGE   200        /* close enough to grab: heavy punch becomes a throw, beating block */
#define BLOCK_PCT     10        /* % of damage taken while blocking */
#define METER_MAX     100       /* super meter: +dmg/4 dealt, +dmg/8 taken */
#define RESPAWN_GRACE 90        /* invulnerable ticks after losing a life */
#define ROUND_WAIT    240       /* ticks of "game over" before the select screen */
#define PROJ_HALF     10
#define MAXPROJ       48        /* shots and bombs in flight */
#define MAXMOVES      25
#define MAXAI         8         /* max AI plugins listed on the select screen */
#define INPUT_QMAX    16        /* server-side per-player input queue */
#define INPUT_QLAG    3         /* inputs queued beyond this are dropped to bound latency */

/* tail flight: hold jump in the air to hover and climb, burning fuel; fuel refills on the ground */
#define FLY_VMAX      7         /* top climb speed while flying */
#define FLY_CEIL      460       /* flight ceiling */
#define FUEL_REGEN    4         /* fuel regained per tick on the ground */

/* called ships and their bombs */
#define SHIP_Y        520       /* ship altitude */
#define SHIP_SPEED    10        /* units per tick */
#define SHIP_DROP     18        /* ticks between bombs */
#define SHIP_MARGIN   100       /* how far off-stage a ship starts */
#define BOMB_GRAV     1
#define BOMB_R        90        /* explosion radius */
#define BOOM_TICKS    14        /* how long the explosion effect lasts */
#define MAXBOOM       16

/* The client sends raw directions and six attack buttons.
   Super moves use a command sequence plus a final button, not a dedicated super button. */
enum { IN_L = 1, IN_R = 2, IN_U = 4, IN_D = 8,
       IN_LP = 16, IN_MP = 32, IN_HP = 64,
       IN_LK = 128, IN_MK = 256, IN_HK = 512 };
#define IN_P (IN_LP | IN_MP | IN_HP)
#define IN_K (IN_LK | IN_MK | IN_HK)

/* cmd uses the last direction-input sequence; move input is the final punch/kick button.
   CMD_NONE means a normal. CMD_QCF = down, down-forward, forward. CMD_HCF = back, down-back,
   down, down-forward, forward. Commands also require the move's meter cost. */
enum { CMD_NONE, CMD_QCF, CMD_HCF };

/* Optional animation override for a move (sprite row); A_AUTO picks one from the button/command. */
enum { A_AUTO, A_LP, A_MP, A_HP, A_LK, A_MK, A_HK, A_CAST, A_SPECIAL, A_SUPER };

/* proj_speed != 0: spawns a projectile; otherwise a melee hitbox.
   air:    a jump attack. It can only be used while off the ground, and ground normals can't be.
           Its hitbox y is measured from the fighter's feet and may be negative (hits downward).
   spread: fires this many projectiles in a fan; spread_vy is the vertical speed step between them.
   invis:  ticks of invisibility granted (any other move ends it).
   throw:  grabs the closest victim in range (ignores blocking), damages and launches them.
   cost:   super meter needed (and spent). Non-throw melee hits everyone in range.
   ship:   calls a ship that drops this many bombs; dmg and kb apply per bomb.
   boom:   on the active frame, area damage at the attacker (like a bomb; owner immune).
   spin:   the projectile's sprite spins in flight (client only).
   leap:   forward speed during the leap; leap_vy is the initial rise (0 = ~70% of jump). */
typedef struct {
    const char *name;
    int input, cmd, air, crouch;
    int startup, active, recover;
    int dmg, kb;
    int reach, y, h;
    int proj_speed, proj_life;
    int spread, spread_vy;
    int invis, throw, cost;
    int ship, boom, spin, anim;
    int leap, leap_vy;
} Move;

/* fly: tail thrust per tick (0 = cannot fly); fuel: ticks of flight on a full tank. */
typedef struct {
    const char *name;
    int hp, lives, speed, jump, w, h;
    int fly, fuel;
    Move moves[MAXMOVES];           /* list ends at the first empty name */
} Character;

/* Edit these tables, then rebuild: DWM-style game configuration. */
static const Character chars[] = {
    { .name = "Ken", .hp = 1000, .lives = 3, .speed = 6, .jump = 28, .w = 62, .h = 158, .moves = {
        { .name="grab",      .input=IN_HP, .startup=5,  .active=3, .recover=20, .dmg=70,  .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=25, .kb=5, .reach=43, .y=91, .h=24 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=48, .kb=10, .reach=57, .y=89, .h=30 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=82, .kb=18, .reach=67, .y=85, .h=38 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=28, .kb=6, .reach=50, .y=18, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=55, .kb=12, .reach=72, .y=10, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=88, .kb=20, .reach=87, .y=24, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=25, .kb=5, .reach=43, .y=45, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=48, .kb=10, .reach=57, .y=44, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=82, .kb=18, .reach=67, .y=42, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=28, .kb=6, .reach=50, .y=9, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=55, .kb=12, .reach=72, .y=5, .h=40 },
        { .name="crouch roundhouse", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=88, .kb=20, .reach=87, .y=12, .h=54 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=36, .kb=7, .reach=48, .y=20, .h=49 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=48, .kb=9, .reach=48, .y=20, .h=49 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=62, .kb=12, .reach=52, .y=20, .h=49 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=46, .kb=10, .reach=56, .y=-44, .h=74 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=62, .kb=12, .reach=56, .y=-44, .h=74 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=81, .kb=16, .reach=60, .y=-44, .h=74 },
        { .name="fireball",  .input=IN_P, .cmd=CMD_QCF, .startup=10,.active=1, .recover=22, .dmg=62, .kb=8, .y=99, .h=20, .proj_speed=12, .proj_life=70 },
        { .name="super punch", .input=IN_HP,.cmd=CMD_HCF, .startup=5, .active=5, .recover=24, .dmg=130,.kb=30,.reach=50,.y=41,.h=123,.cost=50 },
    }},
    { .name = "Tank", .hp = 1400, .lives = 3, .speed = 3, .jump = 22, .w = 74, .h = 172, .moves = {
        { .name="grab",      .input=IN_HP, .startup=5,  .active=3, .recover=20, .dmg=70,  .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="low jab", .input=IN_LP, .startup=4, .active=2, .recover=7, .dmg=32, .kb=6, .reach=37, .y=77, .h=23 },
        { .name="hammer", .input=IN_MP, .startup=7, .active=3, .recover=13, .dmg=65, .kb=14, .reach=51, .y=81, .h=41 },
        { .name="slam", .input=IN_HP, .startup=11, .active=4, .recover=22, .dmg=118, .kb=24, .reach=58, .y=91, .h=56 },
        { .name="toe", .input=IN_LK, .startup=5, .active=2, .recover=8, .dmg=35, .kb=7, .reach=41, .y=5, .h=25 },
        { .name="stomp", .input=IN_MK, .startup=8, .active=3, .recover=16, .dmg=78, .kb=14, .reach=49, .y=0, .h=36 },
        { .name="boot", .input=IN_HK, .startup=12, .active=4, .recover=22, .dmg=112, .kb=24, .reach=64, .y=14, .h=50 },
        { .name="crouch low jab", .input=IN_LP, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=32, .kb=6, .reach=37, .y=38, .h=23 },
        { .name="crouch hammer", .input=IN_MP, .crouch=1, .startup=7, .active=3, .recover=13, .dmg=65, .kb=14, .reach=51, .y=40, .h=41 },
        { .name="crouch slam", .input=IN_HP, .crouch=1, .startup=11, .active=4, .recover=24, .dmg=118, .kb=24, .reach=58, .y=45, .h=56 },
        { .name="crouch toe", .input=IN_LK, .crouch=1, .startup=5, .active=2, .recover=8, .dmg=35, .kb=7, .reach=41, .y=2, .h=25 },
        { .name="crouch stomp", .input=IN_MK, .crouch=1, .startup=8, .active=3, .recover=16, .dmg=78, .kb=14, .reach=49, .y=0, .h=36 },
        { .name="crouch boot", .input=IN_HK, .crouch=1, .startup=12, .active=4, .recover=24, .dmg=112, .kb=24, .reach=64, .y=7, .h=50 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=4, .active=14, .recover=8, .dmg=52, .kb=10, .reach=43, .y=27, .h=54 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=4, .active=14, .recover=8, .dmg=70, .kb=12, .reach=43, .y=27, .h=54 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=5, .active=14, .recover=9, .dmg=91, .kb=16, .reach=47, .y=27, .h=54 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=5, .active=14, .recover=8, .dmg=64, .kb=11, .reach=48, .y=-50, .h=77 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=5, .active=14, .recover=8, .dmg=85, .kb=14, .reach=48, .y=-50, .h=77 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=6, .active=14, .recover=9, .dmg=110, .kb=18, .reach=52, .y=-50, .h=77 },
        { .name="boulder",   .input=IN_P, .cmd=CMD_QCF, .startup=18,.active=1,.recover=30, .dmg=120,.kb=12,.y=109,.h=36,.proj_speed=6,.proj_life=120,.spin=1 },
        { .name="earthquake",.input=IN_HK,.cmd=CMD_HCF,.startup=22,.active=5,.recover=38, .dmg=280,.kb=125,.reach=140,.y=0,.h=91,.cost=75 },
    }},
    { .name = "Ninja", .hp = 800, .lives = 3, .speed = 8, .jump = 32, .w = 56, .h = 150, .moves = {
        { .name="grab",      .input=IN_HP, .startup=5,  .active=3, .recover=20, .dmg=70,  .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="quick hand", .input=IN_LP, .startup=2, .active=2, .recover=5, .dmg=20, .kb=4, .reach=47, .y=75, .h=22 },
        { .name="palm", .input=IN_MP, .startup=4, .active=3, .recover=8, .dmg=38, .kb=8, .reach=58, .y=75, .h=30 },
        { .name="blade hand", .input=IN_HP, .startup=6, .active=3, .recover=12, .dmg=62, .kb=14, .reach=73, .y=70, .h=40 },
        { .name="ankle", .input=IN_LK, .startup=3, .active=2, .recover=6, .dmg=22, .kb=5, .reach=56, .y=8, .h=25 },
        { .name="sweep", .input=IN_MK, .startup=6, .active=3, .recover=10, .dmg=45, .kb=10, .reach=76, .y=0, .h=30 },
        { .name="flying kick", .input=IN_HK, .startup=4, .active=8, .recover=16, .dmg=70, .kb=16, .reach=68, .y=22, .h=58, .leap=15 },
        { .name="crouch quick hand", .input=IN_LP, .crouch=1, .startup=2, .active=2, .recover=5, .dmg=20, .kb=4, .reach=47, .y=37, .h=22 },
        { .name="crouch palm", .input=IN_MP, .crouch=1, .startup=4, .active=3, .recover=8, .dmg=38, .kb=8, .reach=58, .y=37, .h=30 },
        { .name="crouch blade hand", .input=IN_HP, .crouch=1, .startup=6, .active=3, .recover=14, .dmg=62, .kb=14, .reach=73, .y=35, .h=40 },
        { .name="crouch ankle", .input=IN_LK, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=22, .kb=5, .reach=56, .y=4, .h=25 },
        { .name="crouch sweep", .input=IN_MK, .crouch=1, .startup=6, .active=3, .recover=10, .dmg=45, .kb=10, .reach=76, .y=0, .h=30 },
        { .name="crouch flying kick", .input=IN_HK, .crouch=1, .startup=5, .active=7, .recover=16, .dmg=70, .kb=16, .reach=78, .y=17, .h=55, .leap=12 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=2, .active=14, .recover=5, .dmg=28, .kb=6, .reach=54, .y=15, .h=45 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=2, .active=14, .recover=5, .dmg=38, .kb=8, .reach=54, .y=15, .h=45 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=3, .active=14, .recover=6, .dmg=49, .kb=10, .reach=58, .y=15, .h=45 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=3, .active=14, .recover=5, .dmg=38, .kb=9, .reach=63, .y=-40, .h=70 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=3, .active=14, .recover=5, .dmg=50, .kb=11, .reach=63, .y=-40, .h=70 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=4, .active=14, .recover=6, .dmg=65, .kb=14, .reach=67, .y=-40, .h=70 },
        { .name="star",      .input=IN_P,.cmd=CMD_QCF,.startup=5,.active=1,.recover=10,.dmg=28,.kb=5,.y=90,.h=10,.proj_speed=17,.proj_life=60,.spin=1 },
        { .name="vanish",    .input=IN_K,.cmd=CMD_QCF,.startup=4,.active=1,.recover=20,.invis=300,.cost=30 },
        { .name="shadow storm",.input=IN_HP,.cmd=CMD_HCF,.startup=12,.active=4,.recover=30,.dmg=190,.kb=55,.reach=146,.y=20,.h=120,.cost=65 },
    }},
    { .name = "Monkey", .hp = 1800, .lives = 5, .speed = 2, .jump = 16, .w = 74, .h = 165, .moves = {
        { .name="grab",      .input=IN_HP, .startup=5,  .active=3, .recover=20, .dmg=70,  .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="swat", .input=IN_LP, .startup=6, .active=2, .recover=10, .dmg=40, .kb=8, .reach=41, .y=70, .h=29 },
        { .name="palm", .input=IN_MP, .startup=9, .active=3, .recover=16, .dmg=70, .kb=15, .reach=50, .y=68, .h=47 },
        { .name="pound", .input=IN_HP, .startup=13, .active=4, .recover=24, .dmg=108, .kb=24, .reach=58, .y=58, .h=63 },
        { .name="toe", .input=IN_LK, .startup=6, .active=2, .recover=9, .dmg=42, .kb=8, .reach=41, .y=5, .h=29 },
        { .name="kick", .input=IN_MK, .startup=9, .active=3, .recover=16, .dmg=72, .kb=15, .reach=52, .y=12, .h=44 },
        { .name="ape kick", .input=IN_HK, .startup=13, .active=4, .recover=24, .dmg=115, .kb=26, .reach=65, .y=17, .h=60 },
        { .name="crouch swat", .input=IN_LP, .crouch=1, .startup=6, .active=2, .recover=10, .dmg=40, .kb=8, .reach=41, .y=35, .h=29 },
        { .name="crouch palm", .input=IN_MP, .crouch=1, .startup=9, .active=3, .recover=16, .dmg=70, .kb=15, .reach=50, .y=34, .h=47 },
        { .name="crouch pound", .input=IN_HP, .crouch=1, .startup=13, .active=4, .recover=26, .dmg=108, .kb=24, .reach=58, .y=29, .h=63 },
        { .name="crouch toe", .input=IN_LK, .crouch=1, .startup=6, .active=2, .recover=9, .dmg=42, .kb=8, .reach=41, .y=2, .h=29 },
        { .name="crouch kick", .input=IN_MK, .crouch=1, .startup=9, .active=3, .recover=16, .dmg=72, .kb=15, .reach=52, .y=6, .h=44 },
        { .name="crouch ape kick", .input=IN_HK, .crouch=1, .startup=13, .active=4, .recover=26, .dmg=115, .kb=26, .reach=65, .y=8, .h=60 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=5, .active=14, .recover=8, .dmg=60, .kb=11, .reach=41, .y=24, .h=53 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=5, .active=14, .recover=8, .dmg=80, .kb=14, .reach=41, .y=24, .h=53 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=6, .active=14, .recover=9, .dmg=104, .kb=18, .reach=45, .y=24, .h=53 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=6, .active=14, .recover=8, .dmg=71, .kb=13, .reach=47, .y=-49, .h=78 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=6, .active=14, .recover=8, .dmg=95, .kb=16, .reach=47, .y=-49, .h=78 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=7, .active=14, .recover=9, .dmg=124, .kb=21, .reach=51, .y=-49, .h=78 },
        { .name="command grab",.input=IN_HP,.cmd=CMD_QCF,.startup=9,.active=3,.recover=30,.dmg=155,.kb=84,.reach=41,.y=19,.h=102,.throw=1 },
        { .name="smash",     .input=IN_HK,.cmd=CMD_HCF,.startup=25,.active=4,.recover=40,.dmg=300,.kb=140,.reach=115,.y=0,.h=155,.cost=METER_MAX },
    }},
    { .name = "Alien", .hp = 700, .lives = 3, .speed = 9, .jump = 34, .w = 54, .h = 138, .moves = {
        { .name="grab",      .input=IN_HP, .startup=5,  .active=3, .recover=20, .dmg=70,  .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="probe", .input=IN_LP, .startup=2, .active=2, .recover=5, .dmg=16, .kb=3, .reach=42, .y=72, .h=28 },
        { .name="ray jab", .input=IN_MP, .startup=3, .active=2, .recover=7, .dmg=25, .kb=5, .reach=51, .y=69, .h=38 },
        { .name="blaster", .input=IN_HP, .startup=5, .active=3, .recover=10, .dmg=42, .kb=9, .reach=63, .y=60, .h=48 },
        { .name="low beam", .input=IN_LK, .startup=3, .active=2, .recover=6, .dmg=18, .kb=4, .reach=45, .y=14, .h=31 },
        { .name="trip", .input=IN_MK, .startup=5, .active=3, .recover=9, .dmg=32, .kb=7, .reach=57, .y=3, .h=38 },
        { .name="hover kick", .input=IN_HK, .startup=7, .active=3, .recover=12, .dmg=52, .kb=12, .reach=75, .y=35, .h=60 },
        { .name="crouch probe", .input=IN_LP, .crouch=1, .startup=2, .active=2, .recover=5, .dmg=16, .kb=3, .reach=42, .y=36, .h=28 },
        { .name="crouch ray jab", .input=IN_MP, .crouch=1, .startup=3, .active=2, .recover=7, .dmg=25, .kb=5, .reach=51, .y=34, .h=38 },
        { .name="crouch blaster", .input=IN_HP, .crouch=1, .startup=5, .active=3, .recover=12, .dmg=42, .kb=9, .reach=63, .y=30, .h=48 },
        { .name="crouch low beam", .input=IN_LK, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=18, .kb=4, .reach=45, .y=7, .h=31 },
        { .name="crouch trip", .input=IN_MK, .crouch=1, .startup=5, .active=3, .recover=9, .dmg=32, .kb=7, .reach=57, .y=1, .h=38 },
        { .name="crouch hover kick", .input=IN_HK, .crouch=1, .startup=7, .active=3, .recover=14, .dmg=52, .kb=12, .reach=75, .y=17, .h=60 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=2, .active=14, .recover=5, .dmg=18, .kb=4, .reach=45, .y=17, .h=60 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=2, .active=14, .recover=5, .dmg=24, .kb=5, .reach=45, .y=17, .h=60 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=3, .active=14, .recover=6, .dmg=31, .kb=6, .reach=49, .y=17, .h=60 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=3, .active=14, .recover=5, .dmg=24, .kb=6, .reach=54, .y=-48, .h=83 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=3, .active=14, .recover=5, .dmg=32, .kb=7, .reach=54, .y=-48, .h=83 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=4, .active=14, .recover=6, .dmg=42, .kb=9, .reach=58, .y=-48, .h=83 },
        { .name="laser",     .input=IN_P,.cmd=CMD_QCF,.startup=5,.active=1,.recover=9,.dmg=24,.kb=4,.y=78,.h=17,.proj_speed=28,.proj_life=50 },
        { .name="mothership",.input=IN_HP,.cmd=CMD_HCF,.startup=18,.active=1,.recover=40,.dmg=70,.kb=60,.ship=6,.cost=40 },
    }},
    { .name = "Squirrel", .hp = 750, .lives = 3, .speed = 8, .jump = 30, .w = 58, .h = 142, .fly = 3, .fuel = 110, .moves = {
        { .name="grab",      .input=IN_HP, .startup=5,  .active=3, .recover=20, .dmg=70,  .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="paw", .input=IN_LP, .startup=2, .active=2, .recover=5, .dmg=18, .kb=4, .reach=37, .y=50, .h=28 },
        { .name="claw", .input=IN_MP, .startup=3, .active=3, .recover=7, .dmg=30, .kb=6, .reach=50, .y=47, .h=44 },
        { .name="tail slap", .input=IN_HP, .startup=6, .active=3, .recover=11, .dmg=52, .kb=12, .reach=74, .y=44, .h=54 },
        { .name="toe", .input=IN_LK, .startup=3, .active=2, .recover=6, .dmg=20, .kb=4, .reach=45, .y=8, .h=28 },
        { .name="sweep", .input=IN_MK, .startup=5, .active=3, .recover=9, .dmg=36, .kb=8, .reach=61, .y=0, .h=38 },
        { .name="tail kick", .input=IN_HK, .startup=7, .active=3, .recover=12, .dmg=58, .kb=13, .reach=79, .y=28, .h=60 },
        { .name="crouch paw", .input=IN_LP, .crouch=1, .startup=2, .active=2, .recover=5, .dmg=18, .kb=4, .reach=37, .y=25, .h=28 },
        { .name="crouch claw", .input=IN_MP, .crouch=1, .startup=3, .active=3, .recover=7, .dmg=30, .kb=6, .reach=50, .y=23, .h=44 },
        { .name="crouch tail slap", .input=IN_HP, .crouch=1, .startup=6, .active=3, .recover=13, .dmg=52, .kb=12, .reach=74, .y=22, .h=54 },
        { .name="crouch toe", .input=IN_LK, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=20, .kb=4, .reach=45, .y=4, .h=28 },
        { .name="crouch sweep", .input=IN_MK, .crouch=1, .startup=5, .active=3, .recover=9, .dmg=36, .kb=8, .reach=61, .y=0, .h=38 },
        { .name="crouch tail kick", .input=IN_HK, .crouch=1, .startup=7, .active=3, .recover=14, .dmg=58, .kb=13, .reach=79, .y=14, .h=60 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=2, .active=14, .recover=5, .dmg=20, .kb=4, .reach=42, .y=19, .h=55 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=2, .active=14, .recover=5, .dmg=26, .kb=5, .reach=42, .y=19, .h=55 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=3, .active=14, .recover=6, .dmg=34, .kb=6, .reach=46, .y=19, .h=55 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=3, .active=14, .recover=5, .dmg=26, .kb=6, .reach=50, .y=-39, .h=71 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=3, .active=14, .recover=5, .dmg=34, .kb=7, .reach=50, .y=-39, .h=71 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=4, .active=14, .recover=6, .dmg=44, .kb=9, .reach=54, .y=-39, .h=71 },
        { .name="nut",       .input=IN_P,.cmd=CMD_QCF,.startup=6,.active=1,.recover=11,.dmg=30,.kb=8,.y=71,.h=19,.proj_speed=15,.proj_life=70,.spin=1 },
        { .name="nut spray", .input=IN_HP,.cmd=CMD_HCF,.startup=10,.active=1,.recover=22,.dmg=22,.kb=6,.y=71,.h=19,.proj_speed=13,.proj_life=60,.spread=3,.spread_vy=4,.spin=1,.cost=35,.anim=A_SUPER },
    }},
    { .name = "Rump", .hp = 900, .lives = 3, .speed = 5, .jump = 26, .w = 64, .h = 160, .moves = {
        { .name="grab",      .input=IN_HP, .startup=5,  .active=3, .recover=20, .dmg=70,  .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="poke", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=44, .y=88, .h=26 },
        { .name="shove", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=46, .kb=10, .reach=58, .y=86, .h=32 },
        { .name="haymaker", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=78, .kb=18, .reach=68, .y=82, .h=40 },
        { .name="toe tap", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=28, .kb=6, .reach=48, .y=16, .h=28 },
        { .name="heel", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=54, .kb=12, .reach=70, .y=8, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=86, .kb=20, .reach=85, .y=22, .h=54 },
        { .name="crouch poke", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=44, .y=44, .h=26 },
        { .name="crouch shove", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=46, .kb=10, .reach=58, .y=43, .h=32 },
        { .name="crouch haymaker", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=78, .kb=18, .reach=68, .y=40, .h=40 },
        { .name="crouch toe tap", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=28, .kb=6, .reach=48, .y=8, .h=28 },
        { .name="crouch heel", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=54, .kb=12, .reach=70, .y=4, .h=40 },
        { .name="crouch sweep", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=86, .kb=20, .reach=96, .y=0, .h=32 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=34, .kb=7, .reach=50, .y=22, .h=48 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=46, .kb=9, .reach=50, .y=22, .h=48 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=60, .kb=12, .reach=54, .y=22, .h=48 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=44, .kb=10, .reach=58, .y=-42, .h=72 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=60, .kb=12, .reach=58, .y=-42, .h=72 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=78, .kb=16, .reach=62, .y=-42, .h=72 },
        /* Special: throws fake orange hair (projectile) */
        { .name="hair toss", .input=IN_P, .cmd=CMD_QCF, .startup=9, .active=1, .recover=20, .dmg=55, .kb=10, .y=96, .h=20, .proj_speed=13, .proj_life=70, .spin=1, .anim=A_CAST },
        /* Special: upper cup (uppercut-style melee) with a yell (HP-based special) */
        { .name="upper cup", .input=IN_HP, .cmd=CMD_HCF, .startup=6, .active=5, .recover=24, .dmg=140, .kb=34, .reach=52, .y=38, .h=120, .cost=40, .anim=A_SPECIAL },
        /* Charged special: calls the airforce (ship strike) */
        { .name="call airforce", .input=IN_HK, .cmd=CMD_HCF, .startup=18, .active=1, .recover=40, .dmg=80, .kb=60, .ship=5, .cost=60, .anim=A_SUPER },
    }},
    { .name = "JV", .hp = 1100, .lives = 3, .speed = 4, .jump = 22, .w = 78, .h = 172, .moves = {
        { .name="grab", .input=IN_HP, .startup=5, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=43, .y=88, .h=24 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=46, .kb=10, .reach=57, .y=86, .h=30 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=78, .kb=18, .reach=67, .y=84, .h=38 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=28, .kb=6, .reach=50, .y=16, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=54, .kb=12, .reach=72, .y=8, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=86, .kb=20, .reach=87, .y=22, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=43, .y=44, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=46, .kb=10, .reach=57, .y=43, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=78, .kb=18, .reach=67, .y=40, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=28, .kb=6, .reach=50, .y=8, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=54, .kb=12, .reach=72, .y=4, .h=40 },
        { .name="crouch sweep", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=86, .kb=20, .reach=98, .y=0, .h=32 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=34, .kb=7, .reach=48, .y=20, .h=49 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=46, .kb=9, .reach=48, .y=20, .h=49 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=60, .kb=12, .reach=52, .y=20, .h=49 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=44, .kb=10, .reach=56, .y=-44, .h=74 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=58, .kb=12, .reach=56, .y=-44, .h=74 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=76, .kb=16, .reach=60, .y=-44, .h=74 },
        { .name="chicken bone", .input=IN_P, .cmd=CMD_QCF, .startup=8, .active=1, .recover=18, .dmg=50, .kb=9, .y=92, .h=18, .proj_speed=14, .proj_life=65, .spin=1, .anim=A_CAST },
        { .name="uppercut", .input=IN_HP, .cmd=CMD_HCF, .startup=6, .active=5, .recover=22, .dmg=130, .kb=32, .reach=50, .y=40, .h=118, .cost=40, .anim=A_SPECIAL },
        { .name="hillbilly truck", .input=IN_HK, .cmd=CMD_HCF, .startup=16, .active=1, .recover=38, .dmg=75, .kb=55, .ship=5, .cost=55, .anim=A_SUPER },
    }},
    { .name = "Kammy", .hp = 850, .lives = 3, .speed = 6, .jump = 28, .w = 60, .h = 156, .moves = {
        { .name="grab", .input=IN_HP, .startup=5, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=22, .kb=5, .reach=42, .y=90, .h=24 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=44, .kb=9, .reach=55, .y=88, .h=30 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=76, .kb=17, .reach=65, .y=84, .h=38 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=26, .kb=6, .reach=48, .y=18, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=52, .kb=11, .reach=70, .y=10, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=84, .kb=19, .reach=85, .y=24, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=22, .kb=5, .reach=42, .y=45, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=44, .kb=9, .reach=55, .y=44, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=76, .kb=17, .reach=65, .y=42, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=26, .kb=6, .reach=48, .y=9, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=52, .kb=11, .reach=70, .y=5, .h=40 },
        { .name="crouch roundhouse", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=84, .kb=19, .reach=85, .y=12, .h=54 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=32, .kb=7, .reach=46, .y=20, .h=49 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=42, .kb=9, .reach=46, .y=20, .h=49 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=56, .kb=11, .reach=50, .y=20, .h=49 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=40, .kb=9, .reach=54, .y=-44, .h=74 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=54, .kb=11, .reach=54, .y=-44, .h=74 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=72, .kb=15, .reach=58, .y=-44, .h=74 },
        { .name="manifesto", .input=IN_P, .cmd=CMD_QCF, .startup=9, .active=1, .recover=20, .dmg=44, .kb=8, .y=88, .h=22, .proj_speed=12, .proj_life=70, .anim=A_CAST },
        { .name="cackle", .input=IN_HP, .cmd=CMD_HCF, .startup=5, .active=4, .recover=20, .dmg=115, .kb=28, .reach=48, .y=36, .h=110, .cost=35, .anim=A_SPECIAL },
        { .name="mega cackle", .input=IN_HK, .cmd=CMD_HCF, .startup=14, .active=1, .recover=36, .dmg=78, .kb=48, .y=60, .h=80, .boom=1, .cost=60, .anim=A_SUPER },
    }},
    { .name = "Megadami", .hp = 1000, .lives = 3, .speed = 5, .jump = 24, .w = 66, .h = 162, .moves = {
        { .name="grab", .input=IN_HP, .startup=5, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=26, .kb=5, .reach=44, .y=92, .h=24 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=50, .kb=10, .reach=58, .y=90, .h=30 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=84, .kb=18, .reach=68, .y=86, .h=38 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=30, .kb=6, .reach=50, .y=16, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=56, .kb=12, .reach=72, .y=8, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=90, .kb=20, .reach=88, .y=22, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=26, .kb=5, .reach=44, .y=46, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=50, .kb=10, .reach=58, .y=45, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=84, .kb=18, .reach=68, .y=42, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=30, .kb=6, .reach=50, .y=8, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=56, .kb=12, .reach=72, .y=4, .h=40 },
        { .name="crouch roundhouse", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=90, .kb=20, .reach=88, .y=10, .h=54 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=36, .kb=7, .reach=48, .y=20, .h=49 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=48, .kb=9, .reach=48, .y=20, .h=49 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=62, .kb=12, .reach=52, .y=20, .h=49 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=46, .kb=10, .reach=56, .y=-44, .h=74 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=60, .kb=12, .reach=56, .y=-44, .h=74 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=80, .kb=16, .reach=60, .y=-44, .h=74 },
        { .name="ar15", .input=IN_P, .cmd=CMD_QCF, .startup=4, .active=1, .recover=10, .dmg=26, .kb=5, .y=82, .h=20, .proj_speed=34, .proj_life=45, .anim=A_CAST },
        { .name="dynamite vest", .input=IN_HP, .cmd=CMD_HCF, .startup=8, .active=1, .recover=28, .dmg=95, .kb=50, .y=50, .h=90, .boom=1, .cost=45, .anim=A_SPECIAL },
        { .name="old plane", .input=IN_HK, .cmd=CMD_HCF, .startup=18, .active=1, .recover=40, .dmg=72, .kb=58, .ship=6, .cost=50, .anim=A_SUPER },
    }},
    { .name = "Flusk", .hp = 880, .lives = 3, .speed = 7, .jump = 30, .w = 58, .h = 152, .moves = {
        { .name="grab", .input=IN_HP, .startup=5, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=2, .active=2, .recover=5, .dmg=20, .kb=4, .reach=45, .y=78, .h=22 },
        { .name="strong", .input=IN_MP, .startup=4, .active=3, .recover=8, .dmg=38, .kb=8, .reach=56, .y=76, .h=28 },
        { .name="fierce", .input=IN_HP, .startup=6, .active=3, .recover=12, .dmg=64, .kb=14, .reach=66, .y=72, .h=36 },
        { .name="short", .input=IN_LK, .startup=3, .active=2, .recover=6, .dmg=22, .kb=5, .reach=48, .y=12, .h=26 },
        { .name="forward", .input=IN_MK, .startup=6, .active=3, .recover=10, .dmg=46, .kb=10, .reach=70, .y=6, .h=38 },
        { .name="roundhouse", .input=IN_HK, .startup=8, .active=4, .recover=14, .dmg=68, .kb=16, .reach=82, .y=20, .h=52 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=2, .active=2, .recover=5, .dmg=20, .kb=4, .reach=45, .y=38, .h=22 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=4, .active=3, .recover=8, .dmg=38, .kb=8, .reach=56, .y=37, .h=28 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=6, .active=3, .recover=14, .dmg=64, .kb=14, .reach=66, .y=34, .h=36 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=22, .kb=5, .reach=48, .y=6, .h=26 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=6, .active=3, .recover=10, .dmg=46, .kb=10, .reach=70, .y=2, .h=38 },
        { .name="crouch roundhouse", .input=IN_HK, .crouch=1, .startup=8, .active=4, .recover=16, .dmg=68, .kb=16, .reach=82, .y=8, .h=52 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=2, .active=14, .recover=5, .dmg=26, .kb=6, .reach=50, .y=16, .h=45 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=2, .active=14, .recover=5, .dmg=34, .kb=7, .reach=50, .y=16, .h=45 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=3, .active=14, .recover=6, .dmg=46, .kb=9, .reach=54, .y=16, .h=45 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=3, .active=14, .recover=5, .dmg=34, .kb=8, .reach=58, .y=-40, .h=70 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=3, .active=14, .recover=5, .dmg=44, .kb=10, .reach=58, .y=-40, .h=70 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=4, .active=14, .recover=6, .dmg=58, .kb=13, .reach=62, .y=-40, .h=70 },
        { .name="laser gun", .input=IN_P, .cmd=CMD_QCF, .startup=5, .active=1, .recover=9, .dmg=28, .kb=5, .y=76, .h=16, .proj_speed=30, .proj_life=52, .anim=A_CAST },
        { .name="awkward shrug", .input=IN_HP, .cmd=CMD_HCF, .startup=7, .active=5, .recover=22, .dmg=125, .kb=30, .reach=46, .y=42, .h=105, .cost=40, .anim=A_SPECIAL },
        { .name="flying car", .input=IN_HK, .cmd=CMD_HCF, .startup=16, .active=1, .recover=38, .dmg=70, .kb=52, .ship=5, .cost=55, .anim=A_SUPER },
    }},
    { .name = "Spitsger", .hp = 920, .lives = 3, .speed = 6, .jump = 27, .w = 62, .h = 158, .moves = {
        { .name="grab", .input=IN_HP, .startup=5, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=23, .kb=5, .reach=44, .y=88, .h=24 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=45, .kb=10, .reach=57, .y=86, .h=30 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=80, .kb=18, .reach=67, .y=82, .h=38 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=27, .kb=6, .reach=49, .y=16, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=53, .kb=12, .reach=71, .y=8, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=87, .kb=20, .reach=86, .y=22, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=23, .kb=5, .reach=44, .y=44, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=45, .kb=10, .reach=57, .y=43, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=80, .kb=18, .reach=67, .y=40, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=27, .kb=6, .reach=49, .y=8, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=53, .kb=12, .reach=71, .y=4, .h=40 },
        { .name="crouch roundhouse", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=87, .kb=20, .reach=86, .y=10, .h=54 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=33, .kb=7, .reach=47, .y=20, .h=49 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=44, .kb=9, .reach=47, .y=20, .h=49 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=58, .kb=12, .reach=51, .y=20, .h=49 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=42, .kb=10, .reach=55, .y=-44, .h=74 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=56, .kb=12, .reach=55, .y=-44, .h=74 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=74, .kb=16, .reach=59, .y=-44, .h=74 },
        { .name="spit", .input=IN_P, .cmd=CMD_QCF, .startup=6, .active=1, .recover=12, .dmg=38, .kb=7, .y=94, .h=16, .proj_speed=16, .proj_life=55, .anim=A_CAST },
        { .name="eat", .input=IN_HP, .cmd=CMD_HCF, .startup=7, .active=3, .recover=26, .dmg=150, .kb=70, .reach=THROW_RANGE, .y=10, .h=130, .throw=1, .cost=40, .anim=A_SPECIAL },
        { .name="driveby van", .input=IN_HK, .cmd=CMD_HCF, .startup=15, .active=1, .recover=36, .dmg=68, .kb=50, .ship=6, .cost=55, .anim=A_SUPER },
    }},
};

#define NCHARS (int)(sizeof chars / sizeof chars[0])

#endif
