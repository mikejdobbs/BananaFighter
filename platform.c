#include "platform.h"

#include <stdlib.h>
#include <stdio.h>
#ifndef _WIN32
#  include <time.h>
#endif

#ifdef _WIN32
static int wsa_ready;

int platform_net_init(void)
{
    if (wsa_ready)
        return 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return -1;
    wsa_ready = 1;
    return 0;
}

void platform_net_shutdown(void)
{
    if (!wsa_ready)
        return;
    WSACleanup();
    wsa_ready = 0;
}

void bf_close(bf_socket_t s)
{
    if (s != BF_INVALID_SOCKET)
        closesocket(s);
}

int bf_set_nonblock(bf_socket_t s, int on)
{
    u_long mode = on ? 1 : 0;
    return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
}

int bf_poll(struct pollfd *fds, unsigned long nfds, int timeout_ms)
{
    return WSAPoll(fds, (ULONG)nfds, timeout_ms);
}

int bf_sock_err(void) { return WSAGetLastError(); }

int bf_wouldblock(void)
{
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
}

long long bf_now_ms(void)
{
    static LARGE_INTEGER freq;
    static int init;
    LARGE_INTEGER now;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    QueryPerformanceCounter(&now);
    return (long long)(now.QuadPart * 1000 / freq.QuadPart);
}

struct thread_wrap {
    void *(*fn)(void *);
    void *arg;
};

static DWORD WINAPI thread_trampoline(void *p)
{
    struct thread_wrap *w = (struct thread_wrap *)p;
    void *(*fn)(void *) = w->fn;
    void *arg = w->arg;
    free(w);
    fn(arg);
    return 0;
}

int bf_thread_create(bf_thread *t, void *(*fn)(void *), void *arg)
{
    struct thread_wrap *w = (struct thread_wrap *)malloc(sizeof *w);
    if (!w)
        return -1;
    w->fn = fn;
    w->arg = arg;
    t->h = CreateThread(NULL, 0, thread_trampoline, w, 0, NULL);
    if (!t->h) {
        free(w);
        return -1;
    }
    return 0;
}

void bf_thread_join(bf_thread *t)
{
    if (!t || !t->h)
        return;
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
    t->h = NULL;
}

#else /* Unix */

int platform_net_init(void) { return 0; }
void platform_net_shutdown(void) {}

void bf_close(bf_socket_t s)
{
    if (s != BF_INVALID_SOCKET)
        close(s);
}

int bf_set_nonblock(bf_socket_t s, int on)
{
    int flags = fcntl(s, F_GETFL);
    if (flags == -1)
        return -1;
    if (on)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    return fcntl(s, F_SETFL, flags) == -1 ? -1 : 0;
}

int bf_poll(struct pollfd *fds, unsigned long nfds, int timeout_ms)
{
    return poll(fds, (nfds_t)nfds, timeout_ms);
}

int bf_sock_err(void) { return errno; }

int bf_wouldblock(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

long long bf_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

int bf_thread_create(bf_thread *t, void *(*fn)(void *), void *arg)
{
    t->alive = 0;
    if (pthread_create(&t->id, NULL, fn, arg) != 0)
        return -1;
    t->alive = 1;
    return 0;
}

void bf_thread_join(bf_thread *t)
{
    if (!t || !t->alive)
        return;
    pthread_join(t->id, NULL);
    t->alive = 0;
}

#endif
