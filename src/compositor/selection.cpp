#include "compositor/selection.hpp"

#include <unistd.h>

#include <cstdint>
#include <cstring>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace vesper::compositor {
namespace {

Selection* selection_of(wl_resource* resource) noexcept {
    return static_cast<Selection*>(wl_resource_get_user_data(resource));
}

const struct wl_data_device_manager_interface kManagerTable = {
    .create_data_source = Selection::Wiring::manager_create_data_source,
    .get_data_device = Selection::Wiring::manager_get_data_device,
};

const struct wl_data_device_interface kDeviceTable = {
    .start_drag = Selection::Wiring::device_start_drag,
    .set_selection = Selection::Wiring::device_set_selection,
};

const struct wl_data_source_interface kSourceTable = {
    .offer = Selection::Wiring::source_offer,
    .destroy = Selection::Wiring::source_destroy,
};

const struct wl_data_offer_interface kOfferTable = {
    .accept = Selection::Wiring::offer_accept,
    .receive = Selection::Wiring::offer_receive,
    .destroy = Selection::Wiring::offer_destroy,
};

} // namespace

// --- the desk's half ---------------------------------------------------------

void Selection::note_keyboard_client(wl_client* client) noexcept {
    if (client == focused_client_) {
        return;
    }
    focused_client_ = client;
    announce_to_focused();
}

Selection::Device* Selection::device_of_client(wl_client* client) noexcept {
    for (auto& device : devices_) {
        if (device.client == client) {
            return &device;
        }
    }
    return nullptr;
}

Selection::Source* Selection::source_of_resource(wl_resource* resource) noexcept {
    for (auto& source : sources_) {
        if (source.resource == resource) {
            return &source;
        }
    }
    return nullptr;
}

Selection::Offer* Selection::offer_of_resource(wl_resource* resource) noexcept {
    for (auto& offer : offers_) {
        if (offer.resource == resource) {
            return &offer;
        }
    }
    return nullptr;
}

void Selection::announce_to_focused() noexcept {
    if (focused_client_ == nullptr) {
        return; // nobody holds the keyboard; nothing to tell
    }
    const Device* device = device_of_client(focused_client_);
    if (device == nullptr) {
        // The focused guest never asked for a data device; the standing
        // selection stays standing for whoever asks next.
        return;
    }
    if (selection_ == nullptr) {
        wl_data_device_send_selection(device->device, nullptr);
        return;
    }
    Offer* slot = nullptr;
    for (auto& offer : offers_) {
        if (offer.resource == nullptr) {
            slot = &offer;
            break;
        }
    }
    if (slot == nullptr) {
        // A guest that collects offers without retiring them filled the
        // book. The honest refusal is the one libwayland understands.
        wl_client_post_no_memory(focused_client_);
        return;
    }
    wl_resource* resource = wl_resource_create(focused_client_, &wl_data_offer_interface,
                                               wl_resource_get_version(device->device), 0);
    if (resource == nullptr) {
        wl_client_post_no_memory(focused_client_);
        return;
    }
    slot->resource = resource;
    slot->source = selection_->resource;
    wl_resource_set_implementation(resource, &kOfferTable, this,
                                   &Selection::Wiring::offer_destroyed);
    wl_data_device_send_data_offer(device->device, resource);
    for (std::size_t i = 0; i < selection_->mime_count; ++i) {
        wl_data_offer_send_offer(resource, selection_->mimes[i].data());
    }
    wl_data_device_send_selection(device->device, resource);
}

// --- the Wiring, the protocol tables' hands ----------------------------------

void Selection::Wiring::bind(wl_client* client, void* data, std::uint32_t version,
                             std::uint32_t id) noexcept {
    // The manager itself answers two requests and speaks no events; every
    // conversation happens on the objects it hands out.
    wl_resource* resource = wl_resource_create(client, &wl_data_device_manager_interface,
                                               static_cast<int>(version), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &kManagerTable, data, nullptr);
}

void Selection::Wiring::keyboard_client_moved(wl_client* client, void* data) noexcept {
    static_cast<Selection*>(data)->note_keyboard_client(client);
}

void Selection::Wiring::manager_create_data_source(wl_client* client, wl_resource* manager,
                                                   std::uint32_t id) noexcept {
    Selection* selection = selection_of(manager);
    wl_resource* resource =
        wl_resource_create(client, &wl_data_source_interface, wl_resource_get_version(manager), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    Source* slot = nullptr;
    for (auto& source : selection->sources_) {
        if (source.resource == nullptr) {
            slot = &source;
            break;
        }
    }
    if (slot == nullptr) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    slot->resource = resource;
    slot->client = client;
    wl_resource_set_implementation(resource, &kSourceTable, selection,
                                   &Selection::Wiring::source_destroyed);
}

void Selection::Wiring::manager_get_data_device(wl_client* client, wl_resource* manager,
                                                std::uint32_t id, wl_resource* /*seat*/) noexcept {
    Selection* selection = selection_of(manager);
    wl_resource* resource =
        wl_resource_create(client, &wl_data_device_interface, wl_resource_get_version(manager), id);
    if (resource == nullptr) {
        wl_client_post_no_memory(client);
        return;
    }
    // One device per client: a second request from the same client is a
    // refill of the slot it already owns, the seat's own rule. The retired
    // resource keeps its table -- requests on it find no record and stay
    // quiet, which is kinder than an error a real guest never means.
    Device* device = selection->device_of_client(client);
    if (device == nullptr) {
        for (auto& slot : selection->devices_) {
            if (slot.client == nullptr) {
                device = &slot;
                break;
            }
        }
    }
    if (device == nullptr) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    device->client = client;
    device->device = resource;
    wl_resource_set_implementation(resource, &kDeviceTable, selection,
                                   &Selection::Wiring::device_destroyed);
    // A device arriving after the focus landed still hears the standing
    // selection: the protocol asks for the current state, not for a race
    // the guest happened to lose.
    if (device->client == selection->focused_client_) {
        selection->announce_to_focused();
    }
}

void Selection::Wiring::device_start_drag(wl_client* /*client*/, wl_resource* /*device*/,
                                          wl_resource* /*source*/, wl_resource* /*origin*/,
                                          wl_resource* /*icon*/,
                                          std::uint32_t /*serial*/) noexcept {
    // Drag-and-drop is a later chapter's consumer; the word lands here
    // inert, the same leniency the touch object gets.
}

void Selection::Wiring::device_set_selection(wl_client* client, wl_resource* device,
                                             wl_resource* source_resource,
                                             std::uint32_t /*serial*/) noexcept {
    // The serial is heard and filed. Pairing it against the ledger of
    // serials the seat handed out is bookkeeping no consumer asked for yet
    // -- and a forged serial steals nothing the keyboard focus does not
    // already gate. The real-app lane is where that ledger earns its keep.
    Selection* selection = selection_of(device);
    Source* source = nullptr;
    if (source_resource != nullptr) {
        Source* found = selection->source_of_resource(source_resource);
        // A guest can only name objects it owns; a stray word is nobody's
        // selection.
        if (found != nullptr && found->client == client) {
            source = found;
        }
    }
    if (source == selection->selection_) {
        return; // the same word again, down to null repeated over null
    }
    if (selection->selection_ != nullptr) {
        // The polite goodbye to the outgoing owner, whose own destroy word
        // follows; the record frees itself when it arrives.
        wl_data_source_send_cancelled(selection->selection_->resource);
    }
    selection->selection_ = source;
    selection->announce_to_focused();
}

void Selection::Wiring::device_destroyed(wl_resource* resource) noexcept {
    // The guest hung up (or the display tore down): the slot frees itself,
    // and any selection the guest owned empties through source_destroyed.
    Selection* selection = selection_of(resource);
    for (auto& device : selection->devices_) {
        if (device.device == resource) {
            device = Device{};
            return;
        }
    }
}

void Selection::Wiring::source_offer(wl_client* /*client*/, wl_resource* source_resource,
                                     const char* mime_type) noexcept {
    Selection* selection = selection_of(source_resource);
    Source* source = selection->source_of_resource(source_resource);
    if (source == nullptr) {
        return;
    }
    if (source->mime_count >= source->mimes.size()) {
        // The GTK text set fits with room to spare; a guest offering more
        // keeps the extra to itself rather than the desk refusing the
        // source over one word too many.
        return;
    }
    auto& mime = source->mimes[source->mime_count++];
    mime.fill('\0');
    std::strncpy(mime.data(), mime_type, mime.size() - 1);
}

void Selection::Wiring::source_destroy(wl_client* /*client*/, wl_resource* source) noexcept {
    wl_resource_destroy(source);
}

void Selection::Wiring::source_destroyed(wl_resource* resource) noexcept {
    Selection* selection = selection_of(resource);
    Source* source = selection->source_of_resource(resource);
    if (source == nullptr) {
        return;
    }
    if (selection->selection_ == source) {
        // The owner dropped its own source: the selection empties, and the
        // focused guest hears the empty word.
        selection->selection_ = nullptr;
        selection->announce_to_focused();
    }
    *source = Source{};
}

void Selection::Wiring::offer_accept(wl_client* /*client*/, wl_resource* /*offer*/,
                                     std::uint32_t /*serial*/, const char* /*mime_type*/) noexcept {
    // The drag feedback word; heard and filed like the pointer's cursor.
}

void Selection::Wiring::offer_receive(wl_client* /*client*/, wl_resource* offer_resource,
                                      const char* mime_type, std::int32_t fd) noexcept {
    // The fd is ours the moment it arrives: it leaves either over the
    // owner's wire or into the close it deserves.
    Selection* selection = selection_of(offer_resource);
    const Offer* offer = selection->offer_of_resource(offer_resource);
    const bool current = selection->selection_ != nullptr && offer != nullptr &&
                         offer->source == selection->selection_->resource;
    if (current) {
        // The mime travels verbatim: the receiver picked it off the offered
        // list, and any vetting here would second-guess a conversation the
        // two guests are having without the desk.
        wl_data_source_send_send(selection->selection_->resource, mime_type, fd);
    }
    ::close(fd);
}

void Selection::Wiring::offer_destroy(wl_client* /*client*/, wl_resource* offer) noexcept {
    wl_resource_destroy(offer);
}

void Selection::Wiring::offer_destroyed(wl_resource* resource) noexcept {
    Selection* selection = selection_of(resource);
    if (Offer* offer = selection->offer_of_resource(resource); offer != nullptr) {
        *offer = Offer{};
    }
}

} // namespace vesper::compositor
