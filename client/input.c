/*
 * input.c
 * Keyboard, gamepad, raw joystick, and on-screen virtual pad input:
 * - Key bindings for directions and six attack buttons
 * - Gamepad mapping (Xbox-style) and dead-zone handling
 * - Touch / mouse virtual controls for iOS (and desktop when no pad)
 * - Edge detection helpers for menu/actions
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "client.h"
#include "lan.h"

/* Keyboard bindings live in g_settings.binds (see settings.c). */

/*
 * Gamepad buttons (Xbox naming):
 * - Top row X, Y, RB -> light/medium/heavy punch
 * - Bottom row A, B, RT -> light/medium/heavy kick
 * D-pad and left stick provide movement (with PAD_DEAD dead-zone).
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

/*
 * Raw joysticks without a gamepad mapping:
 * - Hat or first two axes move (with PAD_DEAD dead-zone)
 * - Buttons 0..5 map to LP, MP, HP, LK, MK, HK
 */
static const int joybuttons[6] = {IN_LP, IN_MP, IN_HP, IN_LK, IN_MK, IN_HK};

static SDL_Gamepad  *pads[MAXPADS];                  /* Opened gamepads */
static SDL_Joystick *joys[MAXPADS];                  /* Opened raw joysticks */
static int           npads, njoys;                   /* Counts */
static int           pprev[SDL_GAMEPAD_BUTTON_COUNT];/* Per-button previous state for edge detection */

/* ---- Virtual pad / touch UI (logical SW x SH coords) ---- */
#define VPAD_CX 150.0f
#define VPAD_CY 560.0f
#define VPAD_R  110.0f
#define VPAD_DEAD 28.0f
#define BTN_R 36.0f
#define MAX_TOUCH 8

enum {
    TOUCH_NONE = 0,
    TOUCH_DPAD,
    TOUCH_BTN,
    TOUCH_OPTS,
    TOUCH_UI
};

typedef struct {
    SDL_FingerID id;
    int kind;   /* TOUCH_* */
    int bit;    /* attack bit when TOUCH_BTN */
    int active;
} TouchSlot;

static TouchSlot touches[MAX_TOUCH];
static int touch_bits;
static int ui_confirm, ui_back, ui_options;
static int ui_title_item = -1, ui_connect_sel = -1, ui_options_item = -1;
static int mouse_held;
static SDL_FingerID mouse_fid = (SDL_FingerID)-2;

static const struct {
    float x, y;
    int bit;
    const char *label;
} atk_btns[] = {
    {980, 500, IN_LP, "LP"}, {1080, 500, IN_MP, "MP"}, {1180, 500, IN_HP, "HP"},
    {980, 600, IN_LK, "LK"}, {1080, 600, IN_MK, "MK"}, {1180, 600, IN_HK, "HK"},
};

static float opts_x(void) { return SW - 70.0f; }
static float opts_y(void) { return 36.0f; }

/* Open a new gamepad if there is room and it isn't already open */
void pad_open(SDL_JoystickID id)
{
    if (npads >= MAXPADS || SDL_GetGamepadFromID(id)) return;
    SDL_Gamepad *g = SDL_OpenGamepad(id);
    if (g) pads[npads++] = g;
}

/* Close a gamepad by ID and compact the array */
void pad_close(SDL_JoystickID id)
{
    for (int i = 0; i < npads; i++)
        if (SDL_GetGamepadID(pads[i]) == id) {
            SDL_CloseGamepad(pads[i]);
            pads[i] = pads[--npads];
            return;
        }
}

/* Open a raw joystick (not recognized as a gamepad) if there is room */
void joy_open(SDL_JoystickID id)
{
    if (njoys >= MAXPADS || SDL_IsGamepad(id) || SDL_GetJoystickFromID(id)) return;
    SDL_Joystick *j = SDL_OpenJoystick(id);
    if (j) joys[njoys++] = j;
}

/* Close a raw joystick by ID and compact the array */
void joy_close(SDL_JoystickID id)
{
    for (int i = 0; i < njoys; i++)
        if (SDL_GetJoystickID(joys[i]) == id) {
            SDL_CloseJoystick(joys[i]);
            joys[i] = joys[--njoys];
            return;
        }
}

int pads_connected(void) { return npads + njoys; }

/* True for a physical MFi / Bluetooth pad we should prefer over the virtual UI.
 * Ignore SDL virtual devices and keyboard-named ghosts (common in Simulator). */
static int is_physical_gamepad(SDL_Gamepad *g)
{
    SDL_JoystickID id;
    const char *name;
    if (!g) return 0;
    id = SDL_GetGamepadID(g);
    if (SDL_IsJoystickVirtual(id))
        return 0;
    name = SDL_GetGamepadName(g);
    if (name) {
        if (SDL_strcasecmp(name, "Keyboard") == 0)
            return 0;
        if (SDL_strcasestr(name, "Keyboard") ||
            SDL_strcasestr(name, "Touchscreen") ||
            SDL_strcasestr(name, "Virtual"))
            return 0;
    }
    return 1;
}

/* Pads that should hide the on-screen overlay (not ghosts / raw accel joys). */
static int overlay_pads_connected(void)
{
    int n = 0;
    for (int i = 0; i < npads; i++)
        if (is_physical_gamepad(pads[i]))
            n++;
#ifndef BF_IOS
    n += njoys;
#endif
    return n;
}

/* Initialize input subsystems and open any currently connected devices */
void pads_init(void)
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#ifdef BF_IOS
    /* Simulator mouse should synthesize touches for the virtual pad. */
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "no gamepad support: %s\n", SDL_GetError());
        return;
    }
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; i < n; i++) pad_open(ids[i]);
    SDL_free(ids);
    ids = SDL_GetJoysticks(&n);
    for (int i = 0; i < n; i++) joy_open(ids[i]);
    SDL_free(ids);
    if (npads + njoys > 0 && overlay_pads_connected() == 0)
        fprintf(stderr, "input: ignoring %d non-physical pad(s); showing virtual controls\n",
                npads + njoys);
}

/* Gather direction and attack bits from every connected pad and joystick. */
int padmask(void)
{
    int m = 0;
    for (int i = 0; i < npads; i++) {
        SDL_Gamepad *g = pads[i];
        int ax = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX),
            ay = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
            ax < -PAD_DEAD)
            m |= IN_L;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
            ax > PAD_DEAD)
            m |= IN_R;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
            ay < -PAD_DEAD)
            m |= IN_U;
        if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
            ay > PAD_DEAD)
            m |= IN_D;
        for (size_t k = 0; k < sizeof padkeys / sizeof padkeys[0]; k++)
            if (SDL_GetGamepadButton(g, padkeys[k].b)) m |= padkeys[k].bit;
        if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > PAD_TRIG)
            m |= IN_HK;
    }
    for (int i = 0; i < njoys; i++) {
        SDL_Joystick *j = joys[i];
        Uint8 hat = SDL_GetNumJoystickHats(j) > 0 ? SDL_GetJoystickHat(j, 0) : 0;
        int ax = SDL_GetNumJoystickAxes(j) > 0 ? SDL_GetJoystickAxis(j, 0) : 0;
        int ay = SDL_GetNumJoystickAxes(j) > 1 ? SDL_GetJoystickAxis(j, 1) : 0;
        if ((hat & SDL_HAT_LEFT) || ax < -PAD_DEAD) m |= IN_L;
        if ((hat & SDL_HAT_RIGHT) || ax > PAD_DEAD) m |= IN_R;
        if ((hat & SDL_HAT_UP) || ay < -PAD_DEAD) m |= IN_U;
        if ((hat & SDL_HAT_DOWN) || ay > PAD_DEAD) m |= IN_D;
        for (int b = 0; b < 6 &&
                b < SDL_GetNumJoystickButtons(j); b++)
            if (SDL_GetJoystickButton(j, b)) m |= joybuttons[b];
    }
    return m;
}

/* True once when a gamepad button goes down (call once per frame per button). */
int pad_edge(SDL_GamepadButton b)
{
    int down = 0;
    for (int i = 0; i < npads; i++)
        if (SDL_GetGamepadButton(pads[i], b)) down = 1;
    int edge = down && !pprev[b];   /* newly pressed this frame */
    pprev[b] = down;
    return edge;
}

/* Return OR-ed input bits from the current keyboard state */
int keymask(void)
{
    const bool *ks = SDL_GetKeyboardState(NULL);
    int mask = 0;
    for (int i = 0; i < BIND_COUNT; i++) {
        SDL_Scancode sc = g_settings.binds[i];
        if (sc != SDL_SCANCODE_UNKNOWN && ks[sc])
            mask |= bind_to_input_bit(i);
    }
    /* Arrow keys always move, in addition to remappable binds. */
    if (ks[SDL_SCANCODE_LEFT])
        mask |= IN_L;
    if (ks[SDL_SCANCODE_RIGHT])
        mask |= IN_R;
    if (ks[SDL_SCANCODE_UP])
        mask |= IN_U;
    if (ks[SDL_SCANCODE_DOWN])
        mask |= IN_D;
    return mask;
}

/* During options rebind: assign sc to the waiting action; swap on conflict. */
int input_rebind_key(SDL_Scancode sc)
{
    int bind, other;
    if (options_rebinding < 0 || options_rebinding >= BIND_COUNT)
        return 0;
    if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_UNKNOWN)
        return 0;
    /* Arrows are permanently reserved for movement. */
    if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT ||
        sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_DOWN)
        return 0;
    bind = options_rebinding;
    for (other = 0; other < BIND_COUNT; other++) {
        if (other != bind && g_settings.binds[other] == sc) {
            g_settings.binds[other] = g_settings.binds[bind];
            break;
        }
    }
    g_settings.binds[bind] = sc;
    options_rebinding = -1;
    settings_save();
    return 1;
}

int touchmask(void) { return touch_want_overlay() ? touch_bits : 0; }

int touch_want_overlay(void)
{
    /* On-screen controls are for iPhone only (never macOS/desktop). */
#ifdef BF_IOS
    return 1;
#else
    {
        const char *plat = SDL_GetPlatform();
        if (plat && SDL_strcasecmp(plat, "iOS") == 0)
            return 1;
    }
    return 0;
#endif
}

void touch_clear(void)
{
    memset(touches, 0, sizeof touches);
    touch_bits = 0;
    mouse_held = 0;
    ui_confirm = ui_back = ui_options = 0;
    ui_title_item = ui_connect_sel = ui_options_item = -1;
}

int touch_ui_confirm(void)
{
    int v = ui_confirm;
    ui_confirm = 0;
    return v;
}
int touch_ui_back(void)
{
    int v = ui_back;
    ui_back = 0;
    return v;
}
int touch_ui_options(void)
{
    int v = ui_options;
    ui_options = 0;
    return v;
}
int touch_title_item(void)
{
    int v = ui_title_item;
    ui_title_item = -1;
    return v;
}
int touch_connect_sel(void)
{
    int v = ui_connect_sel;
    ui_connect_sel = -1;
    return v;
}
int touch_options_item(void)
{
    int v = ui_options_item;
    ui_options_item = -1;
    return v;
}

static int hit_circle(float x, float y, float cx, float cy, float r)
{
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

static void recompute_touch_bits(void)
{
    int m = 0;
    for (int i = 0; i < MAX_TOUCH; i++) {
        if (!touches[i].active) continue;
        if (touches[i].kind == TOUCH_BTN)
            m |= touches[i].bit;
        else if (touches[i].kind == TOUCH_DPAD) {
            /* Direction is stored in .bit as a temporary OR of dirs each move. */
            m |= touches[i].bit;
        }
    }
    touch_bits = m;
}

static void set_dpad_from_pos(TouchSlot *t, float x, float y)
{
    float dx = x - VPAD_CX, dy = y - VPAD_CY;
    int bits = 0;
    if (fabsf(dx) >= VPAD_DEAD || fabsf(dy) >= VPAD_DEAD) {
        float ang = atan2f(dy, dx); /* -pi..pi, 0 = right */
        /* 8-way: map to cardinals with diagonals allowed */
        if (dx <= -VPAD_DEAD) bits |= IN_L;
        if (dx >= VPAD_DEAD) bits |= IN_R;
        if (dy <= -VPAD_DEAD) bits |= IN_U;
        if (dy >= VPAD_DEAD) bits |= IN_D;
        (void)ang;
    }
    t->bit = bits;
}

static TouchSlot *touch_find(SDL_FingerID id)
{
    for (int i = 0; i < MAX_TOUCH; i++)
        if (touches[i].active && touches[i].id == id)
            return &touches[i];
    return NULL;
}

static TouchSlot *touch_alloc(SDL_FingerID id)
{
    TouchSlot *t = touch_find(id);
    if (t) return t;
    for (int i = 0; i < MAX_TOUCH; i++)
        if (!touches[i].active) {
            touches[i].id = id;
            touches[i].active = 1;
            touches[i].kind = TOUCH_NONE;
            touches[i].bit = 0;
            return &touches[i];
        }
    return NULL;
}

static void touch_release(SDL_FingerID id)
{
    TouchSlot *t = touch_find(id);
    if (!t) return;
    t->active = 0;
    t->kind = TOUCH_NONE;
    t->bit = 0;
    recompute_touch_bits();
}

/* Title menu panel geometry (must match draw_title). */
static int title_hit(float x, float y)
{
    const float panel_x = 48, panel_y = 160, panel_w = 400;
    for (int i = 0; i < TITLE_COUNT; i++) {
        float row_y = panel_y + 56 + i * 48;
        if (x >= panel_x + 16 && x <= panel_x + panel_w - 16 &&
            y >= row_y - 10 && y <= row_y + 26)
            return i;
    }
    return -1;
}

static int connect_list_hit(float x, float y)
{
    int n = lan_count();
    for (int i = 0; i < n && i < MAX_LAN_SERVERS; i++) {
        float row_y = 140 + i * 28;
        if (x >= 90 && x <= SW - 90 && y >= row_y - 6 && y <= row_y + 18)
            return i;
    }
    /* Connect button strip under address field */
    if (x >= 80 && x <= SW - 80 && y >= 480 && y <= 540)
        return -2; /* special: confirm connect */
    /* Back strip */
    if (x >= 80 && x <= 280 && y >= SH - 60 && y <= SH - 20)
        return -3;
    return -1;
}

static int options_hit(float x, float y)
{
    return options_hit_test(x, y);
}

static void touch_down_at(SDL_FingerID id, float x, float y, int screen)
{
    TouchSlot *t = touch_alloc(id);
    if (!t) return;

    if (options_open) {
        int oi = options_hit(x, y);
        if (oi >= 0) {
            t->kind = TOUCH_UI;
            ui_options_item = oi;
            ui_confirm = 1;
            return;
        }
    }

    if (hit_circle(x, y, opts_x(), opts_y(), 28.0f)) {
        t->kind = TOUCH_OPTS;
        ui_options = 1;
        return;
    }

    if (screen == SCR_TITLE && !options_open) {
        int ti = title_hit(x, y);
        if (ti >= 0) {
            t->kind = TOUCH_UI;
            ui_title_item = ti;
            ui_confirm = 1;
            return;
        }
    }

    if (screen == SCR_CONNECT && !options_open) {
        int ci = connect_list_hit(x, y);
        if (ci >= 0) {
            t->kind = TOUCH_UI;
            ui_connect_sel = ci;
            return;
        }
        if (ci == -2) {
            t->kind = TOUCH_UI;
            ui_confirm = 1;
            return;
        }
        if (ci == -3) {
            t->kind = TOUCH_UI;
            ui_back = 1;
            return;
        }
    }

    if (!touch_want_overlay())
        return;

    /* Virtual pad only useful in play / lobby; still allow on title for familiarity. */
    if (hit_circle(x, y, VPAD_CX, VPAD_CY, VPAD_R)) {
        t->kind = TOUCH_DPAD;
        set_dpad_from_pos(t, x, y);
        recompute_touch_bits();
        return;
    }
    for (size_t i = 0; i < sizeof atk_btns / sizeof atk_btns[0]; i++) {
        if (hit_circle(x, y, atk_btns[i].x, atk_btns[i].y, BTN_R)) {
            t->kind = TOUCH_BTN;
            t->bit = atk_btns[i].bit;
            recompute_touch_bits();
            return;
        }
    }
}

static void touch_move_at(SDL_FingerID id, float x, float y)
{
    TouchSlot *t = touch_find(id);
    if (!t || t->kind != TOUCH_DPAD) return;
    set_dpad_from_pos(t, x, y);
    recompute_touch_bits();
}

/* Caller must run SDL_ConvertEventToRenderCoordinates so coords are logical. */
void touch_handle_event(const SDL_Event *e, int screen)
{
    if (e->type == SDL_EVENT_FINGER_DOWN) {
        touch_down_at(e->tfinger.fingerID, e->tfinger.x, e->tfinger.y, screen);
    } else if (e->type == SDL_EVENT_FINGER_MOTION) {
        touch_move_at(e->tfinger.fingerID, e->tfinger.x, e->tfinger.y);
    } else if (e->type == SDL_EVENT_FINGER_UP || e->type == SDL_EVENT_FINGER_CANCELED) {
        touch_release(e->tfinger.fingerID);
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               e->button.button == SDL_BUTTON_LEFT) {
        mouse_held = 1;
        touch_down_at(mouse_fid, e->button.x, e->button.y, screen);
    } else if (e->type == SDL_EVENT_MOUSE_MOTION && mouse_held) {
        touch_move_at(mouse_fid, e->motion.x, e->motion.y);
    } else if (e->type == SDL_EVENT_MOUSE_BUTTON_UP &&
               e->button.button == SDL_BUTTON_LEFT) {
        mouse_held = 0;
        touch_release(mouse_fid);
    }
}

/* Minimal filled circle via a box approximation (no dependency on SDL_gfx). */
static void soft_circle(float cx, float cy, float r, int R, int G, int B, int A)
{
    SDL_SetRenderDrawColor(renderer, (Uint8)R, (Uint8)G, (Uint8)B, (Uint8)A);
    for (int dy = (int)-r; dy <= (int)r; dy++) {
        float w = sqrtf(r * r - (float)dy * (float)dy);
        SDL_FRect fr = {cx - w, cy + dy, w * 2.0f, 1.0f};
        SDL_RenderFillRect(renderer, &fr);
    }
}

/* Outer ring + translucent fill so the pad stays readable on bright stages. */
static void pad_knob(float cx, float cy, float r, float hole,
                     int fill_r, int fill_g, int fill_b, int fill_a)
{
    soft_circle(cx, cy, r, 245, 245, 255, 230);
    if (hole > 0.0f && hole < r)
        soft_circle(cx, cy, hole, fill_r, fill_g, fill_b, fill_a);
}

void draw_touch_controls(int screen)
{
    (void)screen;
    /* macOS / desktop: never draw. iPhone: always draw when BF_IOS is set. */
    if (!touch_want_overlay())
        return;

    /* Options hit target. */
    soft_circle(opts_x(), opts_y(), 30.0f, 245, 245, 255, 230);
    soft_circle(opts_x(), opts_y(), 24.0f, 40, 48, 80, 220);
    {
        /* Tiny "≡" stand-in */
        SDL_SetRenderDrawColor(renderer, 245, 245, 255, 255);
        for (int i = 0; i < 3; i++) {
            SDL_FRect r = {opts_x() - 12, opts_y() - 8 + i * 8.0f, 24, 3};
            SDL_RenderFillRect(renderer, &r);
        }
    }

    pad_knob(VPAD_CX, VPAD_CY, VPAD_R, VPAD_R - 10.0f, 30, 36, 60, 150);
    soft_circle(VPAD_CX, VPAD_CY, 28.0f, 245, 245, 255, 180);
    for (size_t i = 0; i < sizeof atk_btns / sizeof atk_btns[0]; i++) {
        int held = (touch_bits & atk_btns[i].bit) != 0;
        pad_knob(atk_btns[i].x, atk_btns[i].y, BTN_R, BTN_R - 4.0f,
                 held ? 255 : 50, held ? 120 : 60, held ? 60 : 120,
                 held ? 220 : 170);
    }
}
