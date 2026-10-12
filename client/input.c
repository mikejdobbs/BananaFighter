#include "client.h"

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

static void option_activate(int *quit)
{
    switch (options_item) {
    case 0: options_open = 0; break;
    case 1: mastervol = mastervol > 0 ? 0 : SOUND_ON; break;
    case 2: show_boxes ^= 1; break;
    case 3: *quit = 1; break;
    }
}

static void options_input(int edge, int confirm, int *quit)
{
    if (edge & IN_U) options_item = (options_item + 4 - 1) % 4;
    if (edge & IN_D) options_item = (options_item + 1) % 4;
    if (confirm) option_activate(quit);
}
