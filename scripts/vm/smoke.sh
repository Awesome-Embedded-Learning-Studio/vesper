#!/usr/bin/env bash
# scripts/vm/smoke.sh -- the fbdev lane's live judgment, under QEMU.
# Adapted from Cinux-GUI@0b0c135 scripts/smoke_p1.sh (2026-10-20): the
# marker gates and the monitor-screendump trick are that repo's; the KVM
# refusal and the pixel gate are this repo's own.
#
# Boots the self-built kernel plus the initramfs (busybox + the static fbdev
# desk), watches the serial log for the guest's markers, screendumps the
# framebuffer mid-run and hands the dump to the pixel gate. KVM is required:
# a bare-machine rehearsal that runs under emulation rehearses nothing, so
# its absence fails loudly instead of falling back to TCG.
#
# Prereqs (build once, cached):
#   scripts/vm/build_kernel.sh    -> build/vm/linux-build/.../bzImage
#   scripts/vm/build_busybox.sh   -> build/vm/busybox-root/
#   scripts/vm/build_static.sh    -> build/vm/vesper-fbdev-static
#   scripts/vm/build_initramfs.sh -> build/vm/rootfs.cpio.gz
#
# Usage:  scripts/vm/smoke.sh          (headless, the judgment lane)
#         VNC=1 scripts/vm/smoke.sh    (also serves VNC on 127.0.0.1:5999)
#         INPUT=1 scripts/vm/smoke.sh  (VNC input plus interaction pixel gates)
# Exit:   0 PASS, 1 FAIL.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VM="$ROOT/build/vm"
BZIMAGE="$VM/linux-build/arch/x86/boot/bzImage"
INITRD="$VM/rootfs.cpio.gz"
RUN="$VM/run"
SERIAL="$RUN/serial.log"
MON="$RUN/monitor.sock"
QEMUPID="$RUN/qemu.pid"
SHOT="$RUN/shot.ppm"

log() { printf '[smoke] %s\n' "$*" >&2; }
die() { log "FAIL: $*"; cleanup; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing tool: $1"; }
cleanup() {
  if [[ -S "$MON" ]]; then
    printf 'quit\n' | socat - UNIX-CONNECT:"$MON" >/dev/null 2>&1 || true
  fi
  if [[ -f "$QEMUPID" ]]; then
    local pid
    pid=$(cat "$QEMUPID" 2>/dev/null || true)
    [[ -n "${pid:-}" ]] && kill "$pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

log "preflight"
need qemu-system-x86_64
need socat
need python3
[[ -r /dev/kvm ]] || die "no readable /dev/kvm -- refusing to fall back to TCG"
[[ -f "$BZIMAGE" ]] || die "no bzImage at $BZIMAGE (run scripts/vm/build_kernel.sh)"
[[ -f "$INITRD" ]] || die "no initramfs at $INITRD (run scripts/vm/build_initramfs.sh)"

mkdir -p "$RUN"; rm -f "$RUN"/*

DISPLAY_ARGS=(-display none)
if [[ "${VNC:-0}" == "1" || "${INPUT:-0}" == "1" ]]; then
  DISPLAY_ARGS=(-display vnc=127.0.0.1:99)
  log "VNC on 127.0.0.1:5999 for eyeballing"
fi

log "launch QEMU: kernel + initramfs (vga std / usb-tablet / serial to $SERIAL)"
qemu-system-x86_64 \
  -accel kvm \
  -m 512 \
  -kernel "$BZIMAGE" \
  -initrd "$INITRD" \
  -append "console=ttyS0 rdinit=/init video=vesafb:640x480-32" \
  -vga std \
  -device qemu-xhci,id=xhci \
  -device usb-tablet \
  "${DISPLAY_ARGS[@]}" \
  -serial "file:$SERIAL" \
  -monitor "unix:$MON,server,nowait" \
  -pidfile "$QEMUPID" \
  -daemonize || die "qemu launch failed"
log "qemu: pid=$(cat "$QEMUPID")"

log "wait: GUEST_RUN_START on serial (boot + device discovery, up to 120s)"
started=0
for _ in $(seq 1 120); do
  if grep -q 'GUEST_RUN_START' "$SERIAL" 2>/dev/null; then
    started=1
    break
  fi
  sleep 1
done
[[ "$started" == "1" ]] || die "guest never reached GUEST_RUN_START; see $SERIAL"

# Let the desk paint its first frames and the shell prompt arrive, then
# catch the framebuffer mid-run.
sleep 3
if [[ "${INPUT:-0}" == "1" ]]; then
  log "gate: real VNC keyboard and tablet interaction"
  python3 -B "$ROOT/scripts/vm/input_gate.py" "$RUN" || die "input interaction failed"
fi
if [[ -S "$MON" ]]; then
  log "shot: screendump -> $SHOT"
  printf 'screendump %s\n' "$SHOT" | socat - UNIX-CONNECT:"$MON" >/dev/null 2>&1 || true
fi

log "wait: GUEST_RUN_DONE on serial (the timeout's kill, up to 60s)"
for _ in $(seq 1 60); do
  grep -q 'GUEST_RUN_DONE' "$SERIAL" 2>/dev/null && break
  sleep 1
done

log "gate: serial markers"
ok=1
grep -q 'GUEST_FB0_OK' "$SERIAL" 2>/dev/null    || { log "FAIL: no /dev/fb0"; ok=0; }
grep -q 'GUEST_KBD_OK' "$SERIAL" 2>/dev/null    || { log "FAIL: no keyboard event node"; ok=0; }
grep -q 'GUEST_TABLET_OK' "$SERIAL" 2>/dev/null || { log "FAIL: no tablet event node"; ok=0; }
grep -Eq 'vesper-fbdev: fb [0-9]+x[0-9]+' "$SERIAL" 2>/dev/null \
  || { log "FAIL: the desk did not open the framebuffer"; ok=0; }
if grep -Eq 'Segmentation fault|Bus error|kernel BUG|Oops:' "$SERIAL" 2>/dev/null; then
  log "FAIL: crash signature"; ok=0
fi
echo "[smoke]   fb0:    $(grep 'GUEST_FB0_OK' "$SERIAL" 2>/dev/null | tail -1)" >&2
echo "[smoke]   kbd:    $(grep 'GUEST_KBD_EVENT=' "$SERIAL" 2>/dev/null | tail -1)" >&2
echo "[smoke]   tablet: $(grep 'GUEST_TABLET_EVENT=' "$SERIAL" 2>/dev/null | tail -1)" >&2
echo "[smoke]   rc:     $(grep 'GUEST_RUN_RC=' "$SERIAL" 2>/dev/null | tail -1)" >&2

log "gate: framebuffer pixels"
if [[ -f "$SHOT" ]]; then
  python3 "$ROOT/scripts/vm/shot_gate.py" "$SHOT" || ok=0
else
  log "FAIL: no screendump at $SHOT"; ok=0
fi

if [[ "$ok" == "1" ]]; then
  log "PASS -- boot, devices, desk loop, painted framebuffer."
  log "  screenshot: $SHOT   serial: $SERIAL"
  exit 0
fi
log "FAIL -- see $SERIAL and $SHOT"
exit 1
