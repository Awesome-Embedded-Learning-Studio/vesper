#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct wl_buffer;
struct wl_callback;
struct wl_compositor;
struct wl_display;
struct wl_keyboard;
struct wl_pointer;
struct wl_registry;
struct wl_shm;
struct wl_shm_pool;
struct wl_surface;
struct wl_seat;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_wm_base;

namespace vesper::compositor {
class Server;
}

namespace vesper::testing {

/**
 * @brief The desk's own sparring partner: a minimal Wayland client the
 *        judgments drive against the server over a socketpair.
 *
 * In process, no filesystem, no display name -- the judgment lane needs
 * nothing but a socket pair and a server to adopt one half of it. The client
 * grows with the chapters: registry and sync first, then the shm pool, the
 * surface and the xdg toplevel, always speaking exactly the words the
 * chapter under test implements. It never judges anything itself; a
 * sparring partner takes the hits, the test scores them.
 */
class SparringClient {
  public:
    /// One global the registry announced: the interface name, the server's
    /// numeric name and the advertised version.
    struct Global {
        std::array<char, 32> interface{};
        std::uint32_t name = 0;
        std::uint32_t version = 0;
    };

    static constexpr std::size_t kGlobalCapacity = 16;
    /// How many buffers the goodbye can track; a judgment that would pass
    /// this asks for a bigger table, not for silent drops.
    static constexpr std::size_t kBufferCapacity = 16;

    SparringClient() noexcept = default;
    ~SparringClient();

    SparringClient(const SparringClient&) = delete;
    SparringClient& operator=(const SparringClient&) = delete;

    /// Takes ownership of the connected fd. On refusal the fd is closed
    /// either way, the same one-way handover the server's adopt_client draws.
    [[nodiscard]] bool connect_fd(int fd) noexcept;

    /// Closes the connection and every proxy; safe on an already-dead one.
    void disconnect() noexcept;

    /// Binds the registry, exchanges everything both sides have waiting and
    /// ends with the sync word answered, leaving the global table fresh.
    /// False when the connection died on the way.
    [[nodiscard]] bool survey_globals(compositor::Server& server) noexcept;

    /// The advertised version of a global the last survey saw, zero when the
    /// server never announced it.
    [[nodiscard]] std::uint32_t version_of(const char* interface) const noexcept;

    /// The global entry itself, null when the last survey saw none.
    [[nodiscard]] const Global* find_global(const char* interface) const noexcept;

    /// One both-directions exchange, repeated rounds times: whatever this
    /// side has queued goes out, the server serves it, whatever the server
    /// sent back lands. False when either side reports a dead connection.
    [[nodiscard]] bool settle(compositor::Server& server, int rounds = 1) noexcept;

    /// The guest's display, for the chapter-specific helpers that grow
    /// beside this class; null before connect_fd.
    [[nodiscard]] wl_display* display() const noexcept { return display_; }

    /// Whether the connection still carries no error. The refusal judgments
    /// read this after the exchange that should have killed them.
    [[nodiscard]] bool alive() const noexcept;

    /// Binds wl_shm using the last survey's table; a survey must have run.
    [[nodiscard]] bool bind_shm() noexcept;

    /// Binds wl_compositor the same way; surfaces come from it.
    [[nodiscard]] bool bind_compositor() noexcept;

    /// Binds xdg_wm_base the same way, answering pings as they come.
    [[nodiscard]] bool bind_wm_base() noexcept;

    /// Creates the surface's toplevel with a title and waits out the birth
    /// configure (the wait needs the server to serve it). auto_ack false
    /// leaves the configure unanswered on purpose: the unconfigured-buffer
    /// refusal runs on that lane.
    [[nodiscard]] bool make_toplevel(compositor::Server& server, const char* title,
                                     bool auto_ack = true) noexcept;

    /// The birth configure arrived (and was acked when auto_ack asked).
    [[nodiscard]] bool configured() const noexcept { return xdg_configured_; }

    /// The desk's close word arrived.
    [[nodiscard]] bool close_requested() const noexcept { return close_requested_; }

    /// Retires the shell pair (toplevel, then xdg_surface) the careful way.
    void retire_toplevel() noexcept;

    /// Binds wl_seat, waits for the capabilities word and asks for both
    /// hands the desk advertised.
    [[nodiscard]] bool bind_seat(compositor::Server& server) noexcept;

    // The recorded input, one entry per word the desk sent.
    struct KeyWord {
        std::uint32_t code = 0;
        bool pressed = false;
    };
    struct PointWord {
        std::int32_t x = 0;
        std::int32_t y = 0;
    };
    struct ButtonWord {
        std::uint32_t code = 0;
        bool pressed = false;
    };
    struct WheelWord {
        std::uint32_t axis = 0;
        std::int32_t value = 0;
    };
    static constexpr std::size_t kInputCapacity = 64;

    [[nodiscard]] std::size_t key_count() const noexcept { return key_count_; }
    [[nodiscard]] const KeyWord* key_at(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t motion_count() const noexcept { return motion_count_; }
    [[nodiscard]] const PointWord* motion_at(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t button_count() const noexcept { return button_count_; }
    [[nodiscard]] const ButtonWord* button_at(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t wheel_count() const noexcept { return wheel_count_; }
    [[nodiscard]] const WheelWord* wheel_at(std::size_t index) const noexcept;
    [[nodiscard]] int pointer_enters() const noexcept { return pointer_enters_; }
    [[nodiscard]] int pointer_leaves() const noexcept { return pointer_leaves_; }
    [[nodiscard]] int keyboard_enters() const noexcept { return keyboard_enters_; }
    [[nodiscard]] int keyboard_leaves() const noexcept { return keyboard_leaves_; }
    [[nodiscard]] std::uint32_t last_modifiers() const noexcept { return last_modifiers_; }
    [[nodiscard]] std::size_t keymap_bytes() const noexcept { return keymap_bytes_; }

    /// Creates one surface; null when the compositor was never bound.
    [[nodiscard]] wl_surface* make_surface() noexcept;

    /// The buffer handover set: attach, damage, commit. Damage with a
    /// non-positive extent is the protocol's "whole surface".
    void attach(wl_buffer* buffer) noexcept;
    void damage(int x, int y, int width, int height) noexcept;
    void commit() noexcept;

    /// Parks one frame callback on the surface; the done words arrive as
    /// frame_done_count().
    void request_frame() noexcept;

    /// How many frame callbacks the server has answered so far.
    [[nodiscard]] int frame_done_count() const noexcept { return frame_done_count_; }

    /// How many wl_buffer release words the server has sent so far, over
    /// every buffer this client made.
    [[nodiscard]] int release_count() const noexcept { return release_count_; }

    /// Creates a shared pool of bytes and maps it for the test's own pen.
    /// False when shm was never bound or the pool already stands.
    [[nodiscard]] bool make_pool(std::size_t bytes) noexcept;

    /// The pool's mapped words, the test's to fill; null before make_pool.
    [[nodiscard]] std::uint32_t* pool_words() const noexcept { return pool_words_; }

    /// Grows or shrinks the pool by the protocol's resize word.
    [[nodiscard]] bool resize_pool(std::size_t bytes) noexcept;

    /// Creates one buffer over the pool: offset in words, shape in pixels,
    /// stride in words, one of the WL_SHM_FORMAT_* words.
    [[nodiscard]] wl_buffer* make_buffer(std::size_t offset_words, int width, int height,
                                         std::size_t stride_words, std::uint32_t format) noexcept;

    /// Retires one buffer the protocol's own way and takes it off the
    /// goodbye list; destroying through the raw word would leave a stale
    /// pointer there for disconnect to trip on.
    void retire_buffer(wl_buffer* buffer) noexcept;

    /// Retires the surface the same careful way.
    void retire_surface() noexcept;

    // The listener hooks, public because the file's own listener tables are
    // the ones calling them; they are the client's own hands the same way
    // visor's Wiring members are.
    void record_global(std::uint32_t name, const char* interface, std::uint32_t version) noexcept;
    void note_sync_done() noexcept;
    void note_frame_done() noexcept;
    void note_release() noexcept;
    void note_configure(std::uint32_t serial) noexcept;
    void note_close() noexcept;
    // The seat listeners' hands: the proxies they create land here, the
    // words they receive land in the records below.
    /// Asks to draw the pointer's own cursor, the word weston-terminal
    /// sends whenever it wants an I-beam over text. The desk must receive
    /// it and keep its own pointer whole.
    void ask_own_cursor() noexcept;

    [[nodiscard]] wl_pointer* pointer_proxy() const noexcept { return pointer_; }
    [[nodiscard]] wl_keyboard* keyboard_proxy() const noexcept { return keyboard_; }
    void adopt_pointer(wl_pointer* pointer) noexcept { pointer_ = pointer; }
    void adopt_keyboard(wl_keyboard* keyboard) noexcept { keyboard_ = keyboard; }
    void record_motion(std::int32_t x, std::int32_t y) noexcept;
    void record_button(std::uint32_t code, bool pressed) noexcept;
    void record_wheel(std::uint32_t axis, std::int32_t value) noexcept;
    void record_key(std::uint32_t code, bool pressed) noexcept;
    void note_modifiers(std::uint32_t mask) noexcept;
    void note_keymap(std::uint32_t bytes) noexcept;
    void note_pointer_enter(std::int32_t x, std::int32_t y) noexcept;
    void note_pointer_leave() noexcept;
    void note_keyboard_enter() noexcept;
    void note_keyboard_leave() noexcept;

  private:
    /// Reads whatever arrived without waiting and runs every buffered event.
    /// False when the connection errored or fell dead.
    [[nodiscard]] bool pump() noexcept;

    wl_display* display_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    wl_surface* surface_ = nullptr;
    wl_shm* shm_ = nullptr;
    xdg_wm_base* wm_base_ = nullptr;
    xdg_surface* xdg_surface_ = nullptr;
    xdg_toplevel* toplevel_ = nullptr;
    bool xdg_auto_ack_ = true;
    wl_seat* seat_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    wl_shm_pool* pool_ = nullptr;
    int pool_fd_ = -1;
    std::uint32_t* pool_words_ = nullptr;
    std::size_t pool_bytes_ = 0;
    std::array<wl_buffer*, kBufferCapacity> buffers_{};
    std::size_t buffer_count_ = 0;
    int frame_done_count_ = 0;
    int release_count_ = 0;
    bool xdg_configured_ = false;
    bool close_requested_ = false;
    std::array<KeyWord, kInputCapacity> keys_{};
    std::size_t key_count_ = 0;
    std::array<PointWord, kInputCapacity> motions_{};
    std::size_t motion_count_ = 0;
    std::array<ButtonWord, kInputCapacity> buttons_{};
    std::size_t button_count_ = 0;
    std::array<WheelWord, 8> wheels_{};
    std::size_t wheel_count_ = 0;
    int pointer_enters_ = 0;
    int pointer_leaves_ = 0;
    int keyboard_enters_ = 0;
    int keyboard_leaves_ = 0;
    std::uint32_t last_modifiers_ = 0;
    std::size_t keymap_bytes_ = 0;
    std::array<Global, kGlobalCapacity> globals_{};
    std::size_t global_count_ = 0;
    bool synced_ = false;
};

} // namespace vesper::testing
