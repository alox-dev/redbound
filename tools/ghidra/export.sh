#!/usr/bin/env bash
set -euo pipefail
BIN="${1:?missing the path to the bounce binary}"
OUT="${2:?missing the output folder}"
EXTRA="${3:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
GHIDRA="${GHIDRA:-$(ls -d "$HOME"/ghidra_*_PUBLIC 2>/dev/null | sort -V | tail -1)}"
PROJ_DIR="${PROJ_DIR:-$HOME/.cache/redbound-ghidra}"
[ -x "$GHIDRA/support/analyzeHeadless" ] || { echo "Ghidra not found; set GHIDRA=/path/to/ghidra_X_PUBLIC" >&2; exit 1; }
mkdir -p "$PROJ_DIR" "$OUT"
OUT="$(cd "$OUT" && pwd)"

if [ -d "$PROJ_DIR/BounceEvolution.rep" ]; then
  MODE=(-process "$(basename "$BIN")" -noanalysis)
else
  MODE=(-import "$BIN")
fi
"$GHIDRA/support/analyzeHeadless" "$PROJ_DIR" BounceEvolution "${MODE[@]}" \
  -scriptPath "$HERE" -postScript ExportGameCode.java "$OUT" "$EXTRA" 2>&1 | tee "$OUT/_ghidra.log" \
  | grep -E 'Exported|ERROR|Exception|SCRIPT' || true
echo "Resultado en $OUT (indice: $OUT/_indice.txt, registro: $OUT/_ghidra.log)"
