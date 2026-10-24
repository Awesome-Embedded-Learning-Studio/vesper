#include "cases.hpp"
#include "testing.hpp"

#include "session/wire.hpp"

#include "visor/base/geometry/point.hpp"
#include "visor/bindings/events.hpp"
#include "visor/bindings/events/key_words.hpp"
#include "visor/bindings/events/modifiers.hpp"
#include "visor/bindings/events/pointer_words.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

using vesper::session::decode_event;
using vesper::session::encode_event;
using vesper::session::EventBody;
using vesper::session::Header;
using vesper::session::HelloBody;
using vesper::session::WireDecoder;
using vesper::session::Word;
using vesper::session::write_message;
using visor::host::Event;
using visor::host::KeyDown;
using visor::host::KeyUp;
using visor::host::Modifiers;
using visor::host::PointerDown;
using visor::host::PointerMove;
using visor::host::PointerUp;
using visor::host::PointerWheel;

constexpr Modifiers kPlain{};
constexpr Modifiers kFull{.shift = true, .ctrl = true, .alt = true, .super = true};

[[nodiscard]] bool same_modifiers(const Modifiers& a, const Modifiers& b) noexcept {
    return a.shift == b.shift && a.ctrl == b.ctrl && a.alt == b.alt && a.super == b.super;
}

/// Field-by-field equality: the event structs carry no operator== of their
/// own (the core compares, never decodes, and so never needed one), and a
/// judgment should not lean on adding one upstream.
[[nodiscard]] bool same_event(const Event& left, const Event& right) noexcept {
    if (left.index() != right.index()) {
        return false;
    }
    switch (left.index()) {
        case 0: {
            const auto& a = std::get<KeyDown>(left);
            const auto& b = std::get<KeyDown>(right);
            return a.code == b.code && same_modifiers(a.modifiers, b.modifiers);
        }
        case 1: {
            const auto& a = std::get<KeyUp>(left);
            const auto& b = std::get<KeyUp>(right);
            return a.code == b.code && same_modifiers(a.modifiers, b.modifiers);
        }
        case 2: {
            const auto& a = std::get<PointerMove>(left);
            const auto& b = std::get<PointerMove>(right);
            return a.pos.x == b.pos.x && a.pos.y == b.pos.y;
        }
        case 3: {
            const auto& a = std::get<PointerDown>(left);
            const auto& b = std::get<PointerDown>(right);
            return a.button == b.button && a.pos.x == b.pos.x && a.pos.y == b.pos.y &&
                   same_modifiers(a.modifiers, b.modifiers);
        }
        case 4: {
            const auto& a = std::get<PointerUp>(left);
            const auto& b = std::get<PointerUp>(right);
            return a.button == b.button && a.pos.x == b.pos.x && a.pos.y == b.pos.y;
        }
        default: {
            const auto& a = std::get<PointerWheel>(left);
            const auto& b = std::get<PointerWheel>(right);
            return a.dx == b.dx && a.dy == b.dy && a.pos.x == b.pos.x && a.pos.y == b.pos.y &&
                   same_modifiers(a.modifiers, b.modifiers);
        }
    }
}

/// One event's round trip through the flat body; the wheel is the named
/// exception, its position spent for the deltas.
[[nodiscard]] bool round_trips(const Event& event) noexcept {
    const EventBody body = encode_event(event);
    Event decoded = Event{KeyDown{}};
    if (!decode_event(body, decoded)) {
        return false;
    }
    return same_event(decoded, event);
}

} // namespace

void run_wire_tests() {
    // The event round trip: all six alternatives survive, field for field,
    // with the wheel the one exception the header names.
    {
        VESPER_CHECK(round_trips(Event{KeyDown{.code = 30, .modifiers = kFull}}));
        VESPER_CHECK(round_trips(Event{KeyUp{.code = 14, .modifiers = kPlain}}));
        VESPER_CHECK(round_trips(Event{PointerMove{.pos = {-4, 130}}}));
        VESPER_CHECK(
            round_trips(Event{PointerDown{.button = 2, .pos = {40, 12}, .modifiers = kFull}}));
        VESPER_CHECK(round_trips(Event{PointerUp{.button = 3, .pos = {9, 9}}}));
        const Event wheel =
            Event{PointerWheel{.dx = -2, .dy = 1, .pos = {0, 0}, .modifiers = kPlain}};
        VESPER_CHECK(round_trips(wheel));

        // The modifier bits pack and unpack through all sixteen combos.
        for (unsigned bits = 0; bits < 16U; ++bits) {
            const Modifiers packed{
                .shift = (bits & 1U) != 0,
                .ctrl = (bits & 2U) != 0,
                .alt = (bits & 4U) != 0,
                .super = (bits & 8U) != 0,
            };
            const EventBody body = encode_event(Event{KeyDown{.code = 1, .modifiers = packed}});
            Event back = Event{KeyUp{}};
            VESPER_CHECK(decode_event(body, back));
            VESPER_CHECK_EQ(std::get<KeyDown>(back).modifiers.shift, packed.shift);
            VESPER_CHECK_EQ(std::get<KeyDown>(back).modifiers.ctrl, packed.ctrl);
            VESPER_CHECK_EQ(std::get<KeyDown>(back).modifiers.alt, packed.alt);
            VESPER_CHECK_EQ(std::get<KeyDown>(back).modifiers.super, packed.super);
        }

        // A kind byte no alternative names is refused, not misread.
        Event untouched = Event{KeyDown{.code = 7}};
        VESPER_CHECK(!decode_event(EventBody{.kind = 99}, untouched));
        VESPER_CHECK_EQ(std::get<KeyDown>(untouched).code, 7U);
    }

    // The write side: one message whole into the buffer, and a buffer one
    // byte short refuses without writing a thing.
    {
        std::array<std::uint8_t, 128> buffer{};
        const HelloBody hello{.name = "block", .width = 64, .height = 48};
        VESPER_CHECK(
            write_message(buffer.data(), buffer.size(), Word::Hello, &hello, sizeof(hello)));
        const auto* header = reinterpret_cast<const Header*>(buffer.data());
        VESPER_CHECK_EQ(header->magic, vesper::session::kMagic);
        VESPER_CHECK_EQ(header->type, static_cast<std::uint16_t>(Word::Hello));
        VESPER_CHECK_EQ(header->length, sizeof(HelloBody));
        VESPER_CHECK_EQ(std::memcmp(buffer.data() + sizeof(Header), &hello, sizeof(hello)), 0);
        VESPER_CHECK(!write_message(buffer.data(), sizeof(Header) + sizeof(HelloBody) - 1,
                                    Word::Hello, &hello, sizeof(hello)));
    }

    // The decoder, whole message at once: a Hello lands in the sink, word
    // and length read true, and taking clears the way for the next one.
    {
        std::array<std::uint8_t, sizeof(Header) + sizeof(HelloBody)> line{};
        const HelloBody hello{.name = "block", .width = 64, .height = 48};
        VESPER_CHECK(write_message(line.data(), line.size(), Word::Hello, &hello, sizeof(hello)));
        std::array<std::uint8_t, 64> sink{};
        WireDecoder decoder{sink.data(), sink.size()};
        VESPER_CHECK_EQ(decoder.feed(line.data(), line.size()), line.size());
        VESPER_CHECK(decoder.message_ready());
        VESPER_CHECK_EQ(static_cast<int>(decoder.word()), static_cast<int>(Word::Hello));
        VESPER_CHECK_EQ(decoder.payload_length(), sizeof(HelloBody));
        const auto* body = reinterpret_cast<const HelloBody*>(sink.data());
        VESPER_CHECK_EQ(body->width, 64U);
        VESPER_CHECK_EQ(body->height, 48U);
        VESPER_CHECK_EQ(std::memcmp(body->name, "block", 5), 0);
        decoder.take();
        VESPER_CHECK(!decoder.message_ready());
    }

    // Split every which way: a message fed byte by byte, and another fed
    // in two uneven halves, both land whole -- the socket owes the decoder
    // no favors about chunk boundaries.
    {
        std::array<std::uint8_t, sizeof(Header) + 8> line{};
        const EventBody press{.kind = vesper::session::kKindKeyDown, .code = 28};
        VESPER_CHECK(write_message(line.data(), line.size(), Word::Event, &press, sizeof(press)));
        std::array<std::uint8_t, 64> sink{};
        WireDecoder byte_by_byte{sink.data(), sink.size()};
        for (const std::uint8_t one : line) {
            VESPER_CHECK_EQ(byte_by_byte.feed(&one, 1), std::size_t{1});
        }
        VESPER_CHECK(byte_by_byte.message_ready());
        VESPER_CHECK_EQ(static_cast<int>(byte_by_byte.word()), static_cast<int>(Word::Event));
        VESPER_CHECK_EQ(sink[0], vesper::session::kKindKeyDown);

        const std::size_t split = 3; // a split inside the header
        WireDecoder halves{sink.data(), sink.size()};
        VESPER_CHECK_EQ(static_cast<std::size_t>(halves.feed(line.data(), split)), split);
        VESPER_CHECK(!halves.message_ready());
        VESPER_CHECK_EQ(halves.feed(line.data() + split, line.size() - split), line.size() - split);
        VESPER_CHECK(halves.message_ready());
        VESPER_CHECK_EQ(sink[2], 28U % 256U); // the code's low byte, little-endian
    }

    // Two messages back to back in one chunk: the first feed stops at the
    // first message's end, the remainder is the caller's to re-feed.
    {
        std::array<std::uint8_t, 64> line{};
        const std::size_t bye_size = sizeof(Header);
        VESPER_CHECK(write_message(line.data(), line.size(), Word::Bye, nullptr, 0));
        const EventBody release{.kind = vesper::session::kKindKeyUp, .code = 28};
        VESPER_CHECK(write_message(line.data() + bye_size, line.size() - bye_size, Word::Event,
                                   &release, sizeof(release)));
        const std::size_t total = bye_size + sizeof(Header) + sizeof(release);
        std::array<std::uint8_t, 64> sink{};
        WireDecoder decoder{sink.data(), sink.size()};
        const auto first = static_cast<std::size_t>(decoder.feed(line.data(), total));
        VESPER_CHECK_EQ(first, bye_size);
        VESPER_CHECK(decoder.message_ready());
        VESPER_CHECK_EQ(static_cast<int>(decoder.word()), static_cast<int>(Word::Bye));
        VESPER_CHECK_EQ(decoder.payload_length(), 0U);
        decoder.take();
        const auto second =
            static_cast<std::size_t>(decoder.feed(line.data() + first, total - first));
        VESPER_CHECK_EQ(second, total - first);
        VESPER_CHECK_EQ(static_cast<int>(decoder.word()), static_cast<int>(Word::Event));
    }

    // The refusal line: a wrong magic breaks the stream once and forever,
    // a payload over the sink breaks it too, and a broken decoder eats
    // nothing and says nothing.
    {
        std::array<std::uint8_t, sizeof(Header)> bad{};
        VESPER_CHECK(write_message(bad.data(), bad.size(), Word::Bye, nullptr, 0));
        bad[0] = 0xEE; // the magic's low byte, corrupted
        std::array<std::uint8_t, 16> sink{};
        WireDecoder decoder{sink.data(), sink.size()};
        // The lie surfaces only when the header completes, so its own bytes
        // are consumed on the way to the verdict.
        VESPER_CHECK_EQ(decoder.feed(bad.data(), bad.size()), sizeof(Header));
        VESPER_CHECK(decoder.broken());
        VESPER_CHECK_EQ(decoder.feed(bad.data(), bad.size()), std::size_t{0});

        std::array<std::uint8_t, sizeof(Header) + 8> oversize{};
        const EventBody body{};
        VESPER_CHECK(write_message(oversize.data(), oversize.size(), Word::Event, &body, 8));
        std::array<std::uint8_t, 4> tiny{};
        WireDecoder small{tiny.data(), tiny.size()};
        VESPER_CHECK_EQ(small.feed(oversize.data(), oversize.size()), sizeof(Header));
        VESPER_CHECK(small.broken());
    }
}
