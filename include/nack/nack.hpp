/*
 * libnack - a cell console for roguelikes, in the spirit of libtcod and
 * BearLibTerminal.
 *
 * The whole API is a grid of cells you draw glyphs and tiles into. libnack
 * owns the window, the graphics context and the renderer; none of that is
 * exposed, because none of it is what a roguelike wants to think about.
 *
 *     nack::app app;
 *     app.set_title("my roguelike");
 *
 *     while (!app.should_close()) {
 *         while (auto ev = app.poll()) { ... }
 *         app.console().clear();
 *         app.console().print(1, 1, nack::white, nack::black, "@ you");
 *         app.present();
 *     }
 *
 * This is the only header: there is no C API behind it any more, so every
 * operation that takes a console, a tileset or the app is a method on that
 * object. The one exception is the handful of free functions - nack::print,
 * nack::clear, nack::put and their neighbours - which draw on the app's root
 * console without making you fetch it first; write a roguelike that never
 * uses an offscreen console and you may never touch a console object by
 * name.
 *
 *   - Consoles and tilesets free themselves, as does the app itself.
 *   - Events arrive as a std::variant, so reading the wrong arm of the union
 *     is a compile error rather than a convention in a comment.
 *   - poll() returns std::optional instead of a bool and an out-parameter,
 *     and sizes come back as structured bindings.
 *   - Keys and modifiers are scoped enums; mods are a real bitmask type
 *     rather than a bare uint32_t.
 *   - print() takes a {fmt} format string rather than C varargs.
 *
 * Exceptions are used only where construction fails. If you build with them
 * off, every constructor has a try_create counterpart returning
 * std::optional, and the throwing paths abort with the message instead.
 */
#ifndef NACK_HPP_INCLUDED
#define NACK_HPP_INCLUDED

/*
 * C++20, and checked rather than assumed: below it {fmt} drops from rejecting
 * a bad format string at compile time to raising at run time, quietly, which
 * is the one thing this header is here for. MSVC reports __cplusplus as
 * 199711L unless it is passed /Zc:__cplusplus, so ask _MSVC_LANG where it
 * exists - otherwise a perfectly good C++20 build would fail this.
 */
#if defined(_MSVC_LANG)
static_assert(_MSVC_LANG >= 202002L,
              "<nack/nack.hpp> requires C++20 (MSVC also needs "
              "/Zc:__cplusplus)");
#else
static_assert(__cplusplus >= 202002L, "<nack/nack.hpp> requires C++20");
#endif

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <fmt/format.h>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  define NACK_HPP_EXCEPTIONS 1
#  include <stdexcept>
#else
#  define NACK_HPP_EXCEPTIONS 0
#endif

/* Opaque: only the engine, not this header, ever sees what one holds. */
struct nack_tileset;
struct nack_console;

namespace nack {

/* ------------------------------------------------------------------------ */
/* Errors                                                                    */
/* ------------------------------------------------------------------------ */

#if NACK_HPP_EXCEPTIONS
/* Thrown when something that must succeed to be usable did not. */
class error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
#endif

namespace detail {

[[noreturn]] void fail(const std::string &what);

}  // namespace detail

/* ------------------------------------------------------------------------ */
/* Colour                                                                    */
/* ------------------------------------------------------------------------ */

struct color {
    std::uint8_t r, g, b, a;
};

constexpr bool operator==(color a, color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

constexpr bool operator!=(color a, color b) { return !(a == b); }

constexpr color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return color{ r, g, b, 255 };
}

constexpr color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                     std::uint8_t a)
{
    return color{ r, g, b, a };
}

inline constexpr color black     = rgb(0, 0, 0);
inline constexpr color white     = rgb(255, 255, 255);
inline constexpr color grey      = rgb(128, 128, 128);
inline constexpr color dark_grey = rgb(64, 64, 64);
inline constexpr color red       = rgb(200, 60, 60);
inline constexpr color green     = rgb(60, 200, 90);
inline constexpr color blue      = rgb(70, 110, 220);
inline constexpr color yellow    = rgb(230, 200, 70);
inline constexpr color cyan      = rgb(80, 200, 220);
inline constexpr color magenta   = rgb(200, 80, 200);
inline constexpr color orange    = rgb(230, 140, 50);
inline constexpr color brown     = rgb(140, 100, 60);
inline constexpr color none      = rgba(0, 0, 0, 0);

/* ------------------------------------------------------------------------ */
/* Keys and modifiers                                                        */
/* ------------------------------------------------------------------------ */

/*
 * Physical keys, by USB HID usage code: where the key is, not what is on it.
 * delete_key carries a suffix because `delete` is a keyword; nothing else in
 * the list collides. The values match the engine's own internal enum, which
 * key_name() and every key event rely on - they are part of what makes this
 * a stable identifier, not an implementation detail to keep in sync by hand.
 */
enum class key : int {
    unknown = 0,

    a = 4, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w,
    x, y, z,

    num1 = 30, num2, num3, num4, num5, num6, num7, num8, num9, num0,

    enter = 40, escape, backspace, tab, space, minus, equal, left_bracket,
    right_bracket, backslash, non_us_hash, semicolon, apostrophe, grave,
    comma, period, slash, caps_lock,

    f1 = 58, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,

    print_screen = 70, scroll_lock, pause, insert, home, page_up,
    delete_key, end, page_down, right, left, down, up,

    num_lock = 83, kp_divide, kp_multiply, kp_subtract, kp_add, kp_enter,
    kp_1, kp_2, kp_3, kp_4, kp_5, kp_6, kp_7, kp_8, kp_9,
    kp_0 = 98, kp_decimal, non_us_backslash, application,
    kp_equal = 103,

    f13 = 104, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24,

    menu = 118,
    mute = 127, volume_up, volume_down,

    left_ctrl = 224, left_shift, left_alt, left_super,
    right_ctrl, right_shift, right_alt, right_super
};

std::string_view key_name(key which);

/*
 * A set of held modifiers.
 *
 * GCC's -Wshadow flags mod::none against the colour above of the same name,
 * even though a scoped enum's enumerators are never found by unqualified
 * lookup - there is nothing here either name could actually be confused
 * with. Silenced locally rather than renaming either "none": both are the
 * obvious, already-public spelling for their own type's empty value.
 */
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wshadow"
#endif
enum class mod : std::uint32_t {
    none      = 0,
    shift     = 1u << 0,
    ctrl      = 1u << 1,
    alt       = 1u << 2,
    super     = 1u << 3,   /* Windows key / Command */
    caps_lock = 1u << 4,
    num_lock  = 1u << 5
};
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

constexpr mod operator|(mod a, mod b)
{
    return static_cast<mod>(static_cast<std::uint32_t>(a) |
                            static_cast<std::uint32_t>(b));
}

constexpr mod operator&(mod a, mod b)
{
    return static_cast<mod>(static_cast<std::uint32_t>(a) &
                            static_cast<std::uint32_t>(b));
}

constexpr mod &operator|=(mod &a, mod b) { return a = a | b; }

/* True when every modifier in `wanted` is held. */
constexpr bool holds(mod set, mod wanted) { return (set & wanted) == wanted; }

constexpr bool any(mod set) { return set != mod::none; }

enum class mouse_button : int { left = 0, right, middle, x1, x2 };

/* ------------------------------------------------------------------------ */
/* Events                                                                    */
/* ------------------------------------------------------------------------ */

struct quit_event {};
struct wakeup_event {};

struct key_event {
    nack::key key;
    nack::mod mods;
    bool repeat;
};

/*
 * Text that has already been through the platform's dead-key and IME
 * handling. The bytes live in the event itself, so text() is valid for as
 * long as the event is and costs no allocation.
 */
struct text_event {
    char utf8[32];
    std::string_view text() const { return std::string_view(utf8); }
};

struct mouse_move_event {
    int x, y;          /* cell under the pointer                       */
    int px, py;        /* pixel within the console area                */
    int dx, dy;        /* cell delta since the last motion event       */
    nack::mod mods;
};

struct mouse_button_event {
    int x, y;          /* cell under the pointer                       */
    int px, py;        /* pixel within the console area                */
    nack::mouse_button button;
    bool down;         /* true on press, false on release              */
    int clicks;        /* 1 single, 2 double, ... (press only)         */
    nack::mod mods;
};

struct scroll_event {
    double dx, dy;     /* positive dy scrolls content up               */
    nack::mod mods;
    bool precise;      /* true for trackpads and high-resolution wheels */
};

struct resize_event {
    int columns, rows;
};

/* Focus gained or lost. */
struct focus_event {
    bool focused;
};

using event = std::variant<quit_event, key_event, text_event,
                           mouse_move_event, mouse_button_event, scroll_event,
                           resize_event, focus_event, wakeup_event>;

/* ------------------------------------------------------------------------ */
/* Consoles                                                                  */
/* ------------------------------------------------------------------------ */

struct cell {
    std::uint32_t glyph;
    ::nack_tileset *tileset;   /* null means the font */
    nack::color fg, bg;
};

enum class layout : int { cp437 = 0, row_major, tcod };

class console;
class tileset;

/*
 * A console someone else owns: the root, or one held by a console object.
 * Copying it copies the reference, never the cells, which is what makes a
 * drawing helper work on either - `void draw_panel(nack::console_view c)`
 * takes the root and an offscreen console alike.
 */
class console_view {
public:
    console_view() = default;

    friend console_view root();

    ::nack_console *get() const { return handle; }
    explicit operator bool() const { return handle != nullptr; }

    std::pair<int, int> size() const;
    int columns() const { return size().first; }
    int rows() const { return size().second; }

    void clear() const;
    void clear(color fg, color bg) const;

    void put(int x, int y, std::uint32_t codepoint, color fg, color bg) const;
    void put_tile(int x, int y, tileset &tiles, int index, color tint,
                  color bg) const;

    void set_glyph(int x, int y, std::uint32_t codepoint) const;
    void set_fg(int x, int y, color fg) const;
    void set_bg(int x, int y, color bg) const;
    cell at(int x, int y) const;

    /*
     * Text as it stands. The arguments are in the order the engine uses:
     * position, then colours, then the text. Returns the cells written.
     */
    int print(int x, int y, color fg, color bg, std::string_view text) const;

    /*
     * The same, formatted with {fmt}.
     *
     * When the format string and the arguments disagree, {fmt} says so
     * where the call is written: its checking constructor is consteval, so
     * the mismatch is a compile error rather than something read off the
     * stack at run time, which is what C varargs would have done. This is
     * the reason the header asks for C++20 - below C++20 {fmt} silently
     * drops to checking at run time instead.
     *
     * This overload takes at least one argument, which is what keeps
     * print(x, y, fg, bg, some_runtime_string) unambiguous and working: a
     * runtime string is not a format string and is printed as it stands.
     */
    template <class... Args,
              class = std::enable_if_t<(sizeof...(Args) > 0)>>
    int print(int x, int y, color fg, color bg,
              fmt::format_string<Args...> spec, Args &&...args) const
    {
        return print(x, y, fg, bg,
                     fmt::format(spec, std::forward<Args>(args)...));
    }

    int print_wrapped(int x, int y, int width, int height, color fg,
                      color bg, std::string_view text) const;

    template <class... Args,
              class = std::enable_if_t<(sizeof...(Args) > 0)>>
    int print_wrapped(int x, int y, int width, int height, color fg, color bg,
                      fmt::format_string<Args...> spec, Args &&...args) const
    {
        return print_wrapped(x, y, width, height, fg, bg,
                             fmt::format(spec, std::forward<Args>(args)...));
    }

    /* Rows the text would take, without drawing any of it. */
    int measure_wrapped(int width, std::string_view text) const
    {
        return print_wrapped(0, 0, width, 0, white, black, text);
    }

    void fill(int x, int y, int width, int height, std::uint32_t codepoint,
              color fg, color bg) const;

    void draw_box(int x, int y, int width, int height, color fg, color bg,
                  const char *title = nullptr) const;

    /* Copies this console onto another. Alphas below 1 blend. */
    void blit_to(console_view dst, int dst_x, int dst_y, int src_x = 0,
                 int src_y = 0, int width = 0, int height = 0,
                 float fg_alpha = 1.0f, float bg_alpha = 1.0f) const;

protected:
    explicit console_view(::nack_console *console) : handle(console) {}

    ::nack_console *handle = nullptr;
};

/* A console this object owns and frees. Move-only: it is a handle. */
class console : public console_view {
public:
    console(int columns, int rows);
    static std::optional<console> try_create(int columns, int rows);

    console(const console &) = delete;
    console &operator=(const console &) = delete;

    console(console &&other) noexcept
    {
        handle = std::exchange(other.handle, nullptr);
    }

    console &operator=(console &&other) noexcept;

    ~console();

    bool resize(int columns, int rows);

private:
    explicit console(::nack_console *raw) : console_view(raw) {}
};

/* ------------------------------------------------------------------------ */
/* Tilesets                                                                  */
/* ------------------------------------------------------------------------ */

class tileset {
public:
    /* A tileset image on disk: PNG or JPEG, decided by its contents. */
    tileset(const char *path, int tile_width, int tile_height,
            nack::layout arrangement);
    tileset(const void *data, std::size_t size, int tile_width,
            int tile_height, nack::layout arrangement);

    tileset(const tileset &) = delete;
    tileset &operator=(const tileset &) = delete;

    tileset(tileset &&other) noexcept
        : handle(std::exchange(other.handle, nullptr)) {}

    tileset &operator=(tileset &&other) noexcept;

    ~tileset();

    ::nack_tileset *get() const { return handle; }

    struct dimensions { int width, height, count; };
    dimensions size() const;

    bool map(std::uint32_t codepoint, int index);
    bool map_range(std::uint32_t first, std::uint32_t last, int first_index);

private:
    ::nack_tileset *handle = nullptr;
};

/* ------------------------------------------------------------------------ */
/* The application                                                           */
/* ------------------------------------------------------------------------ */

/* How the console is fitted to the window when the two do not match. */
enum class scaling : int {
    integer = 0,  /* whole-number zoom, letterboxed: always crisp */
    fit,          /* largest fit preserving aspect, letterboxed   */
    stretch       /* fill the window, ignoring aspect             */
};

struct config {
    const char *title = "libnack";
    int columns = 80, rows = 50;

    /* Path to a tileset image. Null uses the built-in 8x8 font, so a game
     * can start without shipping any assets. */
    const char *tileset = nullptr;
    int tile_width = 0, tile_height = 0;
    nack::layout tileset_layout = layout::cp437;

    nack::scaling scaling = scaling::integer;
    nack::color letterbox = black;   /* colour of the bars around the console */

    bool vsync = true;
    bool resizable = true;
    bool fullscreen = false;

    /*
     * When set, resizing the window changes the console's dimensions and
     * produces a resize_event, rather than scaling a fixed grid. This is
     * what you want if the game reflows its layout.
     */
    bool auto_resize = false;

    /* Initial window size, as a multiple of the console's pixel size.
     * 0 picks the largest whole multiple that fits the monitor. */
    int window_scale = 0;
};

inline config default_config() { return config{}; }

/*
 * The most recent failure from anything in this namespace, or an empty view
 * if the last call that could fail did not. Exists mainly for the moment
 * before an app object exists to ask - app::try_create failing is the one
 * place there is nothing else to ask.
 */
std::string_view last_error();

/* The console the window shows. Valid only while an app is alive. */
console_view root();

/*
 * Owns the library's lifetime: constructing one starts the window and
 * renderer, destroying one tears them down. There can only be one at a time,
 * because the engine underneath is a singleton.
 */
class app {
public:
    explicit app(const config &settings);
    app() : app(default_config()) {}

    static std::optional<app> try_create(const config &settings);

    app(const app &) = delete;
    app &operator=(const app &) = delete;

    app(app &&other) noexcept : active(std::exchange(other.active, false)) {}
    app &operator=(app &&other) noexcept;

    ~app();

    console_view console() const { return root(); }

    void present() const;

    bool should_close() const;
    void set_should_close(bool value) const;

    double time() const;
    double delta_time() const;

    /* Returns nothing when the queue is empty. */
    std::optional<event> poll() const;

    /* Blocks until something happens, so a turn-based game idles at no cost. */
    std::optional<event> wait() const;
    std::optional<event> wait_for(double seconds) const;

    /* Safe from any thread: breaks a wait and delivers a wakeup_event. */
    void wake() const;

    bool key_down(nack::key which) const;
    nack::mod mods() const;
    bool mouse_down(nack::mouse_button button) const;
    std::pair<int, int> mouse_cell() const;

    void set_title(const char *title) const;
    void set_fullscreen(bool on) const;
    bool fullscreen() const;
    void set_vsync(bool on) const;

    void set_font(nack::tileset &tiles) const;

    bool set_clipboard(const char *utf8) const;
    /* Empty when the clipboard holds nothing this program can read. */
    std::string clipboard() const;

private:
    struct adopt {};
    explicit app(adopt) : active(true) {}

    bool active = false;
};

/* ------------------------------------------------------------------------ */
/* Root convenience functions                                                */
/* ------------------------------------------------------------------------ */

/*
 * Everything above draws through a console_view, and the app's console is
 * one - `app.console().print(...)`. These are the same calls, spelled
 * without fetching the root first, for the common case of a roguelike that
 * only ever draws to the screen it shows.
 */

inline void clear() { root().clear(); }
inline void clear(color fg, color bg) { root().clear(fg, bg); }

inline void put(int x, int y, std::uint32_t codepoint, color fg, color bg)
{
    root().put(x, y, codepoint, fg, bg);
}

inline void put_tile(int x, int y, tileset &tiles, int index, color tint,
                     color bg)
{
    root().put_tile(x, y, tiles, index, tint, bg);
}

inline void set_glyph(int x, int y, std::uint32_t codepoint)
{
    root().set_glyph(x, y, codepoint);
}

inline void set_fg(int x, int y, color fg) { root().set_fg(x, y, fg); }
inline void set_bg(int x, int y, color bg) { root().set_bg(x, y, bg); }
inline cell at(int x, int y) { return root().at(x, y); }

inline int print(int x, int y, color fg, color bg, std::string_view text)
{
    return root().print(x, y, fg, bg, text);
}

template <class... Args,
          class = std::enable_if_t<(sizeof...(Args) > 0)>>
int print(int x, int y, color fg, color bg, fmt::format_string<Args...> spec,
          Args &&...args)
{
    return root().print(x, y, fg, bg, spec, std::forward<Args>(args)...);
}

inline int print_wrapped(int x, int y, int width, int height, color fg,
                         color bg, std::string_view text)
{
    return root().print_wrapped(x, y, width, height, fg, bg, text);
}

template <class... Args,
          class = std::enable_if_t<(sizeof...(Args) > 0)>>
int print_wrapped(int x, int y, int width, int height, color fg, color bg,
                  fmt::format_string<Args...> spec, Args &&...args)
{
    return root().print_wrapped(x, y, width, height, fg, bg, spec,
                                std::forward<Args>(args)...);
}

inline int measure_wrapped(int width, std::string_view text)
{
    return root().measure_wrapped(width, text);
}

inline void fill(int x, int y, int width, int height, std::uint32_t codepoint,
                 color fg, color bg)
{
    root().fill(x, y, width, height, codepoint, fg, bg);
}

inline void draw_box(int x, int y, int width, int height, color fg, color bg,
                     const char *title = nullptr)
{
    root().draw_box(x, y, width, height, fg, bg, title);
}

inline std::pair<int, int> size() { return root().size(); }
inline int columns() { return root().columns(); }
inline int rows() { return root().rows(); }

}  // namespace nack

#endif /* NACK_HPP_INCLUDED */
