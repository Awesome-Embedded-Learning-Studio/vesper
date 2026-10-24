#include "session/wire.hpp"

#include <cstdint>
#include <cstring>
#include <variant>

#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"

namespace vesper::session {
namespace {

/// The four modifier flags as the wire's bit layout; shift first, the
/// order the struct names them in.
constexpr std::uint8_t pack_modifiers(const visor::host::Modifiers& modifiers) noexcept {
    std::uint8_t bits = 0;
    if (modifiers.shift) {
        bits |= 1U;
    }
    if (modifiers.ctrl) {
        bits |= 2U;
    }
    if (modifiers.alt) {
        bits |= 4U;
    }
    if (modifiers.super) {
        bits |= 8U;
    }
    return bits;
}

visor::host::Modifiers unpack_modifiers(std::uint8_t bits) noexcept {
    return visor::host::Modifiers{
        .shift = (bits & 1U) != 0,
        .ctrl = (bits & 2U) != 0,
        .alt = (bits & 4U) != 0,
        .super = (bits & 8U) != 0,
    };
}

/// The pointer position one flat body carries, shared by the four pointer
/// alternatives.
constexpr EventBody pointer_body(std::uint8_t kind, std::uint16_t code,
                                 const visor::base::PointI& pos,
                                 const visor::host::Modifiers& modifiers) noexcept {
    return EventBody{
        .kind = kind,
        .modifiers = pack_modifiers(modifiers),
        .code = code,
        .x = static_cast<std::int16_t>(pos.x),
        .y = static_cast<std::int16_t>(pos.y),
    };
}

} // namespace

EventBody encode_event(const visor::host::Event& event) noexcept {
    if (const auto* down = std::get_if<visor::host::KeyDown>(&event)) {
        return EventBody{
            .kind = kKindKeyDown,
            .modifiers = pack_modifiers(down->modifiers),
            .code = static_cast<std::uint16_t>(down->code),
            .x = 0,
            .y = 0,
        };
    }
    if (const auto* up = std::get_if<visor::host::KeyUp>(&event)) {
        return EventBody{
            .kind = kKindKeyUp,
            .modifiers = pack_modifiers(up->modifiers),
            .code = static_cast<std::uint16_t>(up->code),
            .x = 0,
            .y = 0,
        };
    }
    if (const auto* move = std::get_if<visor::host::PointerMove>(&event)) {
        return pointer_body(kKindPointerMove, 0, move->pos, visor::host::Modifiers{});
    }
    if (const auto* down = std::get_if<visor::host::PointerDown>(&event)) {
        return pointer_body(kKindPointerDown, static_cast<std::uint16_t>(down->button), down->pos,
                            down->modifiers);
    }
    if (const auto* up = std::get_if<visor::host::PointerUp>(&event)) {
        return pointer_body(kKindPointerUp, static_cast<std::uint16_t>(up->button), up->pos,
                            visor::host::Modifiers{});
    }
    // The wheel spends its fields on the two deltas; the position is the
    // price of the eight-byte shape, named in the header's own comment.
    const auto& wheel = std::get<visor::host::PointerWheel>(event);
    return EventBody{
        .kind = kKindPointerWheel,
        .modifiers = pack_modifiers(wheel.modifiers),
        .code = static_cast<std::uint16_t>(wheel.dx),
        .x = 0,
        .y = static_cast<std::int16_t>(wheel.dy),
    };
}

bool decode_event(EventBody body, visor::host::Event& out) noexcept {
    switch (body.kind) {
        case kKindKeyDown:
            out = visor::host::Event{visor::host::KeyDown{
                .code = body.code,
                .modifiers = unpack_modifiers(body.modifiers),
            }};
            return true;
        case kKindKeyUp:
            out = visor::host::Event{visor::host::KeyUp{
                .code = body.code,
                .modifiers = unpack_modifiers(body.modifiers),
            }};
            return true;
        case kKindPointerMove:
            out = visor::host::Event{visor::host::PointerMove{
                .pos = visor::base::PointI{body.x, body.y},
            }};
            return true;
        case kKindPointerDown:
            out = visor::host::Event{visor::host::PointerDown{
                .button = body.code,
                .pos = visor::base::PointI{body.x, body.y},
                .modifiers = unpack_modifiers(body.modifiers),
            }};
            return true;
        case kKindPointerUp:
            out = visor::host::Event{visor::host::PointerUp{
                .button = body.code,
                .pos = visor::base::PointI{body.x, body.y},
            }};
            return true;
        case kKindPointerWheel:
            out = visor::host::Event{visor::host::PointerWheel{
                .dx = static_cast<std::int32_t>(static_cast<std::int16_t>(body.code)),
                .dy = static_cast<std::int32_t>(static_cast<std::int16_t>(body.y)),
                .pos = visor::base::PointI{0, 0},
                .modifiers = unpack_modifiers(body.modifiers),
            }};
            return true;
        default:
            return false;
    }
}

bool write_message(std::uint8_t* buffer, std::size_t capacity, Word word, const void* payload,
                   std::uint32_t length) noexcept {
    if (buffer == nullptr || capacity < sizeof(Header) + static_cast<std::size_t>(length)) {
        return false;
    }
    const Header header{
        .magic = kMagic,
        .type = static_cast<std::uint16_t>(word),
        .reserved = 0,
        .length = length,
    };
    std::memcpy(buffer, &header, sizeof(Header));
    if (length > 0 && payload != nullptr) {
        std::memcpy(buffer + sizeof(Header), payload, length);
    }
    return true;
}

WireDecoder::WireDecoder(std::uint8_t* sink, std::size_t sink_bytes) noexcept
    : sink_{sink}, sink_bytes_{sink_bytes} {}

void WireDecoder::take_header_byte(std::uint8_t byte) noexcept {
    // The header fills byte by byte through the struct's own storage, so
    // the wire layout and the struct layout stay one and the same without
    // a second copy.
    auto* raw = reinterpret_cast<std::uint8_t*>(&header_);
    raw[header_taken_] = byte;
    ++header_taken_;
    if (header_taken_ < sizeof(Header)) {
        return;
    }
    header_taken_ = 0;
    if (header_.magic != kMagic) {
        broken_ = true;
        return;
    }
    if (static_cast<std::size_t>(header_.length) > sink_bytes_) {
        // A message the sink cannot hold is a lie about the stream's own
        // frame size; refusing it whole beats clipping it quietly.
        broken_ = true;
        return;
    }
    word_ = static_cast<Word>(header_.type);
    payload_taken_ = 0;
    if (header_.length == 0) {
        ready_ = true;
        phase_ = Phase::Header;
    } else {
        phase_ = Phase::Payload;
    }
}

std::size_t WireDecoder::feed(const std::uint8_t* bytes, std::size_t count) noexcept {
    if (broken_ || ready_ || sink_ == nullptr) {
        return 0;
    }
    std::size_t consumed = 0;
    while (consumed < count && !broken_ && !ready_) {
        if (phase_ == Phase::Header) {
            take_header_byte(bytes[consumed]);
            ++consumed;
            continue;
        }
        // Payload: as many bytes as the message still owes and the chunk
        // still holds, one copy.
        const std::uint32_t owed = header_.length - payload_taken_;
        const std::size_t chunk = count - consumed;
        const std::size_t take = chunk < owed ? chunk : static_cast<std::size_t>(owed);
        std::memcpy(sink_ + payload_taken_, bytes + consumed, take);
        payload_taken_ += static_cast<std::uint32_t>(take);
        consumed += take;
        if (payload_taken_ == header_.length) {
            ready_ = true;
            phase_ = Phase::Header;
        }
    }
    return consumed;
}

void WireDecoder::take() noexcept {
    ready_ = false;
}

bool WireDecoder::message_ready() const noexcept {
    return ready_;
}

Word WireDecoder::word() const noexcept {
    return word_;
}

std::uint32_t WireDecoder::payload_length() const noexcept {
    return header_.length;
}

bool WireDecoder::broken() const noexcept {
    return broken_;
}

} // namespace vesper::session
