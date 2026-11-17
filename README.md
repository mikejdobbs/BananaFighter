# BananaFighter
An OSS fighting game



https://github.com/user-attachments/assets/02dc9696-6fc8-40b8-a1f7-813f27c25d2d




A small networked fighting game: a C server that runs the match simulation, and an optional SDL3 client.
Up to `MAX_PLAYERS` fighters (humans and computer-controlled bots) share one stage. The server is authoritative and
ticks at `TICK_HZ`.

Server logic lives once in the `fighter_server` library. The dedicated `./server` binary and the client both link it.
With no arguments, `./client` opens a title screen: **Single Player** (local vs a bot), **Host Game**
(embedded listen server + LAN discovery beacon), or **Connect** (scan LAN / enter `host` or `host:port`).
Pass a host (and optional port) on the command line to skip the title screen and join immediately.
Match end still returns to the fighter-select lobby; a disconnect returns to the title screen.

## Layout

```
CMakeLists.txt
cmake/ios.toolchain.cmake  optional iOS CMake toolchain helper
tools/build_ios_sodium.sh       build libsodium for device + simulator
tools/build_ios_sdl.sh          build SDL3 for device + simulator
tools/build_ios_xcframeworks.sh pack both into XCFrameworks (one Xcode project)
ios/Info.plist                  iOS bundle keys (landscape, local network)
sim.h, config.h          shared by client and server (simulation types, constants)
platform.h / platform.c  sockets, time, threads (Unix vs Windows)
server/
  server.h               types, shared state, prototypes, protocol notes
  embed.h / embed.c      public start/stop API and poll() loop (background thread)
  main.c                 dedicated server entry (calls server_run_forever)
  util.c                 now_ms, make_token, overlap, nearest, slot helpers
  state.c                global state, lobby/match lifecycle, bots, drop()
  combat.c               hits, projectiles, bombs, ships, melee
  sim.c                  per-tick simulation: input, bot AI calls, step()
  net.c                  TCP lobby protocol, UDP input/snapshots, accept
  ai.h                   bot AI plugins (rusher, zoner, turtle)
client/                  SDL3 client (main, audio, input, network, render)
sprites/, sounds/        client assets (PNG sprite sheets; WAV)
make_sprites.py          rebuild all (or selected) sprite sheets
make_sounds.py           regenerate sound assets
make_macos.sh            Release .app → versioned DMG under dist/
make_windows.sh          MinGW cross-build → versioned zip; optional Inno via Docker/CI
packaging/windows/       Inno Setup script + portable staging helper
tools/
  build_<char>_sprites.py   one builder per roster fighter
  procedural_sprites.py     procedural sheet / shots / fx / background drawing
  sprite_common.py          CHAR_ORDER, PNG save, shot-cell patching
  sprite_packer_util.py     pack painted style-A frames into sheets
  refs/*_style_a_frames/    source frames for painted fighters (jv, rump, kammy, slurpy, flusk, stephen, lopicardo)
tests/                   black-box server protocol tests
```

## Building

Requires CMake 3.16+, a C11 compiler, and **libsodium** for wire encryption. The client needs SDL3
(skipped with a warning if missing). Protocol tests (`server_test`) are Unix-only (fork harness).

### macOS / Linux

```
cmake -B build
cmake --build build
open build/BananaFighter.app   # macOS client (.app with Dock icon)
./build/server                 # dedicated / headless (advertises on DISCOVER_PORT)
./build/server --version       # print BananaFighter server BF_VERSION (-v also works)
```

Optional session password: `./build/server -k secret` or `BANANAFIGHTER_KEY`.

On Linux the client stays a plain binary: `./build/client`. Join a remote host with
`open build/BananaFighter.app --args 192.0.2.1` (macOS) or `./build/client 192.0.2.1`.

For Xcode: `cmake -B buildx -G Xcode`. Headers are listed as target sources so they appear in the project.

### iOS (Xcode + SDL3)

The client builds as an iOS `.app` with the same logical resolution (`1280×720`, letterboxed), an on-screen
virtual pad (hidden when an MFi / Bluetooth gamepad is connected), and the embedded listen server for
**Single Player** / **Host**. LAN discover and Host need the Local Network permission prompt
(`NSLocalNetworkUsageDescription` in [`ios/Info.plist`](ios/Info.plist)); Connect-by-address always works.

You need Xcode with an iOS SDK. Device and Simulator are different platforms (both arm64), so
deps are packaged as **XCFrameworks** that contain both slices — one Xcode project runs on either.

```
./tools/build_ios_xcframeworks.sh --build
rm -rf build-ios
cmake -B build-ios -G Xcode \
  -DCMAKE_TOOLCHAIN_FILE=cmake/ios.toolchain.cmake \
  -DBF_DEVELOPMENT_TEAM=YOUR_TEAM_ID
open build-ios/fighter.xcodeproj
```

That builds libsodium + SDL3 for device and Simulator, packs `deps/ios/*.xcframework`, and
configures a single project. Select the `client` scheme, pick any Simulator or a plugged-in
iPhone, then Run. Signing uses your team (`BF_DEVELOPMENT_TEAM` or Xcode’s Signing settings);
bundle id is `com.bananafighter.client`. Minimum iOS is **15.0**.

The dedicated `server` binary and `server_test` harness are skipped on iOS; assets ship in the
app bundle next to the executable (`sprites/`, `sounds/`, `icons/`).

On-device notes:

- Landscape only
- Virtual pad: left stick / d-pad, six attack buttons on the right, top-right menu button for options
- Backgrounding pauses audio and simulation ticks; a long background may still UDP-timeout a match

### Windows (MSVC + vcpkg)

```
vcpkg install libsodium sdl3
cmake -B build -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

Or point `SODIUM_ROOT` at a libsodium prefix if not using vcpkg's `unofficial-sodium` package.
Link against `ws2_32` is handled by CMake. The client must include `embed.h` only, not `server.h`
(overlapping types and symbols).

### Cross-compile with MinGW-w64 (macOS / Linux → Windows x86_64)

Install the toolchain (`brew install mingw-w64` on macOS). Build **MinGW** copies of
libsodium and SDL3 into a prefix (do not use Homebrew’s arm64/mac libraries), e.g.
`$HOME/mingw-deps`, then pack a versioned portable zip:

```
./make_windows.sh
```

That always builds into `build-mingw/` and writes `dist/BananaFighter-<BF_VERSION>-windows-x86_64.zip` with:

```
BananaFighter-windows-x86_64/
  client.exe        # embeds icons/icon.ico
  server.exe
  SDL3.dll          # only third-party DLL (libsodium is static)
  icons/icon.png    # window / taskbar icon at runtime
  sprites/
  sounds/
```

Players unzip and run `client.exe` from that folder. Dedicated hosts can run `server.exe` alone.
Override the deps prefix with `MINGW_DEPS=/path ./make_windows.sh` if needed. Test the binaries on
a Windows machine; they will not run natively on macOS.

The Inno Setup `-setup.exe` is separate (Inno’s compiler is Windows-only). Prefer one of:

1. **Docker Desktop** on macOS — `make_windows.sh` runs `amake/innosetup` when `docker` is available
2. **Native `iscc`** on Windows
3. **CI** — tagging `v*` builds `-setup.exe` on GitHub Actions (`windows-latest`)

The zip alone is a valid release artifact if you skip the installer locally.

Advanced: if you already have a working Wine prefix with Inno Setup, `make_windows.sh` can drive
it (`./packaging/windows/setup_iscc_wine.sh`). Homebrew’s Wine casks are currently disabled for
Gatekeeper, so do not treat `brew install --cask wine-stable` as the default path.

### macOS DMG

On macOS, pack a drag-to-Applications disk image:

```
./make_macos.sh
```

Writes `dist/BananaFighter-<BF_VERSION>-macos-<arch>.dmg` (Release build under `build-release/`).

## Assets

The client loads `sprites/*.png` with `SDL_LoadPNG` (SDL 3.4+, no SDL_image). Sheets are lossless PNGs
with straight alpha so edges blend correctly. Roster names in `config.h` map to lowercase files (`Slurpy` → `sprites/slurpy.png`).
Stage art is `sprites/background.png` (stadium) plus `sprites/background_<name>.png` per fighter; the select
screen shows the cursor fighter's stage, and each match picks one stage at random.

App icons live under `icons/` (`icon.png`, `icon.ico`, `BananaFighter.icns`). Regenerate with
`python3 tools/make_icons.py` (needs Pillow; `.icns` also needs macOS `iconutil`).

Regenerate sprites with Pillow (e.g. project `.venv`):

```
python3 make_sprites.py              # every fighter + shots/fx + background
python3 make_sprites.py slurpy flusk   # selected fighters only
python3 make_sprites.py misc bg      # fx.png, background.png + per-fighter stages
python3 tools/build_rump_sprites.py  # one fighter directly
```

Each fighter has `tools/build_<name>_sprites.py`. Painted characters (`jv`, `rump`, `kammy`, `slurpy`, `flusk`, `stephen`, `lopicardo`) pack
frames from `tools/refs/<name>_style_a_frames/`; the others are drawn procedurally. Projectile icons live on
each fighter sheet (row `ROW_SHOT`). `misc` regenerates `fx.png`. `bg` rebuilds the stadium and every
character home stage.

Sounds: `python3 make_sounds.py` writes under `sounds/`.

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

After the cleartext `K` crypto exchange, both sides send an encrypted `V` line with
`BF_VERSION` from `config.h`. A mismatch (or a peer that never sends `V`) drops the
connection. On match the server continues with:

| Line | Meaning |
|---|---|
| `V version` | protocol build; client replies with the same line |
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

## Releasing

Builds are **unsigned** for now (no Apple Developer ID / notarization, no Windows Authenticode). Players may
see Gatekeeper or SmartScreen warnings — that is expected for OSS builds.

1. Bump `BF_VERSION` in [`config.h`](config.h) (client and server must match).
2. Commit, then tag and push:
   ```
   git tag v1.0.3
   git push origin v1.0.3
   ```
3. The [Release](.github/workflows/release.yml) workflow builds and uploads to a GitHub Release:
   - `BananaFighter-<ver>-macos-<arch>.dmg`
   - `BananaFighter-<ver>-windows-x86_64-setup.exe` (Inno Setup)
   - `BananaFighter-<ver>-windows-x86_64.zip` (portable)

Local packaging (this Mac):

```
./make_macos.sh      # → dist/*.dmg
./make_windows.sh    # → dist/*.zip  (+ *-setup.exe if Docker or iscc is available)
gh release create v1.0.3 dist/BananaFighter-1.0.3-*
```

Without Docker/iscc locally, ship the zip from MinGW and let the tag workflow attach `-setup.exe`.

**Opening unsigned builds**

- **macOS:** right-click the app → Open, or System Settings → Privacy & Security → Open Anyway after the first block.
- **Windows:** if SmartScreen appears, choose More info → Run anyway.
