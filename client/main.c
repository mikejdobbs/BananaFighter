#ifdef BF_IOS
#  include <SDL3/SDL_main.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"
#include "lan.h"
#include "server/embed.h"

enum { MODE_NONE, MODE_SINGLE, MODE_HOST, MODE_JOIN };

static int screen = SCR_TITLE;
static int title_item;
static int connect_sel;
static int connect_addr_dirty; /* 1 after the user types; LAN pick won't overwrite */
static int session_mode = MODE_NONE;
static int hosting;
static int need_sp_bot;
static char connect_addr[128];
static char connect_status[128];
static char cli_host[128];
static char cli_port[16];
static SDL_Window *window;
static int app_in_background;

/* Tear down client sockets and any embedded listen server (single / host). */
static void end_session(void)
{
    network_disconnect();
    /* Always stop: safe no-op if we never started; do not rely on `hosting`
     * alone so Main Menu always cancels a local game. */
    server_embed_stop();
    server_embed_set_lan_advertise(0);
    lan_stop();
    hosting = 0;
    session_mode = MODE_NONE;
    need_sp_bot = 0;
}

/* Leave play (or a failed connect) and return to the title screen. */
static void leave_to_title(void)
{
    end_session();
    options_open = 0;
    options_rebinding = -1;
    options_submenu = OPTMENU_PAUSE;
    connect_status[0] = 0;
    screen = SCR_TITLE;
    touch_clear();
    SDL_StopTextInput(NULL);
}

/* Parse "host", "host:port", or "[ipv6]:port" into host/port buffers. */
static int parse_addr(const char *in, char *host, size_t host_n, char *port, size_t port_n)
{
    if (!in || !in[0])
        return 0;
    const char *colon = strrchr(in, ':');
    if (colon && colon != in && strchr(colon + 1, '.') == NULL) {
        /* host:port when port is all digits */
        int all_digit = 1;
        for (const char *p = colon + 1; *p; p++)
            if (*p < '0' || *p > '9')
                all_digit = 0;
        if (all_digit && colon[1]) {
            size_t hn = (size_t)(colon - in);
            if (hn >= host_n)
                hn = host_n - 1;
            memcpy(host, in, hn);
            host[hn] = 0;
            snprintf(port, port_n, "%s", colon + 1);
            return 1;
        }
    }
    snprintf(host, host_n, "%s", in);
    snprintf(port, port_n, "%d", PORT);
    return 1;
}

static int begin_session(int mode, const char *host, const char *port)
{
    char dial_host[128];
    char dial_port[16];

    end_session();

    session_mode = mode;
    need_sp_bot = mode == MODE_SINGLE;
    hosting = (mode == MODE_SINGLE || mode == MODE_HOST);

    if (hosting) {
        server_embed_set_lan_advertise(mode == MODE_HOST);
        if (server_embed_start() < 0) {
            snprintf(connect_status, sizeof connect_status,
                     "cannot start local server on %d", PORT);
            end_session();
            return 0;
        }
        snprintf(dial_host, sizeof dial_host, "127.0.0.1");
        snprintf(dial_port, sizeof dial_port, "%d", PORT);
    } else {
        server_embed_set_lan_advertise(0);
        snprintf(dial_host, sizeof dial_host, "%s", host);
        snprintf(dial_port, sizeof dial_port, "%s", port);
    }

    if ((networkSocketFd = dial(dial_host, dial_port)) == BF_INVALID_SOCKET) {
        snprintf(connect_status, sizeof connect_status, "cannot connect to %s:%s", dial_host,
                 dial_port);
        end_session();
        return 0;
    }
    if (!network_handshake()) {
        const char *err = network_handshake_error();
        snprintf(connect_status, sizeof connect_status, "%s",
                 err && err[0] ? err : "encrypted handshake failed");
        end_session();
        return 0;
    }

    lan_stop();
    SDL_StopTextInput(NULL);
    touch_clear();
    connect_status[0] = 0;
    screen = SCR_PLAY;
    return 1;
}

static void try_connect_addr(void)
{
    char host[128], port[16];
    if (!parse_addr(connect_addr, host, sizeof host, port, sizeof port)) {
        snprintf(connect_status, sizeof connect_status, "enter a host or host:port");
        return;
    }
    if (!begin_session(MODE_JOIN, host, port))
        screen = SCR_CONNECT;
}

static void maybe_add_sp_bot(void)
{
    if (!need_sp_bot || phase != 'L' || nai <= 0)
        return;
    for (int i = 0; i < nl; i++)
        if (U[i].bot) {
            need_sp_bot = 0;
            return;
        }
    int my_ch = 0;
    for (int i = 0; i < nl; i++)
        if (U[i].slot == me)
            my_ch = U[i].ch;
    int bot_ch = (my_ch + 1) % NCHARS;
    sendf("B %d %d\n", bot_ch, 0);
    need_sp_bot = 0;
}

/* iOS lifecycle events are delivered through the event watch, not PollEvent. */
static bool lifecycle_watch(void *userdata, SDL_Event *e)
{
    (void)userdata;
    if (e->type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
        e->type == SDL_EVENT_DID_ENTER_BACKGROUND) {
        app_in_background = 1;
        audio_set_suspended(1);
    } else if (e->type == SDL_EVENT_WILL_ENTER_FOREGROUND ||
               e->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        app_in_background = 0;
        audio_set_suspended(0);
    }
    return true;
}

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

    if (network_init() != 0) {
        fprintf(stderr, "crypto init failed\n");
        return 1;
    }
    if (session_key && session_key[0]) {
        network_set_session_password(session_key);
        server_embed_set_session_password(session_key);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL init error: %s\n", SDL_GetError());
        return 1;
    }
    settings_load();
    show_boxes = g_settings.show_boxes;
    SDL_AddEventWatch(lifecycle_watch, NULL);

    SDL_WindowFlags win_flags = 0;
#ifdef BF_IOS
    win_flags = SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
    if (g_settings.fullscreen)
        win_flags = SDL_WINDOW_FULLSCREEN;
#endif
    if (!SDL_CreateWindowAndRenderer("BananaFighter", SW, SH, win_flags, &window, &renderer)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    g_window = window;
    SDL_SetRenderLogicalPresentation(renderer, SW, SH, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    /* Window / taskbar icon (Dock also uses the .icns on macOS bundles) */
    {
        char path[512];
        const char *base = SDL_GetBasePath();
        SDL_Surface *icon = NULL;
        if (base) {
            snprintf(path, sizeof path, "%sicons/icon.png", base);
            icon = SDL_LoadPNG(path);
        }
        if (!icon)
            icon = SDL_LoadPNG("icons/icon.png");
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_DestroySurface(icon);
        }
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderVSync(renderer, 1);
    load_sprites();
    pads_init();
    audio_init();

    /* CLI host/port jumps straight into a join session. */
    if (host_arg) {
        snprintf(cli_host, sizeof cli_host, "%s", host_arg);
        snprintf(cli_port, sizeof cli_port, "%d", port_arg ? atoi(port_arg) : PORT);
        snprintf(connect_addr, sizeof connect_addr, "%s:%s", cli_host, cli_port);
        if (!begin_session(MODE_JOIN, cli_host, cli_port)) {
            fprintf(stderr, "%s\n", connect_status);
            SDL_Quit();
            return 1;
        }
    }

    const Uint64 fixedDeltaNs = SDL_NS_PER_SECOND / TICK_HZ;
    Uint64 accumulatedNs = 0, previousTickNs = SDL_GetTicksNS();
    int cursorIndex = 0, aiSelectionIndex = 0, shouldQuit = 0, previousInputMask = 0;

    while (!shouldQuit) {
        int returnKeyPressed = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT)
                shouldQuit = 1;
            else if (e.type == SDL_EVENT_GAMEPAD_ADDED)
                pad_open(e.gdevice.which);
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED)
                pad_close(e.gdevice.which);
            else if (e.type == SDL_EVENT_JOYSTICK_ADDED)
                joy_open(e.jdevice.which);
            else if (e.type == SDL_EVENT_JOYSTICK_REMOVED)
                joy_close(e.jdevice.which);
            else if (e.type == SDL_EVENT_TEXT_INPUT && screen == SCR_CONNECT) {
                if (strlen(connect_addr) + strlen(e.text.text) < sizeof connect_addr - 1) {
                    strncat(connect_addr, e.text.text,
                            sizeof connect_addr - strlen(connect_addr) - 1);
                    connect_addr_dirty = 1;
                }
                continue;
            }

            /* Touch / mouse in logical render space for virtual pad + UI taps. */
            if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_UP ||
                e.type == SDL_EVENT_FINGER_MOTION || e.type == SDL_EVENT_FINGER_CANCELED ||
                e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                e.type == SDL_EVENT_MOUSE_MOTION) {
                SDL_ConvertEventToRenderCoordinates(renderer, &e);
                touch_handle_event(&e, screen);
                continue;
            }

            if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat)
                continue;
            SDL_Keycode k = e.key.key;

            if (screen == SCR_CONNECT) {
                if (k == SDLK_ESCAPE) {
                    lan_stop();
                    SDL_StopTextInput(NULL);
                    screen = SCR_TITLE;
                    connect_status[0] = 0;
                    continue;
                }
                if (k == SDLK_BACKSPACE && connect_addr[0]) {
                    connect_addr[strlen(connect_addr) - 1] = 0;
                    connect_addr_dirty = 1;
                    continue;
                }
                if (k == SDLK_RETURN) {
                    try_connect_addr();
                    continue;
                }
            }

            if (options_open && options_rebinding >= 0) {
                if (k == SDLK_ESCAPE) {
                    options_rebinding = -1;
                    continue;
                }
                if (input_rebind_key(e.key.scancode))
                    continue;
            }
            if (k == SDLK_ESCAPE) {
                if (options_open) {
                    options_back();
                    continue;
                }
                if (screen == SCR_TITLE) {
                    shouldQuit = 1;
                    continue;
                }
                if (screen == SCR_PLAY) {
                    options_open = 1;
                    options_in_play = 1;
                    options_submenu = OPTMENU_PAUSE;
                    options_item = OPT_RESUME;
                    options_rebinding = -1;
                    options_scroll = 0;
                    options_ensure_scroll();
                }
                continue;
            }
            if (k == SDLK_F1) {
                show_boxes ^= 1;
                g_settings.show_boxes = show_boxes;
                settings_save();
            }
            if (k == SDLK_M)
                mastervol = mastervol > 0 ? 0 : 1.0f;
            if (options_open) {
                if (k == SDLK_RETURN) {
                    int leaveTitle = 0;
                    option_activate(&leaveTitle);
                    if (leaveTitle && screen == SCR_PLAY)
                        leave_to_title();
                }
                continue;
            }
            if (k == SDLK_RETURN && (screen == SCR_TITLE || screen == SCR_PLAY))
                returnKeyPressed = 1;
            if (screen != SCR_PLAY || phase != 'L')
                continue;
            if (k == SDLK_LEFTBRACKET && nai)
                aiSelectionIndex = (aiSelectionIndex + nai - 1) % nai;
            if (k == SDLK_RIGHTBRACKET && nai)
                aiSelectionIndex = (aiSelectionIndex + 1) % nai;
            if (k == SDLK_T) {
                sendf("B %d %d\n", cursorIndex, aiSelectionIndex);
                sfx(SN_BOT, 0.8f);
            }
            /* Remove the bot standing on the highlighted fighter, not the last one added. */
            if (k == SDLK_BACKSPACE || k == SDLK_DELETE)
                for (int i = 0; i < nl; i++)
                    if (U[i].bot && U[i].ch == cursorIndex) {
                        sendf("X %d\n", U[i].slot);
                        break;
                    }
        }

        if (screen == SCR_CONNECT)
            lan_pump();

        if (screen == SCR_PLAY) {
            if (!pump()) {
                leave_to_title();
                snprintf(connect_status, sizeof connect_status, "disconnected");
            } else {
                maybe_add_sp_bot();
            }
        }

        int currentInputMask = keymask() | padmask() | touchmask(),
            inputEdge = currentInputMask & ~previousInputMask;
        previousInputMask = currentInputMask;
        int padStartEdge = pad_edge(SDL_GAMEPAD_BUTTON_START),
            padBackEdge = pad_edge(SDL_GAMEPAD_BUTTON_BACK);
        int padLeftShoulderEdge = pad_edge(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),
            padRightShoulderEdge = pad_edge(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        int confirm = !!(inputEdge & IN_LK) || touch_ui_confirm();
        int touchBack = touch_ui_back();
        int touchOpts = touch_ui_options();
        int tappedTitle = touch_title_item();
        int tappedConnect = touch_connect_sel();
        int tappedOpt = touch_options_item();

        if (touchOpts && screen == SCR_CONNECT) {
            lan_stop();
            SDL_StopTextInput(NULL);
            screen = SCR_TITLE;
            connect_status[0] = 0;
        }
        if (tappedOpt >= 0)
            options_item = tappedOpt;
        if (tappedTitle >= 0)
            title_item = tappedTitle;
        if (tappedConnect >= 0) {
            connect_sel = tappedConnect;
            connect_addr_dirty = 0;
        }

        int menuFrameBeganOpen = options_open;
        if (options_open) {
            if ((padStartEdge || touchOpts) && options_rebinding < 0) {
                options_back();
            } else {
                int leaveTitle = 0;
                options_input(inputEdge, confirm, &leaveTitle);
                if (leaveTitle && screen == SCR_PLAY)
                    leave_to_title();
            }
        } else if (screen == SCR_PLAY && (padStartEdge || touchOpts) &&
                   (phase == 'S' || phase == 'L')) {
            options_open = 1;
            options_in_play = 1;
            options_submenu = OPTMENU_PAUSE;
            options_item = OPT_RESUME;
            options_rebinding = -1;
            options_scroll = 0;
            options_ensure_scroll();
            menuFrameBeganOpen = 1;
        }

        if (screen == SCR_TITLE && !options_open) {
            if (inputEdge & IN_U)
                title_item = (title_item + TITLE_COUNT - 1) % TITLE_COUNT;
            if (inputEdge & IN_D)
                title_item = (title_item + 1) % TITLE_COUNT;
            if (confirm || returnKeyPressed || padStartEdge) {
                sfx(SN_SELECT, 0.8f);
                if (title_item == TITLE_QUIT) {
                    shouldQuit = 1;
                } else if (title_item == TITLE_OPTIONS) {
                    options_open = 1;
                    options_in_play = 0;
                    options_submenu = OPTMENU_SETTINGS;
                    options_item = OPT_RESUME;
                    options_rebinding = -1;
                    options_scroll = 0;
                    options_ensure_scroll();
                } else if (title_item == TITLE_CONNECT) {
                    screen = SCR_CONNECT;
                    connect_sel = 0;
                    connect_addr[0] = 0;
                    connect_addr_dirty = 0;
                    connect_status[0] = 0;
                    lan_start();
                    SDL_StartTextInput(window);
                } else if (title_item == TITLE_SINGLE) {
                    char port[8];
                    snprintf(port, sizeof port, "%d", PORT);
                    if (!begin_session(MODE_SINGLE, "127.0.0.1", port))
                        screen = SCR_TITLE;
                } else if (title_item == TITLE_HOST) {
                    char port[8];
                    snprintf(port, sizeof port, "%d", PORT);
                    if (!begin_session(MODE_HOST, "127.0.0.1", port))
                        screen = SCR_TITLE;
                }
            }
        } else if (screen == SCR_CONNECT) {
            int n = lan_count();
            int prev_sel = connect_sel;
            if (inputEdge & IN_U && n > 0)
                connect_sel = (connect_sel + n - 1) % n;
            if (inputEdge & IN_D && n > 0)
                connect_sel = (connect_sel + 1) % n;
            if (n > 0 && connect_sel >= n)
                connect_sel = n - 1;
            int pick = n > 0 && (connect_sel != prev_sel || !connect_addr_dirty);
            if (pick) {
                const LanServer *s = lan_at(connect_sel);
                if (s) {
                    snprintf(connect_addr, sizeof connect_addr, "%s:%d", s->host, s->port);
                    if (connect_sel != prev_sel)
                        connect_addr_dirty = 0;
                }
            }
            if (confirm || padStartEdge)
                try_connect_addr();
            if (padBackEdge || touchBack) {
                lan_stop();
                SDL_StopTextInput(NULL);
                screen = SCR_TITLE;
            }
        } else if (screen == SCR_PLAY && phase == 'L' && !options_open && !menuFrameBeganOpen) {
            const Lob *myLobbyEntry = NULL;
            for (int i = 0; i < nl; i++)
                if (U[i].slot == me)
                    myLobbyEntry = &U[i];
            int isLockedIn = myLobbyEntry && myLobbyEntry->ready;
            if (!isLockedIn) {
                int previousCursorIndex = cursorIndex;
                if (inputEdge & IN_L)
                    cursorIndex = (cursorIndex + NCHARS - 1) % NCHARS;
                if (inputEdge & IN_R)
                    cursorIndex = (cursorIndex + 1) % NCHARS;
                if (inputEdge & IN_U)
                    cursorIndex = (cursorIndex + NCHARS - SEL_COLS) % NCHARS;
                if (inputEdge & IN_D)
                    cursorIndex = (cursorIndex + SEL_COLS) % NCHARS;
                if (cursorIndex != previousCursorIndex) {
                    sendf("J %d\n", cursorIndex);
                    sfx(SN_MOVE, 0.6f);
                }
            }
            if ((inputEdge & (IN_P | IN_K)) || returnKeyPressed || padStartEdge) {
                if (!isLockedIn)
                    sendf("J %d\n", cursorIndex);
                sendf("R\n");
                sfx(isLockedIn ? SN_SELECT : SN_READY, 0.9f);
            }
            if (padBackEdge) {
                sendf("B %d %d\n", cursorIndex, aiSelectionIndex);
                sfx(SN_BOT, 0.8f);
            }
            if (padLeftShoulderEdge && nai)
                aiSelectionIndex = (aiSelectionIndex + nai - 1) % nai;
            if (padRightShoulderEdge && nai)
                aiSelectionIndex = (aiSelectionIndex + 1) % nai;
        }

        Uint64 nowNs = SDL_GetTicksNS();
        accumulatedNs += nowNs - previousTickNs;
        previousTickNs = nowNs;
        if (accumulatedNs > 5 * fixedDeltaNs)
            accumulatedNs = 5 * fixedDeltaNs;

        /* While backgrounded, skip simulation ticks so UDP timeouts can settle
           without burning CPU; pump() still ran above when on SCR_PLAY. */
        if (!app_in_background) {
            if (screen == SCR_PLAY) {
                for (; accumulatedNs >= fixedDeltaNs; accumulatedNs -= fixedDeltaNs) {
                    client_tick();
                    anim_tick();
                }
            } else if (screen == SCR_TITLE) {
                for (; accumulatedNs >= fixedDeltaNs; accumulatedNs -= fixedDeltaNs)
                    aclk++;
            } else {
                accumulatedNs = 0;
            }

            SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
            SDL_RenderClear(renderer);
            if (screen == SCR_TITLE)
                draw_title(title_item);
            else if (screen == SCR_CONNECT)
                draw_connect(connect_sel, connect_addr, connect_status);
            else if (phase == 'S')
                draw_match();
            else
                draw_select(cursorIndex, aiSelectionIndex);
            if (options_open)
                draw_options();
            draw_touch_controls(screen);
            /* Music after draw so match stage is latched before fight BGM picks. */
            if (screen == SCR_PLAY && phase == 'S')
                music(music_for_stage(match_stage_for_music()));
            else
                music(SN_MUSIC_MENU);
            audio_pump();
            SDL_RenderPresent(renderer);
        } else {
            accumulatedNs = 0;
            SDL_Delay(50);
        }
    }

    end_session();
    SDL_RemoveEventWatch(lifecycle_watch, NULL);
    SDL_Quit();
    return 0;
}
