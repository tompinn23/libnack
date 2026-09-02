/*
 * A C++17 face for libnack. Header-only, inline, and nothing but a wrapper:
 * every function here forwards to the C API and the compiler folds it away.
 *
 * This file is optional. libnack is a C library and stays one; including
 * <nack/nack.h> directly still works and is what other languages bind to.
 * What this adds is the part C cannot express:
 *
 *   - Consoles and tilesets free themselves. So does nack_init/nack_shutdown.
 *   - Events arrive as a std::variant, so reading the wrong arm of the union
 *     is a compile error rather than a convention in a comment.
 *   - poll() returns std::optional instead of a bool and an out-parameter,
 *     and sizes come back as structured bindings.
 *   - Keys and modifiers are scoped enums; mods are a real bitmask type
 *     rather than a bare uint32_t.
 *
 * Two things it has to do for you, which is half the reason it exists:
 * <nack/nack.h> carries no extern "C" guards, because the C library does not
 * pretend to be C++; this header supplies them. And the NACK_RGB family are
 * compound literals, which are a GCC and Clang extension in C++ and rejected
 * outright by MSVC - so the colours below are constexpr functions and
 * constants instead. Do not reach for the C macros from C++.
 *
 * Exceptions are used only where construction fails. If you build with them
 * off, every constructor has a try* counterpart returning std::optional, and
 * the throwing paths abort with the message instead.
 */
#ifndef NACK_HPP_INCLUDED
#define NACK_HPP_INCLUDED

#include <cstdarg>
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

extern "C" {
#include <nack/nack.h>
}

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
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
#endif

namespace detail {

/* The library's error string, or a stand-in when it has nothing to say. */
inline std::string lastError(const char *fallback)
{
    const char *why = ::nack_get_error();
    return why ? std::string(why) : std::string(fallback);
}

[[noreturn]] inline void fail(const std::string &what)
{
#if NACK_HPP_EXCEPTIONS
    throw Error(what);
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
using Color = ::nack_color;

constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    return Color{ r, g, b, 255 };
}

constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                     std::uint8_t a)
{
    return Color{ r, g, b, a };
}

inline constexpr Color black    = rgb(0, 0, 0);
inline constexpr Color white    = rgb(255, 255, 255);
inline constexpr Color grey     = rgb(128, 128, 128);
inline constexpr Color darkGrey = rgb(64, 64, 64);
inline constexpr Color red      = rgb(200, 60, 60);
inline constexpr Color green    = rgb(60, 200, 90);
inline constexpr Color blue     = rgb(70, 110, 220);
inline constexpr Color yellow   = rgb(230, 200, 70);
inline constexpr Color cyan     = rgb(80, 200, 220);
inline constexpr Color magenta  = rgb(200, 80, 200);
inline constexpr Color orange   = rgb(230, 140, 50);
inline constexpr Color brown    = rgb(140, 100, 60);
inline constexpr Color none     = rgba(0, 0, 0, 0);

/* ------------------------------------------------------------------------ */
/* Keys and modifiers                                                        */
/* ------------------------------------------------------------------------ */

/* Physical keys, by USB HID usage code: where the key is, not what is on it. */
enum class Key : std::underlying_type_t<::nack_key> {
    Unknown = NACK_KEY_UNKNOWN, A = NACK_KEY_A, B = NACK_KEY_B,
    C = NACK_KEY_C, D = NACK_KEY_D, E = NACK_KEY_E, F = NACK_KEY_F,
    G = NACK_KEY_G, H = NACK_KEY_H, I = NACK_KEY_I, J = NACK_KEY_J,
    K = NACK_KEY_K, L = NACK_KEY_L, M = NACK_KEY_M, N = NACK_KEY_N,
    O = NACK_KEY_O, P = NACK_KEY_P, Q = NACK_KEY_Q, R = NACK_KEY_R,
    S = NACK_KEY_S, T = NACK_KEY_T, U = NACK_KEY_U, V = NACK_KEY_V,
    W = NACK_KEY_W, X = NACK_KEY_X, Y = NACK_KEY_Y, Z = NACK_KEY_Z,
    Num1 = NACK_KEY_1, Num2 = NACK_KEY_2, Num3 = NACK_KEY_3,
    Num4 = NACK_KEY_4, Num5 = NACK_KEY_5, Num6 = NACK_KEY_6,
    Num7 = NACK_KEY_7, Num8 = NACK_KEY_8, Num9 = NACK_KEY_9,
    Num0 = NACK_KEY_0, Enter = NACK_KEY_ENTER, Escape = NACK_KEY_ESCAPE,
    Backspace = NACK_KEY_BACKSPACE, Tab = NACK_KEY_TAB,
    Space = NACK_KEY_SPACE, Minus = NACK_KEY_MINUS, Equal = NACK_KEY_EQUAL,
    LeftBracket = NACK_KEY_LEFT_BRACKET,
    RightBracket = NACK_KEY_RIGHT_BRACKET, Backslash = NACK_KEY_BACKSLASH,
    NonUsHash = NACK_KEY_NON_US_HASH, Semicolon = NACK_KEY_SEMICOLON,
    Apostrophe = NACK_KEY_APOSTROPHE, Grave = NACK_KEY_GRAVE,
    Comma = NACK_KEY_COMMA, Period = NACK_KEY_PERIOD, Slash = NACK_KEY_SLASH,
    CapsLock = NACK_KEY_CAPS_LOCK, F1 = NACK_KEY_F1, F2 = NACK_KEY_F2,
    F3 = NACK_KEY_F3, F4 = NACK_KEY_F4, F5 = NACK_KEY_F5, F6 = NACK_KEY_F6,
    F7 = NACK_KEY_F7, F8 = NACK_KEY_F8, F9 = NACK_KEY_F9, F10 = NACK_KEY_F10,
    F11 = NACK_KEY_F11, F12 = NACK_KEY_F12,
    PrintScreen = NACK_KEY_PRINT_SCREEN, ScrollLock = NACK_KEY_SCROLL_LOCK,
    Pause = NACK_KEY_PAUSE, Insert = NACK_KEY_INSERT, Home = NACK_KEY_HOME,
    PageUp = NACK_KEY_PAGE_UP, Delete = NACK_KEY_DELETE, End = NACK_KEY_END,
    PageDown = NACK_KEY_PAGE_DOWN, Right = NACK_KEY_RIGHT,
    Left = NACK_KEY_LEFT, Down = NACK_KEY_DOWN, Up = NACK_KEY_UP,
    NumLock = NACK_KEY_NUM_LOCK, KpDivide = NACK_KEY_KP_DIVIDE,
    KpMultiply = NACK_KEY_KP_MULTIPLY, KpSubtract = NACK_KEY_KP_SUBTRACT,
    KpAdd = NACK_KEY_KP_ADD, KpEnter = NACK_KEY_KP_ENTER, Kp1 = NACK_KEY_KP_1,
    Kp2 = NACK_KEY_KP_2, Kp3 = NACK_KEY_KP_3, Kp4 = NACK_KEY_KP_4,
    Kp5 = NACK_KEY_KP_5, Kp6 = NACK_KEY_KP_6, Kp7 = NACK_KEY_KP_7,
    Kp8 = NACK_KEY_KP_8, Kp9 = NACK_KEY_KP_9, Kp0 = NACK_KEY_KP_0,
    KpDecimal = NACK_KEY_KP_DECIMAL,
    NonUsBackslash = NACK_KEY_NON_US_BACKSLASH,
    Application = NACK_KEY_APPLICATION, KpEqual = NACK_KEY_KP_EQUAL,
    F13 = NACK_KEY_F13, F14 = NACK_KEY_F14, F15 = NACK_KEY_F15,
    F16 = NACK_KEY_F16, F17 = NACK_KEY_F17, F18 = NACK_KEY_F18,
    F19 = NACK_KEY_F19, F20 = NACK_KEY_F20, F21 = NACK_KEY_F21,
    F22 = NACK_KEY_F22, F23 = NACK_KEY_F23, F24 = NACK_KEY_F24,
    Menu = NACK_KEY_MENU, Mute = NACK_KEY_MUTE, VolumeUp = NACK_KEY_VOLUME_UP,
    VolumeDown = NACK_KEY_VOLUME_DOWN, LeftCtrl = NACK_KEY_LEFT_CTRL,
    LeftShift = NACK_KEY_LEFT_SHIFT, LeftAlt = NACK_KEY_LEFT_ALT,
    LeftSuper = NACK_KEY_LEFT_SUPER, RightCtrl = NACK_KEY_RIGHT_CTRL,
    RightShift = NACK_KEY_RIGHT_SHIFT, RightAlt = NACK_KEY_RIGHT_ALT,
    RightSuper = NACK_KEY_RIGHT_SUPER};

inline const char *keyName(Key key)
{
    return ::nack_key_name(static_cast<::nack_key>(key));
}

/* A set of held modifiers. The C API hands these back as a bare uint32_t. */
enum class Mod : std::uint32_t {
    None     = 0,
    Shift    = NACK_MOD_SHIFT,
    Ctrl     = NACK_MOD_CTRL,
    Alt      = NACK_MOD_ALT,
    Super    = NACK_MOD_SUPER,
    CapsLock = NACK_MOD_CAPSLOCK,
    NumLock  = NACK_MOD_NUMLOCK
};

constexpr Mod operator|(Mod a, Mod b)
{
    return static_cast<Mod>(static_cast<std::uint32_t>(a) |
                            static_cast<std::uint32_t>(b));
}

constexpr Mod operator&(Mod a, Mod b)
{
    return static_cast<Mod>(static_cast<std::uint32_t>(a) &
                            static_cast<std::uint32_t>(b));
}

constexpr Mod &operator|=(Mod &a, Mod b) { return a = a | b; }

/* True when every modifier in `wanted` is held. */
constexpr bool holds(Mod set, Mod wanted)
{
    return (set & wanted) == wanted;
}

constexpr bool any(Mod set) { return set != Mod::None; }

enum class MouseButton : int {
    Left   = NACK_MOUSE_LEFT,
    Right  = NACK_MOUSE_RIGHT,
    Middle = NACK_MOUSE_MIDDLE,
    X1     = NACK_MOUSE_X1,
    X2     = NACK_MOUSE_X2
};

/* ------------------------------------------------------------------------ */
/* Events                                                                    */
/* ------------------------------------------------------------------------ */

struct Quit {};
struct Wakeup {};

struct KeyEvent {
    Key key;
    Mod mods;
    bool repeat;
};

/*
 * Text that has already been through the platform's dead-key and IME
 * handling. The bytes live in the event itself, so text() is valid for as
 * long as the event is and costs no allocation.
 */
struct TextEvent {
    char utf8[32];
    std::string_view text() const { return std::string_view(utf8); }
};

struct MouseEvent {
    int x, y;          /* cell under the pointer                       */
    int px, py;        /* pixel within the console area                */
    int dx, dy;        /* cell delta since the last motion event       */
    MouseButton button;
    int clicks;        /* 1 single, 2 double, ...                      */
    Mod mods;
};

struct ScrollEvent {
    double dx, dy;     /* positive dy scrolls content up               */
    Mod mods;
    bool precise;      /* true for trackpads and high-resolution wheels */
};

struct ResizeEvent {
    int columns, rows;
};

/* Focus gained or lost; the C API spells these as two event types. */
struct FocusEvent {
    bool focused;
};

using Event = std::variant<Quit, KeyEvent, TextEvent, MouseEvent, ScrollEvent,
                           ResizeEvent, FocusEvent, Wakeup>;

namespace detail {

inline Mod modsOf(std::uint32_t raw) { return static_cast<Mod>(raw); }

/* Turns the C tagged union into the variant, reading only the arm the type
 * says is live. NACK_EVENT_NONE has no variant member and yields nothing. */
inline std::optional<Event> toEvent(const ::nack_event &ev)
{
    switch (ev.type) {
    case NACK_EVENT_QUIT:
        return Event{ Quit{} };
    case NACK_EVENT_KEY_DOWN:
    case NACK_EVENT_KEY_UP: {
        KeyEvent out;
        out.key = static_cast<Key>(ev.data.key.key);
        out.mods = modsOf(ev.data.key.mods);
        out.repeat = ev.data.key.repeat;
        return Event{ out };
    }
    case NACK_EVENT_TEXT: {
        TextEvent out{};
        std::size_t i = 0;
        for (; i + 1 < sizeof out.utf8 && ev.data.text.utf8[i]; ++i)
            out.utf8[i] = ev.data.text.utf8[i];
        out.utf8[i] = '\0';
        return Event{ out };
    }
    case NACK_EVENT_MOUSE_MOVE:
    case NACK_EVENT_MOUSE_DOWN:
    case NACK_EVENT_MOUSE_UP: {
        MouseEvent out;
        out.x = ev.data.mouse.x;
        out.y = ev.data.mouse.y;
        out.px = ev.data.mouse.px;
        out.py = ev.data.mouse.py;
        out.dx = ev.data.mouse.dx;
        out.dy = ev.data.mouse.dy;
        out.button = static_cast<MouseButton>(ev.data.mouse.button);
        out.clicks = ev.data.mouse.clicks;
        out.mods = modsOf(ev.data.mouse.mods);
        return Event{ out };
    }
    case NACK_EVENT_MOUSE_SCROLL: {
        ScrollEvent out;
        out.dx = ev.data.scroll.dx;
        out.dy = ev.data.scroll.dy;
        out.mods = modsOf(ev.data.scroll.mods);
        out.precise = ev.data.scroll.precise;
        return Event{ out };
    }
    case NACK_EVENT_RESIZE:
        return Event{ ResizeEvent{ ev.data.resize.columns,
                                   ev.data.resize.rows } };
    case NACK_EVENT_FOCUS:
        return Event{ FocusEvent{ true } };
    case NACK_EVENT_BLUR:
        return Event{ FocusEvent{ false } };
    case NACK_EVENT_WAKEUP:
        return Event{ Wakeup{} };
    case NACK_EVENT_NONE:
    default:
        return std::nullopt;
    }
}

}  // namespace detail

/*
 * Whether a key event was a press rather than a release is not in the variant,
 * because the two carry identical data and mixing them up is the common bug.
 * Ask the keyboard state instead, or use pressed()/released() below.
 */

/* ------------------------------------------------------------------------ */
/* Consoles                                                                  */
/* ------------------------------------------------------------------------ */

struct Cell {
    std::uint32_t glyph;
    ::nack_tileset *tileset;   /* null means the font */
    Color fg, bg;
};

enum class Layout : int {
    Cp437    = NACK_LAYOUT_CP437,
    RowMajor = NACK_LAYOUT_ROW_MAJOR,
    Tcod     = NACK_LAYOUT_TCOD
};

/*
 * A console someone else owns: the root, or one held by a Console. Copying it
 * copies the reference, never the cells, which is what makes a drawing helper
 * work on either - `void drawPanel(nack::ConsoleView c)` takes the root and an
 * offscreen console alike.
 */
class ConsoleView {
public:
    ConsoleView() = default;
    explicit ConsoleView(::nack_console *console) : console_(console) {}

    ::nack_console *get() const { return console_; }
    explicit operator bool() const { return console_ != nullptr; }

    std::pair<int, int> size() const
    {
        int columns = 0, rows = 0;
        ::nack_console_size(console_, &columns, &rows);
        return { columns, rows };
    }

    int columns() const { return size().first; }
    int rows() const { return size().second; }

    void clear() const { ::nack_clear(console_); }
    void clear(Color fg, Color bg) const { ::nack_clear_to(console_, fg, bg); }

    void put(int x, int y, std::uint32_t codepoint, Color fg, Color bg) const
    {
        ::nack_put(console_, x, y, codepoint, fg, bg);
    }

    void putTile(int x, int y, ::nack_tileset *tileset, int index, Color tint,
                 Color bg) const
    {
        ::nack_put_tile(console_, x, y, tileset, index, tint, bg);
    }

    void setGlyph(int x, int y, std::uint32_t codepoint) const
    {
        ::nack_set_glyph(console_, x, y, codepoint);
    }

    void setFg(int x, int y, Color fg) const
    {
        ::nack_set_fg(console_, x, y, fg);
    }

    void setBg(int x, int y, Color bg) const
    {
        ::nack_set_bg(console_, x, y, bg);
    }

    Cell at(int x, int y) const
    {
        ::nack_cell c = ::nack_get(console_, x, y);
        return Cell{ c.glyph, c.tileset, c.fg, c.bg };
    }

    /* Returns the number of cells written. */
    int print(int x, int y, std::string_view text, Color fg, Color bg) const
    {
        /*
         * The C function takes a NUL-terminated string. A string_view need
         * not be terminated, so anything that is not already gets copied -
         * which most literals and std::strings avoid.
         */
        if (isTerminated(text))
            return ::nack_print(console_, x, y, fg, bg, text.data());
        std::string owned(text);
        return ::nack_print(console_, x, y, fg, bg, owned.c_str());
    }

    /*
     * Forwards to nack_printf, so the format string and arguments are C
     * varargs: only trivially copyable types may be passed, which is what the
     * assertion below is for. std::string will not compile; use .c_str().
     */
    template <class... Args>
    int printf(int x, int y, Color fg, Color bg, const char *fmt,
               Args... args) const
    {
        static_assert((std::is_trivially_copyable_v<Args> && ...),
                      "printf forwards to C varargs: pass only trivially "
                      "copyable types (std::string needs .c_str())");
        return ::nack_printf(console_, x, y, fg, bg, fmt, args...);
    }

    int printWrapped(int x, int y, int width, int height, std::string_view text,
                     Color fg, Color bg) const
    {
        if (isTerminated(text))
            return ::nack_print_wrapped(console_, x, y, width, height, fg, bg,
                                        text.data());
        std::string owned(text);
        return ::nack_print_wrapped(console_, x, y, width, height, fg, bg,
                                    owned.c_str());
    }

    /* Rows the text would take, without drawing any of it. */
    int measureWrapped(int width, std::string_view text) const
    {
        return printWrapped(0, 0, width, 0, text, white, black);
    }

    void fill(int x, int y, int width, int height, std::uint32_t codepoint,
              Color fg, Color bg) const
    {
        ::nack_fill(console_, x, y, width, height, codepoint, fg, bg);
    }

    void drawBox(int x, int y, int width, int height, Color fg, Color bg,
                 const char *title = nullptr) const
    {
        ::nack_draw_box(console_, x, y, width, height, fg, bg, title);
    }

    /* Copies this console onto another. Alphas below 1 blend. */
    void blitTo(ConsoleView dst, int dstX, int dstY, int srcX = 0, int srcY = 0,
                int width = 0, int height = 0, float fgAlpha = 1.0f,
                float bgAlpha = 1.0f) const
    {
        ::nack_blit(console_, srcX, srcY, width, height, dst.get(), dstX, dstY,
                    fgAlpha, bgAlpha);
    }

protected:
    static bool isTerminated(std::string_view text)
    {
        /*
         * Reading one past a string_view is only safe when something already
         * put a NUL there. An empty view has no data() to read at all.
         */
        return !text.empty() && text.data()[text.size()] == '\0';
    }

    ::nack_console *console_ = nullptr;
};

/* A console this object owns and frees. Move-only: it is a handle. */
class Console : public ConsoleView {
public:
    Console(int columns, int rows)
    {
        console_ = ::nack_console_new(columns, rows);
        if (!console_)
            detail::fail(detail::lastError("cannot create a console"));
    }

    static std::optional<Console> tryCreate(int columns, int rows)
    {
        ::nack_console *raw = ::nack_console_new(columns, rows);
        if (!raw)
            return std::nullopt;
        return Console(raw);
    }

    Console(const Console &) = delete;
    Console &operator=(const Console &) = delete;

    Console(Console &&other) noexcept { console_ = std::exchange(other.console_, nullptr); }

    Console &operator=(Console &&other) noexcept
    {
        if (this != &other) {
            ::nack_console_free(console_);
            console_ = std::exchange(other.console_, nullptr);
        }
        return *this;
    }

    ~Console() { ::nack_console_free(console_); }

    bool resize(int columns, int rows)
    {
        return ::nack_console_resize(console_, columns, rows);
    }

private:
    explicit Console(::nack_console *raw) { console_ = raw; }
};

/* ------------------------------------------------------------------------ */
/* Tilesets                                                                  */
/* ------------------------------------------------------------------------ */

class Tileset {
public:
    /* A tileset image on disk: PNG or JPEG, decided by its contents. */
    Tileset(const char *path, int tileWidth, int tileHeight, Layout layout)
    {
        tileset_ = ::nack_tileset_load(path, tileWidth, tileHeight,
                                       static_cast<::nack_tileset_layout>(layout));
        if (!tileset_)
            detail::fail(detail::lastError("cannot load a tileset"));
    }

    Tileset(const void *data, std::size_t size, int tileWidth, int tileHeight,
            Layout layout)
    {
        tileset_ = ::nack_tileset_load_memory(data, size, tileWidth, tileHeight,
                                              static_cast<::nack_tileset_layout>(layout));
        if (!tileset_)
            detail::fail(detail::lastError("cannot load a tileset"));
    }

    Tileset(const Tileset &) = delete;
    Tileset &operator=(const Tileset &) = delete;

    Tileset(Tileset &&other) noexcept
        : tileset_(std::exchange(other.tileset_, nullptr)) {}

    Tileset &operator=(Tileset &&other) noexcept
    {
        if (this != &other) {
            ::nack_tileset_free(tileset_);
            tileset_ = std::exchange(other.tileset_, nullptr);
        }
        return *this;
    }

    ~Tileset() { ::nack_tileset_free(tileset_); }

    ::nack_tileset *get() const { return tileset_; }

    struct Size { int width, height, count; };

    Size size() const
    {
        Size s{ 0, 0, 0 };
        ::nack_tileset_size(tileset_, &s.width, &s.height, &s.count);
        return s;
    }

    bool map(std::uint32_t codepoint, int index)
    {
        return ::nack_tileset_map(tileset_, codepoint, index);
    }

    bool mapRange(std::uint32_t first, std::uint32_t last, int firstIndex)
    {
        return ::nack_tileset_map_range(tileset_, first, last, firstIndex);
    }

private:
    ::nack_tileset *tileset_ = nullptr;
};

/* ------------------------------------------------------------------------ */
/* The application                                                           */
/* ------------------------------------------------------------------------ */

using Config = ::nack_config;

inline Config defaultConfig()
{
    Config config;
    ::nack_config_defaults(&config);
    return config;
}

/* The console the window shows. Valid only while an App is alive. */
inline ConsoleView root() { return ConsoleView(::nack_root()); }

/*
 * Owns the library's lifetime: constructing one calls nack_init, destroying
 * one calls nack_shutdown. There can only be one at a time, because the C
 * library is a singleton.
 */
class App {
public:
    explicit App(const Config &config)
    {
        if (!::nack_init(&config))
            detail::fail(detail::lastError("cannot start libnack"));
        active_ = true;
    }

    App() : App(defaultConfig()) {}

    static std::optional<App> tryCreate(const Config &config)
    {
        if (!::nack_init(&config))
            return std::nullopt;
        return App(Adopt{});
    }

    App(const App &) = delete;
    App &operator=(const App &) = delete;

    App(App &&other) noexcept : active_(std::exchange(other.active_, false)) {}

    App &operator=(App &&other) noexcept
    {
        if (this != &other) {
            if (active_)
                ::nack_shutdown();
            active_ = std::exchange(other.active_, false);
        }
        return *this;
    }

    ~App()
    {
        if (active_)
            ::nack_shutdown();
    }

    ConsoleView console() const { return root(); }

    void present() const { ::nack_present(); }

    bool shouldClose() const { return ::nack_should_close(); }
    void setShouldClose(bool value) const { ::nack_set_should_close(value); }

    double time() const { return ::nack_time(); }
    double deltaTime() const { return ::nack_delta_time(); }

    /* Returns nothing when the queue is empty. */
    std::optional<Event> poll() const
    {
        ::nack_event ev;
        while (::nack_poll_event(&ev)) {
            if (auto out = detail::toEvent(ev))
                return out;
        }
        return std::nullopt;
    }

    /* Blocks until something happens, so a turn-based game idles at no cost. */
    std::optional<Event> wait() const
    {
        ::nack_event ev;
        while (::nack_wait_event(&ev)) {
            if (auto out = detail::toEvent(ev))
                return out;
        }
        return std::nullopt;
    }

    std::optional<Event> waitFor(double seconds) const
    {
        ::nack_event ev;
        while (::nack_wait_event_timeout(&ev, seconds)) {
            if (auto out = detail::toEvent(ev))
                return out;
        }
        return std::nullopt;
    }

    /* Safe from any thread: breaks a wait and delivers a Wakeup. */
    void wakeup() const { ::nack_wakeup(); }

    bool keyDown(Key key) const
    {
        return ::nack_key_down(static_cast<::nack_key>(key));
    }

    Mod mods() const { return static_cast<Mod>(::nack_mods()); }

    bool mouseDown(MouseButton button) const
    {
        return ::nack_mouse_down(static_cast<int>(button));
    }

    std::pair<int, int> mouseCell() const
    {
        int x = 0, y = 0;
        ::nack_mouse_cell(&x, &y);
        return { x, y };
    }

    void setTitle(const char *title) const { ::nack_set_title(title); }
    void setFullscreen(bool on) const { ::nack_set_fullscreen(on); }
    bool fullscreen() const { return ::nack_is_fullscreen(); }
    void setVsync(bool on) const { ::nack_set_vsync(on); }

    void setFont(Tileset &tileset) const { ::nack_set_font(tileset.get()); }

    bool setClipboard(const char *utf8) const
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
    struct Adopt {};
    explicit App(Adopt) : active_(true) {}

    bool active_ = false;
};

}  // namespace nack

#endif /* NACK_HPP_INCLUDED */
