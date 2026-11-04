#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KA3D_DIR="$ROOT/third_party/ka3d"
KA3D_COMMIT=0efab4267897d62639ce0a5bbafdd63c4dcc71da
STAMP="$KA3D_DIR/.redbound-patches"
PATCHES_HASH="$(cat "$ROOT"/third_party/ka3d-patches/*.patch | sha256sum | cut -d' ' -f1)"

if [ -d "$KA3D_DIR/.git" ] && [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$PATCHES_HASH" ]; then
  echo "KA3D is up to date in $KA3D_DIR"
  exit 0
fi
if [ ! -d "$KA3D_DIR/.git" ]; then
  git clone https://github.com/pent0/ka3d.git "$KA3D_DIR"
fi
cd "$KA3D_DIR"
git checkout -q "$KA3D_COMMIT"
git reset -q --hard
git clean -qfd include/config/linux || true
for p in "$ROOT"/third_party/ka3d-patches/*.patch; do
  echo "Applying $(basename "$p")"
  git apply "$p"
done
echo "$PATCHES_HASH" > "$STAMP"
echo "KA3D ready in $KA3D_DIR"
