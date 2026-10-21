#include "cases.hpp"
#include "host/fbdev_device.hpp"

#include "testing.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"

namespace {

// The device's logic half (which mono bit paints which word) is visor's
// frame expansion under its own judgments; what is ours is the open stance:
// a path that is no framebuffer leaves the device closed, and a closed
// device paints nothing. The live open-and-paint path is the VM lane's
// judgment, run under QEMU by scripts/vm/smoke.sh.
void a_missing_device_stays_closed() {
    vesper::host::FbdevDevice fb;
    VESPER_CHECK(!fb.open("/dev/vesper-no-such-framebuffer"));
    VESPER_CHECK(!fb.ready());
    // A closed device refuses to paint rather than touching a null map.
    fb.expand(
        visor::base::RectI{
            .pos = visor::base::PointI{0, 0},
            .size = visor::base::SizeI{.w = 8, .h = 1},
        },
        nullptr, 1, 0, 0xffffffff);
}

} // namespace

void run_fbdev_device_tests() {
    a_missing_device_stays_closed();
}
