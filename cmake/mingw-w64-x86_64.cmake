# Cross-compile for 64-bit Windows with MinGW-w64 (e.g. from macOS/Linux).
#
#   brew install mingw-w64   # macOS
#   # Build libsodium + SDL3 for this host into a prefix, then:
#   cmake -B build-mingw \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
#     -DCMAKE_PREFIX_PATH=$HOME/mingw-deps \
#     -DSODIUM_ROOT=$HOME/mingw-deps
#   cmake --build build-mingw -j

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Prefer target libraries/headers from CMAKE_PREFIX_PATH / SODIUM_ROOT.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
