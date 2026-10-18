/*
 * test.c - black-box test client for the fighting-game server.
 *
 * Build:   cc -std=c11 -Wall -Wextra -O1 -o test test.c
 *
 * Usage:   ./test [-H host] [-p port] [-s server_binary] [--slow] [-v] [test_name ...]
 *
 *   -s PATH   Start a fresh copy of the server for every test (recommended: the
 *             server keeps state such as "match in progress" between clients).
 *             Without -s, the tests talk to a server that is already running,
 *             and you should restart it between runs.
 *   --slow    Also run tests that wait more than 5 seconds (UDP timeout tests).
 *   -v        Let the spawned server print to the terminal.
 *
 * The tests speak the same protocol as a real client:
 *   TCP:  server sends "Y slot names...", "A ais...", "U token"; client sends
 *         "J char", "R", "B char ai", "X slot"; server sends "L {U slot char ready bot}..."
 *   UDP:  client sends "H slot token" and "I slot token seq m0 m1 ...";
 *         server sends "S tick winner ack {P ...} ..." snapshots during a match.
 *
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Values that mirror config.h / server.h. */
#define DEFAULT_PORT 7777
#define SERVER_MAX_PLAYERS 10
#define SERVER_UDP_PKT_MAX 1400
#define SERVER_UDP_TIMEOUT_MS 5000
#define SERVER_NCHARS 6
#define TCP_LINE_BUFFER 4096
#define MAX_TEST_CLIENTS 16

static const char *server_host = "127.0.0.1";
static int server_port = DEFAULT_PORT;
static const char *server_path = NULL;
static int run_slow_tests = 0;
static int verbose = 0;
static pid_t server_pid = -1;

static int check_count = 0;
static int fail_count = 0;

/* CHECK records a failure and continues; REQUIRE records it and ends the test. */
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        check_count++;                                                         \
        if (!(cond)) {                                                         \
            fail_count++;                                                      \
            printf("    FAIL (line %d): ", __LINE__);                          \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

#define REQUIRE(cond, ...)                                                     \
    do {                                                                       \
        check_count++;                                                         \
        if (!(cond)) {                                                         \
            fail_count++;                                                      \
            printf("    FAIL (line %d): ", __LINE__);                          \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            return;                                                            \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sleep_ms(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
        ;
}

/* Returns 1 if fd is readable (or hung up) within timeout_ms, else 0. */
static int wait_readable(int fd, int timeout_ms) {
    struct pollfd pfd = {fd, POLLIN, 0};
    int r;
    do {
        r = poll(&pfd, 1, timeout_ms);
    } while (r < 0 && errno == EINTR);
    return r > 0;
}

static struct sockaddr_in make_server_addr(void) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)server_port);
    inet_pton(AF_INET, server_host, &addr.sin_addr);
    return addr;
}

/* ------------------------------------------------------------------------ */
/* Client = one TCP connection plus up to two UDP sockets                   */
/* ------------------------------------------------------------------------ */

typedef struct {
    int in_use;
    int tcp_fd;
    int udp_fd;  /* main UDP socket, connected to the server */
    int udp_fd2; /* second UDP socket, for forged-packet and rebinding tests */
    char buf[TCP_LINE_BUFFER];
    int len;
    int slot;
    unsigned long long token;
} Client;

static Client client_pool[MAX_TEST_CLIENTS];

static Client *new_client(void) {
    for (int i = 0; i < MAX_TEST_CLIENTS; i++) {
        if (!client_pool[i].in_use) {
            memset(&client_pool[i], 0, sizeof client_pool[i]);
            client_pool[i].in_use = 1;
            client_pool[i].tcp_fd = -1;
            client_pool[i].udp_fd = -1;
            client_pool[i].udp_fd2 = -1;
            client_pool[i].slot = -1;
            return &client_pool[i];
        }
    }
    return NULL;
}

static void client_close(Client *c) {
    if (c->tcp_fd >= 0) close(c->tcp_fd);
    if (c->udp_fd >= 0) close(c->udp_fd);
    if (c->udp_fd2 >= 0) close(c->udp_fd2);
    c->tcp_fd = c->udp_fd = c->udp_fd2 = -1;
    c->in_use = 0;
}

static void release_all_clients(void) {
    for (int i = 0; i < MAX_TEST_CLIENTS; i++)
        if (client_pool[i].in_use) client_close(&client_pool[i]);
}

static int tcp_connect_fd(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = make_server_addr();
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int udp_open_fd(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = make_server_addr();
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int tcp_send_raw(Client *c, const void *data, size_t len) {
    return send(c->tcp_fd, data, len, 0) == (ssize_t)len;
}

static int tcp_send(Client *c, const char *fmt, ...) {
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof text) return 0;
    return tcp_send_raw(c, text, (size_t)n);
}

static int udp_send_to(int fd, const char *fmt, ...) {
    char text[SERVER_UDP_PKT_MAX + 1];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof text) return 0;
    return send(fd, text, (size_t)n, 0) == n;
}

/*
 * Read one newline-terminated TCP line.
 * Returns 1 = line read, 0 = timeout, -1 = connection closed or reset.
 */
static int read_line(Client *c, char *out, size_t out_size, int timeout_ms) {
    long long deadline = now_ms() + timeout_ms;
    for (;;) {
        char *newline = memchr(c->buf, '\n', (size_t)c->len);
        if (newline) {
            size_t line_len = (size_t)(newline - c->buf);
            size_t copy_len = line_len < out_size - 1 ? line_len : out_size - 1;
            memcpy(out, c->buf, copy_len);
            out[copy_len] = '\0';
            int used = (int)line_len + 1;
            memmove(c->buf, c->buf + used, (size_t)(c->len - used));
            c->len -= used;
            return 1;
        }
        long long left = deadline - now_ms();
        if (left < 0) left = 0;
        if (!wait_readable(c->tcp_fd, (int)left)) return 0;
        if (c->len >= (int)sizeof c->buf) c->len = 0; /* discard junk */
        ssize_t r = recv(c->tcp_fd, c->buf + c->len,
                         sizeof c->buf - (size_t)c->len, 0);
        if (r == 0) return -1;
        if (r < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return -1;
        }
        c->len += (int)r;
    }
}

/* Read lines until one starts with prefix. Same return values as read_line. */
static int wait_for_prefix(Client *c, const char *prefix, char *out,
                           size_t out_size, int timeout_ms) {
    long long deadline = now_ms() + timeout_ms;
    for (;;) {
        long long left = deadline - now_ms();
        if (left < 0) left = 0;
        int r = read_line(c, out, out_size, (int)left);
        if (r <= 0) return r;
        if (strncmp(out, prefix, strlen(prefix)) == 0) return 1;
    }
}

/* Connect and consume the three handshake lines. Returns 1 on success. */
static int handshake(Client *c) {
    char line[TCP_LINE_BUFFER];
    c->tcp_fd = tcp_connect_fd();
    if (c->tcp_fd < 0) return 0;
    if (read_line(c, line, sizeof line, 2000) != 1 ||
        sscanf(line, "Y %d", &c->slot) != 1)
        return 0;
    if (read_line(c, line, sizeof line, 2000) != 1 || line[0] != 'A')
        return 0;
    if (read_line(c, line, sizeof line, 2000) != 1 ||
        sscanf(line, "U %llx", &c->token) != 1)
        return 0;
    return 1;
}

/* ------------------------------------------------------------------------ */
/* Lobby ("L") and snapshot ("S") parsing                                   */
/* ------------------------------------------------------------------------ */

typedef struct {
    int slot, character, ready, bot;
} LobbyEntry;

/* Parse "L U slot char ready bot U ..." Returns entry count, or -1 if not an L line. */
static int parse_lobby(const char *line, LobbyEntry *entries, int max_entries) {
    if (line[0] != 'L') return -1;
    const char *p = line + 1;
    int count = 0;
    while (count < max_entries) {
        while (*p == ' ') p++;
        if (*p != 'U') break;
        p++;
        int consumed = 0;
        LobbyEntry *e = &entries[count];
        if (sscanf(p, " %d %d %d %d%n", &e->slot, &e->character, &e->ready,
                   &e->bot, &consumed) != 4)
            break;
        p += consumed;
        count++;
    }
    return count;
}

/* Wait for a lobby line where our slot has the wanted values (-1 = don't care). */
static int lobby_find(Client *c, int slot, int want_char, int want_ready,
                      int timeout_ms, LobbyEntry *found) {
    long long deadline = now_ms() + timeout_ms;
    char line[TCP_LINE_BUFFER];
    LobbyEntry entries[SERVER_MAX_PLAYERS + 2];
    for (;;) {
        long long left = deadline - now_ms();
        if (left < 0) left = 0;
        if (wait_for_prefix(c, "L", line, sizeof line, (int)left) != 1) return 0;
        int n = parse_lobby(line, entries, SERVER_MAX_PLAYERS + 2);
        for (int i = 0; i < n; i++) {
            if (entries[i].slot != slot) continue;
            if (want_char >= 0 && entries[i].character != want_char) continue;
            if (want_ready >= 0 && entries[i].ready != want_ready) continue;
            if (found) *found = entries[i];
            return 1;
        }
    }
}

/* Wait for a lobby line that lists exactly `count` occupied slots. */
static int wait_lobby_count(Client *c, int count, int timeout_ms) {
    long long deadline = now_ms() + timeout_ms;
    char line[TCP_LINE_BUFFER];
    LobbyEntry entries[SERVER_MAX_PLAYERS + 2];
    for (;;) {
        long long left = deadline - now_ms();
        if (left < 0) left = 0;
        if (wait_for_prefix(c, "L", line, sizeof line, (int)left) != 1) return 0;
        if (parse_lobby(line, entries, SERVER_MAX_PLAYERS + 2) == count) return 1;
    }
}

typedef struct {
    int tick, winner, ack;
    char line[SERVER_UDP_PKT_MAX + 1];
} Snapshot;

/* Wait for an "S tick winner ack ..." datagram. Returns 1 on success. */
static int wait_snapshot(int fd, Snapshot *snap, int timeout_ms) {
    long long deadline = now_ms() + timeout_ms;
    for (;;) {
        long long left = deadline - now_ms();
        if (left < 0) left = 0;
        if (!wait_readable(fd, (int)left)) return 0;
        char buf[SERVER_UDP_PKT_MAX + 64];
        ssize_t r = recv(fd, buf, sizeof buf - 1, 0);
        if (r < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return 0;
        }
        buf[r] = '\0';
        if (r > 0 && buf[r - 1] == '\n') buf[r - 1] = '\0';
        if (buf[0] == 'S' &&
            sscanf(buf, "S %d %d %d", &snap->tick, &snap->winner, &snap->ack) == 3) {
            snprintf(snap->line, sizeof snap->line, "%s", buf);
            return 1;
        }
    }
}

static void drain_udp(int fd) {
    char buf[SERVER_UDP_PKT_MAX + 64];
    for (int i = 0; i < 100000 && wait_readable(fd, 0); i++)
        if (recv(fd, buf, sizeof buf, 0) < 0 && errno != EINTR) break;
}

static int count_substring(const char *text, const char *needle) {
    int count = 0;
    for (const char *p = text; (p = strstr(p, needle)) != NULL; p += strlen(needle))
        count++;
    return count;
}

/*
 * Handshake, register UDP, add one bot, ready up, and wait for the first snapshot.
 * Returns 1 once the match is running.
 */
static int start_match(Client *c, Snapshot *first) {
    if (!handshake(c)) return 0;
    c->udp_fd = udp_open_fd();
    if (c->udp_fd < 0) return 0;
    udp_send_to(c->udp_fd, "H %d %llx", c->slot, c->token);
    tcp_send(c, "B 0 0\n");
    if (!wait_lobby_count(c, 2, 2000)) return 0;
    tcp_send(c, "R\n");
    return wait_snapshot(c->udp_fd, first, 3000);
}

/* ------------------------------------------------------------------------ */
/* Tests: handshake and lobby (TCP)                                         */
/* ------------------------------------------------------------------------ */

static void test_handshake_format(void) {
    Client *c = new_client();
    char y_line[TCP_LINE_BUFFER], a_line[TCP_LINE_BUFFER], u_line[TCP_LINE_BUFFER];
    c->tcp_fd = tcp_connect_fd();
    REQUIRE(c->tcp_fd >= 0, "cannot connect to %s:%d", server_host, server_port);

    REQUIRE(read_line(c, y_line, sizeof y_line, 2000) == 1, "no Y line");
    REQUIRE(read_line(c, a_line, sizeof a_line, 2000) == 1, "no A line");
    REQUIRE(read_line(c, u_line, sizeof u_line, 2000) == 1, "no U line");

    int slot = -1;
    CHECK(sscanf(y_line, "Y %d", &slot) == 1, "bad Y line: '%s'", y_line);
    CHECK(slot >= 0 && slot < SERVER_MAX_PLAYERS, "slot %d out of range", slot);

    /* Y line = "Y", slot, then one name per character. Names may contain spaces. */
    CHECK(strlen(y_line) > 4, "Y line has no character names: '%s'", y_line);
    CHECK(a_line[0] == 'A', "A line should start with 'A': '%s'", a_line);

    unsigned long long token = 0;
    CHECK(sscanf(u_line, "U %llx", &token) == 1, "bad U line: '%s'", u_line);
    CHECK(token != 0, "token must be nonzero (zero means 'no token')");
}

static void test_unique_slots_and_tokens(void) {
    Client *a = new_client(), *b = new_client();
    REQUIRE(handshake(a), "client A handshake failed");
    REQUIRE(handshake(b), "client B handshake failed");
    CHECK(a->slot != b->slot, "both clients got slot %d", a->slot);
    CHECK(a->token != b->token, "both clients got the same token");
}

static void test_lobby_initial_state(void) {
    Client *c = new_client();
    LobbyEntry me;
    REQUIRE(handshake(c), "handshake failed");
    REQUIRE(lobby_find(c, c->slot, -1, -1, 2000, &me), "no lobby entry for slot %d", c->slot);
    CHECK(me.character == 0, "default character should be 0, got %d", me.character);
    CHECK(me.ready == 0, "new player should not be ready");
    CHECK(me.bot == 0, "human should have bot == 0");
}

static void test_character_select(void) {
    Client *c = new_client();
    REQUIRE(handshake(c), "handshake failed");

    tcp_send(c, "J 3\n");
    CHECK(lobby_find(c, c->slot, 3, -1, 2000, NULL), "J 3 did not select character 3");

    /* Out-of-range picks fall back to character 0. */
    tcp_send(c, "J 9999\n");
    CHECK(lobby_find(c, c->slot, 0, -1, 2000, NULL), "J 9999 should clamp to 0");
    tcp_send(c, "J -1\n");
    CHECK(lobby_find(c, c->slot, 0, -1, 2000, NULL), "J -1 should clamp to 0");

    /* Garbage must be ignored, not change the pick. */
    tcp_send(c, "J 2\n");
    REQUIRE(lobby_find(c, c->slot, 2, -1, 2000, NULL), "J 2 failed");
    tcp_send(c, "J banana\nHELLO\n\n");
    sleep_ms(150);
    CHECK(lobby_find(c, c->slot, 2, -1, 1000, NULL), "garbage changed the character");
}

static void test_ready_toggle_and_lock(void) {
    Client *c = new_client();
    REQUIRE(handshake(c), "handshake failed");

    tcp_send(c, "J 1\nR\n");
    CHECK(lobby_find(c, c->slot, 1, 1, 2000, NULL), "R did not set ready");

    /* Locked in: J must be ignored while ready. */
    tcp_send(c, "J 4\n");
    sleep_ms(150);
    CHECK(lobby_find(c, c->slot, 1, 1, 1000, NULL), "character changed while ready");

    tcp_send(c, "R\n");
    CHECK(lobby_find(c, c->slot, 1, 0, 2000, NULL), "second R did not clear ready");

    tcp_send(c, "J 4\n");
    CHECK(lobby_find(c, c->slot, 4, 0, 2000, NULL), "J should work again after unready");
}

static void test_line_framing(void) {
    Client *c = new_client();
    REQUIRE(handshake(c), "handshake failed");

    /* One command split across two TCP writes. */
    tcp_send(c, "J");
    sleep_ms(150);
    tcp_send(c, " 5\n");
    CHECK(lobby_find(c, c->slot, 5, -1, 2000, NULL), "split command was not reassembled");

    /* Two commands in one write. */
    tcp_send(c, "J 2\nR\n");
    CHECK(lobby_find(c, c->slot, 2, 1, 2000, NULL), "two commands in one write failed");
}

static void test_overlong_line_disconnects(void) {
    Client *c = new_client();
    char line[TCP_LINE_BUFFER];
    REQUIRE(handshake(c), "handshake failed");

    /* The server's per-player line buffer is 64 bytes; no newline ever arrives. */
    char junk[200];
    memset(junk, 'A', sizeof junk);
    tcp_send_raw(c, junk, sizeof junk);

    int r;
    long long deadline = now_ms() + 2000;
    do {
        r = read_line(c, line, sizeof line, 500);
    } while (r == 1 && now_ms() < deadline);
    CHECK(r == -1, "server should close the connection after an overlong line (r=%d)", r);
}

static void test_bots_add_remove(void) {
    Client *c = new_client();
    char line[TCP_LINE_BUFFER];
    LobbyEntry entries[SERVER_MAX_PLAYERS + 2];
    REQUIRE(handshake(c), "handshake failed");

    tcp_send(c, "B 2 0\n");
    REQUIRE(wait_lobby_count(c, 2, 2000), "bot was not added");

    /* Find the bot's slot in a fresh lobby line. */
    int bot_slot = -1, bot_char = -1, bot_flag = -1;
    long long deadline = now_ms() + 1000;
    while (bot_slot < 0 && now_ms() < deadline) {
        if (wait_for_prefix(c, "L", line, sizeof line, 500) != 1) break;
        int n = parse_lobby(line, entries, SERVER_MAX_PLAYERS + 2);
        for (int i = 0; i < n; i++)
            if (entries[i].bot != 0) {
                bot_slot = entries[i].slot;
                bot_char = entries[i].character;
                bot_flag = entries[i].bot;
            }
    }
    REQUIRE(bot_slot >= 0, "no bot entry found");
    CHECK(bot_char == 2, "bot should have character 2, got %d", bot_char);
    CHECK(bot_flag == 1, "bot field should be AI index + 1 = 1, got %d", bot_flag);

    tcp_send(c, "X %d\n", bot_slot);
    CHECK(wait_lobby_count(c, 1, 2000), "bot was not removed with X");

    /* Removing a human or nonsense must not crash or remove us. */
    tcp_send(c, "X %d\nX -1\nX 9999\n", c->slot);
    sleep_ms(200);
    CHECK(lobby_find(c, c->slot, -1, -1, 1000, NULL), "server dropped us after bad X");
}

static void test_bots_capacity(void) {
    Client *c = new_client();
    char line[TCP_LINE_BUFFER];
    LobbyEntry entries[SERVER_MAX_PLAYERS + 2];
    REQUIRE(handshake(c), "handshake failed");

    for (int i = 0; i < SERVER_MAX_PLAYERS + 5; i++) tcp_send(c, "B 0 0\n");
    sleep_ms(300);

    int max_seen = 0;
    long long deadline = now_ms() + 500;
    while (now_ms() < deadline) {
        if (wait_for_prefix(c, "L", line, sizeof line, 200) != 1) break;
        int n = parse_lobby(line, entries, SERVER_MAX_PLAYERS + 2);
        if (n > max_seen) max_seen = n;
    }
    CHECK(max_seen >= 2, "no bots were added");
    CHECK(max_seen <= SERVER_MAX_PLAYERS, "lobby lists %d entries, max is %d",
          max_seen, SERVER_MAX_PLAYERS);
}

static void test_disconnect_frees_slot(void) {
    Client *a = new_client();
    REQUIRE(handshake(a), "handshake failed");
    int old_slot = a->slot;
    client_close(a);
    sleep_ms(300);

    Client *b = new_client();
    REQUIRE(handshake(b), "reconnect failed");
    CHECK(b->slot == old_slot, "freed slot %d was not reused (got %d)", old_slot, b->slot);
}

static void test_abrupt_resets_survive(void) {
    /* Connect and slam the connection shut with an RST, many times. */
    for (int i = 0; i < 20; i++) {
        int fd = tcp_connect_fd();
        REQUIRE(fd >= 0, "connect %d failed", i);
        struct linger lg = {1, 0};
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
        close(fd);
    }
    sleep_ms(300);
    Client *c = new_client();
    CHECK(handshake(c), "server unusable after abrupt disconnects");
}

static void test_server_full(void) {
    Client *clients[SERVER_MAX_PLAYERS];
    for (int i = 0; i < SERVER_MAX_PLAYERS; i++) {
        clients[i] = new_client();
        REQUIRE(handshake(clients[i]), "client %d could not connect", i);
    }

    /* The next connection is accepted by the OS, then closed with no Y line. */
    Client *extra = new_client();
    char line[TCP_LINE_BUFFER];
    extra->tcp_fd = tcp_connect_fd();
    REQUIRE(extra->tcp_fd >= 0, "extra connect failed");
    int r = read_line(extra, line, sizeof line, 2000);
    CHECK(r == -1, "11th client should be closed without a handshake (r=%d, '%s')",
          r, r == 1 ? line : "");

    /* Freeing one slot lets a new client in again. */
    client_close(clients[3]);
    sleep_ms(300);
    Client *again = new_client();
    REQUIRE(handshake(again), "could not reconnect after freeing a slot");
    CHECK(again->slot == 3, "expected freed slot 3, got %d", again->slot);
}

/* ------------------------------------------------------------------------ */
/* Tests: match and UDP                                                     */
/* ------------------------------------------------------------------------ */

static void test_match_starts_with_bot(void) {
    Client *c = new_client();
    Snapshot s1, s2;
    REQUIRE(start_match(c, &s1), "match did not start (does add_bot() mark bots ready?)");

    CHECK(s1.winner == 0, "winner should be 0 at the start, got %d", s1.winner);

    char want_player[32];
    snprintf(want_player, sizeof want_player, " P %d ", c->slot);
    CHECK(strstr(s1.line, want_player) != NULL, "snapshot has no P entry for our slot");
    CHECK(count_substring(s1.line, " P ") == 2, "expected 2 fighters (human + bot) in snapshot");
    CHECK(strstr(s1.line, " Q ") != NULL, "snapshot is missing our own Q state");

    /* Ticks must advance at roughly TICK_HZ (60/s). */
    long long t0 = now_ms();
    sleep_ms(500);
    udp_send_to(c->udp_fd, "H %d %llx", c->slot, c->token);
    drain_udp(c->udp_fd);
    REQUIRE(wait_snapshot(c->udp_fd, &s2, 1000), "no snapshot after waiting");
    long long elapsed = now_ms() - t0;
    int ticks = s2.tick - s1.tick;
    CHECK(ticks > 0, "tick did not advance (%d -> %d)", s1.tick, s2.tick);
    CHECK(ticks >= elapsed * 60 / 1000 / 2 && ticks <= elapsed * 60 / 1000 * 2 + 5,
          "tick rate off: %d ticks in %lld ms", ticks, elapsed);

    /* Joining mid-match must not break the running match. */
    Client *late = new_client();
    CHECK(handshake(late), "late client could not connect during a match");
}

static void test_udp_input_ack(void) {
    Client *c = new_client();
    Snapshot snap;
    REQUIRE(start_match(c, &snap), "match did not start");
    CHECK(snap.ack == 0, "ack should start at 0, got %d", snap.ack);

    /* Five inputs numbered 0..4. */
    udp_send_to(c->udp_fd, "I %d %llx 0 0 0 0 0 0", c->slot, c->token);

    int ack = 0;
    long long deadline = now_ms() + 2000;
    while (now_ms() < deadline && ack < 5) {
        if (!wait_snapshot(c->udp_fd, &snap, 200)) continue;
        ack = snap.ack;
    }
    CHECK(ack >= 1, "ack never increased after sending inputs");

    /* Let the ack settle, then replay the same packet. It must be ignored. */
    sleep_ms(400);
    drain_udp(c->udp_fd);
    REQUIRE(wait_snapshot(c->udp_fd, &snap, 1000), "no snapshot");
    int settled = snap.ack;

    udp_send_to(c->udp_fd, "I %d %llx 0 0 0 0 0 0", c->slot, c->token);
    sleep_ms(400);
    drain_udp(c->udp_fd);
    REQUIRE(wait_snapshot(c->udp_fd, &snap, 1000), "no snapshot");
    CHECK(snap.ack == settled, "duplicate inputs changed ack (%d -> %d)", settled, snap.ack);

    /* A negative sequence number must be rejected. */
    udp_send_to(c->udp_fd, "I %d %llx -1 1 1 1", c->slot, c->token);
    sleep_ms(300);
    drain_udp(c->udp_fd);
    REQUIRE(wait_snapshot(c->udp_fd, &snap, 1000), "no snapshot");
    CHECK(snap.ack == settled, "negative sequence changed ack (%d -> %d)", settled, snap.ack);

    /* The next in-order inputs (seq 5, 6) must be accepted. */
    udp_send_to(c->udp_fd, "I %d %llx 5 0 0", c->slot, c->token);
    deadline = now_ms() + 2000;
    int advanced = 0;
    while (now_ms() < deadline && !advanced)
        if (wait_snapshot(c->udp_fd, &snap, 200) && snap.ack > settled) advanced = 1;
    CHECK(advanced, "inputs 5-6 were not acknowledged");
}

static void test_udp_forged_packets(void) {
    Client *c = new_client();
    Snapshot snap;
    REQUIRE(start_match(c, &snap), "match did not start");

    c->udp_fd2 = udp_open_fd();
    REQUIRE(c->udp_fd2 >= 0, "second UDP socket failed");

    /* Wrong token, token 0, and every slot number: none may register this socket. */
    for (int slot = -1; slot <= SERVER_MAX_PLAYERS; slot++) {
        udp_send_to(c->udp_fd2, "H %d 0", slot);
        udp_send_to(c->udp_fd2, "H %d deadbeefdeadbeef", slot);
        udp_send_to(c->udp_fd2, "I %d 0 0 1 1 1", slot);
    }
    /* Right slot, wrong token (flip our own token). */
    udp_send_to(c->udp_fd2, "H %d %llx", c->slot, c->token ^ 1ULL);

    sleep_ms(500);
    CHECK(!wait_snapshot(c->udp_fd2, &snap, 500),
          "forged packets registered an address and received snapshots");

    /* The real client must be unaffected. */
    drain_udp(c->udp_fd);
    CHECK(wait_snapshot(c->udp_fd, &snap, 1000), "real client lost its snapshots");
}

static void test_udp_address_rebinding(void) {
    Client *c = new_client();
    Snapshot snap;
    REQUIRE(start_match(c, &snap), "match did not start");

    /* Same player, new source port (simulates a NAT rebinding). */
    c->udp_fd2 = udp_open_fd();
    REQUIRE(c->udp_fd2 >= 0, "second UDP socket failed");
    udp_send_to(c->udp_fd2, "H %d %llx", c->slot, c->token);
    CHECK(wait_snapshot(c->udp_fd2, &snap, 2000),
          "snapshots did not follow the new source address");

    /* Old address should go quiet. */
    sleep_ms(100);
    drain_udp(c->udp_fd);
    CHECK(!wait_snapshot(c->udp_fd, &snap, 400),
          "old address still receives snapshots after rebinding");
}

static void test_udp_garbage_survives(void) {
    Client *c = new_client();
    Snapshot snap;
    REQUIRE(start_match(c, &snap), "match did not start");

    char big[SERVER_UDP_PKT_MAX + 100];
    memset(big, 'A', sizeof big);
    unsigned char binary[64];
    for (size_t i = 0; i < sizeof binary; i++) binary[i] = (unsigned char)(i * 37 + 11);

    send(c->udp_fd, "", 0, 0);
    send(c->udp_fd, "garbage", 7, 0);
    send(c->udp_fd, "I", 1, 0);
    send(c->udp_fd, big, sizeof big, 0);
    send(c->udp_fd, binary, sizeof binary, 0);
    udp_send_to(c->udp_fd, "I 999 1 0");
    udp_send_to(c->udp_fd, "I -1 5 0");
    udp_send_to(c->udp_fd, "I %d zzz 0", c->slot);
    udp_send_to(c->udp_fd, "I %d %llx", c->slot, c->token);
    udp_send_to(c->udp_fd, "I %d %llx 99999999999999999999 1", c->slot, c->token);
    udp_send_to(c->udp_fd, "I %d %llx 4294967296 1", c->slot, c->token);
    udp_send_to(c->udp_fd, "I %d %llx 0 1 2 3 abc 4", c->slot, c->token);
    udp_send_to(c->udp_fd, "I %d %llx 0 99999999999999999999 1", c->slot, c->token);

    /* A packet with far more than UDP_MAX_INPUTS (32) values. */
    char many[SERVER_UDP_PKT_MAX];
    int n = snprintf(many, sizeof many, "I %d %llx 0", c->slot, c->token);
    for (int i = 0; i < 300 && n < (int)sizeof many - 4; i++)
        n += snprintf(many + n, sizeof many - (size_t)n, " 1");
    send(c->udp_fd, many, (size_t)n, 0);

    /* A flood larger than the 256-packet-per-call limit. */
    for (int i = 0; i < 1000; i++)
        udp_send_to(c->udp_fd, "H %d %llx", c->slot, c->token);

    sleep_ms(300);
    drain_udp(c->udp_fd);
    CHECK(wait_snapshot(c->udp_fd, &snap, 2000), "server stopped sending snapshots");

    char line[TCP_LINE_BUFFER];
    CHECK(read_line(c, line, sizeof line, 0) != -1, "TCP connection was dropped");

    Client *probe = new_client();
    CHECK(handshake(probe), "server no longer accepts connections");
}

static void test_udp_timeout_drops_player(void) {
    Client *c = new_client();
    Snapshot snap;
    char line[TCP_LINE_BUFFER];
    REQUIRE(start_match(c, &snap), "match did not start");

    /* Go silent over UDP. The server should close our TCP connection. */
    long long start = now_ms();
    int r;
    do {
        r = read_line(c, line, sizeof line, 9000);
    } while (r == 1);
    long long elapsed = now_ms() - start;

    CHECK(r == -1, "player was not dropped after going silent");
    CHECK(elapsed >= SERVER_UDP_TIMEOUT_MS - 600 && elapsed <= SERVER_UDP_TIMEOUT_MS + 2500,
          "dropped after %lld ms, expected about %d ms", elapsed, SERVER_UDP_TIMEOUT_MS);
}

static void test_udp_keepalive_prevents_timeout(void) {
    Client *c = new_client();
    Snapshot snap;
    char line[TCP_LINE_BUFFER];
    REQUIRE(start_match(c, &snap), "match did not start");

    /* Keep sending "H" for longer than the timeout. */
    long long end = now_ms() + SERVER_UDP_TIMEOUT_MS + 3000;
    while (now_ms() < end) {
        udp_send_to(c->udp_fd, "H %d %llx", c->slot, c->token);
        sleep_ms(500);
        drain_udp(c->udp_fd);
    }
    CHECK(read_line(c, line, sizeof line, 0) != -1, "player dropped despite sending keepalives");
}

/* ------------------------------------------------------------------------ */
/* Runner                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct {
    const char *name;
    void (*fn)(void);
    int slow;
} TestCase;

static const TestCase tests[] = {
    {"handshake_format", test_handshake_format, 0},
    {"unique_slots_and_tokens", test_unique_slots_and_tokens, 0},
    {"lobby_initial_state", test_lobby_initial_state, 0},
    {"character_select", test_character_select, 0},
    {"ready_toggle_and_lock", test_ready_toggle_and_lock, 0},
    {"line_framing", test_line_framing, 0},
    {"overlong_line_disconnects", test_overlong_line_disconnects, 0},
    {"bots_add_remove", test_bots_add_remove, 0},
    {"bots_capacity", test_bots_capacity, 0},
    {"disconnect_frees_slot", test_disconnect_frees_slot, 0},
    {"abrupt_resets_survive", test_abrupt_resets_survive, 0},
    {"server_full", test_server_full, 0},
    {"match_starts_with_bot", test_match_starts_with_bot, 0},
    {"udp_input_ack", test_udp_input_ack, 0},
    {"udp_forged_packets", test_udp_forged_packets, 0},
    {"udp_address_rebinding", test_udp_address_rebinding, 0},
    {"udp_garbage_survives", test_udp_garbage_survives, 0},
    {"udp_timeout_drops_player", test_udp_timeout_drops_player, 1},
    {"udp_keepalive_prevents_timeout", test_udp_keepalive_prevents_timeout, 1},
};
#define TEST_COUNT ((int)(sizeof tests / sizeof tests[0]))

static int start_server(void) {
    server_pid = fork();
    if (server_pid < 0) return 0;
    if (server_pid == 0) {
        if (!verbose) {
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd >= 0) {
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
            }
        }
        execl(server_path, server_path, (char *)NULL);
        _exit(127);
    }
    for (int i = 0; i < 60; i++) {
        int fd = tcp_connect_fd();
        if (fd >= 0) {
            close(fd);
            sleep_ms(150);
            return 1;
        }
        sleep_ms(50);
    }
    return 0;
}

static void stop_server(void) {
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        waitpid(server_pid, NULL, 0);
        server_pid = -1;
    }
}

static int name_selected(const char *name, int argc, char **names) {
    if (argc == 0) return 1;
    for (int i = 0; i < argc; i++)
        if (strcmp(names[i], name) == 0) return 1;
    return 0;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-H host] [-p port] [-s server_binary] [--slow] [-v] [test ...]\n"
            "tests:\n",
            prog);
    for (int i = 0; i < TEST_COUNT; i++)
        fprintf(stderr, "  %s%s\n", tests[i].name, tests[i].slow ? "  (slow)" : "");
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);

    char *selected[64];
    int selected_count = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-H") && i + 1 < argc) server_host = argv[++i];
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) server_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) server_path = argv[++i];
        else if (!strcmp(argv[i], "--slow")) run_slow_tests = 1;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (argv[i][0] == '-') { usage(argv[0]); return 2; }
        else if (selected_count < 64) selected[selected_count++] = argv[i];
    }

    int passed = 0, failed = 0, skipped = 0;
    for (int t = 0; t < TEST_COUNT; t++) {
        const TestCase *tc = &tests[t];
        if (!name_selected(tc->name, selected_count, selected)) continue;
        if (tc->slow && !run_slow_tests && selected_count == 0) {
            printf("SKIP  %s (use --slow)\n", tc->name);
            skipped++;
            continue;
        }

        if (server_path && !start_server()) {
            printf("ERROR %s: could not start server '%s'\n", tc->name, server_path);
            stop_server();
            failed++;
            continue;
        }

        int failures_before = fail_count;
        printf("RUN   %s\n", tc->name);
        tc->fn();
        release_all_clients();
        stop_server();

        if (fail_count == failures_before) {
            printf("PASS  %s\n", tc->name);
            passed++;
        } else {
            printf("FAIL  %s\n", tc->name);
            failed++;
        }
    }

    printf("\n%d passed, %d failed, %d skipped (%d checks, %d failed checks)\n",
           passed, failed, skipped, check_count, fail_count);
    return failed ? 1 : 0;
}
