#include "ui/shell_desk/shell_desk.hpp"

#include "compositor/server.hpp"
#include "compositor/shell.hpp"
#include "ui/desktop/desktop.hpp"
#include "ui/style/style.hpp"
#include "ui/window_frame/window_frame.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/draw/mono_font.hpp"

#include "compositor/seat.hpp"
#include "compositor/surface.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"

#include "visor/ui/widgets/widget.hpp"

#include <algorithm>
#include <cstddef>
#include <new>

namespace vesper::desktop {
namespace {

// The cascade: every window ever placed takes the next seat, so two
// guests mapping in a row always overlap by construction -- the stacking
// judgment's own stage. The first seat clears the desk's icon column (56
// wide at x=16), where the local windows' launchers live.
constexpr int kCascadeStep = 24;
constexpr visor::base::PointI kFirstSeat{88, 56};

} // namespace

ShellDesk::ShellDesk(Desktop& desktop, compositor::Server& server,
                     const visor::draw::MonoFont& title_font) noexcept
    : desktop_{desktop}, server_{server}, title_font_{title_font} {
    server_.set_shell_hooks(compositor::ShellHooks{
        .on_map = &ShellDesk::on_shell_map,
        .on_unmap = &ShellDesk::on_shell_unmap,
        .context = this,
    });
}

ShellDesk::~ShellDesk() {
    // The frames and canvases are the desk's own news; taking every window
    // down keeps the desktop's tree clean when the desk dies before the
    // desktop does.
    for (Slot& slot : slots_) {
        if (slot.frame != nullptr) {
            static_cast<void>(desktop_.remove_child(slot.frame));
            delete slot.frame;
            delete slot.canvas;
            slot = Slot{};
        }
    }
    server_.set_shell_hooks(compositor::ShellHooks{});
}

void ShellDesk::pump() noexcept {
    // Serving the guests runs the map and unmap hooks, which frame and
    // unframe windows mid-round -- exactly the desk's own state changes,
    // safe here because nothing draws inside a dispatch.
    static_cast<void>(server_.dispatch(0));
    if (xwayland_ != nullptr) {
        xwayland_->pump();
    }
    server_.flush_clients();
}

void ShellDesk::set_xwayland(compositor::XWayland* xwayland) noexcept {
    xwayland_ = xwayland;
}

void ShellDesk::collect() noexcept {
    for (Slot& slot : slots_) {
        if (slot.shell == nullptr || slot.shell->surface() == nullptr) {
            continue;
        }
        const compositor::Surface::Frame frame = slot.shell->surface()->frame();
        if (frame.epoch != slot.epoch) {
            slot.epoch = frame.epoch;
            // A resized guest answers its configure with a new-shaped face;
            // the frame follows before the repaint debt lands, so the debt
            // covers the face the window now wears instead of the one it
            // was born with.
            const auto& face = frame.view;
            if (face.words != nullptr && (face.width != slot.canvas->bounds().size.w ||
                                          face.height != slot.canvas->bounds().size.h)) {
                const int chrome_w = 2 * (1 + style::kContentPadding);
                const int chrome_h =
                    WindowFrame::kTitleBarHeight + 1 + (2 * style::kContentPadding);
                slot.frame->reshape(
                    visor::base::SizeI{.w = face.width + chrome_w, .h = face.height + chrome_h});
            }
            slot.canvas->note_damage(frame.damage);
        }
    }
}

void ShellDesk::present() noexcept {
    server_.end_frame();
}

void ShellDesk::release_focus() noexcept {
    defocus_all();
}

void ShellDesk::settle_focus() noexcept {
    // Focus follows the pointer for guest faces: a press or a hover that
    // the router delivered into a canvas aims the seat's keyboard at that
    // guest's window. Band presses reach the frame's own cue instead and
    // land here through on_frame_focus -- one aim, two doors.
    const compositor::Surface* under = server_.seat().pointer_focus();
    for (Slot& slot : slots_) {
        if (slot.frame == nullptr) {
            continue;
        }
        if (under != nullptr && slot.shell->surface() == under && !slot.focused) {
            focus_slot(slot);
        }
        if (slot.focused) {
            reclaim_tree_focus(slot);
        }
    }
}

void ShellDesk::reclaim_tree_focus(Slot& slot) noexcept {
    // The tree is shared with the desk's own windows, so the focused
    // guest's claim runs only so far. A keyboard sitting inside any
    // guest's frame -- the aim's own close button, another guest's band
    // child -- comes back to the aimed canvas: guest chrome is the desk's
    // own furniture. One held outside every guest frame -- an icon's
    // terminal, an about box -- belongs to that window: the guests step
    // aside whole, bands dark and the seat's keyboard word sent, instead
    // of stealing the tree back every round.
    visor::ui::Widget* holder = desktop_.focused_widget();
    if (holder == slot.canvas) {
        return;
    }
    if (holder == nullptr) {
        slot.canvas->set_focused(true);
        return;
    }
    for (const Slot& other : slots_) {
        if (other.frame != nullptr && within(other.frame, holder)) {
            holder->set_focused(false);
            slot.canvas->set_focused(true);
            return;
        }
    }
    defocus_all();
}

const WindowFrame* ShellDesk::frame_of(const compositor::ShellSurface* shell) const noexcept {
    for (const Slot& slot : slots_) {
        if (slot.shell == shell) {
            return slot.frame;
        }
    }
    return nullptr;
}

std::size_t ShellDesk::window_count() const noexcept {
    std::size_t count = 0;
    for (const Slot& slot : slots_) {
        if (slot.frame != nullptr) {
            ++count;
        }
    }
    return count;
}

void ShellDesk::on_shell_map(compositor::ShellSurface& shell, void* context) noexcept {
    auto* desk = static_cast<ShellDesk*>(context);
    desk->handle_map(shell);
}

void ShellDesk::on_shell_unmap(compositor::ShellSurface& shell, void* context) noexcept {
    auto* desk = static_cast<ShellDesk*>(context);
    desk->handle_unmap(shell);
}

void ShellDesk::on_frame_focus(WindowFrame& frame, void* context) noexcept {
    // The press cue: the frame under the pointer takes the desk's focus.
    auto* desk = static_cast<ShellDesk*>(context);
    Slot* slot = desk->slot_of_frame(frame);
    if (slot != nullptr) {
        desk->focus_slot(*slot);
    }
}

void ShellDesk::on_frame_close(WindowFrame& frame, void* context) noexcept {
    // The desk's close button asks, the guest answers: the toplevel close
    // word is a request the guest may even refuse, and the window comes
    // down through the unmap that follows the guest's own goodbye. An
    // XWayland face gets the X courtesy instead (WM_DELETE_WINDOW, or a
    // kill for clients that never learned it).
    auto* desk = static_cast<ShellDesk*>(context);
    const Slot* slot = desk->slot_of_frame(frame);
    if (slot != nullptr && slot->shell != nullptr) {
        if (desk->xwayland_ != nullptr && desk->xwayland_->owns(slot->shell)) {
            desk->xwayland_->request_close(*slot->shell);
        } else {
            slot->shell->request_close();
        }
    }
}

void ShellDesk::on_face_resize(WaylandCanvas& canvas, visor::base::SizeI wanted,
                               void* context) noexcept {
    // The grip spoke; the desk clamps the want into its own bounds (the
    // face cannot push its frame past the desktop's edge) and asks. The
    // window follows when the guest answers, not when the grip moves --
    // the guest owns the pace, the same way it owns the close.
    auto* desk = static_cast<ShellDesk*>(context);
    const Slot* slot = desk->slot_of_canvas(canvas);
    if (slot == nullptr || slot->shell == nullptr || slot->frame == nullptr) {
        return;
    }
    const int chrome_w = 2 * (1 + style::kContentPadding);
    const int chrome_h = WindowFrame::kTitleBarHeight + 1 + (2 * style::kContentPadding);
    const int slack_w = desk->desktop_.bounds().size.w - slot->frame->bounds().pos.x - chrome_w;
    const int slack_h = desk->desktop_.bounds().size.h - slot->frame->bounds().pos.y - chrome_h;
    const int width = std::clamp(wanted.w, WaylandCanvas::kMinFace, std::max(slack_w, 1));
    const int height = std::clamp(wanted.h, WaylandCanvas::kMinFace, std::max(slack_h, 1));
    slot->shell->request_resize(width, height);
}

void ShellDesk::handle_map(compositor::ShellSurface& shell) noexcept {
    Slot* slot = nullptr;
    for (Slot& candidate : slots_) {
        if (candidate.frame == nullptr) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr) {
        // A desk with every seat taken refuses politely: the guest's face
        // stays unmapped from the desk's side, its pixels simply never
        // arrive.
        return;
    }
    const int step = (placed_ % 8) * kCascadeStep;
    const visor::base::PointI seat{kFirstSeat.x + step, kFirstSeat.y + step};
    ++placed_;
    // The frame carries the guest's face plus its own chrome: the band on
    // top, the one-pixel border and the content padding on every side. A
    // frame sized to the bare face leaves its canvas smaller than the
    // guest's picture, and the picture spills past the border's right and
    // bottom edges.
    const visor::base::SizeI face{
        .w = shell.width() + (2 * (1 + style::kContentPadding)),
        .h = shell.height() + WindowFrame::kTitleBarHeight + 1 + (2 * style::kContentPadding),
    };
    const visor::base::RectI bounds{.pos = seat, .size = face};
    auto* frame =
        new (std::nothrow) WindowFrame(bounds, shell.title(), title_font_, desktop_.bounds());
    auto* canvas = new (std::nothrow) WaylandCanvas(frame->content_area(), &shell, &server_.seat());
    if (frame == nullptr || canvas == nullptr || !frame->initialize()) {
        delete frame;
        delete canvas;
        return;
    }
    if (!desktop_.add_child(frame)) {
        delete frame;
        delete canvas;
        return;
    }
    static_cast<void>(frame->set_content(canvas));
    frame->set_on_focus(&ShellDesk::on_frame_focus, this);
    frame->set_on_close(&ShellDesk::on_frame_close, this);
    canvas->set_on_resize(&ShellDesk::on_face_resize, this);
    *slot = Slot{
        .shell = &shell,
        .frame = frame,
        .canvas = canvas,
        .epoch = 0,
        .focused = false,
    };
    focus_slot(*slot);
}

void ShellDesk::handle_unmap(compositor::ShellSurface& shell) noexcept {
    Slot* slot = slot_of(&shell);
    if (slot == nullptr) {
        return;
    }
    const bool was_focused = slot->focused;
    drop_slot(*slot);
    if (was_focused) {
        // The closing window owes its keyboard to whoever stands on top
        // now; nobody standing means the seat simply aims at nothing.
        if (Slot* heir = topmost_slot(); heir != nullptr) {
            focus_slot(*heir);
        } else {
            defocus_all();
        }
    }
}

void ShellDesk::focus_slot(Slot& slot) noexcept {
    defocus_all();
    // The tree's keyboard is a single seat: a local window's widget may
    // hold it when this aim starts, and set_focused only sets its own
    // bit, so the old holder is dismissed by hand first.
    if (visor::ui::Widget* holder = desktop_.focused_widget();
        holder != nullptr && holder != slot.canvas) {
        holder->set_focused(false);
    }
    slot.focused = true;
    slot.frame->set_active(true);
    static_cast<void>(slot.frame->raise());
    slot.canvas->set_focused(true); // the tree's keyboard holder, one at a time
    server_.seat().keyboard_enter(slot.shell->surface());
}

void ShellDesk::defocus_all() noexcept {
    for (Slot& slot : slots_) {
        if (slot.frame == nullptr || !slot.focused) {
            continue;
        }
        slot.focused = false;
        slot.frame->set_active(false);
        slot.canvas->set_focused(false);
    }
    server_.seat().keyboard_leave();
}

// The subtree walk mirrors visor's own draw()/hit_test() shape, recursion
// and all.
// NOLINTNEXTLINE(misc-no-recursion)
bool ShellDesk::within(visor::ui::Widget* node, const visor::ui::Widget* target) noexcept {
    if (node == nullptr || target == nullptr) {
        return false;
    }
    if (node == target) {
        return true;
    }
    for (std::size_t i = 0; i < node->child_count(); ++i) {
        if (within(node->child(i), target)) {
            return true;
        }
    }
    return false;
}

ShellDesk::Slot* ShellDesk::topmost_slot() noexcept {
    // The desktop's child list stacks in add order with raise() moving to
    // the end; the last standing window in that list is the top one.
    for (std::size_t i = desktop_.child_count(); i-- > 0;) {
        const visor::ui::Widget* child = desktop_.child(i);
        for (Slot& slot : slots_) {
            if (slot.frame == child) {
                return &slot;
            }
        }
    }
    return nullptr;
}

ShellDesk::Slot* ShellDesk::slot_of(const compositor::ShellSurface* shell) noexcept {
    for (Slot& slot : slots_) {
        if (slot.shell == shell) {
            return &slot;
        }
    }
    return nullptr;
}

ShellDesk::Slot* ShellDesk::slot_of_frame(const WindowFrame& frame) noexcept {
    for (Slot& slot : slots_) {
        if (slot.frame == &frame) {
            return &slot;
        }
    }
    return nullptr;
}

ShellDesk::Slot* ShellDesk::slot_of_canvas(const WaylandCanvas& canvas) noexcept {
    for (Slot& slot : slots_) {
        if (slot.canvas == &canvas) {
            return &slot;
        }
    }
    return nullptr;
}

void ShellDesk::drop_slot(Slot& slot) noexcept {
    if (slot.frame == nullptr) {
        return;
    }
    static_cast<void>(desktop_.remove_child(slot.frame));
    delete slot.frame;
    delete slot.canvas;
    slot = Slot{};
}

} // namespace vesper::desktop
