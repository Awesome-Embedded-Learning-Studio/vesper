#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "session/wire.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace vesper::desktop {

/**
 * @brief One windowed child program's face on the desk (D6a's first draft).
 *
 * The widget owns the desk's end of the private channel: pump() reads
 * whatever arrived and decodes it into the offscreen image the child's
 * frames fill, on_draw pastes that image through the painter's image verb,
 * and every input callback the router delivers rides back down the socket
 * as one flat event word. The child's process lifecycle is not this
 * widget's business -- the caller spawned it, the caller reaps it; what
 * the widget reports is the channel's own end: a Bye, a hangup or a
 * broken stream all become child_gone() and one on_exit call.
 *
 * The bounds are the channel's frame size, a construction-time agreement
 * the first draft bakes into both programs; the Hello word's size fields
 * stay on the wire for the day the desk lays windows out from them. Input
 * positions are folded into the child's own coordinates before they leave,
 * and the pointer button rides as the primary button: the router's
 * callbacks carry no button of their own, and the capture semantics that
 * would give one is the desk's later layer, not the child's.
 */
class RemoteCanvas final : public visor::ui::Widget {
  public:
    /// What the desk calls when the channel ends (Bye, hangup or a broken
    /// stream): the window's owner closes the frame and reaps the child.
    using ExitHandler = void (*)(RemoteCanvas& self, void* context);

    /// The socket is the desk's end of the channel and becomes this
    /// widget's own: the destructor closes it.
    RemoteCanvas(visor::base::RectI bounds, int socket_fd) noexcept;
    ~RemoteCanvas() override;

    RemoteCanvas(const RemoteCanvas&) = delete;
    RemoteCanvas& operator=(const RemoteCanvas&) = delete;

    /// Reads what arrived without waiting and decodes it. A completed frame
    /// invalidates the widget; the channel's end runs the exit handler once.
    void pump() noexcept;

    /// Sends the Close word: the desk telling the child to leave.
    void send_close() const noexcept;

    /// Whether the channel has ended (by Bye, hangup or breakage).
    [[nodiscard]] bool child_gone() const noexcept;

    void set_on_exit(ExitHandler handler, void* context = nullptr) noexcept;

    // The input overrides stay public the way the base declares them: the
    // router calls them through the widget interface, and hiding them would
    // only promise an encapsulation the tree never asked for.
    void on_key_down(std::uint32_t code, visor::host::Modifiers modifiers) noexcept override;
    void on_key_up(std::uint32_t code, visor::host::Modifiers modifiers) noexcept override;
    void on_pointer_down(visor::base::PointI at,
                         visor::host::Modifiers modifiers) noexcept override;
    void on_pointer_up(visor::base::PointI at) noexcept override;
    void on_pointer_move(visor::base::PointI at) noexcept override;
    void on_pointer_wheel(const visor::host::PointerWheel& wheel) noexcept override;
    [[nodiscard]] bool focusable() const noexcept override;

  protected:
    void on_draw(const visor::ui::Painter& painter) const noexcept override;

  private:
    /// One event word down the wire; a dead channel drops it quietly (the
    /// exit handler already spoke).
    void forward(visor::host::Event event) noexcept;

    /// The position folded into the child's own coordinates.
    [[nodiscard]] visor::base::PointI local(visor::base::PointI at) const noexcept;

    int socket_ = -1;
    std::vector<std::uint8_t> decode_buffer_; // the decoder's sink
    std::vector<std::uint32_t> image_;        // w*h words, the child's frame
    std::size_t frame_words_ = 0;
    vesper::session::WireDecoder decoder_;
    ExitHandler on_exit_ = nullptr;
    void* exit_context_ = nullptr;
    bool gone_ = false;
};
} // namespace vesper::desktop
