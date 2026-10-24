#include "cases.hpp"
#include "compositor/server.hpp"
#include "compositor/shell.hpp"
#include "compositor/sparring_client.hpp"
#include "testing.hpp"
#include "ui/shell_desk/shell_desk.hpp"

#include "fake_color_host.hpp"
#include "fonts/demo_font.hpp"
#include "ui/desktop/desktop.hpp"
#include "ui/style/theme.hpp"
#include "ui/window_frame/window_frame.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/canvas/canvas.hpp"
#include "visor/canvas/pixel_traits.hpp"
#include "visor/ui/dirty_regions.hpp"
#include "visor/ui/input_router.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/run_loop.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <wayland-client-protocol.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using vesper::compositor::Server;
using vesper::compositor::ShellSurface;
using vesper::desktop::Desktop;
using vesper::desktop::ShellDesk;
using vesper::desktop::WindowFrame;
using vesper::host::FakeColorHost;
using vesper::testing::SparringClient;
using visor::base::PointI;
using visor::base::RectI;
using visor::base::SizeI;
using visor::host::Event;
using visor::host::KeyDown;
using visor::host::PointerDown;
using visor::host::PointerMove;
using visor::host::PointerUp;

constexpr SizeI kScreen{.w = 480, .h = 320};
constexpr RectI kScreenBounds{.pos = PointI{0, 0}, .size = kScreen};
using Frame = visor::canvas::Canvas<kScreen, visor::canvas::Xrgb8888Traits>;

// Two guests' own colors: dusk gold and a deep teal, the words the pixel
// judgments look for through the whole window stack.
constexpr std::uint32_t kAmber = 0xffc9'8a2dU;
constexpr std::uint32_t kTeal = 0xff2d'8a9cU;

/// One assembled desk with its server and window layer, live against its
/// own fake color session.
struct Scene {
    FakeColorHost host{480, 320};
    visor::ui::InputRouter input{};
    visor::ui::DirtyRegions dirty{};
    Frame frame{};
    Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::kDemoFont,
                    vesper::desktop::style::kColorDeskTheme};
    Server server;
    ShellDesk desk{desktop, server, vesper::fonts::kDemoFont};
    std::array<SparringClient*, 4> guests{};
    std::size_t guest_count = 0;

    Scene() { desktop.set_dirty_sink(&dirty); }

    /// Registers a guest whose words the rounds should deliver.
    void watch(SparringClient& guest) {
        if (guest_count < static_cast<std::size_t>(guests.size())) {
            guests[guest_count++] = &guest;
        }
    }

    /// Lets every watched guest read what the desk sent.
    void arrive() {
        for (std::size_t i = 0; i < guest_count; ++i) {
            static_cast<void>(guests[i]->settle(server));
        }
    }

    /// One render pass through the router and the debt.
    void render() {
        static_cast<void>(
            visor::ui::run_dirty(host.table(), desktop, input, frame, kScreen, dirty));
    }

    /// One full round: the server serves its guests, the guests read what
    /// came back (maps and unmaps land here), the focus follows the
    /// pointer's last word, and two render passes clear the debt the
    /// round itself raised.
    void round() {
        desk.pump();
        arrive();
        desk.settle_focus();
        render();
        render();
    }

    void feed(const Event& event) {
        VESPER_CHECK(host.push_event(event));
        render();
        desk.settle_focus();
        arrive();
        render();
    }
};

/// One guest: a sparring client that maps one solid block of its color.
struct Guest {
    SparringClient client;
    int fd = -1;

    [[nodiscard]] bool connect(Server& server, const char* title) {
        std::array<int, 2> fds{};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) != 0) {
            return false;
        }
        if (server.adopt_client(fds[1]) == nullptr) {
            ::close(fds[0]);
            return false;
        }
        fd = fds[0];
        return client.connect_fd(fd) && client.survey_globals(server) && client.bind_shm() &&
               client.bind_compositor() && client.bind_wm_base() && client.bind_seat(server) &&
               client.make_pool(64UZ * 64U * 4U) && client.settle(server) &&
               client.make_surface() != nullptr && client.make_toplevel(server, title);
    }

    /// Commits one w x h block of the color, stride 64 words.
    [[nodiscard]] bool map_block(Server& server, std::uint32_t color, int width, int height) {
        wl_buffer* buffer = client.make_buffer(0, width, height, 64, WL_SHM_FORMAT_XRGB8888);
        if (buffer == nullptr) {
            return false;
        }
        for (int row = 0; row < height; ++row) {
            std::uint32_t* line = client.pool_words() + (static_cast<std::size_t>(row) * 64UZ);
            for (int column = 0; column < width; ++column) {
                line[column] = color;
            }
        }
        client.attach(buffer);
        client.damage(0, 0, width, height);
        client.commit();
        return client.settle(server);
    }
};

/// Where a guest's face sits on the desk.
[[nodiscard]] PointI content_probe(const Scene& scene, const ShellSurface* shell, int dx, int dy) {
    const WindowFrame* found = scene.desk.frame_of(shell);
    const RectI content = found != nullptr ? found->content_area() : RectI{};
    return PointI{content.pos.x + dx, content.pos.y + dy};
}

void run_two_windows_tests() {
    Scene scene;
    Guest amber;
    Guest teal;
    VESPER_CHECK(amber.connect(scene.server, "Amber"));
    scene.watch(amber.client);
    VESPER_CHECK(amber.map_block(scene.server, kAmber, 64, 32));
    scene.round();
    VESPER_CHECK_EQ(scene.desk.window_count(), 1UZ);
    VESPER_CHECK_EQ(scene.host.get(content_probe(scene, scene.server.shell_surface(0), 4, 4)),
                    kAmber);

    VESPER_CHECK(teal.connect(scene.server, "Teal"));
    scene.watch(teal.client);
    VESPER_CHECK(teal.map_block(scene.server, kTeal, 64, 32));
    scene.round();
    VESPER_CHECK_EQ(scene.desk.window_count(), 2UZ);

    // Both faces stand beside the cascade's overlap: each guest's own
    // corner shows its own color.
    const ShellSurface* amber_shell = scene.server.shell_surface(0);
    const ShellSurface* teal_shell = scene.server.shell_surface(1);
    VESPER_CHECK_EQ(scene.host.get(content_probe(scene, amber_shell, 4, 4)), kAmber);
    VESPER_CHECK_EQ(scene.host.get(content_probe(scene, teal_shell, 60, 28)), kTeal);
}

void run_focus_tests() {
    Scene scene;
    Guest amber;
    Guest teal;
    VESPER_CHECK(amber.connect(scene.server, "Amber"));
    scene.watch(amber.client);
    VESPER_CHECK(amber.map_block(scene.server, kAmber, 64, 32));
    VESPER_CHECK(teal.connect(scene.server, "Teal"));
    scene.watch(teal.client);
    VESPER_CHECK(teal.map_block(scene.server, kTeal, 64, 32));
    scene.round();

    // The newest face holds the desk: its guest hears the key, the other
    // one hears nothing.
    scene.feed(Event{KeyDown{.code = 30}});
    VESPER_CHECK_EQ(amber.client.key_count(), 0UZ);
    VESPER_CHECK_EQ(teal.client.key_count(), 1UZ);

    // A press inside the amber face moves the aim: amber hears the key
    // now, teal goes quiet, and the press itself crossed as a pointer
    // word to amber's guest.
    const PointI inside_amber = content_probe(scene, scene.server.shell_surface(0), 8, 8);
    scene.feed(Event{PointerDown{.button = 1, .pos = inside_amber}});
    scene.feed(Event{PointerUp{.button = 1, .pos = inside_amber}});
    scene.feed(Event{KeyDown{.code = 30}});
    VESPER_CHECK_EQ(amber.client.key_count(), 1UZ);
    VESPER_CHECK_EQ(teal.client.key_count(), 1UZ);
    VESPER_CHECK_EQ(amber.client.button_count(), 2UZ);

    // A text code crosses the canvas as the wire's evdev number: the desk
    // folds at its own gate, the guest reads its xkb table with the key
    // number the fold names.
    scene.feed(Event{KeyDown{.code = static_cast<std::uint32_t>('a')}});
    const auto* folded = amber.client.key_at(amber.client.key_count() - 1);
    VESPER_CHECK(folded != nullptr);
    if (folded != nullptr) {
        VESPER_CHECK_EQ(folded->code, 30U); // evdev KEY_A
    }

    // The text world's space (0x20 -- the fold retired the evdev 57, which
    // doubles as '9') crosses back as the wire's 57.
    scene.feed(Event{KeyDown{.code = 0x20}});
    const auto* space = amber.client.key_at(amber.client.key_count() - 1);
    VESPER_CHECK(space != nullptr);
    if (space != nullptr) {
        VESPER_CHECK_EQ(space->code, 57U); // evdev KEY_SPACE
    }
}

void run_drag_tests() {
    Scene scene;
    Guest amber;
    VESPER_CHECK(amber.connect(scene.server, "Amber"));
    scene.watch(amber.client);
    VESPER_CHECK(amber.map_block(scene.server, kAmber, 64, 32));
    scene.round();
    const ShellSurface* amber_shell = scene.server.shell_surface(0);
    const WindowFrame* frame = scene.desk.frame_of(amber_shell);
    VESPER_CHECK(frame != nullptr);
    if (frame == nullptr) {
        return;
    }
    const RectI band = frame->title_bar();
    const PointI grab{band.pos.x + 20, band.pos.y + 6};
    const PointI old_seat = frame->bounds().pos;

    // Press on the band, walk 50 pixels right and 20 down, release: the
    // face follows, the old ground shows through.
    const PointI old_probe = content_probe(scene, amber_shell, 4, 4);
    scene.feed(Event{PointerDown{.button = 1, .pos = grab}});
    scene.feed(Event{PointerMove{.pos = PointI{grab.x + 25, grab.y + 6}}});
    scene.feed(Event{PointerMove{.pos = PointI{grab.x + 50, grab.y + 20}}});
    scene.feed(Event{PointerUp{.button = 1, .pos = PointI{grab.x + 50, grab.y + 20}}});
    scene.round();

    const WindowFrame* moved = scene.desk.frame_of(amber_shell);
    VESPER_CHECK(moved != nullptr);
    if (moved == nullptr) {
        return;
    }
    VESPER_CHECK_EQ(moved->bounds().pos.x, old_seat.x + 50);
    VESPER_CHECK_EQ(moved->bounds().pos.y, old_seat.y + 20);
    VESPER_CHECK(scene.host.get(old_probe) != kAmber);
    VESPER_CHECK_EQ(scene.host.get(content_probe(scene, amber_shell, 54, 28)), kAmber);
}

void run_pointer_grab_tests() {
    Scene scene;
    Guest amber;
    VESPER_CHECK(amber.connect(scene.server, "Amber"));
    scene.watch(amber.client);
    VESPER_CHECK(amber.map_block(scene.server, kAmber, 64, 32));
    scene.round();

    // The guest asks to draw its own cursor, the word weston-terminal
    // sends the moment it wants an I-beam. The desk must receive it and
    // live on: a null slot in the pointer's table aborts the compositor.
    amber.client.ask_own_cursor();
    VESPER_CHECK(amber.client.settle(scene.server));
    scene.round();

    // The implicit grab: a press inside the face keeps hearing the moves
    // after the pointer leaves the window whole, surface-local words and
    // all, until the release lands.
    const PointI inside = content_probe(scene, scene.server.shell_surface(0), 10, 10);
    scene.feed(Event{PointerDown{.button = 1, .pos = inside}});
    scene.feed(Event{PointerMove{.pos = PointI{inside.x - 100, inside.y - 100}}});
    scene.feed(Event{PointerUp{.button = 1, .pos = PointI{inside.x - 100, inside.y - 100}}});

    VESPER_CHECK_EQ(amber.client.button_count(), 2UZ);
    VESPER_CHECK(amber.client.motion_count() >= 2UZ);
    const auto* grabbed = amber.client.motion_at(amber.client.motion_count() - 1);
    VESPER_CHECK(grabbed != nullptr);
    if (grabbed != nullptr) {
        // 100 pixels up-left of the press reads as (-90, -90) in the
        // surface's own words: outside the face, still the guest's to hear.
        VESPER_CHECK_EQ(grabbed->x, -90);
        VESPER_CHECK_EQ(grabbed->y, -90);
    }
}

void run_close_tests() {
    Scene scene;
    Guest amber;
    Guest teal;
    VESPER_CHECK(amber.connect(scene.server, "Amber"));
    scene.watch(amber.client);
    VESPER_CHECK(amber.map_block(scene.server, kAmber, 64, 32));
    VESPER_CHECK(teal.connect(scene.server, "Teal"));
    scene.watch(teal.client);
    VESPER_CHECK(teal.map_block(scene.server, kTeal, 64, 32));
    scene.round();

    // The close box asks the amber guest first; the guest's own goodbye
    // takes the window down and hands the keyboard to whoever stands on
    // top.
    const WindowFrame* amber_frame = scene.desk.frame_of(scene.server.shell_surface(0));
    VESPER_CHECK(amber_frame != nullptr);
    if (amber_frame == nullptr) {
        return;
    }
    const RectI close_box = amber_frame->close_box();
    scene.feed(
        Event{PointerDown{.button = 1, .pos = PointI{close_box.pos.x + 2, close_box.pos.y + 2}}});
    scene.feed(
        Event{PointerUp{.button = 1, .pos = PointI{close_box.pos.x + 2, close_box.pos.y + 2}}});

    // The word crossed; the guest takes its own window down. The teal
    // shell is named before the close, because striking the first shell
    // moves the second one's seat in the book.
    VESPER_CHECK(amber.client.close_requested());
    const ShellSurface* teal_shell = scene.server.shell_surface(1);
    amber.client.retire_toplevel();
    VESPER_CHECK(amber.client.settle(scene.server));
    scene.round();

    VESPER_CHECK_EQ(scene.desk.window_count(), 1UZ);
    VESPER_CHECK_EQ(scene.host.get(content_probe(scene, teal_shell, 60, 28)), kTeal);
    // The heir hears the keys now.
    VESPER_CHECK(scene.server.seat().keyboard_focus() == teal_shell->surface());
    scene.feed(Event{KeyDown{.code = 30}});
    VESPER_CHECK_EQ(teal.client.key_count(), 1UZ);
}

void run_stacking_tests() {
    Scene scene;
    Guest amber;
    Guest teal;
    VESPER_CHECK(amber.connect(scene.server, "Amber"));
    scene.watch(amber.client);
    VESPER_CHECK(amber.map_block(scene.server, kAmber, 64, 32));
    VESPER_CHECK(teal.connect(scene.server, "Teal"));
    scene.watch(teal.client);
    VESPER_CHECK(teal.map_block(scene.server, kTeal, 64, 32));
    scene.round();

    // The cascade seats the second window 24 pixels over the first: the
    // faces' shared strip belongs to the later face until the earlier one
    // is raised. (2, 2) into the second face also sits inside the first.
    const PointI overlap = content_probe(scene, scene.server.shell_surface(1), 2, 2);
    VESPER_CHECK_EQ(scene.host.get(overlap), kTeal);

    const WindowFrame* amber_frame = scene.desk.frame_of(scene.server.shell_surface(0));
    VESPER_CHECK(amber_frame != nullptr);
    if (amber_frame == nullptr) {
        return;
    }
    const RectI band = amber_frame->title_bar();
    const PointI on_band{band.pos.x + 30, band.pos.y + 6};
    scene.feed(Event{PointerDown{.button = 1, .pos = on_band}});
    scene.feed(Event{PointerUp{.button = 1, .pos = on_band}});
    scene.round();

    VESPER_CHECK_EQ(scene.host.get(overlap), kAmber);
}

/// A plain focusable widget: a local window's stand-in for the shared-tree
/// judgments, the way an icon's terminal holds the keyboard.
struct Probe final : visor::ui::Widget {
    using visor::ui::Widget::Widget;

    [[nodiscard]] bool focusable() const noexcept override { return true; }

  protected:
    void on_draw(const visor::ui::Painter& /*painter*/) const noexcept override {}
};

void run_shared_tree_focus_tests() {
    Scene scene;
    Guest amber;
    VESPER_CHECK(amber.connect(scene.server, "Amber"));
    scene.watch(amber.client);
    VESPER_CHECK(amber.map_block(scene.server, kAmber, 64, 32));
    scene.round();
    VESPER_CHECK_EQ(scene.server.seat().keyboard_focus(), scene.server.shell_surface(0)->surface());

    // A local window takes the keyboard: the tree's focus crosses to it,
    // and the desk's next round steps the guests aside whole -- the band
    // goes dark, the seat's keyboard word is sent -- instead of stealing
    // the tree back.
    Probe probe{RectI{.pos = PointI{320, 200}, .size = SizeI{.w = 64, .h = 48}}};
    VESPER_CHECK(scene.desktop.add_child(&probe));
    // The seat change is explicit, the same line the local window's opener
    // runs: dismiss the old holder, then seat the new one.
    if (visor::ui::Widget* held = scene.desktop.focused_widget(); held != nullptr) {
        held->set_focused(false);
    }
    probe.set_focused(true);
    scene.round();
    VESPER_CHECK_EQ(scene.desktop.focused_widget(),
                    static_cast<visor::ui::Widget*>(&probe)); // the tree stays with the local one
    VESPER_CHECK(scene.server.seat().keyboard_focus() ==
                 nullptr); // the guest heard the keyboard leave
    VESPER_CHECK(!scene.desk.frame_of(scene.server.shell_surface(0))->active());

    // A press back on the guest's face re-aims the whole desk: the tree's
    // keyboard leaves the local window, the guest's band lights again.
    const PointI inside_amber = content_probe(scene, scene.server.shell_surface(0), 8, 8);
    scene.feed(Event{PointerDown{.button = 1, .pos = inside_amber}});
    scene.feed(Event{PointerUp{.button = 1, .pos = inside_amber}});
    scene.round();
    VESPER_CHECK(scene.desktop.focused_widget() != static_cast<visor::ui::Widget*>(&probe));
    VESPER_CHECK_EQ(scene.server.seat().keyboard_focus(), scene.server.shell_surface(0)->surface());
    VESPER_CHECK(scene.desk.frame_of(scene.server.shell_surface(0))->active());
}

} // namespace

void run_compositor_desk_tests() {
    run_two_windows_tests();
    run_focus_tests();
    run_drag_tests();
    run_pointer_grab_tests();
    run_close_tests();
    run_stacking_tests();
    run_shared_tree_focus_tests();
}
