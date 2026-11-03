#!/bin/sh
set -e
if [ -n "$1" ] && [ -f "$1" ]; then SRC="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; fi
cd "$(dirname "$0")/.."
SRC="${SRC:-assets/icons/icon.png}"
if [ ! -f "$SRC" ]; then
  echo "usage: scripts/make-icons.sh [image.png]   (default: assets/icons/icon.png)"
  echo "Generates local app icons (ignored by git) from any image."
  exit 1
fi
if command -v magick >/dev/null 2>&1; then IM="magick"; elif command -v convert >/dev/null 2>&1; then IM="convert"; else
  echo "ImageMagick is required (sudo apt install imagemagick)"; exit 1
fi
RES=platforms/android/app/src/main/res-icon-local
mkdir -p platforms/linux/icon-local
for pair in mdpi:48 hdpi:72 xhdpi:96 xxhdpi:144 xxxhdpi:192; do
  d=${pair%%:*}; s=${pair##*:}
  mkdir -p "$RES/mipmap-$d"
  $IM "$SRC" -background none -gravity center -resize "${s}x${s}" -extent "${s}x${s}" "$RES/mipmap-$d/ic_launcher.png"
  $IM "$RES/mipmap-$d/ic_launcher.png" \( +clone -alpha extract -fill black -colorize 100 -fill white -draw "circle $((s/2)),$((s/2)) $((s/2)),0" \) -alpha off -compose CopyOpacity -composite "$RES/mipmap-$d/ic_launcher_round.png"
done
$IM "$SRC" -background none -gravity center -resize 256x256 -extent 256x256 platforms/linux/icon-local/redbound.png
mkdir -p platforms/windows/icon-local platforms/switch/icon-local
$IM "$SRC" -background none -gravity center -resize 256x256 -extent 256x256 -define icon:auto-resize=256,128,64,48,32,16 platforms/windows/icon-local/redbound.ico
$IM "$SRC" -background white -gravity center -resize 256x256 -extent 256x256 -flatten -quality 95 platforms/switch/icon-local/icon.jpg
echo "Icons written to $RES and platforms/{linux,windows,switch}/icon-local (not tracked by git)."
echo "Rebuild the APK to use them: cd platforms/android && ./gradlew assembleDebug"
