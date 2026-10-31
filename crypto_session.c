#define _POSIX_C_SOURCE 200809L

#include "crypto_session.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sodium.h>

static char session_password[256];
static int password_set;

void crypto_session_set_password(const char *password)
{
    password_set = 0;
    session_password[0] = '\0';
    if (!password || !password[0])
        return;
    snprintf(session_password, sizeof session_password, "%s", password);
    password_set = 1;
}

int crypto_session_init(void)
{
    if (sodium_init() < 0)
        return -1;
    return 0;
}

void crypto_session_begin(CryptoSession *s)
{
    memset(s, 0, sizeof *s);
    randombytes_buf(s->sk, sizeof s->sk);
    crypto_scalarmult_base(s->pk, s->sk);
}

static void derive_session_key(unsigned char *key, const unsigned char *shared)
{
    crypto_generichash_state st;
    crypto_generichash_init(&st, NULL, 0, crypto_secretbox_KEYBYTES);
    crypto_generichash_update(&st, shared, crypto_scalarmult_BYTES);
    if (password_set)
        crypto_generichash_update(
            &st, (const unsigned char *)session_password,
            strlen(session_password));
    crypto_generichash_final(&st, key, crypto_secretbox_KEYBYTES);
}

static int parse_k_line(const char *line, unsigned char *peer_pk)
{
    if (!line || line[0] != 'K' || line[1] != ' ')
        return -1;
    const char *hex = line + 2;
    size_t hex_len = strlen(hex);
    while (hex_len > 0 && (hex[hex_len - 1] == '\n' || hex[hex_len - 1] == '\r'))
        hex_len--;
    if (hex_len != (size_t)crypto_scalarmult_BYTES * 2)
        return -1;
    if (sodium_hex2bin(peer_pk, (size_t)crypto_scalarmult_BYTES, hex, hex_len,
                       NULL, NULL, NULL) != 0)
        return -1;
    return 0;
}

int crypto_session_format_k_line(char *buf, size_t buf_size, const CryptoSession *s)
{
    char hex[CRYPTO_SESSION_PK_HEX_LEN + 1];
    if (sodium_bin2hex(hex, sizeof hex, s->pk, sizeof s->pk) == NULL)
        return -1;
    return snprintf(buf, buf_size, "K %s\n", hex);
}

int crypto_session_finish_handshake(CryptoSession *s, const char *k_line)
{
    unsigned char peer_pk[crypto_scalarmult_BYTES];
    unsigned char shared[crypto_scalarmult_BYTES];

    if (s->established)
        return -1;
    if (parse_k_line(k_line, peer_pk) != 0)
        return -1;
    if (crypto_scalarmult(shared, s->sk, peer_pk) != 0)
        return -1;
    derive_session_key(s->key, shared);
    sodium_memzero(shared, sizeof shared);
    s->established = 1;
    s->tcp_frame_len = 0;
    s->tcp_rx_len = 0;
    s->plain_len = 0;
    return 0;
}

static int tcp_send_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    while (len > 0) {
        ssize_t r = send(fd, p, len, 0);
        if (r < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (r <= 0)
            return -1;
        p += (size_t)r;
        len -= (size_t)r;
    }
    return 0;
}

static int tcp_send_frame(int fd, CryptoSession *s, const unsigned char *plain,
                          size_t plain_len)
{
    if (plain_len + crypto_secretbox_MACBYTES > CRYPTO_SESSION_TCP_IO_MAX)
        return -1;

    unsigned char boxed[CRYPTO_SESSION_TCP_IO_MAX];
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    size_t boxed_len = plain_len + crypto_secretbox_MACBYTES;
    randombytes_buf(nonce, sizeof nonce);

    if (crypto_secretbox_easy(boxed, plain, plain_len, nonce, s->key) != 0)
        return -1;

    size_t payload = crypto_secretbox_NONCEBYTES + boxed_len;
    if (payload > 65535)
        return -1;

    unsigned char frame[2 + CRYPTO_SESSION_TCP_IO_MAX + crypto_secretbox_NONCEBYTES];
    frame[0] = (unsigned char)((payload >> 8) & 0xff);
    frame[1] = (unsigned char)(payload & 0xff);
    memcpy(frame + 2, nonce, crypto_secretbox_NONCEBYTES);
    memcpy(frame + 2 + crypto_secretbox_NONCEBYTES, boxed, boxed_len);

    return tcp_send_all(fd, frame, 2 + payload);
}

int crypto_session_tcp_send(int fd, CryptoSession *s, const void *plain,
                            size_t plain_len)
{
    if (!s->established)
        return -1;
    return tcp_send_frame(fd, s, plain, plain_len);
}

static int dispatch_plain_lines(CryptoSession *s, crypto_session_line_fn line_fn,
                                void *ctx)
{
    char *start = (char *)s->plain_accum;
    char *end = start + s->plain_len;
    char *nl;

    while ((nl = memchr(start, '\n', (size_t)(end - start))) != NULL) {
        *nl = '\0';
        if (line_fn(ctx, start) != 0)
            return -1;
        start = nl + 1;
    }
    if (start != (char *)s->plain_accum) {
        size_t remain = (size_t)(end - start);
        memmove(s->plain_accum, start, remain);
        s->plain_len = (int)remain;
    }
    return 0;
}

static int decrypt_tcp_frame(CryptoSession *s, crypto_session_line_fn line_fn,
                             void *ctx)
{
    if (s->tcp_frame_len < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)
        return -1;

    const unsigned char *nonce = s->tcp_rx;
    const unsigned char *boxed = s->tcp_rx + crypto_secretbox_NONCEBYTES;
    size_t boxed_len = (size_t)s->tcp_frame_len - crypto_secretbox_NONCEBYTES;

    unsigned char chunk[CRYPTO_SESSION_TCP_IO_MAX];
    if (boxed_len > sizeof chunk)
        return -1;
    if (crypto_secretbox_open_easy(chunk, boxed, boxed_len, nonce, s->key) != 0)
        return -1;

    size_t chunk_len = boxed_len - crypto_secretbox_MACBYTES;
    if (s->plain_len + (int)chunk_len > (int)sizeof s->plain_accum)
        return -1;
    memcpy(s->plain_accum + s->plain_len, chunk, chunk_len);
    s->plain_len += (int)chunk_len;
    s->tcp_rx_len = 0;
    s->tcp_frame_len = 0;

    return dispatch_plain_lines(s, line_fn, ctx);
}

int crypto_session_tcp_feed(CryptoSession *s, const void *data, size_t len,
                            crypto_session_line_fn line_fn, void *ctx)
{
    if (!s->established)
        return -1;

    const unsigned char *p = data;
    size_t left = len;

    while (left > 0) {
        if (s->tcp_frame_len == 0) {
            int need = 2 - s->tcp_rx_len;
            if ((int)left < need) {
                memcpy(s->tcp_rx + s->tcp_rx_len, p, left);
                s->tcp_rx_len += (int)left;
                return 0;
            }
            memcpy(s->tcp_rx + s->tcp_rx_len, p, (size_t)need);
            p += need;
            left -= (size_t)need;
            s->tcp_frame_len =
                ((int)s->tcp_rx[0] << 8) | (int)s->tcp_rx[1];
            s->tcp_rx_len = 0;
            if (s->tcp_frame_len < crypto_secretbox_NONCEBYTES +
                                       crypto_secretbox_MACBYTES ||
                s->tcp_frame_len > CRYPTO_SESSION_TCP_IO_MAX)
                return -1;
            continue;
        }

        int room = s->tcp_frame_len - s->tcp_rx_len;
        int take = (int)left < room ? (int)left : room;
        memcpy(s->tcp_rx + s->tcp_rx_len, p, (size_t)take);
        s->tcp_rx_len += take;
        p += take;
        left -= (size_t)take;

        if (s->tcp_rx_len >= s->tcp_frame_len) {
            if (decrypt_tcp_frame(s, line_fn, ctx) != 0)
                return -1;
        }
    }
    return 0;
}

int crypto_session_udp_seal(const CryptoSession *s, unsigned char *out,
                            size_t out_cap, const void *plain, size_t plain_len,
                            size_t *out_len)
{
    if (!s->established)
        return -1;
    size_t need = crypto_secretbox_NONCEBYTES + plain_len +
                  crypto_secretbox_MACBYTES;
    if (out_cap < need)
        return -1;

    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof nonce);
    memcpy(out, nonce, sizeof nonce);
    if (crypto_secretbox_easy(out + crypto_secretbox_NONCEBYTES, plain, plain_len,
                              nonce, s->key) != 0)
        return -1;
    *out_len = need;
    return 0;
}

int crypto_session_udp_open(const CryptoSession *s, const void *packet,
                            size_t packet_len, char *plain, size_t plain_cap,
                            size_t *plain_len)
{
    if (!s->established)
        return -1;
    if (packet_len <
        crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)
        return -1;

    const unsigned char *p = packet;
    const unsigned char *nonce = p;
    const unsigned char *boxed = p + crypto_secretbox_NONCEBYTES;
    size_t boxed_len = packet_len - crypto_secretbox_NONCEBYTES;
    unsigned long long mlen;
    if (crypto_secretbox_open_easy((unsigned char *)plain, boxed, boxed_len,
                                   nonce, s->key) != 0)
        return -1;
    mlen = boxed_len - crypto_secretbox_MACBYTES;
    if (mlen >= plain_cap)
        return -1;
    plain[mlen] = '\0';
    *plain_len = (size_t)mlen;
    return 0;
}
