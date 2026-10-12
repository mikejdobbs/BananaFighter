#include "server.h"

int main(void)
{
    int reuse_addr = 1;
    struct sockaddr_in listen_addr = { .sin_family = AF_INET, .sin_port = htons(PORT),
                                       .sin_addr.s_addr = htonl(INADDR_ANY) };
    signal(SIGPIPE, SIG_IGN);

    int lobby_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (lobby_fd < 0) { perror("socket"); return 1; }
    setsockopt(lobby_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof reuse_addr);
    
    if (bind(lobby_fd, (struct sockaddr *)&listen_addr, sizeof listen_addr) < 0) { perror("bind tcp"); return 1; }
    if (listen(lobby_fd, 8) < 0) { perror("listen"); return 1; }

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);            /* same port number, different protocol */
    if (udp_fd < 0) { perror("socket udp"); return 1; }
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof reuse_addr);
    if (bind(udp_fd, (struct sockaddr *)&listen_addr, sizeof listen_addr) < 0) { perror("bind udp"); return 1; }
    fcntl(udp_fd, F_SETFL, fcntl(udp_fd, F_GETFL) | O_NONBLOCK);

    for (int i = 0; i < MAX_PLAYERS; i++)
        players[i].fd = -1;

    long long next_tick_ms = now_ms();
    for (;;) {

        struct pollfd poll_fds[MAX_PLAYERS + 2] = { { lobby_fd, POLLIN, 0 } };
        for (int i = 0; i < MAX_PLAYERS; i++)
            poll_fds[i + 1] = (struct pollfd){ players[i].fd, POLLIN, 0 };

        poll_fds[MAX_PLAYERS + 1] = (struct pollfd){ udp_fd, POLLIN, 0 };

        // sleep until something happens or socket timesout
        long long timeout_ms = next_tick_ms - now_ms();
        if (poll(poll_fds, MAX_PLAYERS + 2, timeout_ms > 0 ? (int)timeout_ms : 0) > 0) {
            if (poll_fds[0].revents & POLLIN) accept_client(lobby_fd);
            for (int i = 0; i < MAX_PLAYERS; i++)
                if ((poll_fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) && players[i].fd == poll_fds[i + 1].fd)
                    read_client(i);
            if (poll_fds[MAX_PLAYERS + 1].revents & POLLIN) read_udp();
        }

        if (now_ms() >= next_tick_ms) {
            step();
            check_udp_timeouts();
            for (int i = 0; i < MAX_PLAYERS; i++)
                if (players[i].fd >= 0) send_state(i);
            next_tick_ms += 1000 / TICK_HZ;
        }
    }
}
