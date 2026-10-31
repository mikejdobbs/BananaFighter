#ifndef SERVER_EMBED_H
#define SERVER_EMBED_H

/* Public API for running the game server in-process (listen server) or
   from the dedicated server binary. Do not include server.h from the client. */

/* Optional lobby password (NULL or "" clears). Must match the client when using
   BANANAFIGHTER_KEY / -k. Call before server_embed_start(). */
void server_embed_set_session_password(const char *password);

/* Bind PORT and spawn the poll loop on a background thread. Returns 0 on
   success, -1 on failure (e.g. port already in use). Idempotent if already
   running: returns 0 without starting a second thread. */
int server_embed_start(void);

/* Request the loop to stop, close sockets so poll wakes, and join the thread.
   Safe if start was never called or already stopped. */
void server_embed_stop(void);

/* Run the poll loop on the calling thread until server_embed_stop() (from
   another thread) or a fatal error. Used by the dedicated server binary.
   Returns only after stop; returns -1 if sockets could not be set up. */
int server_run_forever(void);

#endif
