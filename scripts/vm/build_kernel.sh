#!/usr/bin/env bash
# scripts/vm/build_kernel.sh -- a minimal x86_64 kernel for the fbdev VM lane.
#
# x86_64_defconfig plus the symbols the bare guest needs: a /dev/fb0 (vesafb
# or bochs-drm emulation), fbcon, xhci + usbhid for the usb-tablet, and evdev.
# Source defaults to the repo-local third_party/linux (gitignored); the script
# downloads the pinned tarball there when the tree is absent.
#
# Output: build/vm/linux-build/arch/x86/boot/bzImage

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${KERNEL_SRC:-$ROOT/third_party/linux}"
OUT="$ROOT/build/vm/linux-build"
BZIMAGE="$OUT/arch/x86/boot/bzImage"
# Pinned so a rerun builds the same kernel the VM judgment was written
# against; bump deliberately, not by chasing the latest.
KERNEL_VERSION="6.18.57"
TARBALL="linux-${KERNEL_VERSION}.tar.xz"

log() { printf '[kernel] %s\n' "$*"; }
die() { log "FAIL: $*"; exit 1; }

mkdir -p "$ROOT/third_party" "$OUT"

if [[ ! -f "$SRC/Makefile" ]]; then
  log "source absent: fetching ${TARBALL} from cdn.kernel.org"
  curl -fL --http1.1 --retry 3 -o "$ROOT/third_party/${TARBALL}" \
    "https://cdn.kernel.org/pub/linux/kernel/v6.x/${TARBALL}"
  log "extract: $SRC"
  tar -C "$ROOT/third_party" -xJf "$ROOT/third_party/${TARBALL}"
  mv "$ROOT/third_party/linux-${KERNEL_VERSION}" "$SRC"
fi

if [[ ! -f "$OUT/.config" ]]; then
  log "config: x86_64_defconfig"
  make -C "$SRC" O="$OUT" x86_64_defconfig
fi

# x86_64_defconfig alone does not enable the fbdev guest (server profile):
# force the display and input symbols on, then olddefconfig resolves the
# dependency chains behind them.
log "force-enable the guest symbols (FB + vesafb + fbcon + xhci + usbhid + evdev)"
"$SRC/scripts/config" --file "$OUT/.config" \
  --enable CONFIG_FB \
  --enable CONFIG_FB_VESA \
  --enable CONFIG_FRAMEBUFFER_CONSOLE \
  --enable CONFIG_VT \
  --enable CONFIG_VT_CONSOLE \
  --enable CONFIG_USB_SUPPORT \
  --enable CONFIG_USB \
  --enable CONFIG_USB_XHCI \
  --enable CONFIG_USB_XHCI_PCI \
  --enable CONFIG_HID \
  --enable CONFIG_USB_HID \
  --enable CONFIG_INPUT_EVDEV \
  --enable CONFIG_DRM \
  --enable CONFIG_DRM_KMS_HELPER \
  --enable CONFIG_DRM_FBDEV_EMULATION \
  --enable CONFIG_DRM_BOCHS
make -C "$SRC" O="$OUT" olddefconfig
log "post-olddefconfig symbol check:"
for sym in CONFIG_FB CONFIG_FB_VESA CONFIG_FRAMEBUFFER_CONSOLE CONFIG_USB_XHCI \
           CONFIG_USB_HID CONFIG_INPUT_EVDEV; do
  log "  $sym=$(grep "^${sym}=" "$OUT/.config" | cut -d= -f2- || echo missing)"
done

log "build: bzImage (-j$(nproc))"
make -C "$SRC" O="$OUT" -j"$(nproc)" bzImage

[[ -f "$BZIMAGE" ]] || die "bzImage not produced at $BZIMAGE"
log "done: $(ls -lh "$BZIMAGE" | awk '{print $5, $9}')"
