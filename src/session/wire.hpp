#pragma once

#include <cstddef>
#include <cstdint>

#include "visor/bindings/events.hpp"

namespace vesper::session {

/**
 * @file
 * @brief The desk's private channel to a windowed child program (D6a's
 *        first draft).
 *
 * One socket carries both directions. The child speaks Hello (a name and a
 * cell size), Frame (one whole XRGB8888 image for its window) and Bye; the
 * desk speaks Event (one input word in flat form) and Close. Every message
 * is a twelve-byte header -- magic, type, reserved, length -- followed by
 * that many payload bytes. The struct layouts below are the wire layouts:
 * both ends are little-endian machines in every lane this channel serves,
 * and the one place that could differ (a future big-endian host) would
 * rebuild the words anyway, the same stance the frame words themselves
 * take.
 */

/// "VSPR" as a little-endian word; every message opens with it.
inline constexpr std::uint32_t kMagic = 0x5653'5052U;

/// The words the channel carries, one number each way.
enum class Word : std::uint16_t {
    Hello = 1,  // child to desk: a name and the cell size, HelloBody
    Frame = 2,  // child to desk: one whole frame, width*height XRGB words
    Bye = 3,    // child to desk: the child is leaving
    Event = 10, // desk to child: one input word, EventBody
    Close = 11, // desk to child: the desk closed the window
};

/// One message header, twelve bytes on the wire.
struct Header {
    std::uint32_t magic = kMagic;
    std::uint16_t type = 0;
    std::uint16_t reserved = 0;
    std::uint32_t length = 0;
};
static_assert(sizeof(Header) == 12);

/// The Hello payload: a padded name and the cell size the frames will
/// carry. The size is advisory at draft stage -- the desk lays the window
/// out at exactly this size.
struct HelloBody {
    char name[24] = {};
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t padding = 0;
};
static_assert(sizeof(HelloBody) == 32);

/// The flat form of one input word: a kind byte naming the variant
/// alternative (0 key down, 1 key up, 2 move, 3 pointer down, 4 pointer
/// up, 5 wheel), the four modifier bits, and the fields every alternative
/// reuses -- a code for the key words and the pointer buttons, the x/y
/// pair for the pointer position. The wheel spends its fields on the two
/// detent deltas and drops its position: eight bytes hold three of the
/// wheel's four numbers, and where the wheel sat matters less to a
/// scrolling child than how far it turned.
struct EventBody {
    std::uint8_t kind = 0;
    std::uint8_t modifiers = 0; // shift 1, ctrl 2, alt 4, super 8
    std::uint16_t code = 0;
    std::int16_t x = 0;
    std::int16_t y = 0;
};
static_assert(sizeof(EventBody) == 8);

/// The kinds, spelled once for both ends.
inline constexpr std::uint8_t kKindKeyDown = 0;
inline constexpr std::uint8_t kKindKeyUp = 1;
inline constexpr std::uint8_t kKindPointerMove = 2;
inline constexpr std::uint8_t kKindPointerDown = 3;
inline constexpr std::uint8_t kKindPointerUp = 4;
inline constexpr std::uint8_t kKindPointerWheel = 5;

/// One event to its flat body; every alternative fits without loss.
[[nodiscard]] EventBody encode_event(const visor::host::Event& event) noexcept;

/// One flat body back to the event it names; a body whose kind is not one
/// of the six answers false and leaves the event untouched (the honest
/// refusal of an unknown word, not a crash on a stranger's stream).
[[nodiscard]] bool decode_event(EventBody body, visor::host::Event& out) noexcept;

/// Writes one message -- header then payload -- into the caller's buffer;
/// false when the buffer cannot hold it whole, and nothing is written. The
/// child's frames and the desk's events both go out through this one door,
/// so the wire has exactly one spelling per direction.
[[nodiscard]] bool write_message(std::uint8_t* buffer, std::size_t capacity, Word word,
                                 const void* payload, std::uint32_t length) noexcept;

/**
 * @brief A streaming message decoder: bytes in, one message at a time out.
 *
 * The desk reads its socket in whatever chunks arrive and feeds each chunk
 * in; the decoder keeps the header it is building and the payload it is
 * filling between calls, so a message split across any number of reads
 * lands whole. Each message's payload is written into a sink the caller
 * provides -- the frame sink is the window's own offscreen image, so no
 * second copy exists -- and a header whose magic or length misbehaves puts
 * the decoder into a broken state it reports and never leaves: a channel
 * that said one wrong thing is a channel to hang up on, not to resync.
 */
class WireDecoder {
  public:
    /// @param sink       where payload bytes land; must outlive the message
    /// @param sink_bytes the sink's capacity; a longer payload breaks the
    ///                   decoder instead of overrunning it
    WireDecoder(std::uint8_t* sink, std::size_t sink_bytes) noexcept;

    /// Feeds one chunk of bytes, returning how many were consumed. The
    /// caller re-feeds the remainder (a datagram boundary, a read buffer's
    /// tail) after taking any ready message; within a stream the return is
    /// the chunk's size until a message ends mid-chunk.
    std::size_t feed(const std::uint8_t* bytes, std::size_t count) noexcept;

    /// Whether a full message is waiting to be taken; taking is reading
    /// word() and the sink, then the next message begins on the next feed.
    [[nodiscard]] bool message_ready() const noexcept;

    /// The ready message's word.
    [[nodiscard]] Word word() const noexcept;

    /// The ready message's payload length (the sink holds this many bytes).
    [[nodiscard]] std::uint32_t payload_length() const noexcept;

    /// Takes the ready message: the next feed begins the next message.
    /// Taking with none ready is a no-op.
    void take() noexcept;

    /// Whether the stream broke (bad magic, or a payload over the sink).
    /// A broken decoder consumes nothing and produces nothing ever again.
    [[nodiscard]] bool broken() const noexcept;

  private:
    void take_header_byte(std::uint8_t byte) noexcept;

    std::uint8_t* sink_ = nullptr;
    std::size_t sink_bytes_ = 0;
    enum class Phase : std::uint8_t { Header, Payload };

    Header header_{};
    std::size_t header_taken_ = 0;
    std::uint32_t payload_taken_ = 0;
    Phase phase_ = Phase::Header;
    Word word_ = Word::Bye;
    bool ready_ = false;
    bool broken_ = false;
};

} // namespace vesper::session
