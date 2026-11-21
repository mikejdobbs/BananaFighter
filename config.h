/* config.h - edit and recompile, dwm style */
#ifndef CONFIG_H
#define CONFIG_H

#define BF_VERSION    "1.0.5"   /* client/server must match (handshake V line) */
#define PORT          7777
#define DISCOVER_PORT (PORT + 1) /* LAN server beacon (UDP broadcast) */
#define DISCOVER_MAGIC "BFDISC"
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
#define THROW_LIFT    40        /* upward launch speed of a throw */
#define THROW_KB      12        /* default horizontal knockback for throws */
#define KNOCKDOWN_STUN 78       /* crouch HK sweep: fall, lie, knees, then feet */
#define GETUP_TICKS   42        /* last ticks: on the knees, then up onto the feet */
#define THROW_RANGE   80         /* close enough to grab: heavy punch becomes a throw, beating block */
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
#define PACK_Y_MIN    60        /* rocket-pack flight floor (ceiling is FLY_CEIL) */
#define FUEL_REGEN    4         /* fuel regained per tick on the ground */

/* called ships and their bombs */
#define SHIP_Y        520       /* ship altitude */
#define SHIP_SPEED    10        /* units per tick */
#define SHIP_DROP     18        /* ticks between bombs */
#define SHIP_MARGIN   100       /* how far off-stage a ship starts */
#define BOMB_GRAV     1
#define BOMB_R        90        /* explosion radius */
#define BOOM_TICKS    14        /* how long the explosion effect lasts */
#define BARF_BOOM_TICKS 16      /* chicken-barf spray duration */
#define MAXBOOM       16
#define MAXGIB        48        /* client barf spray blobs */
enum { BOOM_NORMAL = 0, BOOM_BARF = 1 };

/* The client sends raw directions and six attack buttons.
   Super moves use a command sequence plus a final button, not a dedicated super button. */
enum { IN_L = 1, IN_R = 2, IN_U = 4, IN_D = 8,
       IN_LP = 16, IN_MP = 32, IN_HP = 64,
       IN_LK = 128, IN_MK = 256, IN_HK = 512 };
#define IN_P (IN_LP | IN_MP | IN_HP)
#define IN_K (IN_LK | IN_MK | IN_HK)

/* cmd uses the last direction-input sequence; move input is the final punch/kick button.
   CMD_NONE means a normal. CMD_QCF = down, down-forward, forward. CMD_HCF = back, down-back,
   down, down-forward, forward. CMD_FDF = forward, down, forward (toward-only, down-only, toward).
   CMD_360 = full circle (toward, down, back, up). cmd2 is a second motion that also triggers
   the move. Commands also require the move's meter cost. */
enum { CMD_NONE, CMD_QCF, CMD_HCF, CMD_FDF, CMD_360 };

/* Optional animation override for a move (sprite row); A_AUTO picks one from the button/command. */
enum { A_AUTO, A_LP, A_MP, A_HP, A_LK, A_MK, A_HK, A_CAST, A_SPECIAL, A_SUPER };

/* proj_speed != 0: spawns a projectile; otherwise a melee hitbox.
   air:    a jump attack. It can only be used while off the ground, and ground normals can't be.
           Its hitbox y is measured from the fighter's feet and may be negative (hits downward).
   spread: fires this many projectiles in a fan; spread_vy is the vertical speed step between them.
   invis:  ticks of invisibility granted (any other move ends it).
   invuln: if set, invulnerable for startup+active+recover ticks when the move begins (uses grace).
   knockdown: on a clean hit, knock the victim down (fall, then get up) and pop them upward.
   throw:  grabs the closest victim in range (ignores blocking), damages and launches them.
   spin_slam: throw that carries a victim airborne then slams (boom AoE on land).
   combo_hits: with spin_slam, this many uppercuts (1 on connect + rest in air) then arm slam.
               dmg is total victim damage (split across uppercuts / slam).
   bucket_burst: eat/startup then barf AoE (BOOM_BARF); no vanish.
   cost:   super meter needed (and spent). Non-throw melee hits everyone in range.
   ship:   calls a ship that drops this many bombs; dmg and kb apply per bomb.
           With pack: number of exploding pitches fired while flying (no ship entity).
   pack:   body-controlled rocket-pack flight: the player steers and pitches exploding shots.
   boom:   on the active frame, area damage at the attacker (like a bomb; owner immune).
   boom_r: AOE radius for boom (0 = BOMB_R).
   proj_x: how far in front of the body center a projectile spawns (0 = the body edge).
   spin:   the projectile's sprite spins in flight (client only).
   leap:   forward speed during the leap; leap_vy is the initial rise (0 = ~70% of jump).
   ride:   grounded dash speed (px/tick). Each foe in the hitbox is hurt once along the
           path. Stays on the ground (no leap jump) and recovers early at the far edge. */
typedef struct {
    const char *name;
    int input, cmd, cmd2, air, crouch;
    int startup, active, recover;
    int dmg, kb;
    int reach, y, h;
    int proj_speed, proj_life, proj_x;
    int spread, spread_vy;
    int invis, invuln, knockdown, throw, spin_slam, combo_hits, bucket_burst, cost;
    int ship, pack, boom, boom_r, spin, anim;
    int leap, leap_vy, ride;
} Move;

#define SPIN_SLAM_TICKS  56 /* max ticks airborne before forced slam */
#define SPIN_SLAM_JUMP   58 /* large leap (above a normal jump); applied on grab */
#define COMBO_HIT_GAP    6  /* ticks between grounded sky-uppercut hits on a falling foe */
#define SKY_LAUNCH_VY    20 /* short pop; Mike stays on the ground */
#define SKY_JUGGLE_VY    10 /* tiny re-pop between uppercuts — keep them near arm height */
#define SKY_Y_CAP        95 /* never let victims sail off-screen */
#define SKY_ARM_Y        42 /* grab / slam when the foe drops to about Mike's arm */
#define SKY_SLAM_WINDUP  10 /* ticks of arm-slam pose before damage (hand comes down) */

/* Sky uppercut: 5×air + slam = 80 (first uppercut is the launch hit). */
#define SKY_AIR_DMG    11
#define SKY_SLAM_DMG   25

/* fly: tail thrust per tick (0 = cannot fly); fuel: ticks of flight on a full tank. */
typedef struct {
    const char *name;
    int hp, lives, speed, jump, w, h;
    int fly, fuel;
    Move moves[MAXMOVES];           /* list ends at the first empty name */
} Character;

/* Edit these tables, then rebuild: DWM-style game configuration. */
static const Character chars[] = {
    { .name = "Slurpy", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="child toss", .input=IN_HP, .startup=4, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=44, .y=86, .h=24 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=46, .kb=10, .reach=56, .y=84, .h=30 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=78, .kb=18, .reach=66, .y=82, .h=38 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=26, .kb=6, .reach=50, .y=14, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=52, .kb=12, .reach=70, .y=8, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=84, .kb=20, .reach=84, .y=20, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=44, .y=42, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=46, .kb=10, .reach=56, .y=41, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=78, .kb=18, .reach=66, .y=40, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=26, .kb=6, .reach=50, .y=6, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=52, .kb=12, .reach=70, .y=3, .h=40 },
        { .name="crouch sweep", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=84, .kb=20, .reach=90, .y=0, .h=30 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=34, .kb=7, .reach=48, .y=20, .h=48 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=46, .kb=9, .reach=48, .y=20, .h=48 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=60, .kb=12, .reach=52, .y=20, .h=48 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=42, .kb=10, .reach=56, .y=-40, .h=72 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=58, .kb=12, .reach=56, .y=-40, .h=72 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=76, .kb=16, .reach=60, .y=-40, .h=72 },
        { .name="child", .input=IN_P, .cmd=CMD_QCF, .startup=12, .active=1, .recover=22, .dmg=55, .kb=10, .y=90, .h=28, .proj_speed=13, .proj_life=70, .spin=1, .anim=A_CAST },
        { .name="ear flap", .input=IN_HP, .cmd=CMD_FDF, .startup=0, .active=10, .recover=24, .dmg=240, .kb=34, .reach=120, .y=-220, .h=400, .invuln=1, .knockdown=1, .anim=A_SPECIAL, .leap=3, .leap_vy=44 },
        { .name="red baron", .input=IN_HK, .cmd=CMD_HCF, .startup=16, .active=1, .recover=38, .dmg=45, .kb=18, .y=80, .h=20, .ship=8, .cost=55, .anim=A_SUPER },
    }},
    { .name = "Alien", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="grab",      .input=IN_HP, .startup=4,  .active=3, .recover=20, .dmg=70,  .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="probe", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=16, .kb=3, .reach=42, .y=72, .h=28 },
        { .name="ray jab", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=25, .kb=5, .reach=51, .y=69, .h=38 },
        { .name="blaster", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=42, .kb=9, .reach=63, .y=60, .h=48 },
        { .name="low beam", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=18, .kb=4, .reach=45, .y=14, .h=31 },
        { .name="trip", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=32, .kb=7, .reach=57, .y=3, .h=38 },
        { .name="hover kick", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=52, .kb=12, .reach=75, .y=35, .h=60 },
        { .name="crouch probe", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=16, .kb=3, .reach=42, .y=36, .h=28 },
        { .name="crouch ray jab", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=25, .kb=5, .reach=51, .y=34, .h=38 },
        { .name="crouch blaster", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=42, .kb=9, .reach=63, .y=30, .h=48 },
        { .name="crouch low beam", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=18, .kb=4, .reach=45, .y=7, .h=31 },
        { .name="crouch trip", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=32, .kb=7, .reach=57, .y=1, .h=38 },
        { .name="crouch hover kick", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=52, .kb=12, .reach=75, .y=17, .h=60 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=18, .kb=4, .reach=45, .y=17, .h=60 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=24, .kb=5, .reach=45, .y=17, .h=60 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=31, .kb=6, .reach=49, .y=17, .h=60 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=24, .kb=6, .reach=54, .y=-48, .h=83 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=32, .kb=7, .reach=54, .y=-48, .h=83 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=42, .kb=9, .reach=58, .y=-48, .h=83 },
        { .name="laser",     .input=IN_P,.cmd=CMD_QCF,.startup=12,.active=1,.recover=22,.dmg=24,.kb=4,.y=96,.h=16,.proj_speed=13,.proj_life=50 },
        { .name="mothership",.input=IN_HP,.cmd=CMD_HCF,.startup=18,.active=1,.recover=40,.dmg=70,.kb=60,.ship=6,.cost=40 },
    }},
    { .name = "Rump", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="grab",      .input=IN_HP, .startup=4,  .active=3, .recover=20, .dmg=70,  .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
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
        { .name="hair toss", .input=IN_P, .cmd=CMD_QCF, .startup=12, .active=1, .recover=22, .dmg=55, .kb=10, .y=94, .h=20, .proj_speed=13, .proj_life=70, .spin=1, .anim=A_CAST },
        /* Special: upper cup (uppercut-style melee) with a yell (HP-based special) */
        { .name="upper cup", .input=IN_HP, .cmd=CMD_FDF, .startup=0, .active=10, .recover=24, .dmg=240, .kb=34, .reach=120, .y=-220, .h=400, .invuln=1, .knockdown=1, .anim=A_SPECIAL, .leap=3, .leap_vy=44 },
        /* Charged special: calls the airforce (ship strike) */
        { .name="call airforce", .input=IN_HK, .cmd=CMD_HCF, .startup=18, .active=1, .recover=40, .dmg=80, .kb=60, .ship=5, .cost=60, .anim=A_SUPER },
    }},
    { .name = "JV", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="grab", .input=IN_HP, .startup=4, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
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
        { .name="chicken bone", .input=IN_P, .cmd=CMD_QCF, .startup=12, .active=1, .recover=22, .dmg=50, .kb=9, .y=95, .h=18, .proj_speed=13, .proj_life=65, .spin=1, .anim=A_CAST },
        { .name="chicken barf", .input=IN_HP, .cmd=CMD_FDF, .startup=42, .active=1, .recover=18, .dmg=240, .kb=40, .y=100, .h=40, .boom=1, .boom_r=120, .invuln=1, .knockdown=1, .bucket_burst=1, .anim=A_SPECIAL },
        { .name="hillbilly truck", .input=IN_HK, .cmd=CMD_HCF, .startup=16, .active=1, .recover=38, .dmg=75, .kb=55, .ship=5, .cost=55, .anim=A_SUPER },
    }},
    { .name = "Kammy", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="grab", .input=IN_HP, .startup=4, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
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
        { .name="manifesto", .input=IN_P, .cmd=CMD_QCF, .startup=12, .active=1, .recover=22, .dmg=44, .kb=8, .y=93, .h=22, .proj_speed=13, .proj_life=70, .anim=A_CAST },
        { .name="cackle", .input=IN_HP, .cmd=CMD_FDF, .startup=4, .active=8, .recover=22, .dmg=200, .kb=40, .reach=160, .y=90, .h=90, .invuln=1, .knockdown=1, .anim=A_SPECIAL },
        { .name="mega cackle", .input=IN_HK, .cmd=CMD_HCF, .startup=14, .active=1, .recover=36, .dmg=120, .kb=55, .y=60, .h=80, .boom=1, .boom_r=STAGE_W, .knockdown=1, .cost=60, .anim=A_SUPER },
    }},
    { .name = "Flusk", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="grab", .input=IN_HP, .startup=4, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=20, .kb=4, .reach=45, .y=78, .h=22 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=38, .kb=8, .reach=56, .y=76, .h=28 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=64, .kb=14, .reach=66, .y=72, .h=36 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=22, .kb=5, .reach=48, .y=12, .h=26 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=46, .kb=10, .reach=70, .y=6, .h=38 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=68, .kb=16, .reach=82, .y=20, .h=52 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=20, .kb=4, .reach=45, .y=38, .h=22 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=38, .kb=8, .reach=56, .y=37, .h=28 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=64, .kb=14, .reach=66, .y=34, .h=36 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=22, .kb=5, .reach=48, .y=6, .h=26 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=46, .kb=10, .reach=70, .y=2, .h=38 },
        { .name="crouch roundhouse", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=68, .kb=16, .reach=82, .y=8, .h=52 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=26, .kb=6, .reach=50, .y=16, .h=45 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=34, .kb=7, .reach=50, .y=16, .h=45 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=46, .kb=9, .reach=54, .y=16, .h=45 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=34, .kb=8, .reach=58, .y=-40, .h=70 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=44, .kb=10, .reach=58, .y=-40, .h=70 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=58, .kb=13, .reach=62, .y=-40, .h=70 },
        { .name="laser gun", .input=IN_P, .cmd=CMD_QCF, .startup=12, .active=1, .recover=22, .dmg=28, .kb=5, .y=96, .h=16, .proj_speed=13, .proj_life=52, .proj_x=95, .anim=A_CAST },
        { .name="awkward shrug", .input=IN_HP, .cmd=CMD_FDF, .cmd2=CMD_HCF, .startup=7, .active=5, .recover=22, .dmg=125, .kb=30, .reach=100, .y=42, .h=105, .anim=A_SPECIAL },
        { .name="flying car", .input=IN_HK, .cmd=CMD_HCF, .startup=16, .active=1, .recover=38, .dmg=70, .kb=52, .ship=5, .cost=55, .anim=A_SUPER },
    }},
    { .name = "Stephen", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="grab", .input=IN_HP, .startup=4, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=22, .kb=5, .reach=46, .y=88, .h=24 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=42, .kb=9, .reach=58, .y=86, .h=30 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=70, .kb=16, .reach=68, .y=84, .h=38 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=24, .kb=6, .reach=50, .y=14, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=48, .kb=11, .reach=72, .y=8, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=76, .kb=18, .reach=86, .y=20, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=22, .kb=5, .reach=46, .y=42, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=42, .kb=9, .reach=58, .y=40, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=70, .kb=16, .reach=68, .y=38, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=24, .kb=6, .reach=50, .y=6, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=48, .kb=11, .reach=72, .y=4, .h=40 },
        { .name="crouch sweep", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=76, .kb=18, .reach=90, .y=0, .h=32 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=30, .kb=7, .reach=48, .y=18, .h=48 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=40, .kb=9, .reach=50, .y=18, .h=48 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=54, .kb=12, .reach=54, .y=18, .h=48 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=36, .kb=9, .reach=56, .y=-40, .h=72 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=50, .kb=11, .reach=58, .y=-40, .h=72 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=68, .kb=15, .reach=62, .y=-40, .h=72 },
        { .name="baseball", .input=IN_HP, .cmd=CMD_QCF, .startup=12, .active=1, .recover=22, .dmg=32, .kb=8, .y=94, .h=20, .proj_speed=13, .proj_life=60, .proj_x=100, .spin=1, .anim=A_CAST },
        { .name="bat uppercut", .input=IN_HP, .cmd=CMD_FDF, .startup=0, .active=10, .recover=24, .dmg=240, .kb=34, .reach=120, .y=-220, .h=400, .invuln=1, .knockdown=1, .anim=A_SPECIAL, .leap=3, .leap_vy=44 },
        { .name="bat rocket", .input=IN_HK, .cmd=CMD_HCF, .startup=10, .active=150, .recover=24, .dmg=50, .kb=35, .y=75, .h=20, .proj_speed=11, .proj_life=45, .proj_x=66, .spin=1, .pack=1, .ship=8, .cost=55, .invuln=1, .anim=A_SUPER },
    }},
    { .name = "LoPicardo", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="grab", .input=IN_HP, .startup=4, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=20, .kb=4, .reach=48, .y=100, .h=22 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=38, .kb=8, .reach=58, .y=98, .h=28 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=64, .kb=14, .reach=68, .y=94, .h=36 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=22, .kb=5, .reach=50, .y=14, .h=26 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=46, .kb=10, .reach=72, .y=8, .h=38 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=68, .kb=16, .reach=84, .y=22, .h=52 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=20, .kb=4, .reach=48, .y=40, .h=22 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=38, .kb=8, .reach=58, .y=38, .h=28 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=64, .kb=14, .reach=68, .y=36, .h=36 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=22, .kb=5, .reach=50, .y=6, .h=26 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=46, .kb=10, .reach=72, .y=4, .h=38 },
        { .name="crouch roundhouse", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=68, .kb=16, .reach=84, .y=10, .h=52 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=26, .kb=6, .reach=50, .y=18, .h=48 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=34, .kb=7, .reach=50, .y=18, .h=48 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=46, .kb=9, .reach=54, .y=18, .h=48 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=34, .kb=8, .reach=58, .y=-42, .h=74 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=44, .kb=10, .reach=58, .y=-42, .h=74 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=58, .kb=13, .reach=62, .y=-42, .h=74 },
        { .name="phaser", .input=IN_P, .cmd=CMD_QCF, .startup=12, .active=1, .recover=22, .dmg=28, .kb=5, .y=96, .h=16, .proj_speed=13, .proj_life=52, .proj_x=42, .anim=A_CAST },
        { .name="assimilate", .input=IN_HP, .cmd=CMD_FDF, .startup=10, .active=12, .recover=22, .dmg=200, .kb=40, .reach=110, .y=20, .h=160, .knockdown=1, .anim=A_SPECIAL },
        { .name="cube ship", .input=IN_HK, .cmd=CMD_HCF, .startup=16, .active=1, .recover=38, .dmg=70, .kb=50, .ship=6, .cost=55, .anim=A_SUPER },
    }},
    { .name = "Mike", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="grab", .input=IN_HP, .startup=4, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=48, .y=92, .h=26 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=52, .kb=12, .reach=62, .y=90, .h=36 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=72, .kb=16, .reach=70, .y=88, .h=40 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=26, .kb=6, .reach=52, .y=14, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=50, .kb=11, .reach=74, .y=8, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=78, .kb=18, .reach=88, .y=22, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=48, .y=44, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=44, .kb=9, .reach=60, .y=42, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=72, .kb=16, .reach=70, .y=40, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=26, .kb=6, .reach=52, .y=6, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=50, .kb=11, .reach=74, .y=4, .h=40 },
        { .name="crouch sweep", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=78, .kb=18, .reach=90, .y=0, .h=32 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=32, .kb=7, .reach=50, .y=18, .h=48 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=42, .kb=9, .reach=52, .y=18, .h=48 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=56, .kb=12, .reach=56, .y=18, .h=48 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=38, .kb=9, .reach=58, .y=-40, .h=72 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=52, .kb=11, .reach=60, .y=-40, .h=72 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=70, .kb=15, .reach=64, .y=-40, .h=72 },
        { .name="fireball", .input=IN_P, .cmd=CMD_QCF, .startup=12, .active=1, .recover=22, .dmg=48, .kb=8, .y=100, .h=24, .proj_speed=13, .proj_life=70, .proj_x=70, .spin=1, .anim=A_CAST },
        /* Area uppercut → 5 air hits → arm slam. No leap_vy: whiffs stay grounded. */
        { .name="sky uppercut", .input=IN_P, .cmd=CMD_FDF, .startup=4, .active=6, .recover=40, .dmg=80, .kb=40, .reach=120, .y=-220, .h=400, .throw=1, .spin_slam=1, .combo_hits=5, .knockdown=1, .boom_r=40, .anim=A_SPECIAL },
        { .name="beach bomber", .input=IN_HK, .cmd=CMD_HCF, .startup=16, .active=1, .recover=38, .dmg=70, .kb=50, .ship=5, .cost=55, .anim=A_SUPER },
    }},
    { .name = "Krampus", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="grab", .input=IN_HP, .startup=4, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=46, .y=88, .h=24 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=46, .kb=10, .reach=58, .y=86, .h=30 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=76, .kb=17, .reach=68, .y=84, .h=38 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=26, .kb=6, .reach=50, .y=14, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=52, .kb=12, .reach=72, .y=8, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=82, .kb=19, .reach=86, .y=20, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=46, .y=42, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=46, .kb=10, .reach=58, .y=40, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=76, .kb=17, .reach=68, .y=38, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=26, .kb=6, .reach=50, .y=6, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=52, .kb=12, .reach=72, .y=4, .h=40 },
        { .name="crouch sweep", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=82, .kb=19, .reach=92, .y=0, .h=32 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=32, .kb=7, .reach=48, .y=18, .h=48 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=42, .kb=9, .reach=50, .y=18, .h=48 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=56, .kb=12, .reach=54, .y=18, .h=48 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=38, .kb=9, .reach=56, .y=-40, .h=72 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=52, .kb=11, .reach=58, .y=-40, .h=72 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=70, .kb=15, .reach=62, .y=-40, .h=72 },
        { .name="coal", .input=IN_P, .cmd=CMD_QCF, .startup=12, .active=1, .recover=22, .dmg=50, .kb=9, .y=94, .h=20, .proj_speed=13, .proj_life=70, .proj_x=66, .spin=1, .anim=A_CAST },
        { .name="whip", .input=IN_HP, .cmd=CMD_FDF, .startup=0, .active=10, .recover=24, .dmg=240, .kb=34, .reach=150, .y=-220, .h=400, .invuln=1, .knockdown=1, .anim=A_SPECIAL, .leap=3, .leap_vy=44 },
        { .name="santa flyby", .input=IN_HK, .cmd=CMD_HCF, .startup=16, .active=1, .recover=38, .dmg=45, .kb=18, .y=80, .h=20, .ship=8, .cost=55, .anim=A_SUPER },
    }},
    { .name = "KevKev", .hp = 900, .lives = 3, .speed = 6, .jump = 28, .w = 58, .h = 156, .moves = {
        { .name="grab", .input=IN_HP, .startup=4, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=46, .y=88, .h=24 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=46, .kb=10, .reach=58, .y=86, .h=30 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=76, .kb=17, .reach=68, .y=84, .h=38 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=26, .kb=6, .reach=50, .y=14, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=52, .kb=12, .reach=72, .y=8, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=82, .kb=19, .reach=86, .y=20, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=24, .kb=5, .reach=46, .y=42, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=46, .kb=10, .reach=58, .y=40, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=76, .kb=17, .reach=68, .y=38, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=26, .kb=6, .reach=50, .y=6, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=52, .kb=12, .reach=72, .y=4, .h=40 },
        { .name="crouch sweep", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=82, .kb=19, .reach=92, .y=0, .h=32 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=32, .kb=7, .reach=48, .y=18, .h=48 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=42, .kb=9, .reach=50, .y=18, .h=48 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=56, .kb=12, .reach=54, .y=18, .h=48 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=38, .kb=9, .reach=56, .y=-40, .h=72 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=52, .kb=11, .reach=58, .y=-40, .h=72 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=70, .kb=15, .reach=62, .y=-40, .h=72 },
        { .name="hair tie", .input=IN_P, .cmd=CMD_QCF, .startup=12, .active=1, .recover=22, .dmg=50, .kb=9, .y=90, .h=18, .proj_speed=13, .proj_life=70, .proj_x=66, .spin=1, .anim=A_CAST },
        /* Area uppercut: ankle-length hair sweeps everyone in the tall box. */
        { .name="hair whip", .input=IN_HP, .cmd=CMD_FDF, .startup=0, .active=10, .recover=24, .dmg=240, .kb=34, .reach=180, .y=-180, .h=360, .invuln=1, .knockdown=1, .anim=A_SPECIAL, .leap=3, .leap_vy=44 },
        /* Grounded dash across the stage. ride speed hits each foe in the path once. */
        { .name="lion ride", .input=IN_HK, .cmd=CMD_HCF, .startup=8, .active=90, .recover=18, .dmg=110, .kb=36, .reach=100, .y=0, .h=180, .invuln=1, .knockdown=1, .cost=55, .anim=A_SUPER, .ride=16 },
    }},
    { .name = "Rose", .hp = 880, .lives = 3, .speed = 7, .jump = 30, .w = 52, .h = 150, .moves = {
        { .name="grab", .input=IN_HP, .startup=4, .active=3, .recover=20, .dmg=70, .kb=50, .reach=THROW_RANGE, .y=0, .h=200, .throw=1 },
        { .name="jab", .input=IN_LP, .startup=3, .active=2, .recover=6, .dmg=22, .kb=5, .reach=44, .y=90, .h=24 },
        { .name="strong", .input=IN_MP, .startup=5, .active=3, .recover=10, .dmg=44, .kb=10, .reach=56, .y=88, .h=30 },
        { .name="fierce", .input=IN_HP, .startup=8, .active=4, .recover=16, .dmg=72, .kb=16, .reach=66, .y=86, .h=38 },
        { .name="short", .input=IN_LK, .startup=4, .active=2, .recover=7, .dmg=24, .kb=6, .reach=48, .y=16, .h=28 },
        { .name="forward", .input=IN_MK, .startup=7, .active=3, .recover=12, .dmg=50, .kb=12, .reach=70, .y=10, .h=40 },
        { .name="roundhouse", .input=IN_HK, .startup=10, .active=4, .recover=18, .dmg=80, .kb=18, .reach=84, .y=22, .h=54 },
        { .name="crouch jab", .input=IN_LP, .crouch=1, .startup=3, .active=2, .recover=6, .dmg=22, .kb=5, .reach=44, .y=42, .h=24 },
        { .name="crouch strong", .input=IN_MP, .crouch=1, .startup=5, .active=3, .recover=10, .dmg=44, .kb=10, .reach=56, .y=40, .h=30 },
        { .name="crouch fierce", .input=IN_HP, .crouch=1, .startup=8, .active=4, .recover=18, .dmg=72, .kb=16, .reach=66, .y=38, .h=38 },
        { .name="crouch short", .input=IN_LK, .crouch=1, .startup=4, .active=2, .recover=7, .dmg=24, .kb=6, .reach=48, .y=6, .h=28 },
        { .name="crouch forward", .input=IN_MK, .crouch=1, .startup=7, .active=3, .recover=12, .dmg=50, .kb=12, .reach=70, .y=4, .h=40 },
        { .name="crouch sweep", .input=IN_HK, .crouch=1, .startup=10, .active=4, .recover=20, .dmg=80, .kb=18, .reach=90, .y=0, .h=32 },
        { .name="air light punch", .input=IN_LP, .air=1, .startup=3, .active=14, .recover=6, .dmg=30, .kb=7, .reach=46, .y=18, .h=48 },
        { .name="air medium punch", .input=IN_MP, .air=1, .startup=3, .active=14, .recover=6, .dmg=40, .kb=9, .reach=48, .y=18, .h=48 },
        { .name="air heavy punch", .input=IN_HP, .air=1, .startup=4, .active=14, .recover=7, .dmg=54, .kb=12, .reach=52, .y=18, .h=48 },
        { .name="air light kick", .input=IN_LK, .air=1, .startup=4, .active=14, .recover=6, .dmg=36, .kb=9, .reach=54, .y=-40, .h=72 },
        { .name="air medium kick", .input=IN_MK, .air=1, .startup=4, .active=14, .recover=6, .dmg=50, .kb=11, .reach=56, .y=-40, .h=72 },
        { .name="air heavy kick", .input=IN_HK, .air=1, .startup=5, .active=14, .recover=7, .dmg=68, .kb=15, .reach=60, .y=-40, .h=72 },
        /* Ice bolt projectile. */
        { .name="ice bolt", .input=IN_P, .cmd=CMD_QCF, .startup=10, .active=1, .recover=20, .dmg=48, .kb=8, .y=96, .h=20, .proj_speed=14, .proj_life=72, .proj_x=62, .spin=1, .anim=A_CAST },
        /* Ice blast from the hand: tall leaping area hit. */
        { .name="ice blast", .input=IN_HP, .cmd=CMD_FDF, .startup=0, .active=10, .recover=24, .dmg=240, .kb=34, .reach=160, .y=-200, .h=380, .invuln=1, .knockdown=1, .anim=A_SPECIAL, .leap=3, .leap_vy=44 },
        /* Dance dash across the stage blasting ice at everyone in her path. */
        { .name="ice dance", .input=IN_HK, .cmd=CMD_HCF, .startup=8, .active=90, .recover=18, .dmg=110, .kb=36, .reach=120, .y=-40, .h=220, .invuln=1, .knockdown=1, .cost=55, .anim=A_SUPER, .ride=14 },
    }},
};

#define NCHARS (int)(sizeof chars / sizeof chars[0])

#endif
