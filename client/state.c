#include "client.h"

int fd = -1, me = -1, tick = 0, winner = 0, hasq = 0, show_boxes = 0, mycmd = 0;
int nf = 0, nl = 0, ns = 0, nb = 0, nsh = 0, nex = 0;
int options_open = 0, options_item = 0;
char phase = 'L', rb[4096];
int rl = 0;
Fig F[MAX_PLAYERS];
Lob U[MAX_PLAYERS];
Shot S[MAXPROJ], BM[MAXPROJ];
Ship SHP[MAX_PLAYERS];
Boom EX[MAXBOOM];
Extra Q;
Anim an[MAX_PLAYERS];
unsigned aclk = 0;
char aiName[MAXAI][16];
int nai = 0;
SDL_Renderer *ren = NULL;
SDL_Texture *tex[NCHARS] = {0};
SDL_Texture *shots = NULL, *shipt = NULL, *fxt = NULL, *bg = NULL;
Body pb;
int predicting = 0, pch = 0;
unsigned sent = 0, ack = 0;
int hist[HIST];
Snd snd[SN_COUNT];
Voice vox[NVOX];
SDL_AudioStream *astream = NULL;
float mastervol = SOUND_ON;
int cur_music = -1;

int num(char **s)
{
    return (int)strtol(*s, s, 10);
}

int look(int x)
{
    (void)x;
    return 0;
}

int detect_cmd(int *w, int n, int face)
{
    (void)w;
    (void)n;
    (void)face;
    return 0;
}

void body_step(void *b, void *c, int mask, int lk, int cmd)
{
    (void)b;
    (void)c;
    (void)mask;
    (void)lk;
    (void)cmd;
}

void window(unsigned i, int *w)
{
    (void)i;
    (void)w;
}
