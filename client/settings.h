/* settings.h - persisted client preferences (keys, volumes, video). */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <SDL3/SDL.h>

enum {
  BIND_LEFT,
  BIND_RIGHT,
  BIND_UP,
  BIND_DOWN,
  BIND_LP,
  BIND_MP,
  BIND_HP,
  BIND_LK,
  BIND_MK,
  BIND_HK,
  BIND_COUNT
};

typedef struct {
  float music_vol; /* 0..1 */
  float sfx_vol;   /* 0..1 */
  int fullscreen;  /* desktop; ignored on iOS */
  int show_boxes;
  SDL_Scancode binds[BIND_COUNT];
} Settings;

extern Settings g_settings;
extern SDL_Window *g_window; /* set by main after create; used to apply video */

void settings_set_defaults(Settings *s);
void settings_load(void);
void settings_save(void);
void settings_apply_audio(void);
void settings_apply_video(void);
void settings_reset_and_save(void);

const char *bind_action_name(int bind);
int bind_to_input_bit(int bind);

#endif
