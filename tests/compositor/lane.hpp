#pragma once

#include "compositor/server.hpp"
#include "compositor/sparring_client.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <array>

namespace vesper::testing {

/// One judgment lane: a server plus one sparring client already talking over
/// a socketpair. Declaration order is the destruction order in reverse, so
/// the client goes first and the display's destroy handlers all run inside a
/// live server.
class Lane {
  public:
    Lane() {
        std::array<int, 2> fds{};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) != 0) {
            return;
        }
        if (server.adopt_client(fds[1]) == nullptr) {
            ::close(fds[0]); // adopt_client already closed its own half
            return;
        }
        if (!client.connect_fd(fds[0])) {
            return; // connect_fd already closed the half it was given
        }
        up = true;
    }

    bool up = false;
    compositor::Server server;
    SparringClient client;
};

} // namespace vesper::testing
