#!/usr/bin/env bash
# scripts/vm/build_initramfs.sh -- pack the fbdev VM's initramfs:
# busybox (static) + the fbdev desk (static) + an /init that finds the
# framebuffer and both input nodes by name and runs the desk under timeout.
# Adapted from Cinux-GUI@0b0c135 scripts/build_initramfs.sh (2026-10-20);
# the additions here are keyboard discovery and the devpts mount for the pty.
#
# Output: build/vm/rootfs.cpio.gz (feed to QEMU -initrd)

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUSYBOX_ROOT="$ROOT/build/vm/busybox-root"
FBDEV_DESK="$ROOT/build/vm/vesper-fbdev-static"
INITRAMFS_ROOT="$ROOT/build/vm/initramfs"
CPIO="$ROOT/build/vm/rootfs.cpio.gz"
# How long /init lets the desk run before timeout kills it back to a shell.
FBDEV_TIMEOUT="${FBDEV_TIMEOUT:-20}"

log() { printf '[initramfs] %s\n' "$*"; }
die() { log "FAIL: $*"; exit 1; }

[[ -d "$BUSYBOX_ROOT/bin" ]] || die "busybox not built (run build_busybox.sh)"
[[ -x "$FBDEV_DESK" ]] || die "vesper-fbdev-static not built (run build_static.sh)"

rm -rf "$INITRAMFS_ROOT"
mkdir -p "$INITRAMFS_ROOT"
cp -a "$BUSYBOX_ROOT/." "$INITRAMFS_ROOT/"
mkdir -p "$INITRAMFS_ROOT/usr/bin" "$INITRAMFS_ROOT/dev" "$INITRAMFS_ROOT/proc" \
         "$INITRAMFS_ROOT/sys" "$INITRAMFS_ROOT/tmp" "$INITRAMFS_ROOT/dev/input" \
         "$INITRAMFS_ROOT/dev/pts"
cp "$FBDEV_DESK" "$INITRAMFS_ROOT/usr/bin/vesper-fbdev"
chmod +x "$INITRAMFS_ROOT/usr/bin/vesper-fbdev"

# Static device nodes (devtmpfs will also populate, but these guarantee
# early access).
mknod -m 600 "$INITRAMFS_ROOT/dev/console" c 5 1 2>/dev/null || true
mknod -m 660 "$INITRAMFS_ROOT/dev/fb0"     c 29 0 2>/dev/null || true
mknod -m 666 "$INITRAMFS_ROOT/dev/null"    c 1 3 2>/dev/null || true
mknod -m 660 "$INITRAMFS_ROOT/dev/ttyS0"   c 4 64 2>/dev/null || true

# /init -- the kernel runs this (rdinit=/init); everything it says lands on
# ttyS0, the serial log the host gates read.
cat > "$INITRAMFS_ROOT/init" <<'INIT_EOF'
#!/bin/sh
set -x
echo "GUEST_INIT_OK"
mount -t proc     none /proc
mount -t sysfs    none /sys
mount -t devtmpfs none /dev 2>/dev/null || true
# devtmpfs shadows the initramfs's static /dev, taking its empty /dev/pts
# with it; the directory must exist in the mounted devtmpfs or the devpts
# mount (and every pty spawn after it) fails with ENOENT.
mkdir -p /dev/pts
mount -t devpts   none /dev/pts 2>/dev/null || true
mount -t tmpfs    none /tmp 2>/dev/null || true

if [ -c /dev/fb0 ]; then
  echo "GUEST_FB0_OK name=$(cat /sys/class/graphics/fb0/name 2>/dev/null || echo '?')" \
    "mode=$(cat /sys/class/graphics/fb0/mode 2>/dev/null || echo '?')"
else
  echo "GUEST_FAIL: no /dev/fb0"
fi

# Discover both input nodes from /proc/bus/input/devices (proc is always
# there). Retry: the usb-tablet registers ~3.5s, later than /init starts.
find_event() {
  grep -A6 "$1" /proc/bus/input/devices 2>/dev/null | grep -o "event[0-9]*" | head -1
}
EVT=""
KBD=""
i=0
while [ "$i" -lt 10 ] && { [ -z "$EVT" ] || [ -z "$KBD" ]; }; do
  i=$((i + 1))
  EVT=$(find_event "USB Tablet")
  KBD=$(find_event "AT Translated Set 2 keyboard")
  { [ -z "$EVT" ] || [ -z "$KBD" ]; } && sleep 1
done
echo "GUEST_TABLET_EVENT=${EVT:-none}"
echo "GUEST_KBD_EVENT=${KBD:-none}"
[ -n "$EVT" ] && [ -c "/dev/input/$EVT" ] && echo "GUEST_TABLET_OK"
[ -n "$KBD" ] && [ -c "/dev/input/$KBD" ] && echo "GUEST_KBD_OK"

# The pty spawn reads these two; a missing either one is the classic bare-guest
# failure, so the log names them before the desk needs them.
ls -l /dev/ptmx 2>&1
ls /dev/pts 2>&1

echo "GUEST_RUN_START"
VESPER_FBDEV_TRACE=${VESPER_FBDEV_TRACE:-0} timeout 20 /usr/bin/vesper-fbdev /dev/fb0 "/dev/input/$KBD" "/dev/input/$EVT"
echo "GUEST_RUN_RC=$?"
echo "GUEST_RUN_DONE"
exec /bin/sh
INIT_EOF
sed -i "s/ timeout 20 / timeout ${FBDEV_TIMEOUT} /" "$INITRAMFS_ROOT/init"
# Same treatment for the trace switch: the heredoc is single-quoted, so the
# build-time value is patched in (the guest env never carries it); the
# substitution stops at the first space so the command stays on the line.
sed -i "s/^VESPER_FBDEV_TRACE=[^ ]*/VESPER_FBDEV_TRACE=${VESPER_FBDEV_TRACE:-0}/" \
  "$INITRAMFS_ROOT/init"
chmod +x "$INITRAMFS_ROOT/init"

log "pack: $CPIO"
( cd "$INITRAMFS_ROOT" && find . | cpio -o -H newc 2>/dev/null | gzip > "$CPIO" )
log "done: $(ls -lh "$CPIO" | awk '{print $5, $9}')"
