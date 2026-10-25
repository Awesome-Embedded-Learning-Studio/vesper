#include "cases.hpp"
#include "compositor/guest_spawn.hpp"
#include "compositor/server.hpp"
#include "compositor/shell.hpp"
#include "compositor/surface.hpp"
#include "testing.hpp"

#include "fonts/term_font.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/host.hpp"
#include "visor/bindings/posix/spawn.hpp"
#include "visor/draw/mono_font.hpp"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

#ifndef VESPER_TERMINAL_PATH
#    error "vesper.tests needs VESPER_TERMINAL_PATH: CMake passes the program's built path"
#endif

namespace {

using vesper::compositor::GuestSpawn;
using vesper::compositor::Server;

// The grid the program is born with, the same walk it folds to compile
// time: 80x24 cells over the term font's own measure.
constexpr int kCellW = [] {
    int width = 1;
    for (const visor::draw::GlyphEntry& glyph : vesper::fonts::term_font::kTermFont.glyphs) {
        width = std::max(width, static_cast<int>(glyph.advance));
    }
    return width;
}();
constexpr int kCellH =
    std::max(1, static_cast<int>(vesper::fonts::term_font::kTermFont.line_height));
constexpr int kWidth = 80 * kCellW;
constexpr int kHeight = 24 * kCellH;

/// One settle round: serve the program, beat the frame clock (the releases
/// and frame words it paints on), flush, breathe.
bool settle(Server& server, int round) noexcept {
    if (!server.dispatch(0)) {
        return false;
    }
    server.note_time_ms(static_cast<std::uint32_t>(round));
    server.end_frame();
    server.flush_clients();
    static_cast<void>(::usleep(5'000));
    return true;
}

/// Bounded waiting for a predicate the program's next round satisfies.
template <typename Predicate>
bool await(Server& server, Predicate done) noexcept {
    for (int round = 0; round < 400; ++round) { // two seconds of budget
        if (done()) {
            return true;
        }
        if (!settle(server, round)) {
            return false;
        }
    }
    return done();
}

/// How many distinct pixel words the surface's first rows hold: the empty
/// screen is ground plus caret, and text adds the glyphs' dress -- three
/// colors is the word "the program's bytes landed".
int distinct_words(const vesper::compositor::Surface::Frame& frame, int rows) noexcept {
    if (frame.view.words == nullptr) {
        return 0;
    }
    std::array<std::uint32_t, 16> seen{};
    int count = 0;
    const std::size_t scan = std::min<std::size_t>(frame.view.stride_words * rows,
                                                   frame.view.stride_words *
                                                       static_cast<std::size_t>(frame.view.height));
    for (std::size_t i = 0; i < scan; ++i) {
        const std::uint32_t word = frame.view.words[i];
        bool known = false;
        for (int j = 0; j < count; ++j) {
            known = seen[static_cast<std::size_t>(j)] == word;
            if (known) {
                break;
            }
        }
        if (!known && std::cmp_less(count, seen.size())) {
            seen[static_cast<std::size_t>(count)] = word;
            ++count;
        }
    }
    return count;
}

void run_terminal_birth_tests() {
    Server server;
    VESPER_CHECK(server.valid());
    visor::host::HostTable birth{};
    birth.spawn = &visor::host::spawn_via_posix;
    GuestSpawn terminal;

    // The program behind the screen prints a word, echoes whatever is
    // typed, and leaves when the desk sends the end-of-file fold.
    const std::array<const char*, 4> argv{
        VESPER_TERMINAL_PATH,
        "/bin/sh",
        "-c",
        "printf 'READY'; cat",
    };
    VESPER_CHECK(terminal.launch(server, birth, VESPER_TERMINAL_PATH, argv));

    // The window stands: one shell face, titled, sized to the grid.
    VESPER_CHECK(await(server, [&] { return server.shell_surface_count() == 1; }));
    vesper::compositor::ShellSurface* shell = server.shell_surface(0);
    VESPER_CHECK(shell != nullptr);
    if (shell == nullptr) {
        return;
    }
    VESPER_CHECK(await(server, [&] { return shell->width() == kWidth; }));
    VESPER_CHECK_EQ(shell->height(), kHeight);
    VESPER_CHECK_EQ(std::strcmp(shell->title(), "Terminal"), 0);
    VESPER_CHECK(shell->mapped());

    // The screen paints: READY's glyphs are a third color the empty screen
    // never held (ground, caret, and now the text's dress). The scan covers
    // the first cell row whole, because a glyph's body starts a couple of
    // pixel rows down from the cell's top.
    VESPER_CHECK(
        await(server, [&] { return distinct_words(server.surface_frame(0), kCellH) >= 3; }));

    // The echo round trip: a key through the seat, the byte through the
    // pty, and the program's answer back onto the screen as a fresh
    // commit. Enter closes the line so the reader behind the pty gets it.
    const std::uint64_t commits = server.surface_frame(0).epoch;
    server.seat().keyboard_enter(shell->surface());
    server.seat().set_modifiers(visor::host::Modifiers{});
    server.seat().key(30, true); // KEY_A, unshifted: the fold spells 'a'
    server.seat().key(30, false);
    server.seat().key(28, true); // KEY_ENTER: the line leaves the discipline
    server.seat().key(28, false);
    VESPER_CHECK(await(server, [&] { return server.surface_frame(0).epoch > commits; }));

    // The goodbye: Ctrl+D is EOF behind the pty, the program leaves, the
    // farewell renders, and the connection follows the program out -- the
    // child reaped by the spawn, no zombie, no window outliving its pty.
    server.seat().set_modifiers(visor::host::Modifiers{.ctrl = true});
    server.seat().key(32, true); // KEY_D under ctrl: the 0x04 fold
    server.seat().key(32, false);
    VESPER_CHECK(await(server, [&] { return !terminal.alive(); }));
    VESPER_CHECK(await(server, [&] { return server.client_count() == 0; }));
    VESPER_CHECK(terminal.client() == nullptr);
}

} // namespace

void run_terminal_app_tests() {
    run_terminal_birth_tests();
}
