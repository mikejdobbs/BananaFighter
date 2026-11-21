/*
 * audio.c
 * Audio system:
 * - Load WAV sounds (SFX mono / music stereo, 16-bit @ AUDIO_RATE) and convert if needed
 * - Stereo voice mixer: music is stereo, SFX are centered mono
 * - Music selection, snapshot-driven effects, and per-frame pumping
 */

#include <stdint.h>
#include <stdio.h>
#include "client.h"
#include "server/embed.h"

/* Basenames for sound assets in sounds/ (without .wav) */
static const char *sndfile[SN_COUNT] = {
    "swing1",      "swing2",      "swing3",     "hit1",    "hit2",
    "hit3",        "block",       "shot",       "jump",    "land",
    "fly",         "boom",        "bomb",       "ship",    "ko",
    "laugh",
    "menu_move",   "menu_select", "menu_ready", "bot_add", "fight",
    "music_menu",
    "music_fight_stadium",
    "music_fight_slurpy", "music_fight_alien", "music_fight_rump", "music_fight_jv",
    "music_fight_kammy", "music_fight_flusk", "music_fight_stephen",
    "music_fight_lopicardo", "music_fight_mike", "music_fight_krampus",
    "music_fight_kevkev", "music_fight_rose",
};

typedef struct {
  Sint16 *pcm;   /* Interleaved 16-bit samples (mono or stereo) */
  Uint32 n;      /* Frame count (one frame = channels samples) */
  int ch;        /* 1 = mono SFX, 2 = stereo music */
} Snd;

typedef struct {
  int s;         /* Sound index (SN_*) */
  int on;        /* 1 while playing */
  int loop;      /* Loop when reaching the end */
  Uint32 pos;    /* Current frame position */
  float vol;     /* Volume (0..1) */
} Voice;

static Snd snd[SN_COUNT];              /* Loaded sounds */
static Voice vox[NVOX];                /* Voice 0 is music; others are SFX */
static SDL_AudioStream *astream;       /* Output stream (NULL until open thread finishes) */
static int cur_music = -1;             /* Currently selected music track */
static int want_suspended;             /* Lifecycle pause requested before/during open */
static int intro_active;               /* match start gated on the fight announcer */
static int intro_pending;              /* waiting for the audio device to open */
static int intro_vox = -1;             /* voice playing SN_FIGHT, or -1 */
float mastervol = 1.0f;                /* Mute switch (0 or 1); M key toggles */

/* Load a WAV by name; convert to S16 @ AUDIO_RATE. Keep stereo for music, mono for SFX. */
static int load_snd(int i)
{
    char path[512];
    const char *base = SDL_GetBasePath();
    snprintf(path, sizeof path, "%ssounds/%s.wav", base ? base : "", sndfile[i]);
    SDL_AudioSpec spec;
    Uint8 *buf;
    Uint32 len;
    if (!SDL_LoadWAV(path, &spec, &buf, &len)) {
        snprintf(path, sizeof path, "sounds/%s.wav", sndfile[i]);
        if (!SDL_LoadWAV(path, &spec, &buf, &len)) return 0;
    }
    int music = (i >= SN_MUSIC_MENU);
    int ch = music ? (spec.channels >= 2 ? 2 : 2) : 1;
    /* Mono music WAVs (legacy) are upmixed to stereo; stereo SFX downmix to mono. */
    if (!music && spec.channels > 1)
        ch = 1;
    if (music && spec.channels < 2)
        ch = 2;
    SDL_AudioSpec want = { SDL_AUDIO_S16, ch, AUDIO_RATE };
    if (spec.format != want.format || spec.channels != want.channels || spec.freq != AUDIO_RATE) {
        Uint8 *cv;
        int cl;
        if (!SDL_ConvertAudioSamples(&spec, buf, (int)len,
                                     &want, &cv, &cl)) {
            SDL_free(buf);
            return 0;
        }
        SDL_free(buf);
        buf = cv;
        len = (Uint32)cl;
    }
    snd[i].pcm = (Sint16 *)buf;
    snd[i].ch = ch;
    snd[i].n = len / (Uint32)(2 * ch); /* frames */
    return 1;
}

/* Open + resume off the main thread so iOS AVAudioSession activate does not
 * stall UI. SDL documents OpenAudioDeviceStream as thread-safe. */
static int SDLCALL audio_open_thread(void *userdata)
{
    SDL_AudioSpec spec = { SDL_AUDIO_S16, AUDIO_CH, AUDIO_RATE };
    SDL_AudioStream *s;
    (void)userdata;
    s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!s) {
        fprintf(stderr, "no audio device: %s\n", SDL_GetError());
        return 1;
    }
    SDL_ResumeAudioStreamDevice(s);
    astream = s;
    if (want_suspended)
        SDL_PauseAudioStreamDevice(s);
    return 0;
}

static int SDLCALL audio_suspend_thread(void *userdata)
{
    int suspended = (int)(intptr_t)userdata;
    SDL_AudioStream *s = astream;
    if (!s)
        return 0;
    if (suspended)
        SDL_PauseAudioStreamDevice(s);
    else
        SDL_ResumeAudioStreamDevice(s);
    return 0;
}

static void audio_run_detached(SDL_ThreadFunction fn, const char *name, void *userdata)
{
    SDL_Thread *t = SDL_CreateThread(fn, name, userdata);
    if (t) {
        SDL_DetachThread(t);
        return;
    }
    /* Fallback: run inline if the thread cannot be created. */
    fn(userdata);
}

void audio_init(void)
{
    int got = 0;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "no audio: %s\n", SDL_GetError());
        return;
    }
    for (int i = 0; i < SN_COUNT; i++)
        got += load_snd(i);
    if (got < SN_COUNT)
        fprintf(stderr, "loaded %d of %d sounds (run make_sounds.py)\n", got, SN_COUNT);
    audio_run_detached(audio_open_thread, "bf_audio_open", NULL);
}

void audio_set_suspended(int suspended)
{
    want_suspended = suspended ? 1 : 0;
    if (!astream)
        return; /* open thread applies want_suspended when it finishes */
    audio_run_detached(audio_suspend_thread, "bf_audio_susp",
                       (void *)(intptr_t)want_suspended);
}

/* Play a one-shot on a free (or stolen) SFX voice; returns voice index or -1 */
static int sfx_voice(int s, float vol)
{
    static int rr = 1;
    if (!astream || !snd[s].pcm) return -1;
    int v = 1;
    while (v < NVOX && vox[v].on) v++;
    if (v == NVOX) { v = rr; rr = rr % (NVOX - 1) + 1; }       /* all busy: steal one */
    vox[v] = (Voice){ .s = s, .on = 1, .vol = vol * g_settings.sfx_vol };
    return v;
}

/* Play a one-shot sound effect on a free (or stolen) voice */
void sfx(int s, float vol) { (void)sfx_voice(s, vol); }

static void intro_clear(void)
{
    intro_active = 0;
    intro_pending = 0;
    intro_vox = -1;
    server_embed_set_fight_intro_hold(0);
}

static void intro_try_start(void)
{
    if (!intro_pending || !astream)
        return;
    intro_pending = 0;
    intro_vox = sfx_voice(SN_FIGHT, 1.0f);
    if (intro_vox < 0)
        intro_clear();
}

/* Announcer at match start; listen server stays frozen until this voice ends. */
void fight_intro_play(void)
{
    intro_vox = -1;
    intro_pending = 0;
    if (!snd[SN_FIGHT].pcm) {
        intro_clear();
        return;
    }
    intro_active = 1;
    server_embed_set_fight_intro_hold(1);
    if (!astream) {
        intro_pending = 1; /* device opens off-thread; start once it is ready */
        return;
    }
    intro_vox = sfx_voice(SN_FIGHT, 1.0f);
    if (intro_vox < 0)
        intro_clear();
}

/* 1 while the match should stay frozen for the announcer */
int fight_intro_blocking(void)
{
    if (!intro_active)
        return 0;
    intro_try_start();
    if (intro_pending)
        return 1;
    if (intro_vox < 0 || !vox[intro_vox].on || vox[intro_vox].s != SN_FIGHT) {
        intro_clear();
        return 0;
    }
    return 1;
}

/* Switch background music to track s (loops on voice 0) */
void music(int s)
{
    if (s == cur_music) return;
    cur_music = s;
    vox[0] = (Voice){ .s = s, .on = astream && snd[s].pcm, .loop = 1,
                      .vol = MUSIC_VOL * g_settings.music_vol };
}

/* Re-apply music volume after settings change without restarting the track. */
void music_refresh_vol(void)
{
    if (vox[0].on)
        vox[0].vol = MUSIC_VOL * g_settings.music_vol;
}

/* Map match stage index to a fight BGM id (-1 = stadium). */
int music_for_stage(int stage)
{
    if (stage < 0)
        return SN_MUSIC_FIGHT_STADIUM;
    if (stage >= NCHARS)
        stage = NCHARS - 1;
    return SN_MUSIC_FIGHT_FIRST + stage;
}

/* Keep about 125 ms of mixed stereo audio queued. Called once per frame from the main thread. */
void audio_pump(void)
{
    intro_try_start();
    if (!astream) return;
    /* Stereo S16: 4 bytes/frame. Target ~125 ms of queued audio. */
    while (SDL_GetAudioStreamQueued(astream) < AUDIO_RATE / 8 * 4) {
        Sint16 out[512 * AUDIO_CH];
        for (int i = 0; i < 512; i++) {
            float L = 0, R = 0;
            for (int v = 0; v < NVOX; v++) {
                Voice *x = &vox[v];
                if (!x->on) continue;
                const Snd *sn = &snd[x->s];
                if (sn->ch >= 2) {
                    Uint32 o = x->pos * 2;
                    L += sn->pcm[o] * x->vol;
                    R += sn->pcm[o + 1] * x->vol;
                } else {
                    float s = sn->pcm[x->pos] * x->vol;
                    L += s;
                    R += s;
                }
                if (++x->pos >= sn->n) { x->pos = 0; if (!x->loop) x->on = 0; }
            }
            L *= mastervol;
            R *= mastervol;
            out[i * 2]     = (Sint16)(L > 32767 ? 32767 : L < -32768 ? -32768 : L);
            out[i * 2 + 1] = (Sint16)(R > 32767 ? 32767 : R < -32768 ? -32768 : R);
        }
        SDL_PutAudioStreamData(astream, out, sizeof out);
    }
    /* Release the listen-server hold as soon as the announcer voice ends. */
    (void)fight_intro_blocking();
}

/* Choose a swing sound based on move strength and play it */
void swing_sound(const Move *m)
{
    if (m->name && strstr(m->name, "cackle")) {
        sfx(SN_LAUGH, 1.0f);
        if (m->boom) sfx(SN_BOOM, 0.7f);
        return;
    }
    if (m->name && strstr(m->name, "chicken")) {
        sfx(SN_SWING2, 0.7f); /* barf SFX comes from the boom snapshot */
        return;
    }
    int row = move_row(m);
    int lvl = row < 6 ? row % 3 : row == 6 ? 1 : row == 9 ? 0 : row == 10 ? 1 : 2;
    sfx(SN_SWING1 + lvl, row == 8 ? 1.0f : 0.8f);
}

/* Sounds that come from the authoritative snapshot: hits, blocks, KOs, shots, bombs, ships. */
static int php[MAX_PLAYERS], plives[MAX_PLAYERS], pns, pnb, pnsh;
static int sfresh = 1;

/* Compare current snapshot to previous and play SFX for hits, KOs, shots, bombs, ships, booms */
void audio_snapshot(void)
{
    for (int i = 0; i < nf; i++) {
        const Fig *f = &F[i];
        if (!sfresh) {
            int d = php[f->slot] - f->hp;
            if (d > 0) {
                if (f->st == -3) sfx(SN_BLOCK, 0.8f);
                else sfx(d < 40 ? SN_HIT1 : d < 90 ? SN_HIT2 : SN_HIT3, 1.0f);
            }
            if (f->lives < plives[f->slot]) sfx(SN_KO, 1.0f);
        }
        php[f->slot] = f->hp;
        plives[f->slot] = f->lives;
    }
    if (!sfresh) {
        if (ns > pns) sfx(SN_SHOT, 0.7f);
        if (nb > pnb) sfx(SN_BOMB, 0.6f);
        if (nsh > pnsh) sfx(SN_SHIP, 0.8f);
        for (int i = 0; i < nex; i++) {
            /* First snapshot is already decremented once on the server. */
            int fresh = (EX[i].kind == BOOM_BARF ? BARF_BOOM_TICKS : BOOM_TICKS) - 1;
            if (EX[i].t >= fresh)
                sfx(EX[i].kind == BOOM_BARF ? SN_HIT2 : SN_BOOM,
                    EX[i].kind == BOOM_BARF ? 0.9f : 1.0f);
        }
    }
    pns = ns; pnb = nb; pnsh = nsh;
    sfresh = 0;
}

/* A new phase restarts the snapshot comparison so it doesn't play a burst of sounds */
void audio_reset(void) { sfresh = 1; }
