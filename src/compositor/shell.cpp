#include "compositor/shell.hpp"

#include <cstdint>
#include <cstring>
#include <new>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include "compositor/surface.hpp"
#include "xdg-shell-server-protocol.h"

namespace vesper::compositor {
namespace {

ShellSurface* shell_of(wl_resource* resource) noexcept {
    return static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
}

// --- the positioner: accepted and forgotten ------------------------------

void positioner_destroy(wl_client* /*client*/, wl_resource* resource) noexcept {
    wl_resource_destroy(resource);
}

void positioner_noop(wl_client* /*client*/, wl_resource* /*resource*/) noexcept {
    // The reactive word of the newer positioners: popups are a later
    // chapter, so the word is collected, never read.
}

void positioner_noop_ints(wl_client* /*client*/, wl_resource* /*resource*/, std::int32_t /*a*/,
                          std::int32_t /*b*/) noexcept {
    // Sizes and offsets place popups, and the desk hosts no popups yet.
}

void positioner_noop_rect(wl_client* /*client*/, wl_resource* /*resource*/, std::int32_t /*x*/,
                          std::int32_t /*y*/, std::int32_t /*width*/,
                          std::int32_t /*height*/) noexcept {}

void positioner_noop_uint(wl_client* /*client*/, wl_resource* /*resource*/,
                          std::uint32_t /*value*/) noexcept {
    // Anchor and gravity arrive as one enum word each: still a popup's
    // business.
}

// The version-3 serial word reads nowhere yet, but the slot still needs a
// body: a null slot aborts the compositor the day a guest sends it.
void positioner_noop_serial(wl_client* /*client*/, wl_resource* /*resource*/,
                            std::uint32_t /*serial*/) noexcept {}

const struct xdg_positioner_interface kPositionerTable = {
    .destroy = positioner_destroy,
    .set_size = positioner_noop_ints,
    .set_anchor_rect = positioner_noop_rect,
    .set_anchor = positioner_noop_uint,
    .set_gravity = positioner_noop_uint,
    .set_constraint_adjustment = positioner_noop_uint,
    .set_offset = positioner_noop_ints,
    .set_reactive = positioner_noop,
    .set_parent_size = positioner_noop_ints,
    .set_parent_configure = positioner_noop_serial,
};

// --- the wm_base words that need no shell state ----------------------------

void wm_base_destroy(wl_client* /*client*/, wl_resource* resource) noexcept {
    wl_resource_destroy(resource);
}

void wm_base_create_positioner(wl_client* client,
                               wl_resource* wm_base, // NOLINT(misc-const-correctness)
                               std::uint32_t id) noexcept {
    wl_resource* resource =
        wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(wm_base), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &kPositionerTable, nullptr, nullptr);
}

void wm_base_pong(wl_client* /*client*/, wl_resource* /*resource*/,
                  std::uint32_t /*serial*/) noexcept {
    // The desk sends no pings yet, so no pong carries news; the word is
    // accepted so a responsive guest never hears silence as an error.
}

const struct xdg_wm_base_interface kWmBaseTable = {
    .destroy = wm_base_destroy,
    .create_positioner = wm_base_create_positioner,
    .get_xdg_surface = ShellSurface::Wiring::wm_base_get_xdg_surface,
    .pong = wm_base_pong,
};

// --- the quiet xdg_surface words --------------------------------------------

void xdg_surface_destroy_request(wl_client* /*client*/, wl_resource* resource) noexcept {
    wl_resource_destroy(resource);
}

void xdg_surface_get_popup(wl_client* /*client*/, wl_resource* xdg_surface, std::uint32_t /*id*/,
                           wl_resource* /*parent*/, wl_resource* /*positioner*/) noexcept {
    // Menus and tooltips are a later chapter; the honest word for one now
    // is an implementation error, not a silent misplace.
    wl_resource_post_error(xdg_surface, WL_DISPLAY_ERROR_IMPLEMENTATION,
                           "the desk hosts no popups yet");
}

void xdg_surface_set_window_geometry(wl_client* /*client*/, wl_resource* /*resource*/,
                                     std::int32_t /*x*/, std::int32_t /*y*/, std::int32_t /*width*/,
                                     std::int32_t /*height*/) noexcept {
    // The desk's geometry is the buffer's geometry; the guest's own window
    // math (shadows, decorations) has no pixels here to subtract.
}

const struct xdg_surface_interface kXdgSurfaceTable = {
    .destroy = xdg_surface_destroy_request,
    .get_toplevel = ShellSurface::Wiring::xdg_surface_get_toplevel,
    .get_popup = xdg_surface_get_popup,
    .set_window_geometry = xdg_surface_set_window_geometry,
    .ack_configure = ShellSurface::Wiring::xdg_surface_ack_configure,
};

// --- the quiet toplevel words ------------------------------------------------

void toplevel_destroy_request(wl_client* /*client*/, wl_resource* resource) noexcept {
    wl_resource_destroy(resource);
}

void toplevel_noop(wl_client* /*client*/, wl_resource* /*resource*/) noexcept {
    // unset_maximized, unset_fullscreen, set_minimized: the desk manages no
    // maximize states, and minimizing is a taskbar's business.
}

void toplevel_filed_resource(wl_client* /*client*/, wl_resource* /*resource*/,
                             wl_resource* /*stranger*/) noexcept {
    // set_parent names a leader window and the maximize/fullscreen pair
    // names an output: window management the desk does by its own hands,
    // so the guest's requests are heard and filed.
}

void toplevel_move(wl_client* /*client*/, wl_resource* /*resource*/, wl_resource* /*seat*/,
                   std::uint32_t /*serial*/) noexcept {
    // The desk drags by its own title band; a guest asking the desk to
    // move its window is filed under the same cabinet.
}

void toplevel_resize(wl_client* /*client*/, wl_resource* /*resource*/, wl_resource* /*seat*/,
                     std::uint32_t /*serial*/, std::uint32_t /*edges*/) noexcept {
    // Resizing is a later chapter (the desk's windows carry fixed faces
    // for now); the word is heard and filed.
}

void toplevel_size_hints(wl_client* /*client*/, wl_resource* /*resource*/, std::int32_t /*width*/,
                         std::int32_t /*height*/) noexcept {
    // Min and max size hints, the same filing cabinet.
}

void toplevel_show_window_menu(wl_client* /*client*/, wl_resource* /*resource*/,
                               wl_resource* /*seat*/, std::uint32_t /*serial*/, std::int32_t /*x*/,
                               std::int32_t /*y*/) noexcept {
    // No window menu exists to show.
}

void toplevel_set_app_id(wl_client* /*client*/, wl_resource* /*resource*/,
                         const char* /*app_id*/) noexcept {
    // The desk titles windows by the title word; an app id is a launcher's
    // key, and this desk launches no one yet.
}

const struct xdg_toplevel_interface kToplevelTable = {
    .destroy = toplevel_destroy_request,
    .set_parent = toplevel_filed_resource,
    .set_title = ShellSurface::Wiring::toplevel_set_title,
    .set_app_id = toplevel_set_app_id,
    .show_window_menu = toplevel_show_window_menu,
    .move = toplevel_move,
    .resize = toplevel_resize,
    .set_max_size = toplevel_size_hints,
    .set_min_size = toplevel_size_hints,
    .set_maximized = toplevel_noop,
    .unset_maximized = toplevel_noop,
    .set_fullscreen = toplevel_filed_resource,
    .unset_fullscreen = toplevel_noop,
    .set_minimized = toplevel_noop,
};

} // namespace

// --- ShellSurface, the desk's view ------------------------------------------

ShellSurface::~ShellSurface() {
    if (surface_ != nullptr) {
        surface_->detach_role(this);
    }
    if (mapped_) {
        mapped_ = false;
        if (book_->hooks.on_unmap != nullptr) {
            book_->hooks.on_unmap(*this, book_->hooks.context);
        }
    }
    if (book_ != nullptr) {
        book_->remove(this);
    }
}

ShellSurface* ShellSurface::adopt_xwindow(Surface& surface, ShellBook& book,
                                          const char* title) noexcept {
    auto* shell = new (std::nothrow) ShellSurface{};
    if (shell == nullptr || !book.add(shell)) {
        delete shell;
        return nullptr;
    }
    shell->book_ = &book;
    if (!surface.attach_role(&commit_notice, &surface_gone, shell)) {
        delete shell;
        return nullptr;
    }
    shell->surface_ = &surface;
    shell->acked_ = true; // X windows have no xdg configure handshake.
    std::strncpy(shell->title_.data(), title, shell->title_.size() - 1);
    // Either protocol stream may arrive first, including the first buffer.
    commit_notice(surface, shell);
    return shell;
}

const char* ShellSurface::title() const noexcept {
    return title_.data();
}

int ShellSurface::width() const noexcept {
    return surface_ != nullptr ? surface_->frame().view.width : 0;
}

int ShellSurface::height() const noexcept {
    return surface_ != nullptr ? surface_->frame().view.height : 0;
}

bool ShellSurface::mapped() const noexcept {
    return mapped_;
}

Surface* ShellSurface::surface() const noexcept {
    return surface_;
}

void ShellSurface::request_close() noexcept {
    if (toplevel_ != nullptr) {
        xdg_toplevel_send_close(toplevel_);
    }
}

void ShellSurface::request_resize(int width, int height) noexcept {
    if (toplevel_ == nullptr || xdg_surface_ == nullptr) {
        return;
    }
    // The same pair of words the birth configure sends, this time carrying
    // a size: the guest acks, commits the face it settled on, and the
    // desk's width()/height() read the answer off that commit.
    struct wl_array states; // still the honest empty state set
    wl_array_init(&states);
    xdg_toplevel_send_configure(toplevel_, width, height, &states);
    xdg_surface_send_configure(xdg_surface_, book_->next_serial());
    wl_array_release(&states);
}

void ShellSurface::commit_notice(Surface& surface, void* context) noexcept {
    auto* shell = static_cast<ShellSurface*>(context);
    const bool content = surface.frame().view.words != nullptr;
    if (content && !shell->acked_) {
        // A buffer before any configure was acked is the one xdg mistake
        // the spec names a word for -- the birth commit itself is empty and
        // legal, so only a buffered commit counts as the mistake.
        wl_resource_post_error(shell->xdg_surface_, XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                               "a buffer committed before any configure");
        return;
    }
    const ShellHooks& hooks = shell->book_->hooks;
    if (content && !shell->mapped_) {
        shell->mapped_ = true;
        if (hooks.on_map != nullptr) {
            hooks.on_map(*shell, hooks.context);
        }
    } else if (!content && shell->mapped_) {
        shell->mapped_ = false;
        if (hooks.on_unmap != nullptr) {
            hooks.on_unmap(*shell, hooks.context);
        }
    }
}

void ShellSurface::surface_gone(Surface& /*surface*/, void* context) noexcept {
    auto* shell = static_cast<ShellSurface*>(context);
    shell->surface_ = nullptr;
    if (shell->mapped_) {
        shell->mapped_ = false;
        const ShellHooks& hooks = shell->book_->hooks;
        if (hooks.on_unmap != nullptr) {
            hooks.on_unmap(*shell, hooks.context);
        }
    }
}

// --- ShellSurface::Wiring, the protocol tables' hands ----------------------

void ShellSurface::Wiring::wm_base_get_xdg_surface(wl_client* client, wl_resource* wm_base,
                                                   std::uint32_t id,
                                                   wl_resource* surface_resource) noexcept {
    auto* book = static_cast<ShellBook*>(wl_resource_get_user_data(wm_base));
    Surface* surface = surface_from_resource(surface_resource);
    if (surface == nullptr) {
        // This wm_base version carries no word for "not your surface", so
        // the honest answer is the display's own implementation error -- a
        // stranger's object is a stranger's object.
        wl_resource_post_error(wm_base, WL_DISPLAY_ERROR_IMPLEMENTATION,
                               "the surface is not one of this desk's");
        return;
    }
    wl_resource* resource =
        wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(wm_base), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    auto* shell = new (std::nothrow) ShellSurface{};
    if (shell == nullptr || !book->add(shell)) {
        delete shell;
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    // One role per surface: the attach itself refuses a second shell, and
    // the refusal is the wm_base role error, not an overwrite.
    if (!surface->attach_role(&ShellSurface::commit_notice, &ShellSurface::surface_gone, shell)) {
        book->remove(shell);
        delete shell;
        wl_resource_destroy(resource);
        wl_resource_post_error(wm_base, XDG_WM_BASE_ERROR_ROLE,
                               "the surface already carries a role");
        return;
    }
    shell->xdg_surface_ = resource;
    shell->surface_ = surface;
    shell->book_ = book;
    wl_resource_set_implementation(resource, &kXdgSurfaceTable, shell,
                                   &Wiring::xdg_surface_destroyed);
}

void ShellSurface::Wiring::xdg_surface_destroyed(wl_resource* resource) noexcept {
    // The goodbye order: the role comes off the surface first (so a dying
    // guest cannot fire the commit notice into a freed shell), the desk
    // hears the unmap, then the book and the memory follow.
    delete shell_of(resource);
}

void ShellSurface::Wiring::xdg_surface_get_toplevel(wl_client* client, wl_resource* xdg_surface,
                                                    std::uint32_t id) noexcept {
    ShellSurface* shell = shell_of(xdg_surface);
    if (shell->toplevel_ != nullptr) {
        // One toplevel per xdg_surface: a second ask is the protocol's own
        // already-constructed mistake.
        wl_resource_post_error(xdg_surface, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                               "a toplevel already stands on this surface");
        return;
    }
    wl_resource* toplevel = wl_resource_create(client, &xdg_toplevel_interface,
                                               wl_resource_get_version(xdg_surface), id);
    if (toplevel == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    shell->toplevel_ = toplevel;
    wl_resource_set_implementation(toplevel, &kToplevelTable, shell, &Wiring::toplevel_destroyed);

    // The birth configure: no size word (the guest picks its own), no
    // states, and the serial the desk will hear acked. It leaves before
    // the handler returns, so a guest that roundtrips sees it at once.
    struct wl_array states; // an empty array is the honest state set
    wl_array_init(&states);
    xdg_toplevel_send_configure(toplevel, 0, 0, &states);
    xdg_surface_send_configure(xdg_surface, ++shell->book_->next_serial_);
    wl_array_release(&states);
}

void ShellSurface::Wiring::toplevel_destroyed(wl_resource* resource) noexcept {
    // The toplevel resource dying alone leaves the xdg_surface alive (the
    // protocol allows the pair to part in this order); the desk simply has
    // no close door on that window anymore.
    shell_of(resource)->toplevel_ = nullptr;
}

void ShellSurface::Wiring::toplevel_set_title(wl_client* /*client*/, wl_resource* resource,
                                              const char* title) noexcept {
    ShellSurface* shell = shell_of(resource);
    const char* words = title != nullptr ? title : "";
    std::strncpy(shell->title_.data(), words, shell->title_.size() - 1);
    shell->title_[shell->title_.size() - 1] = '\0';
}

void ShellSurface::Wiring::xdg_surface_ack_configure(wl_client* /*client*/, wl_resource* resource,
                                                     std::uint32_t /*serial*/) noexcept {
    // Any ack unlocks buffer commits: the serial the guest answers is the
    // serial the desk sent, and this desk's configures are the toplevel's
    // birth and its resize asks.
    shell_of(resource)->acked_ = true;
}

// --- ShellBook ----------------------------------------------------------------

bool ShellBook::add(ShellSurface* shell) noexcept {
    if (count_ >= slots_.size()) {
        return false;
    }
    slots_[count_++] = shell;
    return true;
}

void ShellBook::remove(ShellSurface* shell) noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (slots_[i] == shell) {
            slots_[i] = slots_[count_ - 1];
            --count_;
            return;
        }
    }
}

ShellSurface* ShellBook::at(std::size_t index) const noexcept {
    return index < count_ ? slots_[index] : nullptr;
}

// --- the bind door --------------------------------------------------------------

void bind_wm_base(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) noexcept {
    wl_resource* resource =
        wl_resource_create(client, &xdg_wm_base_interface, static_cast<int>(version), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &kWmBaseTable, data, nullptr);
}

} // namespace vesper::compositor
