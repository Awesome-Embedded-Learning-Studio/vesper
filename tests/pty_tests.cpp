#include "cases.hpp"
#include "testing.hpp"

#include "desktop/terminal.hpp"
#include "fonts/demo_font.hpp"
#include "host/pty.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"

#include <poll.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

using vesper::desktop::Terminal;
using vesper::host::PtySession;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;

constexpr RectI kTermBounds{.pos = PointI{0, 0}, .size = SizeI{.w = 480, .h = 175}};

/// A real program's bytes are asynchronous, so the judgments wait for them
/// instead of assuming an ordering: poll the master in slices until the
/// deadline, feeding the terminal everything that arrives. A child that
/// reports itself gone gets one last drain for the stragglers, then the
/// wait ends.
void drain_into(Terminal& term, PtySession& session, int deadline_ms) {
    std::array<std::uint8_t, 256> chunk{};
    int waited = 0;
    while (waited < deadline_ms) {
        struct pollfd waiter;
        waiter.fd = session.master_fd();
        waiter.events = POLLIN;
        waiter.revents = 0;
        if (::poll(&waiter, 1, 50) > 0) {
            while (true) {
                const int got = session.read_master(chunk.data(), static_cast<int>(chunk.size()));
                if (got <= 0) {
                    break;
                }
                term.receive(std::string_view{reinterpret_cast<const char*>(chunk.data()),
                                              static_cast<std::size_t>(got)});
                if (static_cast<std::size_t>(got) < chunk.size()) {
                    break;
                }
            }
        }
        waited += 50;
        if (session.child_gone()) {
            while (true) {
                const int got = session.read_master(chunk.data(), static_cast<int>(chunk.size()));
                if (got <= 0) {
                    return;
                }
                term.receive(std::string_view{reinterpret_cast<const char*>(chunk.data()),
                                              static_cast<std::size_t>(got)});
            }
        }
    }
}

} // namespace

void run_pty_tests() {
    // The pty's own echo plus the program's own output: cat hands back what
    // it was sent, so one typed line paints twice -- the echo first, the
    // program's copy underneath.
    {
        Terminal term{kTermBounds, vesper::fonts::kDemoFont};
        constexpr std::array<const char*, 1> cat_args{"cat"};
        auto session = PtySession::open("/bin/cat", cat_args);
        VESPER_CHECK(session.has_value());
        if (session.has_value()) {
            VESPER_CHECK(session->write_master("HI\n"));
            drain_into(term, *session, 3000);
            VESPER_CHECK_EQ(term.cell(0, 0), 'H');
            VESPER_CHECK_EQ(term.cell(1, 0), 'I');
            VESPER_CHECK_EQ(term.cell(0, 1), 'H');
            VESPER_CHECK_EQ(term.cell(1, 1), 'I');
        }
    }

    // A program that finishes: its bytes land, then the child side reports
    // itself gone and every later read stays a quiet zero.
    {
        Terminal term{kTermBounds, vesper::fonts::kDemoFont};
        constexpr std::array<const char*, 2> echo_args{"echo", "BYE"};
        auto session = PtySession::open("/bin/echo", echo_args);
        VESPER_CHECK(session.has_value());
        if (session.has_value()) {
            drain_into(term, *session, 3000);
            VESPER_CHECK_EQ(term.cell(0, 0), 'B');
            VESPER_CHECK_EQ(term.cell(1, 0), 'Y');
            VESPER_CHECK_EQ(term.cell(2, 0), 'E');
            VESPER_CHECK(session->child_gone());
            std::array<std::uint8_t, 8> quiet{};
            VESPER_CHECK_EQ(session->read_master(quiet.data(), 8), 0);
        }
    }

    // A failed exec is the child exiting 127, not an open failure; the
    // session still opens, still reports its child gone, and the grid stays
    // blank.
    {
        Terminal term{kTermBounds, vesper::fonts::kDemoFont};
        constexpr std::array<const char*, 1> nobody_args{"nobody"};
        auto session = PtySession::open("/no/such/program", nobody_args);
        VESPER_CHECK(session.has_value());
        if (session.has_value()) {
            drain_into(term, *session, 3000);
            VESPER_CHECK(session->child_gone());
            VESPER_CHECK_EQ(term.cell(0, 0), ' ');
        }
    }
}
