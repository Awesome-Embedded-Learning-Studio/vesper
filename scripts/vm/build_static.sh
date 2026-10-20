#!/usr/bin/env bash
# scripts/vm/build_static.sh -- the fbdev desk as a fully-static binary for
# the initramfs (no dynamic libc there). Adapted from Cinux-GUI@0b0c135
# scripts/build_fbdev_host_static.sh (2026-10-20). glibc-static path; a
# missing glibc-static fails loudly rather than shipping a dynamic binary.
#
# Output: build/vm/vesper-fbdev-static

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/build/vm/fbdev-static-build"
BIN="$ROOT/build/vm/vesper-fbdev-static"
VISOR="${VISOR_ROOT:-"$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"/../visor}"

log() { printf '[fbdev-static] %s\n' "$*"; }
die() { log "FAIL: $*"; exit 1; }

log "build: fbdev_desktop (static) against $VISOR"
cmake -S "$ROOT" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++ -DVISOR_ROOT="$VISOR" \
  -DCMAKE_CXX_FLAGS="-static" \
  -DCMAKE_EXE_LINKER_FLAGS="-static" >/dev/null
cmake --build "$OUT" --parallel "$(nproc)" --target fbdev_desktop >/dev/null
cp "$OUT/examples/fbdev_desktop" "$BIN"
log "done: $(ls -lh "$BIN" | awk '{print $5}')"
if file "$BIN" 2>/dev/null | grep -q "statically linked\|static-pie"; then
  log "OK: statically linked"
else
  die "binary is NOT static (glibc-static missing?)"
fi
