#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct wl_client;   // NOLINT(readability-identifier-naming)
struct wl_resource; // NOLINT(readability-identifier-naming)

namespace vesper::compositor {

/**
 * @brief The desk's one selection (the roadmap's A1): the clipboard half of
 *        wl_data_device, the channel one guest's copy reaches the other
 *        guests through.
 *
 * The desk holds no clipboard of its own. The selection always lives in the
 * owning guest's wl_data_source -- data stays at the source and travels a
 * pipe only when a receiver asks for it, the ownership model X11's
 * selections taught and Wayland kept. Three books cover the traffic: one
 * data device per client that asked for one, the live sources with their
 * mime lists, and the offers handed out. Offers outlive their chapter of
 * the story, because the server retiring one unilaterally would kill a
 * polite guest that still says goodbye to it -- an offer the client never
 * destroyed stays in the book until it does (or the guest leaves).
 *
 * Delivery follows the keyboard: the focused client's data device hears
 * data_offer plus selection whenever either the focus or the selection
 * moves. The drag half of the protocol (start_drag and the offer's action
 * words) waits for its consumer and stays politely inert here, the same
 * leniency the C1 heart gives a touch object.
 */
class Selection {
  public:
    /// One data device per client, the seat's client table again.
    static constexpr std::size_t kClientCapacity = 16;
    /// Sources guests keep alive; a copy-owning guest holds one at a time.
    static constexpr std::size_t kSourceCapacity = 16;
    /// Offers handed out, including retired ones guests never destroyed.
    static constexpr std::size_t kOfferCapacity = 16;
    /// The mime types one source advertises, the set GTK offers text as.
    static constexpr std::size_t kMimeCapacity = 8;
    static constexpr std::size_t kMimeBytes = 48;

    Selection() noexcept = default;
    ~Selection() = default;

    Selection(const Selection&) = delete;
    Selection& operator=(const Selection&) = delete;

    /// The keyboard focus crossed, the seat's word and never this object's
    /// guess: whichever client now holds the keyboard hears whatever
    /// selection stands, an empty one included.
    void note_keyboard_client(wl_client* client) noexcept;

    // The protocol tables' hands, the Wiring shape again: every request and
    // every resource-goodbye that reaches into this object's state.
    struct Wiring {
        static void bind(wl_client* client, void* data, std::uint32_t version,
                         std::uint32_t id) noexcept;
        static void keyboard_client_moved(wl_client* client, void* data) noexcept;
        static void manager_create_data_source(wl_client* client, wl_resource* manager,
                                               std::uint32_t id) noexcept;
        static void manager_get_data_device(wl_client* client, wl_resource* manager,
                                            std::uint32_t id, wl_resource* seat) noexcept;
        static void device_start_drag(wl_client* client, wl_resource* device, wl_resource* source,
                                      wl_resource* origin, wl_resource* icon,
                                      std::uint32_t serial) noexcept;
        static void device_set_selection(wl_client* client, wl_resource* device,
                                         wl_resource* source, std::uint32_t serial) noexcept;
        static void device_destroyed(wl_resource* resource) noexcept;
        static void source_offer(wl_client* client, wl_resource* source,
                                 const char* mime_type) noexcept;
        static void source_destroy(wl_client* client, wl_resource* source) noexcept;
        static void source_destroyed(wl_resource* resource) noexcept;
        static void offer_accept(wl_client* client, wl_resource* offer, std::uint32_t serial,
                                 const char* mime_type) noexcept;
        static void offer_receive(wl_client* client, wl_resource* offer, const char* mime_type,
                                  std::int32_t fd) noexcept;
        static void offer_destroy(wl_client* client, wl_resource* offer) noexcept;
        static void offer_destroyed(wl_resource* resource) noexcept;
    };

  private:
    friend struct Wiring;

    /// One client's data device.
    struct Device {
        wl_client* client = nullptr;
        wl_resource* device = nullptr;
    };

    /// One live source: the mimes its guest advertised so far.
    struct Source {
        wl_resource* resource = nullptr;
        wl_client* client = nullptr;
        std::array<std::array<char, kMimeBytes>, kMimeCapacity> mimes{};
        std::size_t mime_count = 0;
    };

    /// One offer handed out. The source word is the resource it was cut
    /// from, not a pointer -- records are reused, and a retired offer's
    /// pointer must not start naming a stranger.
    struct Offer {
        wl_resource* resource = nullptr;
        wl_resource* source = nullptr;
    };

    [[nodiscard]] Device* device_of_client(wl_client* client) noexcept;
    [[nodiscard]] Source* source_of_resource(wl_resource* resource) noexcept;
    [[nodiscard]] Offer* offer_of_resource(wl_resource* resource) noexcept;

    /// The focused client hears the selection that stands: a fresh offer
    /// with every mime when one stands, the empty word when none does.
    void announce_to_focused() noexcept;

    std::array<Device, kClientCapacity> devices_{};
    std::array<Source, kSourceCapacity> sources_{};
    std::array<Offer, kOfferCapacity> offers_{};
    Source* selection_ = nullptr;
    wl_client* focused_client_ = nullptr;
};

} // namespace vesper::compositor
