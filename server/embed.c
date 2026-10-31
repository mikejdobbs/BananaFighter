#include "embed.h"
#include "server.h"

#include <pthread.h>
#include <stdatomic.h>

void server_embed_set_session_password(const char *password)
{
    server_set_session_password(password);
}

static atomic_int running; /* 1 while the poll loop should keep going */
static int lobby_fd = -1;
static pthread_t thread;
static int thread_alive;

static void close_sockets(void)
{
    if (lobby_fd >= 0) {
        close(lobby_fd);
        lobby_fd = -1;
    }
    if (udp_fd >= 0) {
        close(udp_fd);
        udp_fd = -1;
    }
}

/* Bind TCP + UDP on PORT. Returns 0 on success, -1 on failure. */
static int server_bind(void)
{
    int reuse_addr = 1;
    struct sockaddr_in listen_addr = {.sin_family = AF_INET,
                                      .sin_port = htons(PORT),
                                      .sin_addr.s_addr = htonl(INADDR_ANY)};

    signal(SIGPIPE, SIG_IGN);

    lobby_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (lobby_fd < 0) {
        perror("socket");
        return -1;
    }
    setsockopt(lobby_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr,
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
    if (udp_fd < 0) {
        perror("socket udp");
        close_sockets();
        return -1;
    }

    if (bind(udp_fd, (struct sockaddr *)&listen_addr, sizeof listen_addr) < 0) {
        perror("bind udp");
        close_sockets();
        return -1;
    }

    int flags = fcntl(udp_fd, F_GETFL);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        close_sockets();
        return -1;
    }
    if (fcntl(udp_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        close_sockets();
        return -1;
    }

    for (int i = 0; i < MAX_PLAYERS; i++)
        players[i].fd = -1;

    return 0;
}

static void server_loop(void)
{
    long long next_tick_ms = now_ms();

    while (atomic_load(&running)) {
        struct pollfd poll_fds[MAX_PLAYERS + 2] = {{lobby_fd, POLLIN, 0}};

        for (int i = 0; i < MAX_PLAYERS; i++)
            poll_fds[i + 1] = (struct pollfd){players[i].fd, POLLIN, 0};

        poll_fds[MAX_PLAYERS + 1] = (struct pollfd){udp_fd, POLLIN, 0};

        long long timeout_ms = next_tick_ms - now_ms();
        if (poll(poll_fds, MAX_PLAYERS + 2,
                 timeout_ms > 0 ? (int)timeout_ms : 0) > 0) {
            if (lobby_fd >= 0 && (poll_fds[0].revents & POLLIN))
                accept_client_to_lobby(lobby_fd);

            for (int i = 0; i < MAX_PLAYERS; i++)
                if ((poll_fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) &&
                    players[i].fd == poll_fds[i + 1].fd)
                    read_client_in_lobby(i);

            if (udp_fd >= 0 && (poll_fds[MAX_PLAYERS + 1].revents & POLLIN))
                read_udp();
        }

        if (!atomic_load(&running))
            break;

        if (now_ms() >= next_tick_ms) {
            step();
            check_udp_timeouts();

            for (int i = 0; i < MAX_PLAYERS; i++)
                if (players[i].fd >= 0)
                    send_state(i);
            next_tick_ms += 1000 / TICK_HZ;
        }
    }

    /* Drop remaining clients and release sockets. */
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (players[i].fd >= 0)
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
    if (crypto_session_init() != 0)
        return -1;
    if (server_bind() < 0)
        return -1;
    atomic_store(&running, 1);
    printf("Server starting on port %d\n", PORT);
    server_loop();
    return 0;
}

int server_embed_start(void)
{
    if (thread_alive)
        return 0;
    if (crypto_session_init() != 0)
        return -1;
    if (server_bind() < 0)
        return -1;
    atomic_store(&running, 1);
    printf("Server starting on port %d\n", PORT);
    if (pthread_create(&thread, NULL, server_thread_main, NULL) != 0) {
        perror("pthread_create");
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
    /* Closing sockets wakes a blocked poll() in the server thread. */
    close_sockets();

    if (thread_alive) {
        pthread_join(thread, NULL);
        thread_alive = 0;
    }
}
