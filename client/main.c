#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "client.h"

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
