#!/usr/bin/env bash
# Build SDL2 and libpng as static arm64 iOS libraries into ios/build/deps/prefix and
# write ios/build/deps/deps.xcconfig for the Xcode project. Needs macOS, Xcode, cmake, ninja.
set -euo pipefail

SDL_TAG=release-2.32.10
SDL_SHA=5d249570393f7a37e037abf22cd6012a4cc56a71
PNG_TAG=v1.6.59
PNG_SHA=cd952f49f95bb27154ae77dbb103032d95f6e580

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPS="$HERE/build/deps"
PREFIX="$DEPS/prefix"
mkdir -p "$DEPS"

fetch() { # name url tag sha
    local dir="$DEPS/src-$1"
    if [ ! -d "$dir/.git" ]; then
        git clone --depth 1 --branch "$3" "$2" "$dir"
    fi
    local got
    got="$(git -C "$dir" rev-parse HEAD)"
    if [ "$got" != "$4" ]; then
        echo "error: $1 $3 is $got, expected $4" >&2
        exit 1
    fi
}

fetch SDL2 https://github.com/libsdl-org/SDL.git "$SDL_TAG" "$SDL_SHA"
fetch libpng https://github.com/pnggroup/libpng.git "$PNG_TAG" "$PNG_SHA"

COMMON=(-G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
        -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY)

cmake -S "$DEPS/src-SDL2" -B "$DEPS/build-SDL2" "${COMMON[@]}" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_HIDAPI=OFF
cmake --build "$DEPS/build-SDL2"
cmake --install "$DEPS/build-SDL2"

cmake -S "$DEPS/src-libpng" -B "$DEPS/build-libpng" "${COMMON[@]}" \
    -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_FRAMEWORK=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF
cmake --build "$DEPS/build-libpng"
cmake --install "$DEPS/build-libpng"

# The framework list comes from SDL's own build, so it tracks what SDL needs.
SDL_LIBS="$(sh "$PREFIX/bin/sdl2-config" --static-libs)"
cat > "$DEPS/deps.xcconfig" <<XC
HEADER_SEARCH_PATHS = $PREFIX/include $PREFIX/include/SDL2
LIBRARY_SEARCH_PATHS = $PREFIX/lib
OTHER_LDFLAGS = -lSDL2main $SDL_LIBS -lpng16 -lz
XC
echo "wrote $DEPS/deps.xcconfig"
