#include "ui/remote_canvas/remote_canvas.hpp"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>

#include "session/wire.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"
#include "visor/ui/painter.hpp"
#include "visor/ui/widgets/widget.hpp"

namespace vesper::desktop {

RemoteCanvas::RemoteCanvas(visor::base::RectI bounds, int socket_fd) noexcept
    : Widget{bounds}, socket_{socket_fd}, decoder_{
                                              nullptr,
                                              0,
                                          } {
    frame_words_ =
        static_cast<std::size_t>(bounds.size.w) * static_cast<std::size_t>(bounds.size.h);
    image_.assign(frame_words_, 0xff00'0000U);
    decode_buffer_.assign(frame_words_ * 4U, 0);
    decoder_ = vesper::session::WireDecoder{decode_buffer_.data(), decode_buffer_.size()};
}

RemoteCanvas::~RemoteCanvas() {
    if (socket_ >= 0) {
        ::close(socket_);
    }
}

void RemoteCanvas::set_on_exit(ExitHandler handler, void* context) noexcept {
    on_exit_ = handler;
    exit_context_ = context;
}

bool RemoteCanvas::child_gone() const noexcept {
    return gone_;
}

void RemoteCanvas::pump() noexcept {
    if (gone_) {
        return;
    }
    // The channel's own end, taken once: closing the socket at the first
    // sign of death hands the child its EOF, so a hung-up child and a
    // broken stream both leave the child free to exit instead of blocking
    // on a full pipe nobody drains. The exit handler runs after, and no
    // handler may wait on the child -- reaping is the loop's job, not the
    // callback's.
    const auto end_channel = [this] noexcept {
        gone_ = true;
        if (socket_ >= 0) {
            ::close(socket_);
            socket_ = -1;
        }
    };
    // One read buffer, drained in a loop: whatever the child sent paints
    // before the desk's next frame, and a short buffer simply means the
    // next pump finishes the message.
    std::array<std::uint8_t, 8192> chunk{};
    for (;;) {
        const ssize_t got = ::read(socket_, chunk.data(), chunk.size());
        if (got > 0) {
            const auto arrived = static_cast<std::size_t>(got);
            std::size_t fed = 0;
            while (fed < arrived) {
                const std::size_t used = decoder_.feed(chunk.data() + fed, arrived - fed);
                if (decoder_.broken()) {
                    end_channel();
                    break;
                }
                if (used == 0) {
                    break; // a message waits to be taken
                }
                fed += used;
                if (decoder_.message_ready()) {
                    const auto word = decoder_.word();
                    if (word == vesper::session::Word::Frame &&
                        decoder_.payload_length() == (frame_words_ * 4U)) {
                        // The frame is already in place: the sink is the
                        // image's own bytes.
                        std::memcpy(image_.data(), decode_buffer_.data(), decode_buffer_.size());
                        invalidate(bounds());
                    } else if (word == vesper::session::Word::Bye) {
                        end_channel();
                    }
                    // Hello is the construction-time agreement, already
                    // kept; anything else the desk has no business with.
                    decoder_.take();
                }
            }
            if (gone_) {
                break;
            }
            continue;
        }
        if (got == 0) {
            end_channel(); // the child's last word was silence
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            break; // nothing right now, the channel is fine
        }
        end_channel(); // a real error reads as the channel's end
        break;
    }
    if (gone_ && on_exit_ != nullptr) {
        on_exit_(*this, exit_context_);
    }
}

void RemoteCanvas::send_close() const noexcept {
    if (gone_ || socket_ < 0) {
        return;
    }
    std::array<std::uint8_t, sizeof(vesper::session::Header)> line{};
    if (vesper::session::write_message(line.data(), line.size(), vesper::session::Word::Close,
                                       nullptr, 0)) {
        static_cast<void>(::write(socket_, line.data(), line.size()));
    }
}

visor::base::PointI RemoteCanvas::local(visor::base::PointI at) const noexcept {
    return visor::base::PointI{at.x - bounds().pos.x, at.y - bounds().pos.y};
}

void RemoteCanvas::forward(visor::host::Event event) noexcept {
    if (gone_ || socket_ < 0) {
        return;
    }
    const vesper::session::EventBody body = vesper::session::encode_event(event);
    std::array<std::uint8_t, sizeof(vesper::session::Header) + sizeof(body)> line{};
    if (!vesper::session::write_message(line.data(), line.size(), vesper::session::Word::Event,
                                        &body, sizeof(body))) {
        return;
    }
    const ssize_t wrote = ::write(socket_, line.data(), line.size());
    if (wrote < 0 && errno != EINTR) {
        gone_ = true; // a pipe that will not take a word is a dead channel
    }
}

void RemoteCanvas::on_draw(const visor::ui::Painter& painter) const noexcept {
    painter.draw_image(bounds().pos, image_.data(), bounds().size.w, bounds().size.h,
                       static_cast<std::size_t>(bounds().size.w));
}

void RemoteCanvas::on_key_down(std::uint32_t code, visor::host::Modifiers modifiers) noexcept {
    forward(visor::host::Event{visor::host::KeyDown{.code = code, .modifiers = modifiers}});
}

void RemoteCanvas::on_key_up(std::uint32_t code, visor::host::Modifiers modifiers) noexcept {
    forward(visor::host::Event{visor::host::KeyUp{.code = code, .modifiers = modifiers}});
}

void RemoteCanvas::on_pointer_down(visor::base::PointI at,
                                   visor::host::Modifiers modifiers) noexcept {
    // Button one, the primary: the router's callbacks carry no button of
    // their own, and the wire's button lane waits for the desk's capture
    // layer to fill it.
    forward(visor::host::Event{
        visor::host::PointerDown{.button = 1, .pos = local(at), .modifiers = modifiers}});
}

void RemoteCanvas::on_pointer_up(visor::base::PointI at) noexcept {
    forward(visor::host::Event{visor::host::PointerUp{.button = 1, .pos = local(at)}});
}

void RemoteCanvas::on_pointer_move(visor::base::PointI at) noexcept {
    forward(visor::host::Event{visor::host::PointerMove{.pos = local(at)}});
}

void RemoteCanvas::on_pointer_wheel(const visor::host::PointerWheel& wheel) noexcept {
    forward(visor::host::Event{wheel});
}

bool RemoteCanvas::focusable() const noexcept {
    return true;
}

} // namespace vesper::desktop
