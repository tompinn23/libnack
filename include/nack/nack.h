/*
 * libnack - a cell console for roguelikes.
 *
 * The whole API is a grid of cells you draw glyphs and tiles into, in the
 * spirit of libtcod and BearLibTerminal. libnack owns the window, the OpenGL
 * context and the renderer; none of that is exposed, because none of it is
 * what a roguelike wants to think about.
 *
 *     struct nack_config cfg;
 *     nack_config_defaults(&cfg);
 *     cfg.title = "my roguelike";
 *     cfg.columns = 80;
 *     cfg.rows = 50;
 *     nack_init(&cfg);
 *
 *     while (!nack_should_close()) {
 *         struct nack_event ev;
 *         while (nack_poll_event(&ev))
 *             handle(&ev);
 *
 *         nack_clear(NULL);
 *         nack_print(NULL, 1, 1, NACK_WHITE, NACK_BLACK, "@ you");
 *         nack_present();
 *     }
 *     nack_shutdown();
 *
 * Turn-based games can block instead of spinning: nack_wait_event sleeps until
 * the player does something, so an idle game costs no CPU.
 */
#ifndef NACK_H_INCLUDED
#define NACK_H_INCLUDED

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(NACK_SHARED) && defined(_WIN32)
#  if defined(NACK_BUILD)
#    define NACK_API __declspec(dllexport)
#  else
#    define NACK_API __declspec(dllimport)
#  endif
#elif defined(NACK_SHARED) && defined(__GNUC__)
#  define NACK_API __attribute__((visibility("default")))
#else
#  define NACK_API extern
#endif

#define NACK_VERSION_MAJOR 0
#define NACK_VERSION_MINOR 2
#define NACK_VERSION_PATCH 0

/* -------------------------------------------------------------------------- */
/* Colour                                                                     */
/* -------------------------------------------------------------------------- */

struct nack_color {
    uint8_t r, g, b, a;
};

#define NACK_RGB(r, g, b)      ((struct nack_color){ (uint8_t)(r), (uint8_t)(g), \
                                                     (uint8_t)(b), 255 })
#define NACK_RGBA(r, g, b, a)  ((struct nack_color){ (uint8_t)(r), (uint8_t)(g), \
                                                     (uint8_t)(b), (uint8_t)(a) })

#define NACK_BLACK        NACK_RGB(0, 0, 0)
#define NACK_WHITE        NACK_RGB(255, 255, 255)
#define NACK_GREY         NACK_RGB(128, 128, 128)
#define NACK_DARK_GREY    NACK_RGB(64, 64, 64)
#define NACK_RED          NACK_RGB(200, 60, 60)
#define NACK_GREEN        NACK_RGB(90, 190, 90)
#define NACK_BLUE         NACK_RGB(80, 130, 220)
#define NACK_YELLOW       NACK_RGB(220, 200, 90)
#define NACK_CYAN         NACK_RGB(90, 200, 210)
#define NACK_MAGENTA      NACK_RGB(190, 100, 190)
#define NACK_ORANGE       NACK_RGB(220, 140, 60)
#define NACK_BROWN        NACK_RGB(140, 100, 60)
/* Fully transparent: a cell background that lets what is behind it show. */
#define NACK_NONE         NACK_RGBA(0, 0, 0, 0)

/* -------------------------------------------------------------------------- */
/* Keys                                                                       */
/* -------------------------------------------------------------------------- */

/*
 * Physical key identifiers, using USB HID usage codes. They describe the
 * position of a key rather than the symbol printed on it, so a movement
 * binding does not move when the player changes layout. The symbol, when you
 * want it (entering a character name), arrives as NACK_EVENT_TEXT.
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

NACK_API const char *nack_key_name(enum nack_key key);

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
    NACK_EVENT_WAKEUP         /* produced by nack_wakeup                 */
};

struct nack_event {
    enum nack_event_type type;
    union {
        struct {
            enum nack_key key;
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

/*
 * Loads a PNG or a JPEG, whichever the file turns out to be. tile_width and
 * tile_height may be 0 for a CP437 sheet, in which
 * case they are inferred by dividing the image into 16 by 16 glyphs.
 *
 * A greyscale or white-on-transparent sheet is treated as a font: glyphs are
 * tinted by the cell's foreground colour. A sheet with other colours in it is
 * treated as graphical tiles and drawn as-is, tinted only if you ask.
 */
NACK_API struct nack_tileset *nack_tileset_load(const char *path,
                                                int tile_width, int tile_height,
                                                enum nack_tileset_layout layout);

/* Same, from memory, so tilesets can be embedded in the executable. */
NACK_API struct nack_tileset *nack_tileset_load_memory(const void *data, size_t size,
                                                       int tile_width, int tile_height,
                                                       enum nack_tileset_layout layout);

NACK_API void nack_tileset_free(struct nack_tileset *tileset);

NACK_API void nack_tileset_size(const struct nack_tileset *tileset,
                                int *tile_width, int *tile_height, int *count);

/*
 * Maps codepoints onto tile indices, for characters outside whatever ordering
 * the sheet came in. A CP437 sheet is pre-mapped for the codepoints CP437
 * covers, including the box drawing characters.
 */
NACK_API bool nack_tileset_map(struct nack_tileset *tileset, uint32_t codepoint,
                               int index);
NACK_API bool nack_tileset_map_range(struct nack_tileset *tileset, uint32_t first,
                                     uint32_t last, int first_index);

/* The tileset used to draw codepoints. Defaults to the built-in font. */
NACK_API void nack_set_font(struct nack_tileset *tileset);
NACK_API struct nack_tileset *nack_get_font(void);

/* -------------------------------------------------------------------------- */
/* Configuration and lifetime                                                 */
/* -------------------------------------------------------------------------- */

/* How the console is fitted to the window when the two do not match. */
enum nack_scaling {
    NACK_SCALE_INTEGER = 0,  /* whole-number zoom, letterboxed: always crisp */
    NACK_SCALE_FIT,          /* largest fit preserving aspect, letterboxed   */
    NACK_SCALE_STRETCH       /* fill the window, ignoring aspect             */
};

struct nack_config {
    const char *title;
    int columns, rows;

    /* Path to a tileset image. NULL uses the built-in 8x8 font, so a game can
     * start without shipping any assets. */
    const char *tileset;
    int tile_width, tile_height;
    enum nack_tileset_layout tileset_layout;

    enum nack_scaling scaling;
    struct nack_color letterbox;   /* colour of the bars around the console */

    bool vsync;
    bool resizable;
    bool fullscreen;

    /*
     * When set, resizing the window changes the console's dimensions and
     * produces NACK_EVENT_RESIZE, rather than scaling a fixed grid. This is
     * what you want if the game reflows its layout.
     */
    bool auto_resize;

    /* Initial window size, as a multiple of the console's pixel size.
     * 0 picks the largest whole multiple that fits the monitor. */
    int window_scale;
};

NACK_API void nack_config_defaults(struct nack_config *config);
NACK_API bool nack_init(const struct nack_config *config);
NACK_API void nack_shutdown(void);

/* The most recent failure, or NULL if the last call succeeded. */
NACK_API const char *nack_get_error(void);

/* -------------------------------------------------------------------------- */
/* Consoles                                                                   */
/* -------------------------------------------------------------------------- */

struct nack_console;

/*
 * Every drawing call takes a console. Offscreen consoles are for composing:
 * draw a panel or a menu into one, then blit it onto the root.
 */
/*
 * The console the window shows. Created by nack_init and destroyed by
 * nack_shutdown, so it is never freed by the caller and is NULL only before
 * nack_init has succeeded.
 *
 * Every drawing call takes a console explicitly. Passing NULL is an error
 * rather than a shorthand for this one, so a console that failed to allocate
 * cannot quietly draw to the screen instead.
 */
NACK_API struct nack_console *nack_root(void);

NACK_API struct nack_console *nack_console_new(int columns, int rows);
NACK_API void nack_console_free(struct nack_console *console);
NACK_API void nack_console_size(const struct nack_console *console,
                                int *columns, int *rows);
NACK_API bool nack_console_resize(struct nack_console *console,
                                  int columns, int rows);

struct nack_cell {
    uint32_t glyph;                 /* codepoint, or tile index when tiled */
    struct nack_tileset *tileset;   /* NULL means the font                 */
    struct nack_color fg, bg;
};

NACK_API void nack_clear(struct nack_console *console);
NACK_API void nack_clear_to(struct nack_console *console, struct nack_color fg,
                            struct nack_color bg);

NACK_API void nack_put(struct nack_console *console, int x, int y,
                       uint32_t codepoint, struct nack_color fg,
                       struct nack_color bg);

/* Draws a tile from a graphical tileset. tint is applied multiplicatively;
 * pass NACK_WHITE to draw the tile's own colours unchanged. */
NACK_API void nack_put_tile(struct nack_console *console, int x, int y,
                            struct nack_tileset *tileset, int index,
                            struct nack_color tint, struct nack_color bg);

NACK_API void nack_set_glyph(struct nack_console *console, int x, int y,
                             uint32_t codepoint);
NACK_API void nack_set_fg(struct nack_console *console, int x, int y,
                          struct nack_color fg);
NACK_API void nack_set_bg(struct nack_console *console, int x, int y,
                          struct nack_color bg);
NACK_API struct nack_cell nack_get(const struct nack_console *console,
                                   int x, int y);

/* Returns the number of cells written. UTF-8 in, so box drawing and accented
 * characters work if the tileset has them. */
NACK_API int nack_print(struct nack_console *console, int x, int y,
                        struct nack_color fg, struct nack_color bg,
                        const char *utf8);
NACK_API int nack_printf(struct nack_console *console, int x, int y,
                         struct nack_color fg, struct nack_color bg,
                         const char *fmt, ...);
NACK_API int nack_vprintf(struct nack_console *console, int x, int y,
                          struct nack_color fg, struct nack_color bg,
                          const char *fmt, va_list args);

/* Word-wrapped into a box; returns the number of rows used. Pass height 0 to
 * measure without drawing, which is how you size a message log. */
NACK_API int nack_print_wrapped(struct nack_console *console, int x, int y,
                                int width, int height, struct nack_color fg,
                                struct nack_color bg, const char *utf8);

NACK_API void nack_fill(struct nack_console *console, int x, int y,
                        int width, int height, uint32_t codepoint,
                        struct nack_color fg, struct nack_color bg);

/* A single-line box, using the tileset's box drawing characters. The title,
 * when given, is centred on the top edge. */
NACK_API void nack_draw_box(struct nack_console *console, int x, int y,
                            int width, int height, struct nack_color fg,
                            struct nack_color bg, const char *title);

/*
 * Copies a region of one console onto another. fg_alpha and bg_alpha blend
 * against what is already there, so a translucent overlay is a blit with a
 * low bg_alpha.
 */
NACK_API void nack_blit(const struct nack_console *src, int src_x, int src_y,
                        int width, int height, struct nack_console *dst,
                        int dst_x, int dst_y, float fg_alpha, float bg_alpha);

/* -------------------------------------------------------------------------- */
/* Frames                                                                     */
/* -------------------------------------------------------------------------- */

/* Draws the root console and shows it. */
NACK_API void nack_present(void);

NACK_API bool nack_should_close(void);
NACK_API void nack_set_should_close(bool value);

NACK_API double nack_time(void);        /* seconds since nack_init */
NACK_API double nack_delta_time(void);  /* seconds between the last two frames */

/* -------------------------------------------------------------------------- */
/* Input                                                                      */
/* -------------------------------------------------------------------------- */

/* Non-blocking; returns false when nothing is queued. */
NACK_API bool nack_poll_event(struct nack_event *event);
/* Blocks until something happens. A turn-based game can sit here and use no
 * CPU at all between the player's moves. */
NACK_API bool nack_wait_event(struct nack_event *event);
NACK_API bool nack_wait_event_timeout(struct nack_event *event, double seconds);
/* Thread-safe: unblocks a waiting thread, for background work finishing. */
NACK_API void nack_wakeup(void);

/* Instantaneous state, for real-time games that would rather poll. */
NACK_API bool nack_key_down(enum nack_key key);
NACK_API uint32_t nack_mods(void);
NACK_API bool nack_mouse_down(int button);
NACK_API void nack_mouse_cell(int *x, int *y);

/* -------------------------------------------------------------------------- */
/* Window                                                                     */
/* -------------------------------------------------------------------------- */

NACK_API void nack_set_title(const char *title);
NACK_API void nack_set_fullscreen(bool fullscreen);
NACK_API bool nack_is_fullscreen(void);
NACK_API void nack_set_vsync(bool vsync);

NACK_API bool nack_clipboard_set(const char *utf8);
NACK_API const char *nack_clipboard_get(void);

#endif /* NACK_H_INCLUDED */
