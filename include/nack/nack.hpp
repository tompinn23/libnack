/*
 * A C++17 face for libnack. Header-only, inline, and nothing but a wrapper:
 * every function here forwards to the C API and the compiler folds it away.
 *
 * This file is optional. libnack is a C library and stays one; including
 * <nack/nack.h> directly still works and is what other languages bind to. A C
 * program links no C++ runtime and does not build {fmt}.
 *
 * What this adds is the part C cannot express:
 *
 *   - Consoles and tilesets free themselves. So does nack_init/nack_shutdown.
 *   - Events arrive as a std::variant, so reading the wrong arm of the union
 *     is a compile error rather than a convention in a comment.
 *   - poll() returns std::optional instead of a bool and an out-parameter,
 *     and sizes come back as structured bindings.
 *   - Keys and modifiers are scoped enums; mods are a real bitmask type
 *     rather than a bare uint32_t.
 *   - print() takes a {fmt} format string rather than C varargs.
 *
 * <nack/nack.h> carries its own extern "C" guards now that the library behind
 * it is C++, and NACK_RGB spells itself as a braced init there rather than a
 * compound literal, so both are usable from C++. The constexpr colours below
 * are still the idiomatic spelling here - they are typed, scoped and usable
 * in constant expressions, which a macro is not.
 *
 * Exceptions are used only where construction fails. If you build with them
 * off, every constructor has a try_create counterpart returning
 * std::optional, and the throwing paths abort with the message instead.
 */
#ifndef NACK_HPP_INCLUDED
#define NACK_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <fmt/format.h>

#include <nack/nack.h>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  define NACK_HPP_EXCEPTIONS 1
#  include <stdexcept>
#else
#  define NACK_HPP_EXCEPTIONS 0
#endif

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

/* The library's error string, or a stand-in when it has nothing to say. */
inline std::string last_error(const char *fallback)
{
    const char *why = ::nack_get_error();
    return why ? std::string(why) : std::string(fallback);
}

[[noreturn]] inline void fail(const std::string &what)
{
#if NACK_HPP_EXCEPTIONS
    throw error(what);
#else
    std::fprintf(stderr, "nack: %s\n", what.c_str());
    std::abort();
#endif
}

}  // namespace detail

/* ------------------------------------------------------------------------ */
/* Colour                                                                    */
/* ------------------------------------------------------------------------ */

/*
 * The same struct the C API uses, so it passes straight through with no
 * conversion. Only the spelling of the constants changes.
 */
using color = ::nack_color;

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
 * the list collides.
 */
enum class key : std::underlying_type_t<::nack_key> {
    unknown = NACK_KEY_UNKNOWN, a = NACK_KEY_A, b = NACK_KEY_B,
    c = NACK_KEY_C, d = NACK_KEY_D, e = NACK_KEY_E, f = NACK_KEY_F,
    g = NACK_KEY_G, h = NACK_KEY_H, i = NACK_KEY_I, j = NACK_KEY_J,
    k = NACK_KEY_K, l = NACK_KEY_L, m = NACK_KEY_M, n = NACK_KEY_N,
    o = NACK_KEY_O, p = NACK_KEY_P, q = NACK_KEY_Q, r = NACK_KEY_R,
    s = NACK_KEY_S, t = NACK_KEY_T, u = NACK_KEY_U, v = NACK_KEY_V,
    w = NACK_KEY_W, x = NACK_KEY_X, y = NACK_KEY_Y, z = NACK_KEY_Z,
    num1 = NACK_KEY_1, num2 = NACK_KEY_2, num3 = NACK_KEY_3,
    num4 = NACK_KEY_4, num5 = NACK_KEY_5, num6 = NACK_KEY_6,
    num7 = NACK_KEY_7, num8 = NACK_KEY_8, num9 = NACK_KEY_9,
    num0 = NACK_KEY_0, enter = NACK_KEY_ENTER, escape = NACK_KEY_ESCAPE,
    backspace = NACK_KEY_BACKSPACE, tab = NACK_KEY_TAB,
    space = NACK_KEY_SPACE, minus = NACK_KEY_MINUS, equal = NACK_KEY_EQUAL,
    left_bracket = NACK_KEY_LEFT_BRACKET,
    right_bracket = NACK_KEY_RIGHT_BRACKET, backslash = NACK_KEY_BACKSLASH,
    non_us_hash = NACK_KEY_NON_US_HASH, semicolon = NACK_KEY_SEMICOLON,
    apostrophe = NACK_KEY_APOSTROPHE, grave = NACK_KEY_GRAVE,
    comma = NACK_KEY_COMMA, period = NACK_KEY_PERIOD, slash = NACK_KEY_SLASH,
    caps_lock = NACK_KEY_CAPS_LOCK, f1 = NACK_KEY_F1, f2 = NACK_KEY_F2,
    f3 = NACK_KEY_F3, f4 = NACK_KEY_F4, f5 = NACK_KEY_F5, f6 = NACK_KEY_F6,
    f7 = NACK_KEY_F7, f8 = NACK_KEY_F8, f9 = NACK_KEY_F9, f10 = NACK_KEY_F10,
    f11 = NACK_KEY_F11, f12 = NACK_KEY_F12,
    print_screen = NACK_KEY_PRINT_SCREEN, scroll_lock = NACK_KEY_SCROLL_LOCK,
    pause = NACK_KEY_PAUSE, insert = NACK_KEY_INSERT, home = NACK_KEY_HOME,
    page_up = NACK_KEY_PAGE_UP, delete_key = NACK_KEY_DELETE,
    end = NACK_KEY_END, page_down = NACK_KEY_PAGE_DOWN,
    right = NACK_KEY_RIGHT, left = NACK_KEY_LEFT, down = NACK_KEY_DOWN,
    up = NACK_KEY_UP, num_lock = NACK_KEY_NUM_LOCK,
    kp_divide = NACK_KEY_KP_DIVIDE, kp_multiply = NACK_KEY_KP_MULTIPLY,
    kp_subtract = NACK_KEY_KP_SUBTRACT, kp_add = NACK_KEY_KP_ADD,
    kp_enter = NACK_KEY_KP_ENTER, kp_1 = NACK_KEY_KP_1, kp_2 = NACK_KEY_KP_2,
    kp_3 = NACK_KEY_KP_3, kp_4 = NACK_KEY_KP_4, kp_5 = NACK_KEY_KP_5,
    kp_6 = NACK_KEY_KP_6, kp_7 = NACK_KEY_KP_7, kp_8 = NACK_KEY_KP_8,
    kp_9 = NACK_KEY_KP_9, kp_0 = NACK_KEY_KP_0,
    kp_decimal = NACK_KEY_KP_DECIMAL,
    non_us_backslash = NACK_KEY_NON_US_BACKSLASH,
    application = NACK_KEY_APPLICATION, kp_equal = NACK_KEY_KP_EQUAL,
    f13 = NACK_KEY_F13, f14 = NACK_KEY_F14, f15 = NACK_KEY_F15,
    f16 = NACK_KEY_F16, f17 = NACK_KEY_F17, f18 = NACK_KEY_F18,
    f19 = NACK_KEY_F19, f20 = NACK_KEY_F20, f21 = NACK_KEY_F21,
    f22 = NACK_KEY_F22, f23 = NACK_KEY_F23, f24 = NACK_KEY_F24,
    menu = NACK_KEY_MENU, mute = NACK_KEY_MUTE,
    volume_up = NACK_KEY_VOLUME_UP, volume_down = NACK_KEY_VOLUME_DOWN,
    left_ctrl = NACK_KEY_LEFT_CTRL, left_shift = NACK_KEY_LEFT_SHIFT,
    left_alt = NACK_KEY_LEFT_ALT, left_super = NACK_KEY_LEFT_SUPER,
    right_ctrl = NACK_KEY_RIGHT_CTRL, right_shift = NACK_KEY_RIGHT_SHIFT,
    right_alt = NACK_KEY_RIGHT_ALT, right_super = NACK_KEY_RIGHT_SUPER};

inline const char *key_name(key which)
{
    return ::nack_key_name(static_cast<::nack_key>(which));
}

/* A set of held modifiers. The C API hands these back as a bare uint32_t. */
enum class mod : std::uint32_t {
    none      = 0,
    shift     = NACK_MOD_SHIFT,
    ctrl      = NACK_MOD_CTRL,
    alt       = NACK_MOD_ALT,
    super     = NACK_MOD_SUPER,
    caps_lock = NACK_MOD_CAPSLOCK,
    num_lock  = NACK_MOD_NUMLOCK
};

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

enum class mouse_button : int {
    left   = NACK_MOUSE_LEFT,
    right  = NACK_MOUSE_RIGHT,
    middle = NACK_MOUSE_MIDDLE,
    x1     = NACK_MOUSE_X1,
    x2     = NACK_MOUSE_X2
};

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

struct mouse_event {
    int x, y;          /* cell under the pointer                       */
    int px, py;        /* pixel within the console area                */
    int dx, dy;        /* cell delta since the last motion event       */
    nack::mouse_button button;
    int clicks;        /* 1 single, 2 double, ...                      */
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

/* Focus gained or lost; the C API spells these as two event types. */
struct focus_event {
    bool focused;
};

using event = std::variant<quit_event, key_event, text_event, mouse_event,
                           scroll_event, resize_event, focus_event,
                           wakeup_event>;

namespace detail {

inline nack::mod mods_of(std::uint32_t raw)
{
    return static_cast<nack::mod>(raw);
}

/* Turns the C tagged union into the variant, reading only the arm the type
 * says is live. NACK_EVENT_NONE has no variant member and yields nothing. */
inline std::optional<event> to_event(const ::nack_event &ev)
{
    switch (ev.type) {
    case NACK_EVENT_QUIT:
        return event{ quit_event{} };
    case NACK_EVENT_KEY_DOWN:
    case NACK_EVENT_KEY_UP: {
        key_event out;
        out.key = static_cast<nack::key>(ev.data.key.key);
        out.mods = mods_of(ev.data.key.mods);
        out.repeat = ev.data.key.repeat;
        return event{ out };
    }
    case NACK_EVENT_TEXT: {
        text_event out{};
        std::size_t i = 0;
        for (; i + 1 < sizeof out.utf8 && ev.data.text.utf8[i]; ++i)
            out.utf8[i] = ev.data.text.utf8[i];
        out.utf8[i] = '\0';
        return event{ out };
    }
    case NACK_EVENT_MOUSE_MOVE:
    case NACK_EVENT_MOUSE_DOWN:
    case NACK_EVENT_MOUSE_UP: {
        mouse_event out;
        out.x = ev.data.mouse.x;
        out.y = ev.data.mouse.y;
        out.px = ev.data.mouse.px;
        out.py = ev.data.mouse.py;
        out.dx = ev.data.mouse.dx;
        out.dy = ev.data.mouse.dy;
        out.button = static_cast<nack::mouse_button>(ev.data.mouse.button);
        out.clicks = ev.data.mouse.clicks;
        out.mods = mods_of(ev.data.mouse.mods);
        return event{ out };
    }
    case NACK_EVENT_MOUSE_SCROLL: {
        scroll_event out;
        out.dx = ev.data.scroll.dx;
        out.dy = ev.data.scroll.dy;
        out.mods = mods_of(ev.data.scroll.mods);
        out.precise = ev.data.scroll.precise;
        return event{ out };
    }
    case NACK_EVENT_RESIZE:
        return event{ resize_event{ ev.data.resize.columns,
                                    ev.data.resize.rows } };
    case NACK_EVENT_FOCUS:
        return event{ focus_event{ true } };
    case NACK_EVENT_BLUR:
        return event{ focus_event{ false } };
    case NACK_EVENT_WAKEUP:
        return event{ wakeup_event{} };
    case NACK_EVENT_NONE:
    default:
        return std::nullopt;
    }
}

}  // namespace detail

/* ------------------------------------------------------------------------ */
/* Consoles                                                                  */
/* ------------------------------------------------------------------------ */

struct cell {
    std::uint32_t glyph;
    ::nack_tileset *tileset;   /* null means the font */
    nack::color fg, bg;
};

enum class layout : int {
    cp437     = NACK_LAYOUT_CP437,
    row_major = NACK_LAYOUT_ROW_MAJOR,
    tcod      = NACK_LAYOUT_TCOD
};

/*
 * A console someone else owns: the root, or one held by a console object.
 * Copying it copies the reference, never the cells, which is what makes a
 * drawing helper work on either - `void draw_panel(nack::console_view c)`
 * takes the root and an offscreen console alike.
 */
class console_view {
public:
    console_view() = default;
    explicit console_view(::nack_console *console) : handle(console) {}

    ::nack_console *get() const { return handle; }
    explicit operator bool() const { return handle != nullptr; }

    std::pair<int, int> size() const
    {
        int columns = 0, rows = 0;
        ::nack_console_size(handle, &columns, &rows);
        return { columns, rows };
    }

    int columns() const { return size().first; }
    int rows() const { return size().second; }

    void clear() const { ::nack_clear(handle); }
    void clear(color fg, color bg) const { ::nack_clear_to(handle, fg, bg); }

    void put(int x, int y, std::uint32_t codepoint, color fg, color bg) const
    {
        ::nack_put(handle, x, y, codepoint, fg, bg);
    }

    void put_tile(int x, int y, ::nack_tileset *tiles, int index, color tint,
                  color bg) const
    {
        ::nack_put_tile(handle, x, y, tiles, index, tint, bg);
    }

    void set_glyph(int x, int y, std::uint32_t codepoint) const
    {
        ::nack_set_glyph(handle, x, y, codepoint);
    }

    void set_fg(int x, int y, color fg) const
    {
        ::nack_set_fg(handle, x, y, fg);
    }

    void set_bg(int x, int y, color bg) const
    {
        ::nack_set_bg(handle, x, y, bg);
    }

    cell at(int x, int y) const
    {
        ::nack_cell c = ::nack_get(handle, x, y);
        return cell{ c.glyph, c.tileset, c.fg, c.bg };
    }

    /*
     * Text as it stands. The arguments are in the order the C API uses:
     * position, then colours, then the text. Returns the cells written.
     */
    int print(int x, int y, color fg, color bg, std::string_view text) const
    {
        /*
         * The C function wants a NUL-terminated string. A string_view need
         * not be terminated, so anything that is not already gets copied -
         * which literals and std::strings avoid.
         */
        if (is_terminated(text))
            return ::nack_print(handle, x, y, fg, bg, text.data());
        std::string owned(text);
        return ::nack_print(handle, x, y, fg, bg, owned.c_str());
    }

    /*
     * The same, formatted with {fmt}.
     *
     * When the format string and the arguments disagree, {fmt} says so:
     * compiled as C++20 it is a compile error, and as C++17 - which is all
     * this header requires - a fmt::format_error at run time, because the
     * consteval checking {fmt} uses needs C++20. Either way the mismatch is
     * caught, where C varargs would have read whatever was on the stack.
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

    int print_wrapped(int x, int y, int width, int height, color fg, color bg,
                      std::string_view text) const
    {
        if (is_terminated(text))
            return ::nack_print_wrapped(handle, x, y, width, height, fg, bg,
                                        text.data());
        std::string owned(text);
        return ::nack_print_wrapped(handle, x, y, width, height, fg, bg,
                                    owned.c_str());
    }

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
              color fg, color bg) const
    {
        ::nack_fill(handle, x, y, width, height, codepoint, fg, bg);
    }

    void draw_box(int x, int y, int width, int height, color fg, color bg,
                  const char *title = nullptr) const
    {
        ::nack_draw_box(handle, x, y, width, height, fg, bg, title);
    }

    /* Copies this console onto another. Alphas below 1 blend. */
    void blit_to(console_view dst, int dst_x, int dst_y, int src_x = 0,
                 int src_y = 0, int width = 0, int height = 0,
                 float fg_alpha = 1.0f, float bg_alpha = 1.0f) const
    {
        ::nack_blit(handle, src_x, src_y, width, height, dst.get(), dst_x,
                    dst_y, fg_alpha, bg_alpha);
    }

protected:
    static bool is_terminated(std::string_view text)
    {
        /*
         * Reading one past a string_view is only safe when something already
         * put a NUL there. An empty view has no data() to read at all.
         */
        return !text.empty() && text.data()[text.size()] == '\0';
    }

    ::nack_console *handle = nullptr;
};

/* A console this object owns and frees. Move-only: it is a handle. */
class console : public console_view {
public:
    console(int columns, int rows)
    {
        handle = ::nack_console_new(columns, rows);
        if (!handle)
            detail::fail(detail::last_error("cannot create a console"));
    }

    static std::optional<console> try_create(int columns, int rows)
    {
        ::nack_console *raw = ::nack_console_new(columns, rows);
        if (!raw)
            return std::nullopt;
        return console(raw);
    }

    console(const console &) = delete;
    console &operator=(const console &) = delete;

    console(console &&other) noexcept
    {
        handle = std::exchange(other.handle, nullptr);
    }

    console &operator=(console &&other) noexcept
    {
        if (this != &other) {
            ::nack_console_free(handle);
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    ~console() { ::nack_console_free(handle); }

    bool resize(int columns, int rows)
    {
        return ::nack_console_resize(handle, columns, rows);
    }

private:
    explicit console(::nack_console *raw) { handle = raw; }
};

/* ------------------------------------------------------------------------ */
/* Tilesets                                                                  */
/* ------------------------------------------------------------------------ */

class tileset {
public:
    /* A tileset image on disk: PNG or JPEG, decided by its contents. */
    tileset(const char *path, int tile_width, int tile_height,
            nack::layout arrangement)
    {
        handle = ::nack_tileset_load(
            path, tile_width, tile_height,
            static_cast<::nack_tileset_layout>(arrangement));
        if (!handle)
            detail::fail(detail::last_error("cannot load a tileset"));
    }

    tileset(const void *data, std::size_t size, int tile_width,
            int tile_height, nack::layout arrangement)
    {
        handle = ::nack_tileset_load_memory(
            data, size, tile_width, tile_height,
            static_cast<::nack_tileset_layout>(arrangement));
        if (!handle)
            detail::fail(detail::last_error("cannot load a tileset"));
    }

    tileset(const tileset &) = delete;
    tileset &operator=(const tileset &) = delete;

    tileset(tileset &&other) noexcept
        : handle(std::exchange(other.handle, nullptr)) {}

    tileset &operator=(tileset &&other) noexcept
    {
        if (this != &other) {
            ::nack_tileset_free(handle);
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    ~tileset() { ::nack_tileset_free(handle); }

    ::nack_tileset *get() const { return handle; }

    struct dimensions { int width, height, count; };

    dimensions size() const
    {
        dimensions d{ 0, 0, 0 };
        ::nack_tileset_size(handle, &d.width, &d.height, &d.count);
        return d;
    }

    bool map(std::uint32_t codepoint, int index)
    {
        return ::nack_tileset_map(handle, codepoint, index);
    }

    bool map_range(std::uint32_t first, std::uint32_t last, int first_index)
    {
        return ::nack_tileset_map_range(handle, first, last, first_index);
    }

private:
    ::nack_tileset *handle = nullptr;
};

/* ------------------------------------------------------------------------ */
/* The application                                                           */
/* ------------------------------------------------------------------------ */

using config = ::nack_config;

inline config default_config()
{
    config settings;
    ::nack_config_defaults(&settings);
    return settings;
}

/* The console the window shows. Valid only while an app is alive. */
inline console_view root() { return console_view(::nack_root()); }

/*
 * Owns the library's lifetime: constructing one calls nack_init, destroying
 * one calls nack_shutdown. There can only be one at a time, because the C
 * library is a singleton.
 */
class app {
public:
    explicit app(const config &settings)
    {
        if (!::nack_init(&settings))
            detail::fail(detail::last_error("cannot start libnack"));
        active = true;
    }

    app() : app(default_config()) {}

    static std::optional<app> try_create(const config &settings)
    {
        if (!::nack_init(&settings))
            return std::nullopt;
        return app(adopt{});
    }

    app(const app &) = delete;
    app &operator=(const app &) = delete;

    app(app &&other) noexcept : active(std::exchange(other.active, false)) {}

    app &operator=(app &&other) noexcept
    {
        if (this != &other) {
            if (active)
                ::nack_shutdown();
            active = std::exchange(other.active, false);
        }
        return *this;
    }

    ~app()
    {
        if (active)
            ::nack_shutdown();
    }

    console_view console() const { return root(); }

    void present() const { ::nack_present(); }

    bool should_close() const { return ::nack_should_close(); }
    void set_should_close(bool value) const
    {
        ::nack_set_should_close(value);
    }

    double time() const { return ::nack_time(); }
    double delta_time() const { return ::nack_delta_time(); }

    /* Returns nothing when the queue is empty. */
    std::optional<event> poll() const
    {
        ::nack_event ev;
        while (::nack_poll_event(&ev)) {
            if (auto out = detail::to_event(ev))
                return out;
        }
        return std::nullopt;
    }

    /* Blocks until something happens, so a turn-based game idles at no cost. */
    std::optional<event> wait() const
    {
        ::nack_event ev;
        while (::nack_wait_event(&ev)) {
            if (auto out = detail::to_event(ev))
                return out;
        }
        return std::nullopt;
    }

    std::optional<event> wait_for(double seconds) const
    {
        ::nack_event ev;
        while (::nack_wait_event_timeout(&ev, seconds)) {
            if (auto out = detail::to_event(ev))
                return out;
        }
        return std::nullopt;
    }

    /* Safe from any thread: breaks a wait and delivers a wakeup_event. */
    void wake() const { ::nack_wakeup(); }

    bool key_down(nack::key which) const
    {
        return ::nack_key_down(static_cast<::nack_key>(which));
    }

    nack::mod mods() const { return static_cast<nack::mod>(::nack_mods()); }

    bool mouse_down(nack::mouse_button button) const
    {
        return ::nack_mouse_down(static_cast<int>(button));
    }

    std::pair<int, int> mouse_cell() const
    {
        int x = 0, y = 0;
        ::nack_mouse_cell(&x, &y);
        return { x, y };
    }

    void set_title(const char *title) const { ::nack_set_title(title); }
    void set_fullscreen(bool on) const { ::nack_set_fullscreen(on); }
    bool fullscreen() const { return ::nack_is_fullscreen(); }
    void set_vsync(bool on) const { ::nack_set_vsync(on); }

    void set_font(nack::tileset &tiles) const
    {
        ::nack_set_font(tiles.get());
    }

    bool set_clipboard(const char *utf8) const
    {
        return ::nack_clipboard_set(utf8);
    }

    /* Empty when the clipboard holds nothing this program can read. */
    std::string clipboard() const
    {
        const char *text = ::nack_clipboard_get();
        return text ? std::string(text) : std::string();
    }

private:
    struct adopt {};
    explicit app(adopt) : active(true) {}

    bool active = false;
};

}  // namespace nack

#endif /* NACK_HPP_INCLUDED */
