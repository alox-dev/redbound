#!/bin/sh
set -e
VER=2.30.9
cd "$(dirname "$0")/.."
if [ -f third_party/SDL2/CMakeLists.txt ] && grep -q "$VER" third_party/SDL2/.version 2>/dev/null; then
  echo "SDL2 $VER is already in third_party/SDL2"; exit 0
fi
rm -rf third_party/SDL2 /tmp/sdl2-dl && mkdir -p /tmp/sdl2-dl
echo "downloading SDL2 $VER..."
curl -sSfL -o /tmp/sdl2-dl/sdl2.tar.gz "https://github.com/libsdl-org/SDL/archive/refs/tags/release-$VER.tar.gz"
tar xzf /tmp/sdl2-dl/sdl2.tar.gz -C /tmp/sdl2-dl
mv "/tmp/sdl2-dl/SDL-release-$VER" third_party/SDL2
echo "$VER" > third_party/SDL2/.version
echo "SDL2 $VER ready in third_party/SDL2"
