# BananaFighter
A OSS Fighting Game

A small networked fighting game: a C server that runs the match simulation, and an optional SDL3 client.
Up to `MAX_PLAYERS` fighters (humans and computer-controlled bots) share one stage. The server is authoritative and
ticks at `TICK_HZ`.

Server logic lives once in the `fighter_server` library. The dedicated `./server` binary and the client both link it.
With no arguments, `./client` starts an embedded listen server on a background thread and connects to
`127.0.0.1:PORT`. Pass a host (and optional port) to join a remote or dedicated server instead.

## Layout

```
CMakeLists.txt
sim.h, config.h          shared by client and server (simulation types, constants)
server/
  server.h               types, shared state, prototypes, protocol notes
  embed.h / embed.c      public start/stop API and poll() loop (pthread for listen)
  main.c                 dedicated server entry (calls server_run_forever)
  util.c                 now_ms, make_token, overlap, nearest, slot helpers
  state.c                global state, lobby/match lifecycle, bots, drop()
  combat.c               hits, projectiles, bombs, ships, melee
  sim.c                  per-tick simulation: input, bot AI calls, step()
  net.c                  TCP lobby protocol, UDP input/snapshots, accept
  ai.h                   bot AI plugins (rusher, zoner, turtle)
client/                  SDL3 client (main, audio, input, network, render)
sprites/, sounds/        client assets
```

## Building

Requires CMake 3.16+ and a C11 compiler. The server needs only POSIX sockets. The client needs SDL3 and is skipped
(with a warning) if SDL3 is not found.

```
cmake -B build
cmake --build build
./build/client              # hosts locally (embedded server) and plays
./build/server              # dedicated / headless (for other machines or CI)
./build/client 192.0.2.1    # join a remote host (optional port as 2nd arg)
```

For Xcode: `cmake -B buildx -G Xcode`. Headers are listed as target sources so they appear in the project.

The server is built with `-std=c11` (extensions off) and defines `_POSIX_C_SOURCE 200809L` at the top of
`server/server.h`; keep that define before any system include. The client must include `embed.h` only, not
`server.h` (overlapping types and symbols).

## How it works

The server uses one `poll()` loop over the TCP listener, each client socket and the UDP socket. In the dedicated
binary that loop owns the process; in the client it runs on a background thread. Every `1000 / TICK_HZ` ms it runs
`step()`, checks UDP timeouts, then sends each client its state.

Two phases:

- **LOBBY:** fighter select. Players pick characters, add or remove bots and ready up. A match starts when at least
  one human is connected, there are at least `MIN_PLAYERS` fighters and everyone is ready.
- **PLAY:** a match. The last fighter with lives remaining wins (`winner` = slot + 1, or 0 for a draw). After
  `ROUND_WAIT` ticks the server returns to the lobby.

## Network protocol

Both transports use the same port number. All messages are newline-terminated text.

### TCP: lobby and handshake

On connect the server sends:

| Line | Meaning |
|---|---|
| `Y slot name0 name1 ...` | your slot and the character names |
| `A ai0 ai1 ...` | available bot AI names |
| `U token` | hex UDP credential; echo it in every UDP packet |

Client commands (lobby only unless noted):

| Command | Effect |
|---|---|
| `B <char> <ai>` | add a bot with that character and AI |
| `X <slot>` | remove a bot |
| `J <char>` | hover/pick a character (not allowed once ready) |
| `R` | toggle ready |

While in the lobby the server sends `L {U slot char ready bot}...` each tick (`bot` is 0 for humans, otherwise
AI index + 1).

### UDP: in-match

Client to server, one datagram each:

| Packet | Meaning |
|---|---|
| `H slot token` | hello / keepalive; registers the client's UDP address |
| `I slot token seq m0 m1 ...` | inputs numbered `seq`, `seq+1`, ...; resend every input not yet acked |

`seq` starts at 0 each match. Duplicates are skipped, and gaps are filled by repeating the last input.

Server to client: `S tick winner ack {P ...}... Q ... {J|B|Z|X ...}...`

- `P slot ch x y hp lives meter face state invis` for each fighter in the match
- `Q vx vy frame stun grace invis_ticks hit fuel` for the viewer's own extra state
- `J` shot, `B` bomb, `Z` ship, `X` explosion
- `state`: `>= 0` attacking (move index), `-1` idle, `-2` stunned, `-3` blocking, `-4` flying, `-5` crouching
- invisible opponents have their x, y and state hidden

Clients should ignore any snapshot whose `tick` is not newer than the last one applied. `ack` reports how many of
the client's inputs the server has consumed.

### Authentication

Every UDP packet must name a connected slot and carry that slot's token. Packets with an unknown slot or wrong token
are dropped. The token is generated from `/dev/urandom` and sent over TCP. It is not encrypted, so it protects
against off-path forgery only.

## Disconnects

A player is dropped when their TCP connection closes or errors, their line buffer overflows, a lobby write fails,
or, during a match, no valid UDP packet arrives for `UDP_TIMEOUT_MS` (5 s). Dropping closes the socket, wipes the
slot (including the token), and removes that player's ship and projectiles. In a match the player counts as out,
which can end the round.

## Bots

Bots occupy a slot with no socket or input queue. Each `AI_THINK_TICKS` ticks, a plugin from `ais[]` in
`server/ai.h` returns an input mask and optionally a motion command. The built-in AIs are `rusher`, `zoner` and
`turtle`. To add one, write a function with the `AiFn` signature, append it to `ais[]`, and raise `MAXAI` in
`config.h` if needed.
