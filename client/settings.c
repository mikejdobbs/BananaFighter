/*
 * settings.c
 * Load / save client preferences under SDL_GetPrefPath, and apply
 * audio + video changes immediately.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "client.h"
#include "settings.h"

Settings g_settings;
SDL_Window *g_window;

static const char *bind_keys[BIND_COUNT] = {
    "bind_left", "bind_right", "bind_up", "bind_down",
    "bind_lp",   "bind_mp",    "bind_hp", "bind_lk",
    "bind_mk",   "bind_hk",
};

static const char *bind_names[BIND_COUNT] = {
    "Left", "Right", "Up", "Down",
    "Light Punch", "Medium Punch", "Heavy Punch",
    "Light Kick", "Medium Kick", "Heavy Kick",
};

static const int bind_bits[BIND_COUNT] = {
    IN_L, IN_R, IN_U, IN_D, IN_LP, IN_MP, IN_HP, IN_LK, IN_MK, IN_HK,
};

const char *bind_action_name(int bind)
{
    if (bind < 0 || bind >= BIND_COUNT)
        return "?";
    return bind_names[bind];
}

int bind_to_input_bit(int bind)
{
    if (bind < 0 || bind >= BIND_COUNT)
        return 0;
    return bind_bits[bind];
}

void settings_set_defaults(Settings *s)
{
    s->music_vol = 1.0f;
    s->sfx_vol = 1.0f;
    s->fullscreen = 0;
    s->show_boxes = 0;
    s->binds[BIND_LEFT] = SDL_SCANCODE_A;
    s->binds[BIND_RIGHT] = SDL_SCANCODE_D;
    s->binds[BIND_UP] = SDL_SCANCODE_W;
    s->binds[BIND_DOWN] = SDL_SCANCODE_S;
    s->binds[BIND_LP] = SDL_SCANCODE_U;
    s->binds[BIND_MP] = SDL_SCANCODE_I;
    s->binds[BIND_HP] = SDL_SCANCODE_O;
    s->binds[BIND_LK] = SDL_SCANCODE_J;
    s->binds[BIND_MK] = SDL_SCANCODE_K;
    s->binds[BIND_HK] = SDL_SCANCODE_L;
}

static char *settings_path(void)
{
    char *pref = SDL_GetPrefPath("BananaFighter", "BananaFighter");
    char *path;
    size_t n;
    if (!pref)
        return NULL;
    n = strlen(pref) + 16;
    path = (char *)malloc(n);
    if (!path) {
        SDL_free(pref);
        return NULL;
    }
    snprintf(path, n, "%ssettings.cfg", pref);
    SDL_free(pref);
    return path;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

void settings_load(void)
{
    char *path = settings_path();
    FILE *f;
    char line[256];

    settings_set_defaults(&g_settings);
    if (!path)
        return;
    f = fopen(path, "r");
    free(path);
    if (!f)
        return;

    while (fgets(line, sizeof line, f)) {
        char key[64];
        char val[64];
        float fv;
        int iv;
        if (sscanf(line, "%63[^=]=%63s", key, val) != 2)
            continue;
        if (strcmp(key, "music") == 0 && sscanf(val, "%f", &fv) == 1)
            g_settings.music_vol = clampf(fv, 0.f, 1.f);
        else if (strcmp(key, "sfx") == 0 && sscanf(val, "%f", &fv) == 1)
            g_settings.sfx_vol = clampf(fv, 0.f, 1.f);
        else if (strcmp(key, "fullscreen") == 0 && sscanf(val, "%d", &iv) == 1)
            g_settings.fullscreen = iv ? 1 : 0;
        else if (strcmp(key, "show_boxes") == 0 && sscanf(val, "%d", &iv) == 1)
            g_settings.show_boxes = iv ? 1 : 0;
        else {
            for (int i = 0; i < BIND_COUNT; i++) {
                if (strcmp(key, bind_keys[i]) == 0 && sscanf(val, "%d", &iv) == 1) {
                    if (iv > 0 && iv < SDL_SCANCODE_COUNT)
                        g_settings.binds[i] = (SDL_Scancode)iv;
                    break;
                }
            }
        }
    }
    fclose(f);
}

void settings_save(void)
{
    char *path = settings_path();
    FILE *f;
    if (!path)
        return;
    f = fopen(path, "w");
    if (!f) {
        free(path);
        return;
    }
    fprintf(f, "music=%.2f\n", g_settings.music_vol);
    fprintf(f, "sfx=%.2f\n", g_settings.sfx_vol);
    fprintf(f, "fullscreen=%d\n", g_settings.fullscreen ? 1 : 0);
    fprintf(f, "show_boxes=%d\n", g_settings.show_boxes ? 1 : 0);
    for (int i = 0; i < BIND_COUNT; i++)
        fprintf(f, "%s=%d\n", bind_keys[i], (int)g_settings.binds[i]);
    fclose(f);
    free(path);
}

void settings_apply_audio(void)
{
    music_refresh_vol();
}

void settings_apply_video(void)
{
#ifdef BF_IOS
    return;
#else
    if (!g_window)
        return;
    if (g_settings.fullscreen) {
        SDL_SetWindowFullscreen(g_window, true);
    } else {
        SDL_SetWindowFullscreen(g_window, false);
        SDL_SetWindowSize(g_window, SW, SH);
    }
    /* Logical 1280x720 + letterbox already set; scales to any display. */
#endif
}

void settings_reset_and_save(void)
{
    settings_set_defaults(&g_settings);
    show_boxes = g_settings.show_boxes;
    settings_apply_audio();
    settings_apply_video();
    settings_save();
}
