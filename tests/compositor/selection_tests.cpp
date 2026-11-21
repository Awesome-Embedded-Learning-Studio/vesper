#include "cases.hpp"
#include "compositor/lane.hpp"
#include "compositor/seat.hpp"
#include "compositor/server.hpp"
#include "compositor/shell.hpp"
#include "compositor/sparring_client.hpp"
#include "testing.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstring>

namespace {

using vesper::compositor::Server;
using vesper::compositor::ShellSurface;
using vesper::testing::Lane;
using vesper::testing::SparringClient;

/// Arms one guest the way every selection judgment needs it: a mapped
/// toplevel under the given title (a face the keyboard focus can land on),
/// the seat's hands, and the data manager. False when any link refused.
bool arm_guest(Server& server, SparringClient& guest, const char* title) {
    return guest.survey_globals(server) && guest.bind_shm() && guest.bind_compositor() &&
           guest.bind_wm_base() && guest.bind_seat(server) && guest.make_pool(64UZ * 32U * 4U) &&
           guest.settle(server) && guest.make_surface() != nullptr &&
           guest.make_toplevel(server, title) && guest.bind_data_manager() &&
           guest.make_data_device(server) && guest.settle(server);
}

/// The desk's window for a guest's title, the way the desk itself finds a
/// face: by what the guest called it.
ShellSurface* shell_of(Server& server, const char* title) {
    for (int i = 0; i < server.shell_surface_count(); ++i) {
        ShellSurface* shell = server.shell_surface(i);
        if (std::strcmp(shell->title(), title) == 0) {
            return shell;
        }
    }
    return nullptr;
}

/// One lane, two guests: an owner holding a source and a paster holding a
/// device, both with faces on the desk. The judgment drives them by hand.
class SelectionLane {
  public:
    SelectionLane() {
        if (!lane.up) {
            return;
        }
        std::array<int, 2> fds{};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) != 0) {
            return;
        }
        if (lane.server.adopt_client(fds[1]) == nullptr) {
            ::close(fds[0]);
            return;
        }
        if (!paster.connect_fd(fds[0])) {
            return;
        }
        ok = arm_guest(lane.server, lane.client, "Owner") &&
             arm_guest(lane.server, paster, "Paster");
    }

    bool ok = false;
    Lane lane;
    SparringClient paster;
};

void run_round_trip_tests() {
    SelectionLane selection_lane;
    VESPER_CHECK(selection_lane.ok);
    if (!selection_lane.ok) {
        return;
    }
    Lane& lane = selection_lane.lane;
    SparringClient& owner = lane.client;
    SparringClient& paster = selection_lane.paster;

    // The copy side: a source advertising two flavors of text, set as the
    // selection from a device whose guest holds no focus at all. The word
    // rides to the server first -- the desk carries whatever has arrived
    // when the keyboard crosses, not what a guest is still holding back.
    VESPER_CHECK(owner.make_data_source() != nullptr);
    owner.offer_mime("text/plain;charset=utf-8");
    owner.offer_mime("UTF8_STRING");
    owner.set_selection(7, true);
    VESPER_CHECK(owner.settle(lane.server));

    // The paste side hears it the moment the keyboard crosses to it: one
    // offer carrying both mimes, then the selection word naming the offer.
    ShellSurface* paster_shell = shell_of(lane.server, "Paster");
    VESPER_CHECK(paster_shell != nullptr);
    if (paster_shell == nullptr || paster_shell->surface() == nullptr) {
        return;
    }
    lane.server.seat().keyboard_enter(paster_shell->surface());
    VESPER_CHECK(paster.settle(lane.server));
    VESPER_CHECK_EQ(paster.selection_count(), 1);
    VESPER_CHECK(paster.selection_held());
    VESPER_CHECK_EQ(paster.offer_mime_count(), 2UZ);
    if (paster.offer_mime_count() == 2) {
        VESPER_CHECK(std::strcmp(paster.offer_mime_at(0), "text/plain;charset=utf-8") == 0);
        VESPER_CHECK(std::strcmp(paster.offer_mime_at(1), "UTF8_STRING") == 0);
    }

    // The pipe: the paster asks for one flavor, the desk forwards the
    // question, the owner pours, the paster drinks from the other half.
    // The ask rides to the server on the paster's own flush, and the
    // answer rides back on the owner's.
    std::array<int, 2> pipe_ends{};
    VESPER_CHECK_EQ(::pipe(pipe_ends.data()), 0);
    paster.receive_selection("UTF8_STRING", pipe_ends[1]);
    VESPER_CHECK(paster.settle(lane.server) && owner.settle(lane.server));
    // The kernel carried its own copy over the wire; the paster's write end
    // has said everything it has to say. Held open, it would keep the pipe
    // singing EOF-less forever no matter how the owner pours.
    ::close(pipe_ends[1]);
    VESPER_CHECK_EQ(owner.source_send_count(), 1);
    if (owner.source_send_count() > 0) {
        VESPER_CHECK(std::strcmp(owner.source_send_mime(), "UTF8_STRING") == 0);
    }
    const int pour_fd = owner.take_source_send_fd();
    VESPER_CHECK(pour_fd >= 0);
    if (pour_fd >= 0) {
        VESPER_CHECK_EQ(::write(pour_fd, "vesper", 6), 6);
        ::close(pour_fd);
    }
    std::array<char, 16> poured{};
    std::size_t poured_count = 0;
    for (;;) {
        // A bounded wait: a clipboard whose writer never closes would hang
        // the whole suite, and a two-second quiet pipe is the failed
        // round trip saying so, not a judgment worth waiting on.
        struct pollfd waiter{
            .fd = pipe_ends[0],
            .events = POLLIN,
            .revents = 0,
        };
        const int ready = ::poll(&waiter, 1, 2000);
        if (ready <= 0) {
            break; // quiet or interrupted: what arrived is what poured
        }
        const auto got =
            ::read(pipe_ends[0], poured.data() + poured_count, poured.size() - poured_count);
        if (got <= 0) {
            break; // EOF: every writer closed, the round trip is home
        }
        poured_count += static_cast<std::size_t>(got);
    }
    ::close(pipe_ends[0]);
    VESPER_CHECK_EQ(poured_count, 6UZ);
    VESPER_CHECK(std::memcmp(poured.data(), "vesper", 6) == 0);
    VESPER_CHECK(owner.alive());
    VESPER_CHECK(paster.alive());
}

void run_handover_tests() {
    SelectionLane selection_lane;
    VESPER_CHECK(selection_lane.ok);
    if (!selection_lane.ok) {
        return;
    }
    Lane& lane = selection_lane.lane;
    SparringClient& owner = lane.client;
    SparringClient& paster = selection_lane.paster;
    ShellSurface* paster_shell = shell_of(lane.server, "Paster");
    VESPER_CHECK(paster_shell != nullptr);
    if (paster_shell == nullptr || paster_shell->surface() == nullptr) {
        return;
    }
    lane.server.seat().keyboard_enter(paster_shell->surface());

    // A selection stands; its owner clears it by word. The owner hears
    // cancelled, the paster hears the empty selection, nobody dies. The
    // paster's first word was the empty selection the focus crossing
    // itself carried, so the counts start there.
    VESPER_CHECK(owner.make_data_source() != nullptr);
    owner.offer_mime("text/plain");
    owner.set_selection(7, true);
    VESPER_CHECK(owner.settle(lane.server) && paster.settle(lane.server));
    VESPER_CHECK(paster.selection_held());
    owner.set_selection(8, false);
    VESPER_CHECK(owner.settle(lane.server));
    VESPER_CHECK_EQ(owner.source_cancelled_count(), 1);
    owner.destroy_data_source(); // the polite owner's answer to cancelled
    VESPER_CHECK(owner.settle(lane.server));
    VESPER_CHECK(paster.settle(lane.server));
    VESPER_CHECK_EQ(paster.selection_count(), 3);
    VESPER_CHECK(!paster.selection_held());

    // A source dropped while it is the selection empties the clipboard the
    // same way, through the goodbye instead of the word.
    VESPER_CHECK(owner.make_data_source() != nullptr);
    owner.offer_mime("text/plain");
    owner.set_selection(9, true);
    VESPER_CHECK(owner.settle(lane.server) && paster.settle(lane.server));
    VESPER_CHECK(paster.selection_held());
    owner.destroy_data_source();
    VESPER_CHECK(owner.settle(lane.server));
    VESPER_CHECK(paster.settle(lane.server));
    VESPER_CHECK_EQ(paster.selection_count(), 5);
    VESPER_CHECK(!paster.selection_held());
    VESPER_CHECK(owner.alive());
    VESPER_CHECK(paster.alive());
}

void run_focus_tests() {
    SelectionLane selection_lane;
    VESPER_CHECK(selection_lane.ok);
    if (!selection_lane.ok) {
        return;
    }
    Lane& lane = selection_lane.lane;
    SparringClient& owner = lane.client;
    SparringClient& paster = selection_lane.paster;
    ShellSurface* owner_shell = shell_of(lane.server, "Owner");
    ShellSurface* paster_shell = shell_of(lane.server, "Paster");
    VESPER_CHECK(owner_shell != nullptr);
    VESPER_CHECK(paster_shell != nullptr);
    if (owner_shell == nullptr || paster_shell == nullptr || owner_shell->surface() == nullptr ||
        paster_shell->surface() == nullptr) {
        return;
    }

    // A selection set while nobody holds the keyboard waits unheard; the
    // focus crossing is what carries it, and the owner hears its own copy
    // the same as any stranger would.
    VESPER_CHECK(owner.make_data_source() != nullptr);
    owner.offer_mime("text/plain");
    owner.set_selection(7, true);
    VESPER_CHECK(owner.settle(lane.server));
    lane.server.seat().keyboard_enter(owner_shell->surface());
    VESPER_CHECK(owner.settle(lane.server));
    VESPER_CHECK_EQ(owner.selection_count(), 1);
    VESPER_CHECK(owner.selection_held());
    VESPER_CHECK_EQ(owner.offer_mime_count(), 1UZ);

    // Crossing back and forth hands the same selection over again, and
    // crossing to nobody tells nobody anything new.
    lane.server.seat().keyboard_enter(paster_shell->surface());
    VESPER_CHECK(paster.settle(lane.server));
    VESPER_CHECK_EQ(paster.selection_count(), 1);
    VESPER_CHECK(paster.selection_held());
    lane.server.seat().keyboard_leave();
    VESPER_CHECK(owner.settle(lane.server));
    VESPER_CHECK(paster.settle(lane.server));
    VESPER_CHECK_EQ(owner.selection_count(), 1);
    VESPER_CHECK_EQ(paster.selection_count(), 1);
    VESPER_CHECK(owner.alive());
    VESPER_CHECK(paster.alive());
}

} // namespace

void run_compositor_selection_tests() {
    run_round_trip_tests();
    run_handover_tests();
    run_focus_tests();
}
