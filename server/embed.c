#include "embed.h"
#include "server.h"

#include <stdatomic.h>
#ifndef _WIN32
#  include <signal.h>
#endif

void server_embed_set_session_password(const char *password)
{
    server_set_session_password(password);
}

static atomic_int running; /* 1 while the poll loop should keep going */
static atomic_int lan_advertise;
static atomic_int fight_intro_hold; /* client clears when fight.wav finishes */
static int listen_from_client; /* 1 when started via server_embed_start() */
static bf_socket_t lobby_fd = BF_INVALID_SOCKET;
static bf_socket_t beacon_fd = BF_INVALID_SOCKET;
static long long last_beacon_ms;
static bf_thread thread;
static int thread_alive;

void server_embed_set_fight_intro_hold(int hold)
{
    atomic_store(&fight_intro_hold, hold ? 1 : 0);
}

/* Used by sim.c: listen servers wait on the client; dedicated uses the WAV. */
int server_embed_fight_intro_from_client(void)
{
    return listen_from_client;
}

int server_embed_fight_intro_held(void)
{
    return atomic_load(&fight_intro_hold) != 0;
}

void server_embed_set_lan_advertise(int on)
{
    atomic_store(&lan_advertise, on ? 1 : 0);
}

static void close_sockets(void)
{
    if (lobby_fd != BF_INVALID_SOCKET) {
        bf_close(lobby_fd);
        lobby_fd = BF_INVALID_SOCKET;
    }
    if (udp_fd != BF_INVALID_SOCKET) {
        bf_close(udp_fd);
        udp_fd = BF_INVALID_SOCKET;
    }
    if (beacon_fd != BF_INVALID_SOCKET) {
        bf_close(beacon_fd);
        beacon_fd = BF_INVALID_SOCKET;
    }
}

/* Broadcast a cleartext LAN beacon so Connect screens can list this host. */
static void beacon_tick(void)
{
    if (!atomic_load(&lan_advertise))
        return;
    
    long long t = now_ms();
    if (last_beacon_ms && t - last_beacon_ms < 1000)
        return;
    
    last_beacon_ms = t;

    if (beacon_fd == BF_INVALID_SOCKET) {
        int one = 1;
        beacon_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (beacon_fd == BF_INVALID_SOCKET)
            return;
        
        setsockopt(beacon_fd, SOL_SOCKET, SO_BROADCAST, (const char *)&one,
                   sizeof one);
    }

    char msg[64];
    int n = snprintf(msg, sizeof msg, "%s %d\n", DISCOVER_MAGIC, PORT);
    if (n <= 0)
        return;
    struct sockaddr_in dst = {.sin_family = AF_INET,
                              .sin_port = htons(DISCOVER_PORT),
                              .sin_addr.s_addr = htonl(INADDR_BROADCAST)};
    sendto(beacon_fd, msg, (size_t)n, 0, (struct sockaddr *)&dst, sizeof dst);
}

/* Bind TCP + UDP on PORT. Returns 0 on success, -1 on failure. */
static int server_bind(void)
{
    int reuse_addr = 1;
    struct sockaddr_in listen_addr = {.sin_family = AF_INET,
                                      .sin_port = htons(PORT),
                                      .sin_addr.s_addr = htonl(INADDR_ANY)};

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    lobby_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (lobby_fd == BF_INVALID_SOCKET) {
        perror("socket");
        return -1;
    }
    setsockopt(lobby_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse_addr,
               sizeof reuse_addr);

    if (bind(lobby_fd, (struct sockaddr *)&listen_addr, sizeof listen_addr) <
        0) {
        perror("bind tcp");
        close_sockets();
        return -1;
    }
    if (listen(lobby_fd, 8) < 0) {
        perror("listen");
        close_sockets();
        return -1;
    }

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd == BF_INVALID_SOCKET) {
        perror("socket udp");
        close_sockets();
        return -1;
    }

    if (bind(udp_fd, (struct sockaddr *)&listen_addr, sizeof listen_addr) < 0) {
        perror("bind udp");
        close_sockets();
        return -1;
    }

    if (bf_set_nonblock(udp_fd, 1) != 0) {
        perror("bf_set_nonblock udp");
        close_sockets();
        return -1;
    }

    server_reset_world();
    return 0;
}

static void server_loop(void)
{
    long long next_tick_ms = now_ms();

    while (atomic_load(&running)) {
        struct pollfd poll_fds[MAX_PLAYERS + 2];
        poll_fds[0].fd = lobby_fd;
        poll_fds[0].events = POLLIN;
        poll_fds[0].revents = 0;

        for (int i = 0; i < MAX_PLAYERS; i++) {
            poll_fds[i + 1].fd = players[i].fd;
            poll_fds[i + 1].events = POLLIN;
            poll_fds[i + 1].revents = 0;
        }

        poll_fds[MAX_PLAYERS + 1].fd = udp_fd;
        poll_fds[MAX_PLAYERS + 1].events = POLLIN;
        poll_fds[MAX_PLAYERS + 1].revents = 0;

        long long timeout_ms = next_tick_ms - now_ms();
        if (bf_poll(poll_fds, MAX_PLAYERS + 2,
                    timeout_ms > 0 ? (int)timeout_ms : 0) > 0) {
            if (lobby_fd != BF_INVALID_SOCKET && (poll_fds[0].revents & POLLIN))
                accept_client_to_lobby(lobby_fd);

            for (int i = 0; i < MAX_PLAYERS; i++)
                if ((poll_fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) &&
                    players[i].fd == poll_fds[i + 1].fd)
                    read_client_in_lobby(i);

            if (udp_fd != BF_INVALID_SOCKET &&
                (poll_fds[MAX_PLAYERS + 1].revents & POLLIN))
                read_udp();
        }

        if (!atomic_load(&running))
            break;

        if (now_ms() >= next_tick_ms) {
            step();
            check_udp_timeouts();
            beacon_tick();

            for (int i = 0; i < MAX_PLAYERS; i++)
                if (players[i].fd != BF_INVALID_SOCKET)
                    send_state(i);
            next_tick_ms += 1000 / TICK_HZ;
        }
    }

    /* Drop remaining clients and release sockets. */
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (players[i].fd != BF_INVALID_SOCKET)
            drop(i);
    close_sockets();
}

static void *server_thread_main(void *arg)
{
    (void)arg;
    server_loop();
    return NULL;
}

int server_run_forever(void)
{
    if (platform_net_init() != 0)
        return -1;
    if (crypto_session_init() != 0)
        return -1;
    if (server_bind() < 0)
        return -1;
    listen_from_client = 0;
    atomic_store(&fight_intro_hold, 0);
    atomic_store(&lan_advertise, 1);
    atomic_store(&running, 1);
    printf("Server starting on port %d (LAN discovery %d)\n", PORT, DISCOVER_PORT);
    server_loop();
    return 0;
}

int server_embed_start(void)
{
    if (thread_alive)
        return 0;
    if (platform_net_init() != 0)
        return -1;
    if (crypto_session_init() != 0)
        return -1;
    if (server_bind() < 0)
        return -1;
    listen_from_client = 1;
    atomic_store(&fight_intro_hold, 0);
    atomic_store(&running, 1);
    printf("Server starting on port %d\n", PORT);
    if (bf_thread_create(&thread, server_thread_main, NULL) != 0) {
        perror("bf_thread_create");
        atomic_store(&running, 0);
        close_sockets();
        return -1;
    }
    thread_alive = 1;
    return 0;
}

void server_embed_stop(void)
{
    if (!thread_alive && !atomic_load(&running))
        return;

    atomic_store(&running, 0);
    atomic_store(&lan_advertise, 0);
    atomic_store(&fight_intro_hold, 0);
    /* Closing sockets wakes a blocked poll() in the server thread. */
    close_sockets();

    if (thread_alive) {
        bf_thread_join(&thread);
        thread_alive = 0;
    }
    listen_from_client = 0;
    /* Drop bots / winner / PLAY phase so the next Single Player is a fresh lobby. */
    server_reset_world();
}
