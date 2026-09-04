#ifndef NACK_CONSOLE_INTERNAL_H_INCLUDED
#define NACK_CONSOLE_INTERNAL_H_INCLUDED

#include "../nack_internal.h"
#include "nack_gfx.h"

/*
 * C++ only: the structs below hold std::vector and std::string. Nothing
 * still-C reaches in here any more - the Win32 ABI check tests a hand-rolled
 * header replacement against the real SDK one and never links this library
 * at all, and image_test.cpp is C++ now like every other test.
 */
#ifndef __cplusplus
#  error "nack_console_internal.h is C++"
#endif

#include <array>
#include <string>
#include <vector>

/*
 * A tileset is one atlas texture plus the mapping from codepoints to the tiles
 * in it. Fonts and graphical tilesets differ only in how the renderer tints
 * them, which is decided at load time from the image's contents.
 */
/*
 * One codepoint's tile.
 */
struct nack_codepoint_map {
    uint32_t codepoint;
    int index;
};

struct nack_tileset {
    nack_texture *texture = nullptr;
    int tile_width = 0, tile_height = 0;
    int columns = 0, rows = 0;   /* tiles across and down the atlas */
    int count = 0;
    bool is_font = false;       /* tint by the cell's foreground colour */

    /*
     * Codepoint to tile index. Codepoints below 0x100 are the common case and
     * get a direct table; anything above goes in a sorted array, so a sheet
     * with a few hundred CJK glyphs costs a binary search rather than a 1M
     * entry table. Both hold the index as a full int: a 2048x2048 sheet of
     * 8x8 tiles has 65536 of them, so a narrower slot would wrap and map the
     * codepoint to nothing.
     */
    std::array<int, 256> direct{};
    std::vector<nack_codepoint_map> sparse;   /* sorted by codepoint */

    ~nack_tileset();

    /* Where the tile for a codepoint is, or -1 if none is mapped. */
    int index_for(uint32_t codepoint) const;
    bool map(uint32_t codepoint, int index);
    bool map_range(uint32_t first, uint32_t last, int first_index);

    /* Joins/leaves the set present() walks to batch draws by atlas. */
    void track();
    void untrack();

    static nack_tileset *from_rgba(uint8_t *rgba, int width,
                                          int height, int tile_width,
                                          int tile_height,
                                          nack_tileset_layout layout);
    static nack_tileset *builtin();
    static nack_tileset *load_memory(const void *data, size_t size,
                                            int tile_width, int tile_height,
                                            nack_tileset_layout layout);
    static nack_tileset *load(const char *path, int tile_width,
                                     int tile_height,
                                     nack_tileset_layout layout);

    /* Releases a tileset the way every caller needs: tolerant of NULL, and
     * of the built-in font, which nothing but shutdown may free. */
    static void destroy(nack_tileset *tileset);
};

struct nack_console {
    int columns = 0, rows = 0;
    std::vector<nack_cell> cells;

    static nack_console *create(int columns, int rows);

    /* Releases a console the way every caller needs: tolerant of NULL, and
     * of the root console, which only shutdown may free. */
    static void destroy(nack_console *console);

    bool resize(int columns, int rows);

    void clear();
    void clear_to(nack_color fg, nack_color bg);

    void put(int x, int y, uint32_t codepoint, nack_color fg,
            nack_color bg);
    void put_tile(int x, int y, nack_tileset *tileset, int index,
                 nack_color tint, nack_color bg);
    void set_glyph(int x, int y, uint32_t codepoint);
    void set_fg(int x, int y, nack_color fg);
    void set_bg(int x, int y, nack_color bg);
    nack_cell get(int x, int y) const;

    int print(int x, int y, nack_color fg, nack_color bg,
             const char *utf8);
    int print_wrapped(int x, int y, int width, int height,
                      nack_color fg, nack_color bg,
                      const char *utf8);

    void fill(int x, int y, int width, int height, uint32_t codepoint,
             nack_color fg, nack_color bg);
    void draw_box(int x, int y, int width, int height, nack_color fg,
                 nack_color bg, const char *title);

    /* Copies this console onto `dst`. Alphas below 1 blend. */
    void blit_to(nack_console *dst, int src_x, int src_y, int width,
                int height, int dst_x, int dst_y, float fg_alpha,
                float bg_alpha) const;
};

namespace nack { namespace detail {

/* Decodes one codepoint and advances the cursor. Invalid bytes yield U+FFFD
 * and consume one byte, so bad input cannot stall the loop. Not a method:
 * it walks a raw cursor, not a console. */
uint32_t utf8_next(const char **cursor);

} }   /* namespace nack::detail */

struct nack_console_state {
    bool initialized = false;
    nack_window *window = nullptr;
    nack_gl_context *gl = nullptr;

    nack_console *root = nullptr;
    nack_tileset *font = nullptr;
    nack_tileset *builtin_font = nullptr;

    /* Every tileset ever handed out, so present() can batch by atlas. */
    std::vector<nack_tileset *> tilesets;

    nack_scaling scaling = NACK_SCALE_INTEGER;
    nack_color letterbox = NACK_BLACK;
    bool auto_resize = false;
    bool vsync = true;

    /* Where the console sits inside the window, in framebuffer pixels. */
    int viewport_x = 0, viewport_y = 0, viewport_w = 0, viewport_h = 0;
    int fb_width = 0, fb_height = 0;
    float dpi_scale = 0.0f;

    /* Quad scratch space, reused between frames. */
    std::vector<float> vertices;

    double start_time = 0.0;
    double last_frame_time = 0.0;
    double delta = 0.0;

    int mouse_cell_x = 0, mouse_cell_y = 0;

    /* Set by set_should_close(); should_close() also asks the window. */
    bool close_requested = false;

    std::string error;
    bool has_error = false;

    /* nack_api.cpp: lifetime, frames, input, window, clipboard */
    bool init(const nack_config *config);
    void shutdown();

    void present();

    bool should_close() const;
    void set_should_close(bool value);

    double time() const;
    double delta_time() const;

    bool poll_event(nack_event *event);
    bool wait_event(nack_event *event);
    bool wait_event_timeout(nack_event *event, double seconds);
    void wakeup();

    bool key_down(nack_key key) const;
    uint32_t mods() const;
    bool mouse_down(int button) const;
    void mouse_cell(int *x, int *y) const;

    void set_title(const char *title);
    void set_fullscreen(bool fullscreen);
    bool is_fullscreen() const;
    void set_vsync(bool vsync);

    void set_font(nack_tileset *tileset);

    bool clipboard_set(const char *utf8);
    const char *clipboard_get() const;

    /* Sets the error reported by nack::app::last_error() and returns false. */
    bool set_error(const char *fmt, ...);
    /* Forgets it again, for a failure that was recovered from. */
    void clear_error();
    const char *last_error() const;
};

namespace nack { namespace detail {

extern nack_console_state console_state;

/* nack_render.c */
void render_console(const nack_console *console);
void render_update_viewport();

/*
 * Reads one pixel back out of the framebuffer. Exists so the tests can check
 * that a frame actually rendered rather than merely that present() returned;
 * it is deliberately not part of the public API.
 */
/*
 * Turns on keeping a copy of each presented frame, which debug_read_pixel
 * then reads from. Off by default because it costs a framebuffer read per
 * frame; the tests turn it on right after constructing an app.
 */
void debug_capture_frames(bool capture);
bool debug_read_pixel(int cell_x, int cell_y, uint8_t rgba[4]);

/*
 * Makes the next `count` texture creations fail. The cleanup that runs when a
 * texture cannot be made is unreachable otherwise - every renderer this
 * library has creates textures that always succeed - and it is exactly the
 * path where a half-built tileset has to be released. Test-only, like
 * NACK_RENDERER=test-fail.
 */
void debug_fail_next_textures(int count);

} }   /* namespace nack::detail */

#endif /* NACK_CONSOLE_INTERNAL_H_INCLUDED */
