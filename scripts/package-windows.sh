#!/bin/sh
set -e
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project(redbound VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
NAME="redbound-$VERSION-windows-x86_64"
DIST="dist/$NAME"
DEPS=third_party/windows
command -v x86_64-w64-mingw32-g++-posix >/dev/null 2>&1 || { echo "MinGW-w64 is required (sudo apt install mingw-w64 libz-mingw-w64-dev)"; exit 1; }
./scripts/setup-ka3d.sh
./scripts/setup-windows.sh
mkdir -p dist
if [ -f platforms/windows/icon-local/redbound.ico ]; then
  ICON="$PWD/platforms/windows/icon-local/redbound.ico"
else
  ICON="$PWD/dist/redbound-default.ico"
  convert platforms/linux/redbound.png -define icon:auto-resize=256,128,64,48,32,16 "$ICON"
fi
GENERATOR=""; command -v ninja >/dev/null 2>&1 && GENERATOR="-G Ninja"
cmake -S . -B build-windows $GENERATOR -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release \
  -DREDBOUND_TOOLS=OFF -DREDBOUND_WINDOWS_ICON="$ICON"
cmake --build build-windows
rm -rf "$DIST" && mkdir -p "$DIST/assets" "$DIST/licenses"
cp build-windows/app/redbound.exe "$DIST/"
x86_64-w64-mingw32-strip "$DIST/redbound.exe"
cp "$DEPS/SDL2/bin/SDL2.dll" "$DEPS/angle/libEGL.dll" "$DEPS/angle/libGLESv2.dll" "$DEPS/angle/d3dcompiler_47.dll" "$DIST/"
cp -r assets/lang "$DIST/assets/"
[ -d assets/title ] && cp -r assets/title "$DIST/assets/"
cp third_party/gamecontrollerdb/gamecontrollerdb.txt "$DIST/"
cp LICENSE "$DIST/LICENSE.txt"
cp "$DEPS/angle/LICENSES.chromium.html" "$DIST/licenses/ANGLE-and-Chromium.html"
printf 'Redbound %s for Windows (x86_64)\r\n\r\nRun redbound.exe. On the first run, choose your own Bounce_Evolution.deb to import the game data.\r\nGraphics run on Direct3D 11 through ANGLE, so any GPU with Windows 10 or 11 drivers works.\r\nSettings and imported data: %%APPDATA%%\\redbound\r\n\r\nSource code and documentation: https://github.com/alox-dev/redbound\r\nLicense: GNU GPL v2 (LICENSE.txt). Third party licenses: licenses\\\r\n' "$VERSION" > "$DIST/README.txt"
rm -f "dist/$NAME.zip"
(cd dist && zip -qr "$NAME.zip" "$NAME")
echo "Package: dist/$NAME.zip"
