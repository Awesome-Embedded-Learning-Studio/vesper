#include "ui/wayland_canvas/wayland_canvas.hpp"

#include "compositor/seat.hpp"
#include "compositor/shell.hpp"
#include "compositor/surface.hpp"
#include "host/keymap.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"

#include "visor/ui/painter.hpp"
#include "visor/ui/widgets/widget.hpp"

#include <algorithm>
#include <cstdint>

namespace vesper::desktop {
namespace {

/// The desk speaks the text convention -- every host folds evdev inside its
/// own gate, and the desk's consumers eat code points -- while the Wayland
/// wire speaks evdev. A typable code goes back to its key number before the
/// seat carries it; the core's named keys and the arrows ride values the
/// inverse table does not type, and zero means pass-through.
std::uint32_t wire_code(std::uint32_t code) noexcept {
    const std::uint32_t wire = vesper::host::evdev_code_for(code);
    return wire != 0 ? wire : code;
}

} // namespace

WaylandCanvas::WaylandCanvas(visor::base::RectI bounds, compositor::ShellSurface* shell,
                             compositor::Seat* seat) noexcept
    : Widget{bounds}, shell_{shell}, seat_{seat} {}

void WaylandCanvas::note_commit() noexcept {
    invalidate(bounds());
}

void WaylandCanvas::note_damage(visor::base::RectI surface_damage) noexcept {
    if (surface_damage.is_empty()) {
        invalidate(bounds()); // the guest's word for "the whole face"
        return;
    }
    // The report is surface-local; the debt is canvas-shaped. The overlap
    // of the two is everything the desk owes, clipped where the guest
    // over-reports.
    const visor::base::RectI moved{
        .pos = visor::base::PointI{bounds().pos.x + surface_damage.pos.x,
                                   bounds().pos.y + surface_damage.pos.y},
        .size = surface_damage.size,
    };
    const int left = moved.pos.x > bounds().pos.x ? moved.pos.x : bounds().pos.x;
    const int top = moved.pos.y > bounds().pos.y ? moved.pos.y : bounds().pos.y;
    const int moved_right = moved.pos.x + moved.size.w;
    const int moved_bottom = moved.pos.y + moved.size.h;
    const int own_right = bounds().pos.x + bounds().size.w;
    const int own_bottom = bounds().pos.y + bounds().size.h;
    const int right = moved_right < own_right ? moved_right : own_right;
    const int bottom = moved_bottom < own_bottom ? moved_bottom : own_bottom;
    const visor::base::RectI owed{
        .pos = visor::base::PointI{left, top},
        .size = visor::base::SizeI{.w = right - left, .h = bottom - top},
    };
    if (!owed.is_empty()) {
        invalidate(owed);
    }
}

visor::base::PointI WaylandCanvas::local(visor::base::PointI at) const noexcept {
    return visor::base::PointI{at.x - bounds().pos.x, at.y - bounds().pos.y};
}

void WaylandCanvas::on_key_down(std::uint32_t code, visor::host::Modifiers modifiers) noexcept {
    if (seat_ != nullptr) {
        seat_->set_modifiers(modifiers);
        seat_->key(wire_code(code), true);
    }
}

void WaylandCanvas::on_key_up(std::uint32_t code, visor::host::Modifiers modifiers) noexcept {
    if (seat_ != nullptr) {
        seat_->set_modifiers(modifiers);
        seat_->key(wire_code(code), false);
    }
}

void WaylandCanvas::set_on_resize(ResizeHandler handler, void* context) noexcept {
    on_resize_ = handler;
    resize_context_ = context;
}

void WaylandCanvas::on_pointer_down(visor::base::PointI at,
                                    visor::host::Modifiers modifiers) noexcept {
    static_cast<void>(modifiers); // the pointer wire carries none until grabs land
    // The desk's own grip: a press in the bottom-right square starts a
    // resize instead of a guest press, the one corner of the face the
    // window manager keeps for itself. Capture keeps the moves coming for
    // the drag's whole life, the band's own trick. The test runs in the
    // face's own coordinates -- the router's at is the scene's word.
    const visor::base::PointI face = local(at);
    if (on_resize_ != nullptr && face.x >= bounds().size.w - kResizeGrip &&
        face.y >= bounds().size.h - kResizeGrip) {
        resizing_ = true;
        return;
    }
    if (seat_ != nullptr && shell_->surface() != nullptr) {
        seat_->pointer_enter(shell_->surface(), local(at));
        // The primary button, BTN_LEFT's own number: the router's press
        // callbacks carry no button of their own, and one button is all
        // this wire has ever claimed.
        seat_->pointer_button(0x110, true);
    }
}

void WaylandCanvas::on_pointer_up(visor::base::PointI at) noexcept {
    if (resizing_) {
        resizing_ = false;
        return;
    }
    if (seat_ != nullptr) {
        seat_->pointer_button(0x110, false);
        static_cast<void>(local(at));
    }
}

void WaylandCanvas::on_pointer_move(visor::base::PointI at) noexcept {
    if (resizing_) {
        // The wanted face is the drag's own geometry in face coordinates,
        // floored at one readable cell block; the desk that wired the
        // handler clamps the rest (the desktop's edge is its knowledge,
        // not this widget's).
        const visor::base::PointI face = local(at);
        const visor::base::SizeI wanted{
            .w = std::max(face.x + (kResizeGrip / 2), kMinFace),
            .h = std::max(face.y + (kResizeGrip / 2), kMinFace),
        };
        on_resize_(*this, wanted, resize_context_);
        return;
    }
    if (seat_ != nullptr && shell_->surface() != nullptr) {
        seat_->pointer_enter(shell_->surface(), local(at));
    }
}

void WaylandCanvas::on_pointer_wheel(const visor::host::PointerWheel& wheel) noexcept {
    if (seat_ != nullptr) {
        seat_->pointer_wheel(wheel.dx, wheel.dy);
    }
}

bool WaylandCanvas::focusable() const noexcept {
    return true;
}

void WaylandCanvas::on_draw(const visor::ui::Painter& painter) const noexcept {
    // A shell surface the guest never drew, or drew and then unheld, reads
    // as no content: the ground keeps the window's pixels, which for a
    // mapped face is the honest gap, not a guess.
    const compositor::Surface* surface = shell_->surface();
    if (surface == nullptr) {
        return;
    }
    const compositor::Surface::Frame frame = surface->frame();
    if (frame.view.words == nullptr) {
        return;
    }
    painter.draw_image(bounds().pos, frame.view.words, frame.view.width, frame.view.height,
                       frame.view.stride_words);
}

} // namespace vesper::desktop
