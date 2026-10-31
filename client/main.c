#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "client.h"
#include "server/embed.h"

int main(int argc, char **argv)
{
    const char *session_key = getenv("BANANAFIGHTER_KEY");
    const char *host_arg = NULL;
    const char *port_arg = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-k") && i + 1 < argc) {
            session_key = argv[++i];
            continue;
        }
        if (!host_arg)
            host_arg = argv[i];
        else if (!port_arg)
            port_arg = argv[i];
    }
    int hosting = !host_arg;
    const char *serverHost = hosting ? "127.0.0.1" : host_arg;
    char serverPort[8];
    snprintf(serverPort, sizeof serverPort, "%d",
             port_arg ? atoi(port_arg) : PORT);

    if (network_init() != 0) {
        fprintf(stderr, "crypto init failed\n");
        return 1;
    }
    if (session_key && session_key[0])
        network_set_session_password(session_key);

    if (hosting) {
        if (server_embed_start() < 0) {
            fprintf(stderr, "cannot start embedded server on port %s\n", serverPort);
            return 1;
        }
    }

    if ((networkSocketFd = dial(serverHost, serverPort)) < 0) {
        fprintf(stderr, "cannot connect to %s:%s\n", serverHost, serverPort);
        if (hosting) server_embed_stop();
        return 1;
    }
    if (!network_handshake()) {
        fprintf(stderr, "encrypted handshake failed\n");
        if (hosting) server_embed_stop();
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL init error: %s\n", SDL_GetError()); if (hosting) server_embed_stop(); return 1; }
    SDL_Window *window;
    
    if (!SDL_CreateWindowAndRenderer("BananaFighter", SW, SH, 0, &window, &renderer)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderVSync(renderer, 1);
    load_sprites();
    pads_init();
    audio_init();

    const Uint64 fixedDeltaNs = SDL_NS_PER_SECOND / TICK_HZ;
    
    Uint64 accumulatedNs = 0, previousTickNs = SDL_GetTicksNS();
    int cursorIndex = 0, aiSelectionIndex = 0, shouldQuit = 0, previousInputMask = 0;     /* cursor character, the next bot's AI */

    while (!shouldQuit) {
        int returnKeyPressed = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) shouldQuit = 1;
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
                if (k == SDLK_RETURN) option_activate(&shouldQuit);
                continue;
            }
            if (phase != 'L') continue;
            if (k == SDLK_RETURN) returnKeyPressed = 1;
            if (k == SDLK_LEFTBRACKET && nai)  aiSelectionIndex = (aiSelectionIndex + nai - 1) % nai;
            if (k == SDLK_RIGHTBRACKET && nai) aiSelectionIndex = (aiSelectionIndex + 1) % nai;
            if (k == SDLK_T) { sendf("B %d %d\n", cursorIndex, aiSelectionIndex); sfx(SN_BOT, 0.8f); }   /* B is a kick key */
            if (k == SDLK_BACKSPACE)
                for (int i = nl - 1; i >= 0; i--)
                    if (U[i].bot) { sendf("X %d\n", U[i].slot); break; }
            if (k == SDLK_DELETE)
                for (int i = 0; i < nl; i++)
                    if (U[i].bot) sendf("X %d\n", U[i].slot);
        }

        if (!pump()) { fprintf(stderr, "server closed the connection\n"); break; }

        /* newly pressed directions/buttons from the keyboard and every pad */
        int currentInputMask = keymask() | padmask(), inputEdge = currentInputMask & ~previousInputMask;
        previousInputMask = currentInputMask;
        int padStartEdge = pad_edge(SDL_GAMEPAD_BUTTON_START), padBackEdge = pad_edge(SDL_GAMEPAD_BUTTON_BACK);
        int padLeftShoulderEdge = pad_edge(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), padRightShoulderEdge = pad_edge(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

        int menuFrameBeganOpen = options_open;              /* a frame that began in the menu never also acts on the select screen */
        if (options_open) {
            if (padStartEdge) options_open = 0;
            else options_input(inputEdge, !!(inputEdge & IN_LK), &shouldQuit);
        } else if (phase == 'S' && padStartEdge) {       /* Start opens the menu during a fight */
            options_open = 1;
            options_item = OPT_RESUME;
            menuFrameBeganOpen = 1;
        }

        if (phase == 'L' && !options_open && !menuFrameBeganOpen) {
            const Lob *myLobbyEntry = NULL;
            for (int i = 0; i < nl; i++)
                if (U[i].slot == me) myLobbyEntry = &U[i];
            int isLockedIn = myLobbyEntry && myLobbyEntry->ready;
            if (!isLockedIn) {                          /* a locked-in fighter can't move the cursor */
                int previousCursorIndex = cursorIndex;
                if (inputEdge & IN_L) cursorIndex = (cursorIndex + NCHARS - 1) % NCHARS;
                if (inputEdge & IN_R) cursorIndex = (cursorIndex + 1) % NCHARS;
                if (inputEdge & IN_U) cursorIndex = (cursorIndex + NCHARS - SEL_COLS) % NCHARS;
                if (inputEdge & IN_D) cursorIndex = (cursorIndex + SEL_COLS) % NCHARS;
                if (cursorIndex != previousCursorIndex) { sendf("J %d\n", cursorIndex); sfx(SN_MOVE, 0.6f); }
            }
            if ((inputEdge & (IN_P | IN_K)) || returnKeyPressed || padStartEdge) {
                if (!isLockedIn) sendf("J %d\n", cursorIndex);
                sendf("R\n");
                sfx(isLockedIn ? SN_SELECT : SN_READY, 0.9f);
            }
            if (padBackEdge) { sendf("B %d %d\n", cursorIndex, aiSelectionIndex); sfx(SN_BOT, 0.8f); }
            if (padLeftShoulderEdge && nai) aiSelectionIndex = (aiSelectionIndex + nai - 1) % nai;
            if (padRightShoulderEdge && nai) aiSelectionIndex = (aiSelectionIndex + 1) % nai;
        }

        Uint64 nowNs = SDL_GetTicksNS();
        accumulatedNs += nowNs - previousTickNs;
        previousTickNs = nowNs;
        if (accumulatedNs > 5 * fixedDeltaNs) accumulatedNs = 5 * fixedDeltaNs;             /* don't spiral after a stall */
        for (; accumulatedNs >= fixedDeltaNs; accumulatedNs -= fixedDeltaNs) { client_tick(); anim_tick(); }

        music(phase == 'S' ? SN_MUSIC_FIGHT : SN_MUSIC_MENU);
        audio_pump();

        SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
        SDL_RenderClear(renderer);
        if (phase == 'S') draw_match(); else draw_select(cursorIndex, aiSelectionIndex);
        if (options_open) draw_options();
        SDL_RenderPresent(renderer);
    }
    close(networkSocketFd);
    if (hosting) server_embed_stop();
    SDL_Quit();
    return 0;
}
