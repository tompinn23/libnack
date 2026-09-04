/*
 * Fundamental vocabulary shared by the internal engine and the public C++
 * face in <nack/nack.hpp>.
 *
 * This used to be the public header: colours, keys, events and the rest,
 * declared extern "C" so a C program could link against them. There is no
 * public C API any more - <nack/nack.hpp> is the only thing a caller
 * includes - so this moved out of include/ and lost the extern "C" and
 * NACK_API decoration that existed for that caller's benefit.
 *
 * What it did not lose is the spelling. Every backend's keymap table still
 * says NACK_KEY_A, every renderer still takes a nack_color: renaming
 * several hundred internal call sites across four platforms to an
 * nack::key::a style would touch a lot of code nobody outside the library
 * ever sees the result of, for no behavioural gain. <nack/nack.hpp> is the
 * translation layer that turns this into the namespaced, scoped-enum API a
 * caller actually sees, exactly as it always has.
 */
#ifndef NACK_CORE_H_INCLUDED
#define NACK_CORE_H_INCLUDED

#include <cstdint>

/* -------------------------------------------------------------------------- */
/* Colour                                                                     */
/* -------------------------------------------------------------------------- */

struct nack_color {
    uint8_t r, g, b, a;
};

#define NACK_RGBA(r, g, b, a)  (nack_color{ (uint8_t)(r), (uint8_t)(g), \
                                            (uint8_t)(b), (uint8_t)(a) })
#define NACK_RGB(r, g, b)      NACK_RGBA(r, g, b, 255)

#define NACK_BLACK        NACK_RGB(0, 0, 0)
#define NACK_WHITE        NACK_RGB(255, 255, 255)

/* -------------------------------------------------------------------------- */
/* Keys                                                                       */
/* -------------------------------------------------------------------------- */

/*
 * Physical key identifiers, using USB HID usage codes. They describe the
 * position of a key rather than the symbol printed on it, so a movement
 * binding does not move when the player changes layout. The symbol, when you
 * want it (entering a character name), arrives as a text event.
 */
enum nack_key {
    NACK_KEY_UNKNOWN = 0,

    NACK_KEY_A = 4, NACK_KEY_B, NACK_KEY_C, NACK_KEY_D, NACK_KEY_E, NACK_KEY_F,
    NACK_KEY_G, NACK_KEY_H, NACK_KEY_I, NACK_KEY_J, NACK_KEY_K, NACK_KEY_L,
    NACK_KEY_M, NACK_KEY_N, NACK_KEY_O, NACK_KEY_P, NACK_KEY_Q, NACK_KEY_R,
    NACK_KEY_S, NACK_KEY_T, NACK_KEY_U, NACK_KEY_V, NACK_KEY_W, NACK_KEY_X,
    NACK_KEY_Y, NACK_KEY_Z,

    NACK_KEY_1 = 30, NACK_KEY_2, NACK_KEY_3, NACK_KEY_4, NACK_KEY_5,
    NACK_KEY_6, NACK_KEY_7, NACK_KEY_8, NACK_KEY_9, NACK_KEY_0,

    NACK_KEY_ENTER = 40,
    NACK_KEY_ESCAPE = 41,
    NACK_KEY_BACKSPACE = 42,
    NACK_KEY_TAB = 43,
    NACK_KEY_SPACE = 44,
    NACK_KEY_MINUS = 45,
    NACK_KEY_EQUAL = 46,
    NACK_KEY_LEFT_BRACKET = 47,
    NACK_KEY_RIGHT_BRACKET = 48,
    NACK_KEY_BACKSLASH = 49,
    NACK_KEY_NON_US_HASH = 50,
    NACK_KEY_SEMICOLON = 51,
    NACK_KEY_APOSTROPHE = 52,
    NACK_KEY_GRAVE = 53,
    NACK_KEY_COMMA = 54,
    NACK_KEY_PERIOD = 55,
    NACK_KEY_SLASH = 56,
    NACK_KEY_CAPS_LOCK = 57,

    NACK_KEY_F1 = 58, NACK_KEY_F2, NACK_KEY_F3, NACK_KEY_F4, NACK_KEY_F5,
    NACK_KEY_F6, NACK_KEY_F7, NACK_KEY_F8, NACK_KEY_F9, NACK_KEY_F10,
    NACK_KEY_F11, NACK_KEY_F12,

    NACK_KEY_PRINT_SCREEN = 70,
    NACK_KEY_SCROLL_LOCK = 71,
    NACK_KEY_PAUSE = 72,
    NACK_KEY_INSERT = 73,
    NACK_KEY_HOME = 74,
    NACK_KEY_PAGE_UP = 75,
    NACK_KEY_DELETE = 76,
    NACK_KEY_END = 77,
    NACK_KEY_PAGE_DOWN = 78,
    NACK_KEY_RIGHT = 79,
    NACK_KEY_LEFT = 80,
    NACK_KEY_DOWN = 81,
    NACK_KEY_UP = 82,

    /* The numeric keypad, which is how a lot of roguelikes do eight-way
     * movement, so these are worth having distinct from the number row. */
    NACK_KEY_NUM_LOCK = 83,
    NACK_KEY_KP_DIVIDE = 84,
    NACK_KEY_KP_MULTIPLY = 85,
    NACK_KEY_KP_SUBTRACT = 86,
    NACK_KEY_KP_ADD = 87,
    NACK_KEY_KP_ENTER = 88,
    NACK_KEY_KP_1 = 89, NACK_KEY_KP_2, NACK_KEY_KP_3, NACK_KEY_KP_4,
    NACK_KEY_KP_5, NACK_KEY_KP_6, NACK_KEY_KP_7, NACK_KEY_KP_8, NACK_KEY_KP_9,
    NACK_KEY_KP_0 = 98,
    NACK_KEY_KP_DECIMAL = 99,
    NACK_KEY_NON_US_BACKSLASH = 100,
    NACK_KEY_APPLICATION = 101,
    NACK_KEY_KP_EQUAL = 103,

    NACK_KEY_F13 = 104, NACK_KEY_F14, NACK_KEY_F15, NACK_KEY_F16,
    NACK_KEY_F17, NACK_KEY_F18, NACK_KEY_F19, NACK_KEY_F20,
    NACK_KEY_F21, NACK_KEY_F22, NACK_KEY_F23, NACK_KEY_F24,

    NACK_KEY_MENU = 118,
    NACK_KEY_MUTE = 127,
    NACK_KEY_VOLUME_UP = 128,
    NACK_KEY_VOLUME_DOWN = 129,

    NACK_KEY_LEFT_CTRL = 224,
    NACK_KEY_LEFT_SHIFT = 225,
    NACK_KEY_LEFT_ALT = 226,
    NACK_KEY_LEFT_SUPER = 227,
    NACK_KEY_RIGHT_CTRL = 228,
    NACK_KEY_RIGHT_SHIFT = 229,
    NACK_KEY_RIGHT_ALT = 230,
    NACK_KEY_RIGHT_SUPER = 231,

    NACK_KEY_COUNT = 256
};

enum nack_mod {
    NACK_MOD_SHIFT    = 1u << 0,
    NACK_MOD_CTRL     = 1u << 1,
    NACK_MOD_ALT      = 1u << 2,
    NACK_MOD_SUPER    = 1u << 3,   /* Windows key / Command */
    NACK_MOD_CAPSLOCK = 1u << 4,
    NACK_MOD_NUMLOCK  = 1u << 5
};

enum nack_mouse_button {
    NACK_MOUSE_LEFT = 0,
    NACK_MOUSE_RIGHT = 1,
    NACK_MOUSE_MIDDLE = 2,
    NACK_MOUSE_X1 = 3,
    NACK_MOUSE_X2 = 4,
    NACK_MOUSE_BUTTON_COUNT = 8
};

const char *nack__key_name(nack_key key);

/* -------------------------------------------------------------------------- */
/* Events                                                                     */
/* -------------------------------------------------------------------------- */

enum nack_event_type {
    NACK_EVENT_NONE = 0,
    NACK_EVENT_QUIT,          /* the player closed the window            */
    NACK_EVENT_KEY_DOWN,      /* ev.data.key                             */
    NACK_EVENT_KEY_UP,        /* ev.data.key                             */
    NACK_EVENT_TEXT,          /* ev.data.text, composed UTF-8            */
    NACK_EVENT_MOUSE_MOVE,    /* ev.data.mouse                           */
    NACK_EVENT_MOUSE_DOWN,    /* ev.data.mouse                           */
    NACK_EVENT_MOUSE_UP,      /* ev.data.mouse                           */
    NACK_EVENT_MOUSE_SCROLL,  /* ev.data.scroll                          */
    NACK_EVENT_RESIZE,        /* ev.data.resize, console geometry changed */
    NACK_EVENT_FOCUS,
    NACK_EVENT_BLUR,
    NACK_EVENT_WAKEUP         /* produced by nack::app::wake()            */
};

struct nack_event {
    nack_event_type type;
    union {
        struct {
            nack_key key;
            uint32_t mods;
            bool repeat;
        } key;

        /* Text that has already been through the platform's dead-key and IME
         * handling. Ctrl and Command chords produce key events and no text. */
        struct { char utf8[32]; } text;

        struct {
            int x, y;          /* cell under the pointer                   */
            int px, py;        /* pixel within the console area            */
            int dx, dy;        /* cell delta since the last motion event   */
            int button;        /* for MOUSE_DOWN and MOUSE_UP              */
            int clicks;        /* 1 single, 2 double, ...                  */
            uint32_t mods;
        } mouse;

        struct {
            double dx, dy;     /* positive dy scrolls content up           */
            uint32_t mods;
            bool precise;      /* true for trackpads and high-res wheels   */
        } scroll;

        struct { int columns, rows; } resize;
    } data;
};

/* -------------------------------------------------------------------------- */
/* Tilesets                                                                   */
/* -------------------------------------------------------------------------- */

/*
 * How the glyphs are arranged in the image. CP437 is what nearly every
 * roguelike tileset ships as; ROW_MAJOR suits a sheet of arbitrary tiles;
 * TCOD is libtcod's column-major ordering, for its own sheets.
 */
enum nack_tileset_layout {
    NACK_LAYOUT_CP437 = 0,
    NACK_LAYOUT_ROW_MAJOR,
    NACK_LAYOUT_TCOD
};

struct nack_tileset;

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

/* How the console is fitted to the window when the two do not match. */
enum nack_scaling {
    NACK_SCALE_INTEGER = 0,  /* whole-number zoom, letterboxed: always crisp */
    NACK_SCALE_FIT,          /* largest fit preserving aspect, letterboxed   */
    NACK_SCALE_STRETCH       /* fill the window, ignoring aspect             */
};

struct nack_config {
    const char *title = "libnack";
    int columns = 80, rows = 50;

    /* Path to a tileset image. NULL uses the built-in 8x8 font, so a game can
     * start without shipping any assets. */
    const char *tileset = nullptr;
    int tile_width = 0, tile_height = 0;
    nack_tileset_layout tileset_layout = NACK_LAYOUT_CP437;

    nack_scaling scaling = NACK_SCALE_INTEGER;
    nack_color letterbox = NACK_BLACK;   /* colour of the bars around the console */

    bool vsync = true;
    bool resizable = true;
    bool fullscreen = false;

    /*
     * When set, resizing the window changes the console's dimensions and
     * produces a resize event, rather than scaling a fixed grid. This is
     * what you want if the game reflows its layout.
     */
    bool auto_resize = false;

    /* Initial window size, as a multiple of the console's pixel size.
     * 0 picks the largest whole multiple that fits the monitor. */
    int window_scale = 0;
};

/* -------------------------------------------------------------------------- */
/* Consoles                                                                   */
/* -------------------------------------------------------------------------- */

struct nack_console;

struct nack_cell {
    uint32_t glyph;                 /* codepoint, or tile index when tiled */
    nack_tileset *tileset;   /* NULL means the font                 */
    nack_color fg, bg;
};

#endif /* NACK_CORE_H_INCLUDED */
