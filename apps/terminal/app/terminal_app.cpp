// The terminal program (the roadmap's T2): the grid, the pty and the text
// fold assembled as one Wayland client. The desk births this program through
// the fourth slot (T3), or a hand points WAYLAND_DISPLAY at the desk's
// socket; either way the conversation is the guest's half of the C-series
// words -- registry, shm buffers, one xdg toplevel, keystrokes as bytes.
// Run it beside the compositor example:
//   WAYLAND_DISPLAY=<the socket it printed> vesper_terminal
// The program behind the screen defaults to an interactive bash; arguments
// name any other program, argv[0] conventions intact:
//   vesper_terminal /bin/sh -c 'printf hello'
// The program's exit is the window's goodbye: the farewell line lands, the
// last commit flushes, and the process leaves -- no window outliving its pty.
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#include "xdg-shell-client-protocol.h"

// wayland's C headers name their own shapes; the same exemption wl_block and
// the sparring partner draw.
// NOLINTBEGIN(misc-include-cleaner)

#include "fonts/term_font.hpp"
#include "host/keymap.hpp"
#include "host/pty.hpp"
#include "ui/pty_source/pty_source.hpp"
#include "ui/terminal/terminal.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/color.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/draw/mono_font.hpp"
#include "visor/ui/canvas_painter.hpp"
#include "visor/ui/theme.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <print>
#include <span>
#include <vector>

namespace {

// The classic grid every terminal program assumes before it learns better.
constexpr int kCols = 80;
constexpr int kRows = 24;

// The cell is the font's own measure, the same walk the grid's constructor
// takes: the widest advance and the line height, folded to compile time so
// the screen buffer is sized before the first pixel exists.
constexpr int kCellW = [] {
    int width = 1;
    for (const visor::draw::GlyphEntry& glyph : vesper::fonts::term_font::kTermFont.glyphs) {
        width = std::max(width, static_cast<int>(glyph.advance));
    }
    return width;
}();
constexpr int kCellH =
    std::max(1, static_cast<int>(vesper::fonts::term_font::kTermFont.line_height));

constexpr int kWidth = kCols * kCellW;
constexpr int kHeight = kRows * kCellH;
constexpr int kStrideWords = kWidth;

using Screen = visor::canvas::Canvas<visor::base::SizeI{.w = kWidth, .h = kHeight},
                                     visor::canvas::Xrgb8888Traits>;
constexpr visor::base::RectI kScreenBounds{
    .pos = visor::base::PointI{0, 0},
    .size = visor::base::SizeI{.w = kWidth, .h = kHeight},
};

// The program's own face, owned here so the app lane stays desk-free: soft
// ink ground, paper glyphs, the dusk-gold caret -- the same palette words
// the desk's scene speaks. The grid fills its bounds with the Foreground
// role and punches glyphs as Background holes, so the roles land inverted
// from their names, the stance the screen was born with.
constexpr visor::ui::Theme kTheme{
    .background = visor::canvas::Ink::from_rgb(0xf2ecdc),
    .foreground = visor::canvas::Ink::from_rgb(0x2b2620),
    .muted = visor::canvas::Ink::from_rgb(0x8a7f6a),
    .accent = visor::canvas::Ink::from_rgb(0xc98a2d),
    .on_accent = visor::canvas::Ink::from_rgb(0x2b2620),
    .border = visor::canvas::Ink::from_rgb(0x8a7f6a),
    .disabled = visor::canvas::Ink::from_rgb(0x8a7f6a),
};

/// The client's whole state: the bound globals, the surface with its xdg
/// role, the two-buffer pool and the words the keyboard fold feeds.
struct Guest {
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    xdg_wm_base* wm_base = nullptr;
    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    wl_surface* surface = nullptr;
    xdg_surface* role_surface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    std::array<wl_buffer*, 2> buffers{};
    std::uint32_t* words = nullptr;
    std::array<bool, 2> released{true, true};
    int active = 0;
    bool configured = false;
    bool close_requested = false;
    bool shift = false;
    bool ctrl = false;
    vesper::desktop::Terminal* term = nullptr;
};

void registry_global(void* data, wl_registry* registry, std::uint32_t name, const char* interface,
                     std::uint32_t version) noexcept {
    auto* guest = static_cast<Guest*>(data);
    if (std::strcmp(interface, "wl_compositor") == 0) {
        guest->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 1));
    } else if (std::strcmp(interface, "wl_shm") == 0) {
        guest->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, "xdg_wm_base") == 0) {
        guest->wm_base =
            static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
    } else if (std::strcmp(interface, "wl_seat") == 0) {
        // v5 is the seat the desk speaks: grouping pointers and the
        // high-resolution wheel words, neither of which this guest reads.
        const std::uint32_t bound = std::min(version, 5U);
        guest->seat =
            static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, bound));
    }
}

void registry_global_remove(void* /*data*/, wl_registry* /*registry*/,
                            std::uint32_t /*name*/) noexcept {}

const wl_registry_listener kRegistry = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void wm_base_ping(void* /*data*/, xdg_wm_base* wm_base, std::uint32_t serial) noexcept {
    xdg_wm_base_pong(wm_base, serial);
}

const xdg_wm_base_listener kWmBase = {
    .ping = wm_base_ping,
};

void xdg_configure(void* data, xdg_surface* xdg_surface, std::uint32_t serial) noexcept {
    static_cast<Guest*>(data)->configured = true;
    xdg_surface_ack_configure(xdg_surface, serial);
}

const xdg_surface_listener kXdgSurface = {
    .configure = xdg_configure,
};

void toplevel_close(void* data, xdg_toplevel* /*toplevel*/) noexcept {
    static_cast<Guest*>(data)->close_requested = true;
}

void toplevel_configure(void* /*data*/, xdg_toplevel* /*toplevel*/, std::int32_t /*width*/,
                        std::int32_t /*height*/, wl_array* /*states*/) noexcept {
    // The desk's shell does not resize windows yet, so the grid keeps the
    // size it was born with; the sizing words are heard and filed.
}

const xdg_toplevel_listener kToplevel = {
    .configure = toplevel_configure,
    .close = toplevel_close,
    .configure_bounds = nullptr, // sizing advice this guest cannot follow yet
    .wm_capabilities = nullptr,  // what the desk allows; the guest takes it
};

void frame_done(void* /*data*/, wl_callback* callback, std::uint32_t /*callback_data*/) noexcept {
    // The beat's receipt: the proxy's one job is done. A paint that waited
    // on a buffer release retries on the loop's next round, not here.
    wl_callback_destroy(callback);
}

const wl_callback_listener kFrame = {
    .done = frame_done,
};

void buffer_release(void* data, wl_buffer* buffer) noexcept {
    auto* guest = static_cast<Guest*>(data);
    for (std::size_t i = 0; i < guest->buffers.size(); ++i) {
        if (guest->buffers[i] == buffer) {
            guest->released[i] = true;
        }
    }
}

const wl_buffer_listener kBuffer = {
    .release = buffer_release,
};

void keyboard_keymap(void* /*data*/, wl_keyboard* /*keyboard*/, std::uint32_t /*format*/,
                     std::int32_t fd, std::uint32_t /*size*/) noexcept {
    // This guest folds raw codes through the lane's own US table, so the
    // desk's keymap file is closed unread -- the same reading the sparring
    // partner gives it.
    ::close(fd);
}

void keyboard_enter(void* /*data*/, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/,
                    wl_surface* /*surface*/, wl_array* /*keys*/) noexcept {}

void keyboard_leave(void* /*data*/, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/,
                    wl_surface* /*surface*/) noexcept {}

void keyboard_key(void* data, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/,
                  std::uint32_t /*time*/, std::uint32_t key, std::uint32_t state) noexcept {
    auto* guest = static_cast<Guest*>(data);
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || guest->term == nullptr) {
        return;
    }
    // The wire speaks evdev; the grid eats code points. The fold is the
    // lane's own table, and the arrows come back from their wire numbers to
    // the core's named words -- the exact inverse of the seat's walk out.
    std::uint32_t code = 0;
    switch (key) {
        case 103:
            code = visor::host::kKeyUp;
            break;
        case 108:
            code = visor::host::kKeyDown;
            break;
        case 105:
            code = visor::host::kKeyLeft;
            break;
        case 106:
            code = visor::host::kKeyRight;
            break;
        default:
            code = vesper::host::text_code_for(static_cast<std::uint16_t>(key), guest->shift);
            break;
    }
    guest->term->on_key_down(code, visor::host::Modifiers{.ctrl = guest->ctrl});
}

void keyboard_modifiers(void* data, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/,
                        std::uint32_t depressed, std::uint32_t /*latched*/,
                        std::uint32_t /*locked*/, std::uint32_t /*group*/) noexcept {
    // The xkb masks the seat ships: shift decides the fold's case, ctrl is
    // passed on for the screen's control-byte fold.
    auto* guest = static_cast<Guest*>(data);
    guest->shift = (depressed & 0x1U) != 0;
    guest->ctrl = (depressed & 0x4U) != 0;
}

void keyboard_repeat(void* /*data*/, wl_keyboard* /*keyboard*/, std::int32_t /*rate*/,
                     std::int32_t /*delay*/) noexcept {
    // Repeat is a later layer; the word is heard and filed.
}

const wl_keyboard_listener kKeyboard = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat,
};

void seat_capabilities(void* data, wl_seat* seat, std::uint32_t capabilities) noexcept {
    auto* guest = static_cast<Guest*>(data);
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && guest->keyboard == nullptr) {
        guest->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(guest->keyboard, &kKeyboard, guest);
    }
}

void seat_name(void* /*data*/, wl_seat* /*seat*/, const char* /*name*/) noexcept {
    // Which seat this is carries nothing a one-seat guest reads.
}

const wl_seat_listener kSeat = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

/// One frame: the grid into the canvas, the canvas into a buffer the desk
/// has released, and the commit that hands it over. False when the desk
/// still holds both buffers -- the release word (or the next pty byte)
/// retries on a later round.
bool paint(Guest& guest, Screen& screen, vesper::desktop::Terminal& term) noexcept {
    int next = -1;
    if (guest.released[1 - guest.active]) {
        next = 1 - guest.active;
    } else if (guest.released[guest.active]) {
        next = guest.active;
    }
    if (next < 0) {
        return false;
    }
    guest.active = next;
    {
        const visor::ui::CanvasPainter<Screen> painter{screen, kTheme};
        term.draw(painter);
    }
    std::memcpy(guest.words + (static_cast<std::size_t>(next) * kStrideWords * kHeight),
                screen.pixels().data(), Screen::kBufferBytes);
    wl_surface_attach(guest.surface, guest.buffers[next], 0, 0);
    wl_surface_damage(guest.surface, 0, 0, kWidth, kHeight);
    wl_callback* beat = wl_surface_frame(guest.surface);
    wl_callback_add_listener(beat, &kFrame, &guest);
    wl_surface_commit(guest.surface);
    guest.released[next] = false;
    return true;
}

} // namespace

// C++ fixes the entry point's argument type as char**.
int main(int argc, char* argv[]) { // NOLINT(misc-const-correctness)
    // The program behind the screen: the arguments name it whole (argv[0]
    // included), the default is the interactive bash the desk's demo ran.
    std::vector<const char*> program_args;
    std::vector<const char*> program_env{"TERM=vt100"};
    if (argc > 1) {
        program_args.assign(argv + 1, argv + argc);
    } else {
        program_args = {"bash", "--norc", "-i"};
        program_env.push_back("PS1=> ");
    }

    Guest guest;
    wl_display* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        std::println(stderr, "vesper-terminal: no display (set WAYLAND_DISPLAY to the desk's "
                             "socket, or let the desk spawn this program)");
        return 1;
    }
    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &kRegistry, &guest);
    static_cast<void>(wl_display_roundtrip(display));
    wl_registry_destroy(registry);
    if (guest.compositor == nullptr || guest.shm == nullptr || guest.wm_base == nullptr ||
        guest.seat == nullptr) {
        std::println(stderr, "vesper-terminal: the desk did not offer the four globals a "
                             "terminal needs");
        return 1;
    }
    xdg_wm_base_add_listener(guest.wm_base, &kWmBase, &guest);
    wl_seat_add_listener(guest.seat, &kSeat, &guest);
    // Two settles: the capabilities word on the first, the keyboard object
    // and its keymap file on the second.
    static_cast<void>(wl_display_roundtrip(display));
    static_cast<void>(wl_display_roundtrip(display));

    static Screen screen; // the grid's raster: storage, not stack
    vesper::desktop::Terminal term{kScreenBounds, vesper::fonts::term_font::kTermFont};
    auto session = vesper::host::PtySession::open(program_args.front(), program_args, program_env);
    if (!session.has_value()) {
        std::println(stderr, "vesper-terminal: cannot spawn {} under a pty", program_args.front());
        return 1;
    }
    // The line editor behind the screen redraws against the grid's own
    // cell count, never a guessed default.
    static_cast<void>(session->set_winsize(term.cols(), term.rows()));
    vesper::desktop::PtySource source{term, *session};
    term.set_source(&source);
    guest.term = &term;

    // The pool: one memfd, two buffers, the classic pair.
    constexpr std::size_t plane_words = static_cast<std::size_t>(kStrideWords) * kHeight;
    constexpr std::size_t pool_bytes = plane_words * 4U * 2U;
    const int fd = ::memfd_create("vesper-terminal", 0);
    if (fd < 0 || ::ftruncate(fd, static_cast<off_t>(pool_bytes)) != 0) {
        std::println(stderr, "vesper-terminal: no pool");
        return 1;
    }
    void* map = ::mmap(nullptr, pool_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        std::println(stderr, "vesper-terminal: no mapping");
        return 1;
    }
    guest.words = static_cast<std::uint32_t*>(map);
    wl_shm_pool* pool = wl_shm_create_pool(guest.shm, fd, static_cast<std::int32_t>(pool_bytes));
    ::close(fd);
    for (int i = 0; i < 2; ++i) {
        const auto offset =
            static_cast<std::int32_t>(static_cast<std::size_t>(i) * plane_words * 4U);
        guest.buffers[i] = wl_shm_pool_create_buffer(pool, offset, kWidth, kHeight, kWidth * 4,
                                                     WL_SHM_FORMAT_XRGB8888);
        wl_buffer_add_listener(guest.buffers[i], &kBuffer, &guest);
    }
    wl_shm_pool_destroy(pool);

    guest.surface = wl_compositor_create_surface(guest.compositor);
    guest.role_surface = xdg_wm_base_get_xdg_surface(guest.wm_base, guest.surface);
    xdg_surface_add_listener(guest.role_surface, &kXdgSurface, &guest);
    guest.toplevel = xdg_surface_get_toplevel(guest.role_surface);
    xdg_toplevel_add_listener(guest.toplevel, &kToplevel, &guest);
    xdg_toplevel_set_title(guest.toplevel, "Terminal");
    wl_surface_commit(guest.surface);
    // The configure wait is a read, and a read does not carry the queued
    // requests out by itself: the commit leaves explicitly, or the desk
    // waits for a word the guest never sent and both stand still.
    static_cast<void>(wl_display_flush(display));
    while (!guest.configured && wl_display_dispatch(display) >= 0) {
    }

    // The loop: the desk's words and the program's bytes are two fds, the
    // poll sleeps until either speaks, and a frame rides only on change.
    bool dirty = true; // the empty screen owes its first frame too
    while (!guest.close_requested) {
        std::array<struct pollfd, 2> waiters{};
        waiters[0].fd = wl_display_get_fd(display);
        waiters[0].events = POLLIN;
        waiters[1].fd = session->master_fd();
        waiters[1].events = POLLIN;
        static_cast<void>(::poll(waiters.data(), static_cast<nfds_t>(waiters.size()), 100));
        if ((waiters[0].revents & POLLIN) != 0 && wl_display_dispatch(display) < 0) {
            break; // the desk is gone; there is nothing left to render to
        }
        // The master's error and hangup words count as speech too: a pty
        // whose child left polls POLLERR without POLLIN, and the read that
        // flips child_gone() lives in the pump. Without this the loop
        // spins on an always-ready fd that never says POLLIN again.
        if ((waiters[1].revents & (POLLIN | POLLERR | POLLHUP)) != 0 && source.pump(0)) {
            dirty = true;
        }
        static_cast<void>(wl_display_flush(display));
        if (session->child_gone()) {
            // The program behind the screen has left: the farewell line
            // (fed by the pump above or this one) gets its frame, the flush
            // carries it, and the window follows the program out.
            static_cast<void>(source.pump(0));
            static_cast<void>(paint(guest, screen, term));
            static_cast<void>(wl_display_flush(display));
            break;
        }
        if (dirty && paint(guest, screen, term)) {
            dirty = false;
        }
    }
    wl_display_disconnect(display);
    // The pty's destructor hangs the line up (SIGHUP to whatever remains of
    // the program) and reaps it, so no child outlives its window either.
    return 0;
}
// NOLINTEND(misc-include-cleaner)
