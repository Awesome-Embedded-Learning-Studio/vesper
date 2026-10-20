#!/usr/bin/env bash
# scripts/vm/build_busybox.sh -- static busybox for the initramfs.
# Adapted from Cinux-GUI@0b0c135 scripts/build_busybox_x86_64.sh (2026-10-20).
# The initramfs carries no libc, so busybox builds fully static. Source
# defaults to the repo-local third_party/busybox, fetched as a pinned tarball
# when absent; override with BUSYBOX_SRC.
#
# Output: build/vm/busybox-root/ (the installed applet tree)

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${BUSYBOX_SRC:-$ROOT/third_party/busybox}"
# Pinned so a rerun builds the same busybox the VM judgment was written
# against; bump deliberately, not by chasing the latest.
BUSYBOX_VERSION="1.37.0"
TARBALL="busybox-${BUSYBOX_VERSION}.tar.bz2"
OUT="$ROOT/build/vm/busybox-build"
INSTALL="$ROOT/build/vm/busybox-root"

log() { printf '[busybox] %s\n' "$*"; }
die() { log "FAIL: $*"; exit 1; }

if [[ ! -f "$SRC/Makefile" ]]; then
  log "source absent: fetching ${TARBALL} from busybox.net"
  mkdir -p "$ROOT/third_party"
  curl -fL --http1.1 --retry 3 -o "$ROOT/third_party/${TARBALL}" \
    "https://busybox.net/downloads/${TARBALL}"
  tar -C "$ROOT/third_party" -xjf "$ROOT/third_party/${TARBALL}"
  mv "$ROOT/third_party/busybox-${BUSYBOX_VERSION}" "$SRC"
fi
mkdir -p "$OUT"
cd "$SRC"

if [[ ! -f "$OUT/.config" ]]; then
  log "config: defconfig"
  make O="$OUT" defconfig
fi
# static binary (initramfs has no libc)
sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "$OUT/.config" || true
grep -q '^CONFIG_STATIC=y' "$OUT/.config" || echo 'CONFIG_STATIC=y' >> "$OUT/.config"
sed -i 's/^CONFIG_PIE=y/# CONFIG_PIE is not set/' "$OUT/.config" || true
# CONFIG_TC: busybox 1.37 tc.c uses CBQ uapi removed in kernel 6.19 headers -> compile error
sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' "$OUT/.config" || true
grep -q '^# CONFIG_TC is not set' "$OUT/.config" || echo '# CONFIG_TC is not set' >> "$OUT/.config"
make O="$OUT" olddefconfig 2>/dev/null || yes "" | make O="$OUT" oldconfig >/dev/null 2>&1 || true
log "build: busybox (-j$(nproc)) -- Wno-error (1.37 + gcc16 -Werror)"
make O="$OUT" -j"$(nproc)" EXTRA_CFLAGS="-Wno-error" busybox
rm -rf "$INSTALL"
make O="$OUT" CONFIG_PREFIX="$INSTALL" install
log "done: $(ls -lh "$INSTALL/bin/busybox" | awk '{print $5}')"
