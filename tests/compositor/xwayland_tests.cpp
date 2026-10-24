#include "cases.hpp"
#include "compositor/server.hpp"
#include "compositor/shell.hpp"
#include "compositor/xwayland.hpp"

#include "testing.hpp"
#include "ui/shell_desk/shell_desk.hpp"
#include "ui/window_frame/window_frame.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/posix/spawn.hpp"

#include "fake_color_host.hpp"
#include "fonts/demo_font.hpp"
#include "ui/desktop/desktop.hpp"
#include "ui/style/theme.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/run_loop.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <sys/types.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <print>

// The C-name headers the fork/kill family lives in: the modernize lane
// wants their C++ spellings and the include lane wants these, so the two
// fight over the same lines and the lines opt out.
// NOLINTBEGIN(modernize-deprecated-headers,misc-include-cleaner)
#include <signal.h>
#include <stdlib.h>
// NOLINTEND(modernize-deprecated-headers,misc-include-cleaner)

namespace {

using vesper::compositor::Server;
using vesper::compositor::ShellSurface;
using vesper::compositor::XWayland;
using vesper::desktop::Desktop;
using vesper::desktop::ShellDesk;
using vesper::desktop::WindowFrame;
using vesper::host::FakeColorHost;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

constexpr SizeI kScreen{.w = 320, .h = 256};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
using Frame = visor::canvas::Canvas<kScreen, visor::canvas::Xrgb8888Traits>;
using Strip = visor::canvas::Canvas<SizeI{.w = 320, .h = 64}, visor::canvas::Xrgb8888Traits>;

// The desk's amber, the word the X guest draws and the judgment reads.
constexpr std::uint32_t kAmber = 0xffc9'8a2dU;

void run_readiness_timeout_tests() {
    const char* original = std::getenv("VESPER_XWAYLAND");
    std::array<char, 4096> saved{};
    if (original != nullptr) {
        static_cast<void>(std::snprintf(saved.data(), saved.size(), "%s", original));
    }
    static_cast<void>(::setenv("VESPER_XWAYLAND", "/proc/self/exe", 1));
    Server server;
    XWayland bridge;
    // The unwired stance first: a table without the fourth slot cannot
    // birth, and the bridge says so at the door.
    VESPER_CHECK(!bridge.launch(server, visor::host::HostTable{}));
    // The wired stance with a never-ready stand-in: the birth succeeds,
    // the displayfd word never arrives, and the budget reclaims all.
    visor::host::HostTable birth;
    birth.spawn = &visor::host::spawn_via_posix;
    const auto started = std::chrono::steady_clock::now();
    VESPER_CHECK(!bridge.launch(server, birth));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    VESPER_CHECK(elapsed < std::chrono::seconds{5});
    VESPER_CHECK(!bridge.alive());
    VESPER_CHECK_EQ(bridge.event_fd(), -1);
    VESPER_CHECK_EQ(server.client_count(), 0);
    if (original != nullptr) {
        static_cast<void>(::setenv("VESPER_XWAYLAND", saved.data(), 1));
    } else {
        static_cast<void>(::unsetenv("VESPER_XWAYLAND"));
    }
}

/// Where the bridge and its X guest sit, when the machine carries them.
const char* xwayland_binary() noexcept {
    const char* override = std::getenv("VESPER_XWAYLAND");
    return override != nullptr && override[0] != '\0' ? override : "/usr/bin/Xwayland";
}

const char* xhello_binary() noexcept {
    const char* override = std::getenv("VESPER_XHELLO");
    return override != nullptr && override[0] != '\0' ? override : "../examples/xhello";
}

void run_live_bridge_tests() {
    // The C5 judgment, live: a real Xwayland process, a real X11 program,
    // one amber face on the desk and one graceful goodbye. The judgment is
    // loud about a missing Xwayland rather than quietly green -- the
    // environment owes the binary (arch: xorg-xwayland), the desk owes the
    // rest.
    if (::access(xwayland_binary(), X_OK) != 0) {
        std::println("SKIP compositor/xwayland: no Xwayland at {} (install xorg-xwayland to arm)",
                     xwayland_binary());
        return;
    }
    if (::access(xhello_binary(), X_OK) != 0) {
        std::println("SKIP compositor/xwayland: no xhello at {} (build the examples first)",
                     xhello_binary());
        return;
    }

    FakeColorHost host{320, 256};
    visor::ui::InputRouter input{};
    visor::ui::DirtyRegions dirty{};
    Strip strip{};
    Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::kDemoFont,
                    vesper::desktop::style::kColorDeskTheme};
    Server server;
    ShellDesk desk{desktop, server, vesper::fonts::kDemoFont};
    XWayland bridge;
    desktop.set_dirty_sink(&dirty);
    dirty.push(kScreenBounds);

    server.note_output_extent(kScreen.w, kScreen.h);
    const bool launched = bridge.launch(server, host.table());
    VESPER_CHECK(launched);
    if (!launched) {
        return;
    }
    VESPER_CHECK(!bridge.owns(nullptr));
    const bool running = bridge.alive();
    VESPER_CHECK(running);
    if (!running) {
        std::println(stderr, "FAIL compositor/xwayland: Xwayland died at birth");
        return;
    }
    desk.set_xwayland(&bridge);

    // The X guest, pointed at the display the bridge took.
    std::array<char, 32> display_env{};
    static_cast<void>(
        std::snprintf(display_env.data(), display_env.size(), "DISPLAY=%s", bridge.display()));
    const pid_t guest = ::fork();
    if (guest == 0) {
        static_cast<void>(::putenv(display_env.data()));
        ::execl(xhello_binary(), "xhello", static_cast<char*>(nullptr));
        _exit(127);
    }
    VESPER_CHECK(guest > 0);
    if (guest < 0) {
        return;
    }

    // The desk's round: serve, collect, render, present -- repeated while
    // the guest finds its way to a mapped face.
    const auto round = [&] {
        desk.pump();
        desk.collect();
        static_cast<void>(
            visor::ui::run_dirty(host.table(), desktop, input, strip, kScreen, dirty));
        server.note_time_ms(10);
        desk.present();
    };

    ShellSurface* hello = nullptr;
    for (int waited = 0; waited < 400 && hello == nullptr; ++waited) {
        round();
        ::usleep(20'000);
        for (int i = 0; i < server.shell_surface_count(); ++i) {
            ShellSurface* candidate = server.shell_surface(i);
            if (candidate != nullptr && candidate->mapped() && bridge.owns(candidate)) {
                hello = candidate;
            }
        }
    }
    VESPER_CHECK(hello != nullptr);
    VESPER_CHECK(bridge.owns(hello));
    if (hello == nullptr) {
        static_cast<void>(::kill(guest, SIGKILL));
        static_cast<void>(::waitpid(guest, nullptr, 0));
        return;
    }

    // The amber face, pixel for pixel, through the whole bridge.
    const WindowFrame* window = desk.frame_of(hello);
    VESPER_CHECK(window != nullptr);
    round();
    round();
    bool amber_found = false;
    if (window != nullptr) {
        const RectI content = window->content_area();
        for (int y = content.pos.y + 8; y < content.pos.y + 56 && !amber_found; y += 4) {
            for (int x = content.pos.x + 8; x < content.pos.x + 88; x += 4) {
                if (host.get(PointI{x, y}) == kAmber) {
                    amber_found = true;
                    break;
                }
            }
        }
    }
    VESPER_CHECK(amber_found);

    // The graceful goodbye: the desk's close word for an X face rides
    // WM_DELETE_WINDOW (the same route the close box takes), the guest
    // leaves, the window comes down.
    bridge.request_close(*hello);
    bool closed = false;
    for (int waited = 0; waited < 400 && !closed; ++waited) {
        round();
        ::usleep(20'000);
        if (!bridge.owns(hello)) {
            closed = true;
        }
    }
    int status = 0;
    pid_t exited = 0;
    for (int waited = 0; waited < 100 && exited == 0; ++waited) {
        exited = ::waitpid(guest, &status, WNOHANG);
        if (exited == 0) {
            ::usleep(10'000);
        }
    }
    VESPER_CHECK_EQ(exited, guest);
    VESPER_CHECK(WIFEXITED(status));
    VESPER_CHECK_EQ(WEXITSTATUS(status), 0);
    if (exited == 0) {
        static_cast<void>(::kill(guest, SIGKILL));
        static_cast<void>(::waitpid(guest, nullptr, 0));
    }
    VESPER_CHECK(closed);
}

} // namespace

void run_compositor_xwayland_tests() {
    run_readiness_timeout_tests();
    run_live_bridge_tests();
}
