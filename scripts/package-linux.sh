#!/bin/sh
set -e
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project(redbound VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
NAME="redbound-$VERSION-linux-x86_64"
APPDIR="dist/$NAME.AppDir"
TOOL_VERSION=1.9.0
TOOL_SHA256=46fdd785094c7f6e545b61afcfb0f3d98d8eab243f644b4b17698c01d06083d1
TOOL="dist/tools/appimagetool-$TOOL_VERSION"

command -v desktop-file-validate >/dev/null 2>&1 || { echo "desktop-file-validate is required (sudo apt install desktop-file-utils)"; exit 1; }
./scripts/setup-ka3d.sh
GENERATOR=""; command -v ninja >/dev/null 2>&1 && GENERATOR="-G Ninja"
cmake -S . -B build-release $GENERATOR -DCMAKE_BUILD_TYPE=Release -DREDBOUND_TOOLS=OFF
cmake --build build-release

mkdir -p dist/tools
if [ ! -x "$TOOL" ]; then
  curl -sSfL -o "$TOOL" "https://github.com/AppImage/appimagetool/releases/download/$TOOL_VERSION/appimagetool-x86_64.AppImage"
  chmod +x "$TOOL"
fi
echo "$TOOL_SHA256  $TOOL" | sha256sum -c -

rm -rf "$APPDIR" && mkdir -p "$APPDIR/usr/bin/assets" "$APPDIR/usr/lib"
cp build-release/app/redbound "$APPDIR/usr/bin/"
strip "$APPDIR/usr/bin/redbound"
cp -r assets/lang "$APPDIR/usr/bin/assets/"
[ -d assets/title ] && cp -r assets/title "$APPDIR/usr/bin/assets/"
cp -r assets/fonts "$APPDIR/usr/bin/assets/"
cp third_party/gamecontrollerdb/gamecontrollerdb.txt "$APPDIR/usr/bin/"
SDL_LIB=$(ldd build-release/app/redbound | awk '/libSDL2-2.0.so/ { print $3 }')
[ -n "$SDL_LIB" ] && cp -L "$SDL_LIB" "$APPDIR/usr/lib/"
cp platforms/linux/redbound.desktop "$APPDIR/"
if [ -f platforms/linux/icon-local/redbound.png ]; then ICON=platforms/linux/icon-local/redbound.png; else ICON=platforms/linux/redbound.png; fi
cp "$ICON" "$APPDIR/redbound.png"
cp "$ICON" "$APPDIR/.DirIcon"
cat > "$APPDIR/AppRun" <<'RUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/redbound" "$@"
RUN
chmod +x "$APPDIR/AppRun"

ARCH=x86_64 "$TOOL" --appimage-extract-and-run "$APPDIR" "dist/$NAME.AppImage"
echo "Package: dist/$NAME.AppImage"
