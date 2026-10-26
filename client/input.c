/*
 * input.c
 * Keyboard, gamepad, and raw joystick input:
 * - Key bindings for directions and six attack buttons
 * - Gamepad mapping (Xbox-style) and dead-zone handling
 * - Edge detection helpers for menu/actions
 */

#include <stdio.h>
#include "client.h"

/*
 * Keyboard bindings:
 * - Directions on the left hand (arrows or WASD)
 * - Punches: U I O  (alt: Z X C)
 * - Kicks:   J K L  (alt: V B N)
 */
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

/* Initialize input subsystems and open any currently connected devices */
void pads_init(void)
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
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
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (ks[keys[i].sc]) mask |= keys[i].bit;
    return mask;
}
