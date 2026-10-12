#include "server.h"

long long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

unsigned long long make_token(void)
{
    unsigned long long t = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&t, sizeof t, 1, f) != 1) t = 0;
        fclose(f);
    }
    if (!t) t = ((unsigned long long)rand() << 32) ^ (unsigned long long)rand() ^ (unsigned long long)now_ms();
    return t ? t : 1;
}

int fighting(const Player *p) { return p->in_match && p->lives > 0; }
int player_is_person_or_bot(const Player *p) { return p->fd >= 0 || p->bot; }   /* slot in use */

int overlap(int ax0, int ax1, int ay0, int ay1, int bx0, int bx1, int by0, int by1)
{
    return lo(ax1, bx1) >= hi(ax0, bx0) && lo(ay1, by1) >= hi(ay0, by0);
}

int nearest(int i)
{
    int b = -1;
    for (int k = 0; k < MAX_PLAYERS; k++)
        if (k != i && fighting(&players[k]) &&
            (b < 0 || abs(players[k].body.x - players[i].body.x) < abs(players[b].body.x - players[i].body.x)))
            b = k;
    return b;
}
