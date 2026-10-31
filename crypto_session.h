#ifndef CRYPTO_SESSION_H
#define CRYPTO_SESSION_H

#include <stddef.h>

/* Ephemeral X25519 handshake (cleartext K lines), then crypto_secretbox for
   all game traffic. Optional password mixed into the session key via generichash.
   No server.h dependency — safe for client and tests. */

#define CRYPTO_SESSION_TCP_IO_MAX 4096
#define CRYPTO_SESSION_PK_HEX_LEN 64 /* 32 bytes * 2 */

typedef struct CryptoSession {
    unsigned char pk[32];
    unsigned char sk[32];
    unsigned char key[32];
    int established;

    /* TCP stream reassembly: uint16_be length + secretbox blob */
    unsigned char tcp_rx[CRYPTO_SESSION_TCP_IO_MAX];
    int tcp_rx_len;
    int tcp_frame_len; /* 0 = need 2-byte header; else payload bytes remaining */

    unsigned char plain_accum[CRYPTO_SESSION_TCP_IO_MAX];
    int plain_len;
} CryptoSession;

/* libsodium init; call once at process start. Returns 0 on success. */
int crypto_session_init(void);

/* Optional lobby password (NULL or "" = ECDH only). Applies to new handshakes. */
void crypto_session_set_password(const char *password);

/* Generate ephemeral keypair; not yet established. */
void crypto_session_begin(CryptoSession *s);

/* Write "K <hex_pk>\n" into buf. Returns length or -1. */
int crypto_session_format_k_line(char *buf, size_t buf_size, const CryptoSession *s);

/* Parse "K <hex>\n" and derive session key from peer pk. Returns 0 or -1. */
int crypto_session_finish_handshake(CryptoSession *s, const char *k_line);

/* Send one plaintext chunk (need not include '\n'; caller sends whole lines). */
int crypto_session_tcp_send(int fd, CryptoSession *s, const void *plain,
                            size_t plain_len);

/* Feed raw TCP bytes; calls line_fn for each complete decrypted '\n'-terminated
   line (NUL-terminated, newline stripped). Returns 0, or -1 on crypto/desync. */
typedef int (*crypto_session_line_fn)(void *ctx, char *line);
int crypto_session_tcp_feed(CryptoSession *s, const void *data, size_t len,
                            crypto_session_line_fn line_fn, void *ctx);

/* UDP: random nonce + secretbox. out must hold plain_len + 24 + 16. */
int crypto_session_udp_seal(const CryptoSession *s, unsigned char *out,
                            size_t out_cap, const void *plain, size_t plain_len,
                            size_t *out_len);

/* Returns 0 and sets plain_len on success; -1 on bad packet (silent drop). */
int crypto_session_udp_open(const CryptoSession *s, const void *packet,
                            size_t packet_len, char *plain, size_t plain_cap,
                            size_t *plain_len);

#endif
