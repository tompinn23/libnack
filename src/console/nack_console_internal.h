#ifndef NACK_CONSOLE_INTERNAL_H_INCLUDED
#define NACK_CONSOLE_INTERNAL_H_INCLUDED

#include "../nack_internal.h"
#include "nack_gfx.h"

/*
 * C++ only: the structs below hold std::vector and std::string. image_test.c
 * and the Win32 ABI check are still C on purpose and still link against this
 * library; they reach the few internals they need through
 * tests/nack_test_hooks.h, which declares those functions without the
 * layouts.
 */
#ifndef __cplusplus
#  error "nack_console_internal.h is C++; C callers want tests/nack_test_hooks.h"
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
    struct nack_texture *texture;
    int tile_width, tile_height;
    int columns, rows;          /* tiles across and down the atlas */
    int count;
    bool is_font;               /* tint by the cell's foreground colour */

    /*
     * Codepoint to tile index. Codepoints below 0x100 are the common case and
     * get a direct table; anything above goes in a sorted array, so a sheet
     * with a few hundred CJK glyphs costs a binary search rather than a 1M
     * entry table. Both hold the index as a full int: a 2048x2048 sheet of
     * 8x8 tiles has 65536 of them, so a narrower slot would wrap and map the
     * codepoint to nothing.
     */
    std::array<int, 256> direct;
    std::vector<struct nack_codepoint_map> sparse;   /* sorted by codepoint */
};

struct nack_console {
    int columns, rows;
    std::vector<struct nack_cell> cells;
};

struct nack_console_state {
    bool initialized;
    struct nack_window *window;
    struct nack_gl_context *gl;

    struct nack_console *root;
    struct nack_tileset *font;
    struct nack_tileset *builtin_font;

    /* Every tileset ever handed out, so present() can batch by atlas. */
    std::vector<struct nack_tileset *> tilesets;

    enum nack_scaling scaling;
    struct nack_color letterbox;
    bool auto_resize;
    bool vsync;

    /* Where the console sits inside the window, in framebuffer pixels. */
    int viewport_x, viewport_y, viewport_w, viewport_h;
    int fb_width, fb_height;
    float dpi_scale;

    /* Quad scratch space, reused between frames. */
    std::vector<float> vertices;

    double start_time;
    double last_frame_time;
    double delta;

    int mouse_cell_x, mouse_cell_y;
    bool should_close;

    std::string error;
    bool has_error;
};

extern struct nack_console_state nack__c;

/* Sets the error reported by nack::app::last_error() and returns false. */
bool nack__error(const char *fmt, ...);

/* Forgets it again, for a failure that was recovered from. */
void nack__clear_error(void);

/* nack_tileset.c */
struct nack_tileset *nack__tileset_from_rgba(uint8_t *rgba, int width, int height,
                                             int tile_width, int tile_height,
                                             enum nack_tileset_layout layout);
struct nack_tileset *nack__tileset_builtin(void);
int  nack__tileset_index_for(const struct nack_tileset *tileset,
                             uint32_t codepoint);
void nack__tileset_register(struct nack_tileset *tileset);
void nack__tileset_unregister(struct nack_tileset *tileset);

/* nack_render.c */
void nack__render_console(const struct nack_console *console);
void nack__render_update_viewport(void);

/*
 * Reads one pixel back out of the framebuffer. Exists so the tests can check
 * that a frame actually rendered rather than merely that present() returned;
 * it is deliberately not part of the public API.
 */
/*
 * Turns on keeping a copy of each presented frame, which nack__debug_read_pixel
 * then reads from. Off by default because it costs a framebuffer read per
 * frame; the tests turn it on right after constructing an app.
 */
void nack__debug_capture_frames(bool capture);
bool nack__debug_read_pixel(int cell_x, int cell_y, uint8_t rgba[4]);

/*
 * Makes the next `count` texture creations fail. The cleanup that runs when a
 * texture cannot be made is unreachable otherwise - every renderer this
 * library has creates textures that always succeed - and it is exactly the
 * path where a half-built tileset has to be released. Test-only, like
 * NACK_RENDERER=test-fail.
 */
void nack__debug_fail_next_textures(int count);

/* nack_console.cpp */
struct nack_console *nack__console_resolve(struct nack_console *console);
uint32_t nack__utf8_next(const char **cursor);

struct nack_console *nack__console_new(int columns, int rows);
void nack__console_free(struct nack_console *console);
void nack__console_size(const struct nack_console *console, int *columns,
                        int *rows);
bool nack__console_resize(struct nack_console *console, int columns, int rows);
void nack__console_clear(struct nack_console *console);
void nack__console_clear_to(struct nack_console *console, struct nack_color fg,
                            struct nack_color bg);
void nack__console_put(struct nack_console *console, int x, int y,
                       uint32_t codepoint, struct nack_color fg,
                       struct nack_color bg);
void nack__console_put_tile(struct nack_console *console, int x, int y,
                            struct nack_tileset *tileset, int index,
                            struct nack_color tint, struct nack_color bg);
void nack__console_set_glyph(struct nack_console *console, int x, int y,
                             uint32_t codepoint);
void nack__console_set_fg(struct nack_console *console, int x, int y,
                          struct nack_color fg);
void nack__console_set_bg(struct nack_console *console, int x, int y,
                          struct nack_color bg);
struct nack_cell nack__console_get(const struct nack_console *console, int x,
                                   int y);
int nack__console_print(struct nack_console *console, int x, int y,
                        struct nack_color fg, struct nack_color bg,
                        const char *utf8);
int nack__console_print_wrapped(struct nack_console *console, int x, int y,
                                int width, int height, struct nack_color fg,
                                struct nack_color bg, const char *utf8);
void nack__console_fill(struct nack_console *console, int x, int y, int width,
                        int height, uint32_t codepoint, struct nack_color fg,
                        struct nack_color bg);
void nack__console_draw_box(struct nack_console *console, int x, int y,
                            int width, int height, struct nack_color fg,
                            struct nack_color bg, const char *title);
void nack__console_blit(const struct nack_console *src, int src_x, int src_y,
                        int width, int height, struct nack_console *dst,
                        int dst_x, int dst_y, float fg_alpha, float bg_alpha);

/* nack_tileset.cpp */
struct nack_tileset *nack__tileset_load_memory(const void *data, size_t size,
                                               int tile_width, int tile_height,
                                               enum nack_tileset_layout layout);
struct nack_tileset *nack__tileset_load(const char *path, int tile_width,
                                        int tile_height,
                                        enum nack_tileset_layout layout);
void nack__tileset_free(struct nack_tileset *tileset);
void nack__tileset_size(const struct nack_tileset *tileset, int *tile_width,
                        int *tile_height, int *count);
bool nack__tileset_map(struct nack_tileset *tileset, uint32_t codepoint,
                       int index);
bool nack__tileset_map_range(struct nack_tileset *tileset, uint32_t first,
                             uint32_t last, int first_index);
void nack__app_set_font(struct nack_tileset *tileset);
struct nack_tileset *nack__app_get_font(void);

/* nack_api.cpp: lifetime, frames, input, window, clipboard */
void nack__app_config_defaults(struct nack_config *config);
bool nack__app_init(const struct nack_config *config);
void nack__app_shutdown(void);
void nack__app_present(void);
struct nack_console *nack__app_root(void);
bool nack__app_should_close(void);
void nack__app_set_should_close(bool value);
double nack__app_time(void);
double nack__app_delta_time(void);
bool nack__app_poll_event(struct nack_event *event);
bool nack__app_wait_event(struct nack_event *event);
bool nack__app_wait_event_timeout(struct nack_event *event, double seconds);
void nack__app_wakeup(void);
bool nack__app_key_down(enum nack_key key);
uint32_t nack__app_mods(void);
bool nack__app_mouse_down(int button);
void nack__app_mouse_cell(int *x, int *y);
void nack__app_set_title(const char *title);
void nack__app_set_fullscreen(bool fullscreen);
bool nack__app_is_fullscreen(void);
void nack__app_set_vsync(bool vsync);
bool nack__app_clipboard_set(const char *utf8);
const char *nack__app_clipboard_get(void);
const char *nack__app_get_error(void);

#endif /* NACK_CONSOLE_INTERNAL_H_INCLUDED */
