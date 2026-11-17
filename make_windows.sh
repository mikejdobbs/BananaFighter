#!/usr/bin/env bash
# Cross-build Windows x86_64 binaries and pack a portable zip.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

DEPS="${MINGW_DEPS:-$HOME/mingw-deps}"
BUILD=build-mingw
DIST_NAME=BananaFighter-windows-x86_64
DIST_DIR=dist/"$DIST_NAME"
ZIP=dist/"$DIST_NAME".zip
SDL_DLL="$DEPS/bin/SDL3.dll"

cmake -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
  -DCMAKE_PREFIX_PATH="$DEPS" \
  -DSODIUM_ROOT="$DEPS"
cmake --build "$BUILD" -j

for exe in client.exe server.exe; do
  if [[ ! -f "$BUILD/$exe" ]]; then
    echo "missing $BUILD/$exe" >&2
    exit 1
  fi
done
if [[ ! -f "$SDL_DLL" ]]; then
  echo "missing $SDL_DLL (build MinGW SDL3 into $DEPS)" >&2
  exit 1
fi
if [[ ! -d sprites || ! -d sounds ]]; then
  echo "missing sprites/ or sounds/ in $ROOT" >&2
  exit 1
fi

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

cp "$BUILD/client.exe" "$BUILD/server.exe" "$DIST_DIR/"
cp "$SDL_DLL" "$DIST_DIR/"
cp -R sprites sounds "$DIST_DIR/"
mkdir -p "$DIST_DIR/icons"
cp icons/icon.png "$DIST_DIR/icons/"

if command -v x86_64-w64-mingw32-strip >/dev/null 2>&1; then
  x86_64-w64-mingw32-strip "$DIST_DIR/client.exe" "$DIST_DIR/server.exe" "$DIST_DIR/SDL3.dll"
fi

rm -f "$ZIP"
(
  cd dist
  zip -r "$DIST_NAME.zip" "$DIST_NAME"
)

echo "packed $ZIP"
ls -lh "$ZIP"
