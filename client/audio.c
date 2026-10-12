#include "client.h"

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
    SDL_AudioSpec want = { SDL_AUDIO_S16, 1, AUDIO_RATE };
    if (spec.format != want.format || spec.channels != 1 || spec.freq != AUDIO_RATE) {
        Uint8 *cv;
        int cl;
        if (!SDL_ConvertAudioSamples(&spec, buf, (int)len, &want, &cv, &cl)) { SDL_free(buf); return 0; }
        SDL_free(buf);
        buf = cv;
        len = (Uint32)cl;
    }
    snd[i].pcm = (Sint16 *)buf;
    snd[i].n = len / 2;
    return 1;
}

static void audio_init(void)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { fprintf(stderr, "no audio: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, AUDIO_RATE };
    astream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!astream) { fprintf(stderr, "no audio device: %s\n", SDL_GetError()); return; }
    int got = 0;
    for (int i = 0; i < SN_COUNT; i++) got += load_snd(i);
    if (got < SN_COUNT) fprintf(stderr, "loaded %d of %d sounds (run make_sounds.py)\n", got, SN_COUNT);
    SDL_ResumeAudioStreamDevice(astream);
}

static void sfx(int s, float vol)
{
    static int rr = 1;
    if (!astream || !snd[s].pcm) return;
    int v = 1;
    while (v < NVOX && vox[v].on) v++;
    if (v == NVOX) { v = rr; rr = rr % (NVOX - 1) + 1; }
    vox[v] = (Voice){ .s = s, .on = 1, .vol = vol };
}

static void music(int s)
{
    if (s == cur_music) return;
    cur_music = s;
    vox[0] = (Voice){ .s = s, .on = astream && snd[s].pcm, .loop = 1, .vol = MUSIC_VOL };
}

static void audio_pump(void)
{
    if (!astream) return;
    while (SDL_GetAudioStreamQueued(astream) < AUDIO_RATE / 8 * 2) {
        Sint16 out[512];
        for (int i = 0; i < 512; i++) {
            float acc = 0;
            for (int v = 0; v < NVOX; v++) {
                Voice *x = &vox[v];
                if (!x->on) continue;
                const Snd *sn = &snd[x->s];
                acc += sn->pcm[x->pos] * x->vol;
                if (++x->pos >= sn->n) { x->pos = 0; if (!x->loop) x->on = 0; }
            }
            acc *= mastervol;
            out[i] = (Sint16)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
        }
        SDL_PutAudioStreamData(astream, out, sizeof out);
    }
}

static void swing_sound(const Move *m)
{
    int row = move_row(m);
    int lvl = row < 6 ? row % 3 : row == 6 ? 1 : row == 9 ? 0 : row == 10 ? 1 : 2;
    sfx(SN_SWING1 + lvl, row == 8 ? 1.0f : 0.8f);
}

static void audio_snapshot(void)
{
    for (int i = 0; i < nf; i++) {
        const Fig *f = &F[i];
        if (!show_boxes) {
            int d = 0;
            if (i < MAX_PLAYERS) d = 0;
            (void)d;
        }
        if (f->slot >= 0) {
            if (phase == 'S') { }
        }
    }
}
