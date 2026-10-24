#include "compositor/surface.hpp"

#include <cstddef>
#include <cstdint>
#include <new>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "compositor/shm.hpp"
#include "visor/base/geometry/point.hpp"
#include "visor/base/geometry/rect.hpp"
#include "visor/base/geometry/size.hpp"

namespace vesper::compositor {
namespace {

Surface* surface_of(wl_resource* resource) noexcept {
    return static_cast<Surface*>(wl_resource_get_user_data(resource));
}

void surface_destroy_request(wl_client* /*client*/, wl_resource* resource) noexcept {
    wl_resource_destroy(resource);
}

void surface_destroyed(wl_resource* resource) noexcept {
    // The destructor chain runs inside wl_resource_destroy, so the object
    // is still whole here: quiet the conversations, then the book and the
    // memory follow.
    Surface* surface = surface_of(resource);
    surface->silence();
    delete surface;
}

void surface_attach(wl_client* /*client*/, wl_resource* resource, wl_resource* buffer,
                    std::int32_t x, std::int32_t y) noexcept {
    // The offset words are gone from the modern protocol (shm buffers must
    // sit at the origin); a guest still sending them is tolerated and
    // ignored rather than killed, the same leniency weston shows.
    static_cast<void>(x);
    static_cast<void>(y);
    surface_of(resource)->handle_attach(buffer);
}

void surface_damage(wl_client* /*client*/, wl_resource* resource, std::int32_t x, std::int32_t y,
                    std::int32_t width, std::int32_t height) noexcept {
    // The protocol's own word for "everything": a non-positive extent is
    // the whole surface, not a refusal.
    if (width <= 0 || height <= 0) {
        surface_of(resource)->handle_damage(visor::base::RectI{});
        return;
    }
    surface_of(resource)->handle_damage(visor::base::RectI{
        .pos = visor::base::PointI{x, y},
        .size = visor::base::SizeI{.w = width, .h = height},
    });
}

void surface_frame(wl_client* client, wl_resource* resource, std::uint32_t id) noexcept {
    surface_of(resource)->handle_frame_request(client, id);
}

void surface_commit(wl_client* /*client*/, wl_resource* resource) noexcept {
    surface_of(resource)->handle_commit();
}

void surface_ignore_region(wl_client* /*client*/, wl_resource* /*resource*/,
                           wl_resource* /*region*/) noexcept {
    // Opaque and input regions carry optimizations the desk has not bought
    // yet; the words are accepted and forgotten.
}

void surface_ignore_int(wl_client* /*client*/, wl_resource* /*resource*/,
                        std::int32_t /*value*/) noexcept {
    // Buffer transform and scale: the desk draws identity and 1:1, and a
    // guest asking otherwise is tolerated rather than killed (its pixels
    // may render wrong before they render never).
}

const struct wl_surface_interface kSurfaceTable = {
    .destroy = surface_destroy_request,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = surface_frame,
    .set_opaque_region = surface_ignore_region,
    .set_input_region = surface_ignore_region,
    .commit = surface_commit,
    .set_buffer_transform = surface_ignore_int,
    .set_buffer_scale = surface_ignore_int,
    .damage_buffer = surface_damage,
};

void region_destroy(wl_client* /*client*/, wl_resource* resource) noexcept {
    wl_resource_destroy(resource);
}

void region_noop(wl_client* /*client*/, wl_resource* /*resource*/, std::int32_t /*x*/,
                 std::int32_t /*y*/, std::int32_t /*width*/, std::int32_t /*height*/) noexcept {
    // Regions are bookkeeping this desk keeps nowhere; adding to or
    // subtracting from a forgotten region is the same silence.
}

const struct wl_region_interface kRegionTable = {
    .destroy = region_destroy,
    .add = region_noop,
    .subtract = region_noop,
};

void compositor_create_surface(wl_client* client,
                               wl_resource* compositor_resource, // NOLINT(misc-const-correctness)
                               std::uint32_t id) noexcept {
    auto* book = static_cast<SurfaceBook*>(wl_resource_get_user_data(compositor_resource));
    wl_resource* resource = wl_resource_create(client, &wl_surface_interface,
                                               wl_resource_get_version(compositor_resource), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    auto* surface = new (std::nothrow) Surface(resource, book);
    if (surface == nullptr || !book->add(surface)) {
        delete surface;
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &kSurfaceTable, surface, &surface_destroyed);
}

void compositor_create_region(wl_client* client,
                              wl_resource* compositor_resource, // NOLINT(misc-const-correctness)
                              std::uint32_t id) noexcept {
    wl_resource* resource = wl_resource_create(client, &wl_region_interface,
                                               wl_resource_get_version(compositor_resource), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &kRegionTable, nullptr, nullptr);
}

const struct wl_compositor_interface kCompositorTable = {
    .create_surface = compositor_create_surface,
    .create_region = compositor_create_region,
};

} // namespace

bool SurfaceBook::add(Surface* surface) noexcept {
    if (count_ >= slots_.size()) {
        return false;
    }
    slots_[count_++] = surface;
    return true;
}

void SurfaceBook::remove(Surface* surface) noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (slots_[i] == surface) {
            slots_[i] = slots_[count_ - 1];
            --count_;
            return;
        }
    }
}

Surface* SurfaceBook::at(std::size_t index) const noexcept {
    return index < count_ ? slots_[index] : nullptr;
}

Surface::Surface(wl_resource* resource, SurfaceBook* book) noexcept
    : resource_{resource}, book_{book} {}

Surface::~Surface() {
    // The role hears the death before the book does: its notice is the
    // shell's cue to stop reading this surface.
    if (destroy_hook_ != nullptr) {
        destroy_hook_(*this, role_context_);
    }
    silence();
    book_->remove(this);
}

Surface::Frame Surface::frame() const noexcept {
    return Frame{
        .view = view_,
        .damage = damage_,
        .epoch = epoch_,
    };
}

void Surface::end_frame(std::uint32_t frame_ms) noexcept {
    while (firing_count_ != 0) {
        wl_resource* callback = firing_[0];
        wl_callback_send_done(callback, frame_ms);
        wl_resource_destroy(callback);
    }
    if (held_buffer_ != nullptr && !held_released_) {
        // The desk has rendered this frame already -- the pixels live in
        // its canvas now -- so the word goes home and the guest may draw.
        if (held_buffer_->resource != nullptr) {
            wl_buffer_send_release(held_buffer_->resource);
        }
        held_released_ = true;
    }
}

void Surface::silence() noexcept {
    while (firing_count_ != 0) {
        wl_resource_destroy(firing_[0]);
    }
    if (held_buffer_ != nullptr) {
        if (!held_released_ && held_buffer_->resource != nullptr) {
            wl_buffer_send_release(held_buffer_->resource);
        }
        held_buffer_->unref();
        held_buffer_ = nullptr;
    }
    if (pending_buffer_ != nullptr) {
        pending_buffer_->unref();
        pending_buffer_ = nullptr;
    }
    view_ = BufferView{};
}

void Surface::callback_gone(wl_resource* resource) noexcept {
    auto* surface = static_cast<Surface*>(wl_resource_get_user_data(resource));
    for (std::size_t i = 0; i < surface->firing_count_; ++i) {
        if (surface->firing_[i] == resource) {
            for (std::size_t next = i + 1; next < surface->firing_count_; ++next) {
                surface->firing_[next - 1] = surface->firing_[next];
            }
            --surface->firing_count_;
            return;
        }
    }
}

bool Surface::attach_role(CommitHook on_commit, DestroyHook on_destroy, void* context) noexcept {
    if (commit_hook_ != nullptr) {
        return false; // one role per surface, the shell chapter's own rule
    }
    commit_hook_ = on_commit;
    destroy_hook_ = on_destroy;
    role_context_ = context;
    return true;
}

void Surface::detach_role(const void* context) noexcept {
    if (role_context_ == context) {
        commit_hook_ = nullptr;
        destroy_hook_ = nullptr;
        role_context_ = nullptr;
    }
}

void Surface::handle_attach(wl_resource* buffer) noexcept {
    // A stranger's buffer resource is a guest mixing servers; the honest
    // answer is to drop the attach, which the next commit shows as "no
    // content" instead of a read from a mapping we never owned.
    if (buffer != nullptr && shm_buffer_from_resource(buffer) == nullptr) {
        return;
    }
    ShmBuffer* next = buffer != nullptr ? shm_buffer_from_resource(buffer) : nullptr;
    if (next != nullptr) {
        next->ref();
    }
    if (pending_buffer_ != nullptr) {
        pending_buffer_->unref();
    }
    pending_buffer_ = next;
    pending_attached_ = true;
}

void Surface::handle_damage(visor::base::RectI area) noexcept {
    if (!pending_damaged_) {
        pending_damage_ = area;
        pending_damaged_ = true;
        return;
    }
    // A zero-sized area here means "the whole surface", which against a
    // guest that has not committed yet is still the whole of nothing; the
    // desk clamps damage to the committed buffer when it reads. RectI has
    // no union verb, so the bounds are said by hand.
    const visor::base::RectI& a = pending_damage_;
    const visor::base::RectI& b = area;
    const int left = a.pos.x < b.pos.x ? a.pos.x : b.pos.x;
    const int top = a.pos.y < b.pos.y ? a.pos.y : b.pos.y;
    const int a_right = a.pos.x + a.size.w;
    const int b_right = b.pos.x + b.size.w;
    const int a_bottom = a.pos.y + a.size.h;
    const int b_bottom = b.pos.y + b.size.h;
    const int right = a_right > b_right ? a_right : b_right;
    const int bottom = a_bottom > b_bottom ? a_bottom : b_bottom;
    pending_damage_ = visor::base::RectI{
        .pos = visor::base::PointI{left, top},
        .size = visor::base::SizeI{.w = right - left, .h = bottom - top},
    };
}

void Surface::handle_frame_request(wl_client* client, std::uint32_t id) noexcept {
    wl_resource* callback =
        wl_resource_create(client, &wl_callback_interface, wl_resource_get_version(resource_), id);
    if (callback == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(callback, nullptr, this, &callback_gone);
    if (firing_count_ < firing_.size()) {
        firing_[firing_count_++] = callback;
    } else {
        // Over the cap the callback still owes the guest an answer; sending
        // done at once is late-but-honest against dropping it whole.
        wl_callback_send_done(callback, 0);
        wl_resource_destroy(callback);
    }
}

void Surface::handle_commit() noexcept {
    // The flip: pending becomes current, the retired buffer goes home with
    // its release word (unless the desk already sent it -- a replaced
    // buffer that was consumed and released needs no second word), and the
    // damage the guest reported becomes the damage the desk reads.
    const bool retired_consumed = held_released_; // the old hold's state
    ShmBuffer* retired = nullptr;
    const bool attached = pending_attached_;
    if (attached) {
        retired = held_buffer_;
        held_buffer_ = pending_buffer_;
        pending_buffer_ = nullptr;
        pending_attached_ = false;
    }
    if (held_buffer_ != nullptr) {
        view_ = held_buffer_->view;
        damage_ = pending_damage_;
        pending_damage_ = {};
        pending_damaged_ = false;
        if (attached) {
            held_released_ = false; // A new attachment owes its own release.
        }
        ++epoch_;
    } else {
        // A null attach commit retires content; the desk reads "nothing
        // held" and the guest's damage stops accumulating.
        view_ = BufferView{};
        damage_ = {};
        pending_damage_ = {};
        pending_damaged_ = false;
    }
    if (retired != nullptr) {
        if (retired != held_buffer_ && !retired_consumed && retired->resource != nullptr) {
            wl_buffer_send_release(retired->resource);
        }
        retired->unref();
    }
    // The role's tail runs last: it reads the flipped state, the same
    // state the desk will read between rounds.
    if (commit_hook_ != nullptr) {
        commit_hook_(*this, role_context_);
    }
}

Surface* surface_from_resource(wl_resource* resource) noexcept {
    if (resource == nullptr ||
        wl_resource_instance_of(resource, &wl_surface_interface, &kSurfaceTable) == 0) {
        return nullptr;
    }
    return static_cast<Surface*>(wl_resource_get_user_data(resource));
}

void bind_compositor(wl_client* client, void* data, std::uint32_t version,
                     std::uint32_t id) noexcept {
    wl_resource* resource =
        wl_resource_create(client, &wl_compositor_interface, static_cast<int>(version), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    // The book travels as the resource's user data: create_surface reaches
    // it without the handlers needing to know the server object at all.
    wl_resource_set_implementation(resource, &kCompositorTable, data, nullptr);
}

} // namespace vesper::compositor
