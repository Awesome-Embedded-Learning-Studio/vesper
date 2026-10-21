#pragma once

#include <string_view>

#include "host/pty.hpp"
#include "ui/terminal/terminal.hpp"

namespace vesper::desktop {

/**
 * @brief The D4 source: a real program behind the screen, through a pty.
 *
 * send() is the keyboard, one write on the pty master. pump() is the
 * round's harvest: wait for the master to carry data, read what arrived,
 * hand it to the terminal. The pty's own echo is the typing feedback and
 * the program's bytes are the content, the same division D3's loopback
 * faked in one object; here the kernel's line discipline does the echoing
 * and bash does the content.
 *
 * The wait in pump() is the event loop's sleep: a desk with nothing to do
 * sleeps the full timeout, and output from the program wakes it early.
 */
class PtySource final : public TerminalSource {
  public:
    PtySource(Terminal& term, vesper::host::PtySession& session) noexcept;

    void send(std::string_view bytes) noexcept override;

    /// Waits up to timeout_ms for the master to carry data, then drains
    /// what arrived in bounded chunks, feeding each to the terminal.
    /// Returns true when any bytes landed. A child that reports itself gone
    /// gets one farewell line on the screen, once.
    bool pump(int timeout_ms) noexcept;

    /// Whether the program behind the screen has exited.
    [[nodiscard]] bool child_gone() const noexcept;

  private:
    Terminal* term_;
    vesper::host::PtySession* session_;
    bool farewell_shown_ = false;
};

} // namespace vesper::desktop
