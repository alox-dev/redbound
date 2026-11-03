#!/bin/sh
set -e
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project(redbound VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)-alpha
NAME="redbound-$VERSION-linux-x86_64"
DIST="dist/$NAME"
[ -f third_party/ka3d/CMakeLists.txt ] || [ -d third_party/ka3d/source ] || ./scripts/setup-ka3d.sh
GENERATOR=""; command -v ninja >/dev/null 2>&1 && GENERATOR="-G Ninja"
cmake -S . -B build-release $GENERATOR -DCMAKE_BUILD_TYPE=Release -DREDBOUND_TOOLS=OFF
cmake --build build-release
rm -rf "$DIST" && mkdir -p "$DIST/assets"
cp build-release/app/redbound "$DIST/"
strip "$DIST/redbound"
cp -r assets/lang "$DIST/assets/"
cp third_party/gamecontrollerdb/gamecontrollerdb.txt "$DIST/"
cp platforms/linux/redbound.desktop LICENSE "$DIST/"
if [ -f platforms/linux/icon-local/redbound.png ]; then cp platforms/linux/icon-local/redbound.png "$DIST/"; else cp platforms/linux/redbound.png "$DIST/"; fi
cat > "$DIST/README.txt" <<TXT
Redbound $VERSION for Linux (x86_64)

Requirements: SDL2, OpenGL ES 2 (Mesa) and zlib, for example on Debian and Ubuntu:
  sudo apt install libsdl2-2.0-0 libgles2 zlib1g

Run:
  ./redbound

On the first run, choose your own Bounce_Evolution.deb to import the game data.
Source code and documentation: https://github.com/alox-dev/redbound
License: GNU GPL v2 (see LICENSE).
TXT
tar czf "dist/$NAME.tar.gz" -C dist "$NAME"
echo "Package: dist/$NAME.tar.gz"
