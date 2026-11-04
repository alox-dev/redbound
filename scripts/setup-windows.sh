#!/bin/sh
set -e
cd "$(dirname "$0")/.."
SDL_VERSION=2.30.9
ELECTRON_VERSION=33.2.1
DEPS=third_party/windows
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$DEPS"
if [ ! -f "$DEPS/SDL2/include/SDL2/SDL.h" ]; then
  echo "downloading SDL2 $SDL_VERSION for MinGW..."
  curl -sSfL -o "$TMP/sdl2.tar.gz" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-devel-$SDL_VERSION-mingw.tar.gz"
  tar xzf "$TMP/sdl2.tar.gz" -C "$TMP"
  rm -rf "$DEPS/SDL2" && cp -r "$TMP/SDL2-$SDL_VERSION/x86_64-w64-mingw32" "$DEPS/SDL2"
fi
if [ ! -f "$DEPS/angle/libGLESv2.dll" ]; then
  echo "downloading ANGLE (from Electron $ELECTRON_VERSION)..."
  curl -sSfL -o "$TMP/electron.zip" "https://github.com/electron/electron/releases/download/v$ELECTRON_VERSION/electron-v$ELECTRON_VERSION-win32-x64.zip"
  mkdir -p "$DEPS/angle"
  unzip -o -q "$TMP/electron.zip" libEGL.dll libGLESv2.dll d3dcompiler_47.dll LICENSES.chromium.html -d "$DEPS/angle"
fi
if [ ! -f "$DEPS/khronos/KHR/khrplatform.h" ]; then
  echo "downloading the Khronos OpenGL ES 2 headers..."
  mkdir -p "$DEPS/khronos/GLES2" "$DEPS/khronos/KHR"
  for f in gl2.h gl2ext.h gl2platform.h; do
    curl -sSfL -o "$DEPS/khronos/GLES2/$f" "https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/api/GLES2/$f"
  done
  curl -sSfL -o "$DEPS/khronos/KHR/khrplatform.h" "https://raw.githubusercontent.com/KhronosGroup/EGL-Registry/main/api/KHR/khrplatform.h"
fi
echo "Windows dependencies ready in $DEPS"
