#include "cases.hpp"
#include "testing.hpp"

#include "host/fake_session_host.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/display.hpp"
#include "visor/bindings/events.hpp"
#include "visor/canvas/mono_canvas.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>

namespace {

using vesper::host::FakeSessionHost;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

constexpr SizeI kScreen{.w = 64, .h = 32};
constexpr RectI kFull{.pos = PointI{0, 0}, .size = kScreen};
using Frame = visor::canvas::Canvas<kScreen>;

} // namespace

void run_fake_host_tests() {
    FakeSessionHost host{64, 32};
    const auto table = host.table();

    // The table is fully wired and describes the fixed screen.
    VESPER_CHECK(table.flush != nullptr);
    VESPER_CHECK(table.poll_event != nullptr);
    VESPER_CHECK(table.clock_ms != nullptr);
    VESPER_CHECK_EQ(table.info.screen.w, 64);
    VESPER_CHECK_EQ(table.info.screen.h, 32);
    VESPER_CHECK(table.info.format == visor::host::PixelFormat::Mono1);

    // A full frame lands: counted, logged, and readable pixel by pixel.
    Frame frame;
    frame.set_clear(false);
    VESPER_CHECK((frame.set(PointI{10, 5}, true)));
    VESPER_CHECK((frame.set(PointI{63, 31}, true)));
    VESPER_CHECK(table.submit_frame(frame));
    VESPER_CHECK_EQ(host.flush_count(), 1);
    VESPER_CHECK_EQ(host.regions().size(), (std::size_t{1}));
    VESPER_CHECK(host.regions()[0] == kFull);
    VESPER_CHECK(host.get(PointI{10, 5}));
    VESPER_CHECK(host.get(PointI{63, 31}));
    VESPER_CHECK(!host.get(PointI{11, 5}));
    VESPER_CHECK(!host.get(PointI{-1, 0})); // outside the screen reads unlit
    VESPER_CHECK_EQ(host.rejected_count(), 0);

    // A sub-region lands with only its own rows: the pointer rides the top
    // row of the region, the stride is the canvas's.
    host.clear_regions();
    constexpr int stride = static_cast<int>(Frame::kStrideBytes);
    VESPER_CHECK((frame.set(PointI{5, 5}, true)));
    const std::uint8_t* top_row = frame.pixels().data() + static_cast<std::ptrdiff_t>(4 * stride);
    VESPER_CHECK((table.submit_region(RectI{.pos = PointI{0, 4}, .size = SizeI{.w = 8, .h = 8}},
                                      top_row, stride)));
    VESPER_CHECK_EQ(host.flush_count(), 2);
    VESPER_CHECK_EQ(host.regions().size(), (std::size_t{1}));
    VESPER_CHECK((host.regions()[0] == RectI{.pos = PointI{0, 4}, .size = SizeI{.w = 8, .h = 8}}));
    VESPER_CHECK(host.get(PointI{5, 5}));

    // Refusals follow the offscreen line: wrong format, null pixels, empty
    // rect, out of bounds. Each is counted, none is logged, no pixel moves.
    const std::uint8_t dummy = 0;
    const auto wrong_format = visor::host::FlushRegion{
        .rect = kFull,
        .pixels = &dummy,
        .stride_bytes = stride,
        .format = static_cast<visor::host::PixelFormat>(9),
    };
    host.table().flush(host.table().context, wrong_format);
    VESPER_CHECK((table.submit_region(kFull, nullptr, stride)));
    VESPER_CHECK((table.submit_region(RectI{.pos = PointI{0, 0}, .size = SizeI{.w = 0, .h = 0}},
                                      &dummy, stride)));
    VESPER_CHECK((table.submit_region(RectI{.pos = PointI{60, 0}, .size = SizeI{.w = 8, .h = 32}},
                                      &dummy, stride)));
    VESPER_CHECK_EQ(host.rejected_count(), 4);
    VESPER_CHECK_EQ(host.regions().size(), (std::size_t{1})); // the sub-region from above
    VESPER_CHECK(!host.get(PointI{30, 15}));                  // empty rect left nothing behind

    // Events come back in feed order, then the queue reads empty.
    VESPER_CHECK((host.push_event(visor::host::PointerMove{.pos = PointI{1, 2}})));
    VESPER_CHECK(host.push_event(visor::host::KeyDown{.code = 65}));
    visor::host::Event out;
    VESPER_CHECK(table.poll(out));
    VESPER_CHECK(std::holds_alternative<visor::host::PointerMove>(out));
    VESPER_CHECK((std::get<visor::host::PointerMove>(out).pos == PointI{1, 2}));
    VESPER_CHECK(table.poll(out));
    VESPER_CHECK(std::holds_alternative<visor::host::KeyDown>(out));
    VESPER_CHECK(!table.poll(out));

    // A full feed refuses the newcomer and reports the drop.
    for (int i = 0; i < 15; ++i) {
        VESPER_CHECK((host.push_event(visor::host::PointerMove{.pos = PointI{i, 0}})));
    }
    VESPER_CHECK((!host.push_event(visor::host::PointerMove{.pos = PointI{99, 0}})));
    VESPER_CHECK_EQ(host.dropped_event_count(), 1);
    // The residents survived: fifteen still waiting (thirteen here, the two
    // earlier ones were drained above).
    int drained = 0;
    while (table.poll(out)) {
        ++drained;
    }
    VESPER_CHECK_EQ(drained, 15);

    // The scripted clock starts at zero and walks by advance_ms.
    VESPER_CHECK_EQ(table.now_ms(), (std::uint64_t{0}));
    host.advance_ms(5);
    VESPER_CHECK_EQ(table.now_ms(), (std::uint64_t{5}));
    host.advance_ms(7);
    VESPER_CHECK_EQ(table.now_ms(), (std::uint64_t{12}));

    // clear_regions forgets the log, counts stay.
    host.clear_regions();
    VESPER_CHECK(host.regions().empty());
    VESPER_CHECK_EQ(host.flush_count(), 2);
}
