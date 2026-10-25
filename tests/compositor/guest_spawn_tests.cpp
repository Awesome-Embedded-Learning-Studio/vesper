#include "cases.hpp"
#include "compositor/guest_spawn.hpp"
#include "compositor/server.hpp"
#include "testing.hpp"

#include "visor/bindings/host.hpp"
#include "visor/bindings/posix/spawn.hpp"

#include <unistd.h>

#include <array>
#include <cstdint>

namespace {

using vesper::compositor::GuestSpawn;
using vesper::compositor::Server;

/// One settle round: serve the guests, beat the frame clock, flush, breathe.
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

void run_refusal_tests() {
    // A program that is not there is refused before anything is born.
    Server server;
    VESPER_CHECK(server.valid());
    visor::host::HostTable birth{};
    birth.spawn = &visor::host::spawn_via_posix;
    GuestSpawn guest;
    const std::array<const char*, 1> missing{"/vesper-no-such-program"};
    VESPER_CHECK(!guest.launch(server, birth, "/vesper-no-such-program", missing));
    VESPER_CHECK(guest.client() == nullptr);
    VESPER_CHECK_EQ(server.client_count(), 0);

    // A table without the fourth slot wired cannot birth either: the
    // fake-host stance, refused up front.
    const visor::host::HostTable bare{};
    const std::array<const char*, 1> plain{"/bin/true"};
    VESPER_CHECK(!guest.launch(server, bare, "/bin/true", plain));
}

void run_true_child_tests() {
    // /bin/true connects nothing and speaks nothing, but it is birthed,
    // adopted and reaped all the same: the connection drops when the child
    // leaves, and alive() is the reap itself, so no zombie stays behind.
    Server server;
    VESPER_CHECK(server.valid());
    visor::host::HostTable birth{};
    birth.spawn = &visor::host::spawn_via_posix;
    GuestSpawn guest;
    const std::array<const char*, 1> argv{"/bin/true"};
    VESPER_CHECK(guest.launch(server, birth, "/bin/true", argv));
    VESPER_CHECK(guest.client() != nullptr);
    VESPER_CHECK_EQ(server.client_count(), 1);

    bool gone = false;
    for (int round = 0; round < 200 && !gone; ++round) { // one second of budget
        static_cast<void>(settle(server, round));
        gone = !guest.alive() && guest.client() == nullptr && server.client_count() == 0;
    }
    VESPER_CHECK(gone);
    VESPER_CHECK(!guest.alive());

    // The guest that left may be birthed again: one live connection at a
    // time, a second life on the same object.
    VESPER_CHECK(guest.launch(server, birth, "/bin/true", argv));
    VESPER_CHECK(guest.client() != nullptr);
}

} // namespace

void run_compositor_guest_spawn_tests() {
    run_refusal_tests();
    run_true_child_tests();
}
