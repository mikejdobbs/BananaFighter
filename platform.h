/* Cross-platform sockets, time, and threads (Unix vs Windows/MSVC). */
#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
/* 16-bit legacy macros; break identifiers named near/far. */
#  undef near
#  undef far
#else
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <pthread.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
typedef SOCKET bf_socket_t;
#  define BF_INVALID_SOCKET INVALID_SOCKET
typedef int bf_socklen_t;
typedef SSIZE_T bf_ssize_t;
#else
typedef int bf_socket_t;
#  define BF_INVALID_SOCKET (-1)
typedef socklen_t bf_socklen_t;
typedef ssize_t bf_ssize_t;
#endif

/* Winsock startup / cleanup (no-ops on Unix). Call before any socket use. */
int platform_net_init(void);
void platform_net_shutdown(void);

void bf_close(bf_socket_t s);
int bf_set_nonblock(bf_socket_t s, int on);
int bf_poll(struct pollfd *fds, unsigned long nfds, int timeout_ms);

int bf_sock_err(void);
int bf_wouldblock(void);

long long bf_now_ms(void);

typedef struct bf_thread bf_thread;
int bf_thread_create(bf_thread *t, void *(*fn)(void *), void *arg);
void bf_thread_join(bf_thread *t);

#ifdef _WIN32
struct bf_thread {
    HANDLE h;
};
#else
struct bf_thread {
    pthread_t id;
    int alive;
};
#endif

#endif
