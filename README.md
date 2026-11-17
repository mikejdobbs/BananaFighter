# BananaFighter

An OSS networked fighting game.



https://github.com/user-attachments/assets/02dc9696-6fc8-40b8-a1f7-813f27c25d2d



Fight on a shared stage with friends or bots. Host on your LAN, join a match, or play solo.

## Play

1. Download the latest build from **[Releases](https://github.com/mikejdobbs/BananaFighter/releases)**.
2. Pick the file for your system:
   - **macOS:** `BananaFighter-*-macos-*.dmg` — open the disk image and drag **BananaFighter** to Applications
   - **Windows:** `BananaFighter-*-windows-x86_64-setup.exe` (installer), or the `.zip` (unzip and run `client.exe`)
3. Launch the game.

Builds are unsigned open-source downloads. If your OS warns you:

- **macOS:** right-click the app → **Open**, or System Settings → Privacy & Security → **Open Anyway**
- **Windows:** SmartScreen → **More info** → **Run anyway**

## How to play

On the title screen:

- **Single Player** — local match vs a bot
- **Host Game** — start a match others can find on your LAN
- **Connect** — join a LAN game, or enter `host` / `host:port`

In the lobby, pick a fighter, add bots if you want, ready up, and fight. After a match you return to fighter select; disconnecting returns to the title screen.

### Controls (defaults)

| Action | Keyboard | Gamepad |
|---|---|---|
| Move | W A S D (arrow keys also move) | D-pad / stick |
| Punch (light / medium / heavy) | U I O | X Y RB |
| Kick (light / medium / heavy) | J K L | A B RT |
| Menu / pause | Esc | Start |

Rebind keys in the in-game options menu. Some fighters can hold jump in the air for tail flight.

## Dedicated server

Want a headless host? The Windows zip includes `server.exe`. From a source build, run `./server` (see the developer guide). Optional password: `-k secret` or `BANANAFIGHTER_KEY`.

## Developers

Build from source, packaging, protocol, and contribution details: **[DEVELOPERS.md](DEVELOPERS.md)**.
