#include "server.h"

/* Select screen (TCP): "L {U slot char ready bot}..."   (bot = 0 for humans,
   else AI index + 1) Match (UDP): "S tick winner ack {P slot ch x y hp lives
   meter face state invis}... Q vx vy frame stun grace invis_ticks hit fuel {J
   owner x y}... {B owner x y}... {Z owner x vx}... {X x y t}..." state: >= 0
   attacking (move index), -1 idle, -2 stunned, -3 blocking, -4 flying, -5
   crouching, -6 crouching and blocking. J = shot, B = bomb, Z = ship, X = explosion. Q (viewer's own extra
   state) lets the client resume simulation. Invisible opponents have x, y and
   state hidden. */

void send_state(int v) {
    char buf[UDP_PKT_MAX];
    int n;

    if (phase == LOBBY) {
        n = snprintf(buf, sizeof buf, "L");
        for (int k = 0; k < MAX_PLAYERS; k++)
            if (player_is_person_or_bot(&players[k]) &&
                n < (int)sizeof buf - 48)
                n += snprintf(buf + n, sizeof buf - n, " U %d %d %d %d", k,
                              players[k].s_character, players[k].ready,
                              players[k].bot ? players[k].ai + 1 : 0);
    } else { // NOT IN LOBBY
        n = snprintf(buf, sizeof buf, "S %d %d %d", tick, winner,
                     players[v].num_ack_by_server);

        // share players
        for (int k = 0; k < MAX_PLAYERS; k++) {
            Player *p = &players[k];
            if (!p->in_match)
                continue;

            // hide invisible players to everyone but themself
            int hide = p->body.invis && k != v;

            int st = p->body.stun ? -2
                   : (p->body.block && p->body.crouch) ? -6   /* crouching block */
                   : p->body.block ? -3
                   : p->body.move >= 0 ? p->body.move
                   : p->body.flying ? -4
                   : p->body.crouch ? -5
                   : -1;
            
            n += snprintf(buf + n, sizeof buf - n,
                          " P %d %d %d %d %d %d %d %d %d %d", k, p->s_character,
                          hide ? -1 : p->body.x, hide ? -1 : p->body.y, p->hp,
                          p->lives, p->body.meter, p->body.face, hide ? -1 : st,
                          p->body.invis > 0);
        }

        // show player their stats
        if (players[v].in_match) {
            Body *b = &players[v].body;
            n += snprintf(buf + n, sizeof buf - n, " Q %d %d %d %d %d %d %d %d",
                          b->vx, b->vy, b->frame, b->stun, b->grace, b->invis,
                          b->hit, b->fuel);
        }

        // show projectiles
        for (int k = 0; k < MAXPROJ; k++)
            if (projectiles[k].on && n < (int)sizeof buf - 48)
                n += snprintf(buf + n, sizeof buf - n, " %c %d %d %d",
                              projectiles[k].bomb ? 'B' : 'J',
                              projectiles[k].owner, projectiles[k].x,
                              projectiles[k].y);

        // show ships
        for (int k = 0; k < MAX_PLAYERS; k++)
            if (ships[k].on && n < (int)sizeof buf - 48)
                n += snprintf(buf + n, sizeof buf - n, " Z %d %d %d", k,
                              ships[k].x, ships[k].vx);

        // show explosions
        for (int k = 0; k < MAXBOOM; k++)
            if (explosions[k].on && n < (int)sizeof buf - 48)
                n +=
                    snprintf(buf + n, sizeof buf - n, " X %d %d %d",
                             explosions[k].x, explosions[k].y, explosions[k].t);
    }

    if (n > (int)sizeof buf - 2)
        n = (int)sizeof buf - 2; /* snprintf reports the untruncated length */
    buf[n++] = '\n';

    if (phase == LOBBY) { /* reliable: TCP */
        ssize_t r = send(players[v].fd, buf, n, 0);
        if (r != n && !(r < 0 && errno == EAGAIN))
            drop(v);                /* error or partial write */
    } else if (players[v].udp_ok) { /* lossy is fine: UDP */
        sendto(udp_fd, buf, n, 0, (struct sockaddr *)&players[v].udp_addr,
               sizeof players[v].udp_addr);
    }
}

static void push_input(Player *player, int mask) {
    if (player->q_num == INPUT_QMAX) { /* full: drop the oldest */
        player->q_head = (player->q_head + 1) % INPUT_QMAX;
        player->q_num--;
        player->num_ack_by_server++;
    }
    player->q[(player->q_head + player->q_num++) % INPUT_QMAX] = mask;
}

/* Inputs arrive as seq, seq+1, ... and may be duplicated, late or missing. Skip
   ones already seen; for a gap, repeat the last input so the numbering (and the
   ack) stays aligned. */
static void apply_udp_inputs(Player *player, uint32_t seq, const int *inputs,
                             int count) {
    for (int j = 0; j < count; j++) {

        uint32_t s = (uint32_t)seq + j;
        int32_t d = (int32_t)(s - player->next_seq); /* signed distance */
        if (d < 0)
            continue; /* duplicate */
        int gap = d;
        if (gap > MAX_UDP_GAP)
            continue; /* or drop the player */

        int fill = gap < INPUT_QMAX ? gap : INPUT_QMAX;
        for (int g = 0; g < fill; g++)
            push_input(player, player->last_in);

        player->num_ack_by_server +=
            gap - fill; /* a huge jump: count skipped inputs as consumed */
        push_input(player, inputs[j]);
        player->last_in = inputs[j];
        player->next_seq = s + 1;
    }
}

static void handle_line_in_lobby(int player_num, char *line) {
    Player *player = &players[player_num];
    int character, ai;

    if (phase == LOBBY && !player->in_match &&
        sscanf(line, "B %d %d", &character, &ai) == 2)
        add_bot(character, ai); /* B <char> <ai>: any number of bots */
    else if (phase == LOBBY && !player->in_match &&
             sscanf(line, "X %d", &character) == 1)
        del_bot(character); /* X <slot> removes a bot */
    else if (sscanf(line, "J %d", &character) == 1 && !player->in_match &&
             !player->ready)
        player->s_character =
            (character >= 0 && character < NCHARS)
                ? character
                : 0; /* J <char>: hover/pick; locked-in fighters can't change */
    else if (line[0] == 'R' && phase == LOBBY && !player->in_match)
        player->ready ^= 1;
}

void read_client_in_lobby(int i) {
    Player *player = &players[i];
    ssize_t n = recv(player->fd, player->buf + player->len,
                     sizeof player->buf - player->len - 1, 0);

    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
        drop(i);
        return;
    }

    if (n < 0)
        return;

    player->len += n;
    char *newline;
    while ((newline = memchr(player->buf, '\n', player->len))) {
        *newline = 0;
        handle_line_in_lobby(i, player->buf);
        int used = (int)(newline - player->buf) + 1;
        memmove(player->buf, player->buf + used, player->len - used);
        player->len -= used;
    }
    if (player->len >= (int)sizeof player->buf - 1)
        drop(i); /* line too long */
}

/*
 * Drain the UDP socket. Every packet must name a connected player slot and
 * carry that player's token.
 *
 * Packet format:
 *   <type> <slot> <token-hex> [sequence input0 input1 ...]
 *
 * Gameplay input packets use type 'I'.
 */

void read_udp(void) {
    for (int packets_processed = 0;
         packets_processed < MAX_UDP_PACKETS_PER_CALL; packets_processed++) {

        /* Receive one packet; the extra byte leaves room for a terminator. */
        char packet[UDP_PKT_MAX + 1];
        struct sockaddr_in sender_addr;
        socklen_t sender_addr_len = sizeof sender_addr;

        ssize_t bytes_received =
            recvfrom(udp_fd, packet, UDP_PKT_MAX, 0,
                     (struct sockaddr *)&sender_addr, &sender_addr_len);

        /* Stop when no packet is available (normally EAGAIN: drained). */
        if (bytes_received < 0)
            return;

        /* Make the packet a C string so sscanf/strto* can parse it. */
        packet[bytes_received] = '\0';

        /* Parse the header: type, player slot, and hexadecimal token. */
        char packet_type;
        int player_slot;
        unsigned long long packet_token;
        int header_length = 0;

        if (sscanf(packet, "%c %d %llx %n", &packet_type, &player_slot,
                   &packet_token, &header_length) != 3)
            continue; /* malformed header */

        /* Reject slot numbers outside the player array. */
        if (player_slot < 0 || player_slot >= MAX_PLAYERS)
            continue;

        /* Reject packets for unused slots or with a wrong token. */
        Player *player = &players[player_slot];
        if (player->fd < 0 || player->token == 0 ||
            packet_token != player->token)
            continue; /* unknown or forged */

        /* Authenticated: remember where to send this player's UDP updates.
         * Updating every time follows NAT address/port rebinding. */
        player->udp_addr = sender_addr;
        player->udp_ok = 1;
        player->last_udp_ms = now_ms();

        /* Only input packets during active play, from players in the match,
         * carry data that needs to be parsed. */
        if (packet_type != 'I' || phase != PLAY || !player->in_match ||
            header_length == 0)
            continue;

        /* Start parsing just after the header. */
        char *parse_cursor = packet + header_length;
        char *parse_end;

        /* Skip whitespace before the sequence number. */
        while (*parse_cursor == ' ' || *parse_cursor == '\t')
            parse_cursor++;

        /* strtoull() accepts a leading '-', so reject it explicitly. */
        if (*parse_cursor == '-')
            continue;

        /* Parse the sequence number of the first input in this packet. */
        errno = 0;
        unsigned long long parsed_sequence =
            strtoull(parse_cursor, &parse_end, 10);

        /* Reject missing digits, overflow, or values too large for 32 bits. */
        if (parse_end == parse_cursor || errno == ERANGE ||
            parsed_sequence > UINT32_MAX)
            continue;

        uint32_t first_sequence = (uint32_t)parsed_sequence;

        /* Parse the input values that follow the sequence number. */
        int input_masks[UDP_MAX_INPUTS];
        int input_count = 0;

        parse_cursor = parse_end;
        while (input_count < UDP_MAX_INPUTS) {
            long parsed_input = strtol(parse_cursor, &parse_end, 10);

            /* Stop at the end of the packet or at non-numeric text. */
            if (parse_end == parse_cursor)
                break;

            input_masks[input_count++] = (int)parsed_input;
            parse_cursor = parse_end;
        }

        /* Queue the inputs, handling duplicates, gaps, and acknowledgements. */
        apply_udp_inputs(player, first_sequence, input_masks, input_count);
    }
}

/* In a match a human must keep talking over UDP, or they are dropped. */
void check_udp_timeouts(void) {
    long long now = now_ms();
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (players[i].fd >= 0 && players[i].in_match &&
            now - players[i].last_udp_ms > UDP_TIMEOUT_MS) {
            fprintf(stderr, "slot %d: no UDP for %d ms\n", i, UDP_TIMEOUT_MS);
            drop(i);
        }
}

void accept_client_to_lobby(int listen_fd) {
    int fd = accept(listen_fd, NULL, NULL), one = 1, slot = -1;

    if (fd < 0)
        return;

    for (int i = 0; i < MAX_PLAYERS; i++)
        if (!player_is_person_or_bot(&players[i])) {
            slot = i;
            break;
        }

    /* Check if server is full */
    if (slot < 0) {
        close(fd);
        return;
    }

    /* Setup player struct */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    memset(&players[slot], 0, sizeof players[slot]);
    players[slot].fd = fd;
    players[slot].body.move = -1;
    players[slot].token = make_token();

    /* Reuse this buffer to construct each outgoing protocol message. */
    char message_buffer[256];

    /*
     * Send the assigned player slot and available character names:
     * "Y slot name0 name1 ...\n"
     */
    int message_length =
        snprintf(message_buffer, sizeof message_buffer, "Y %d", slot);

    /* Append each character name to the message. */
    for (int character_index = 0; character_index < NCHARS; character_index++) {
        message_length += snprintf(message_buffer + message_length,
                                   sizeof message_buffer - message_length,
                                   " %s", chars[character_index].name);
    }

    /* Append the line terminator and send the completed message. */
    message_buffer[message_length++] = '\n';
    send(fd, message_buffer, message_length, 0);

    /*
     * Send the available AI names:
     * "A ai0 ai1 ...\n"
     */
    message_length = snprintf(message_buffer, sizeof message_buffer, "A");

    for (int ai_index = 0; ai_index < NAIS; ai_index++) {
        message_length += snprintf(message_buffer + message_length,
                                   sizeof message_buffer - message_length,
                                   " %s", ais[ai_index].name);
    }

    message_buffer[message_length++] = '\n';
    send(fd, message_buffer, message_length, 0);

    /*
     * Send the token the client must include in its UDP packets:
     * "U token\n"
     */
    message_length = snprintf(message_buffer, sizeof message_buffer, "U %llx\n",
                              players[slot].token);

    send(fd, message_buffer, message_length, 0);

    /* Record the successful connection in the server log. */
    fprintf(stderr, "slot %d connected\n", slot);
}
