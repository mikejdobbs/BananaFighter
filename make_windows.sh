#!/usr/bin/env bash
# Cross-build Windows x86_64 binaries and pack a portable zip.
# Inno Setup installer (.exe) is built when iscc is available natively, via
# Docker (amake/innosetup), or via Wine — useful when cross-compiling from macOS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

DEPS="${MINGW_DEPS:-$HOME/mingw-deps}"
BUILD=build-mingw
VERSION="$(sed -n 's/^#define BF_VERSION[[:space:]]*"\([^"]*\)".*/\1/p' config.h | head -1)"
if [[ -z "$VERSION" ]]; then
  echo "could not parse BF_VERSION from config.h" >&2
  exit 1
fi

DIST_NAME=BananaFighter-windows-x86_64
DIST_DIR=dist/"$DIST_NAME"
ZIP="dist/BananaFighter-${VERSION}-windows-x86_64.zip"
SETUP="dist/BananaFighter-${VERSION}-windows-x86_64-setup.exe"
SDL_DLL="$DEPS/bin/SDL3.dll"
ISS="$ROOT/packaging/windows/BananaFighter.iss"
ISS_REL="packaging/windows/BananaFighter.iss"

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
# Inno SetupIconFile; also useful if players want a Windows icon file beside the zip.
if [[ -f icons/icon.ico ]]; then
  cp icons/icon.ico "$DIST_DIR/icons/"
fi

if command -v x86_64-w64-mingw32-strip >/dev/null 2>&1; then
  x86_64-w64-mingw32-strip "$DIST_DIR/client.exe" "$DIST_DIR/server.exe" "$DIST_DIR/SDL3.dll"
fi

rm -f "$ZIP"
(
  cd dist
  zip -r "BananaFighter-${VERSION}-windows-x86_64.zip" "$DIST_NAME"
)

echo "packed $ZIP"
ls -lh "$ZIP"

find_wine() {
  if command -v wine64 >/dev/null 2>&1; then
    echo wine64
  elif command -v wine >/dev/null 2>&1; then
    echo wine
  else
    return 1
  fi
}

find_wine_iscc() {
  local candidates=(
    "$HOME/.wine/drive_c/Program Files (x86)/Inno Setup 6/ISCC.exe"
    "$HOME/.wine/drive_c/Program Files/Inno Setup 6/ISCC.exe"
    "$HOME/.wine/drive_c/Program Files (x86)/Inno Setup 5/ISCC.exe"
  )
  local p
  for p in "${candidates[@]}"; do
    if [[ -f "$p" ]]; then
      echo "$p"
      return 0
    fi
  done
  return 1
}

# Run Inno Setup: native iscc → Docker (amake/innosetup) → Wine ISCC.exe.
run_iscc() {
  if command -v iscc >/dev/null 2>&1; then
    echo "building installer with native iscc..."
    iscc \
      "/DMyAppVersion=${VERSION}" \
      "/DSourceDir=${ROOT}/${DIST_DIR}" \
      "/DOutputDir=${ROOT}/dist" \
      "$ISS"
    return 0
  fi

  if [[ "${BF_SKIP_DOCKER_ISCC:-}" != 1 ]] && command -v docker >/dev/null 2>&1; then
    if docker info >/dev/null 2>&1; then
      echo "building installer with Docker amake/innosetup..."
      # Mount repo at /work; .iss defaults use paths relative to packaging/windows/.
      docker run --rm -i \
        -v "$ROOT:/work" \
        -w /work \
        amake/innosetup \
        "/DMyAppVersion=${VERSION}" \
        "$ISS_REL"
      return 0
    fi
    echo "docker found but daemon not running; skipping Docker iscc." >&2
  fi

  local wine_bin wine_iscc src_win out_win iss_win
  if wine_bin="$(find_wine)" && wine_iscc="$(find_wine_iscc)"; then
    echo "building installer with Wine + Inno Setup..."
    src_win="$("$wine_bin" winepath -w "$ROOT/$DIST_DIR")"
    out_win="$("$wine_bin" winepath -w "$ROOT/dist")"
    iss_win="$("$wine_bin" winepath -w "$ISS")"
    "$wine_bin" "$wine_iscc" \
      "/DMyAppVersion=${VERSION}" \
      "/DSourceDir=${src_win}" \
      "/DOutputDir=${out_win}" \
      "$iss_win"
    return 0
  fi

  return 1
}

if run_iscc; then
  if [[ ! -f "$SETUP" ]]; then
    echo "iscc finished but $SETUP missing" >&2
    exit 1
  fi
  echo "packed $SETUP"
  ls -lh "$SETUP"
else
  echo ""
  echo "Portable zip is ready. Skipped Inno Setup installer (no iscc / Docker / Wine)."
  echo "On macOS while MinGW-cross compiling, pick one:"
  echo "  1) Docker Desktop, then re-run ./make_windows.sh"
  echo "     (uses amake/innosetup; first run pulls the image)"
  echo "  2) ./packaging/windows/setup_iscc_wine.sh   # Wine + Inno Setup"
  echo "  3) Rely on CI: git push origin v${VERSION}   # builds *-setup.exe on windows-latest"
fi
