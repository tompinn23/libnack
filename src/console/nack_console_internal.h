#ifndef NACK_CONSOLE_INTERNAL_H_INCLUDED
#define NACK_CONSOLE_INTERNAL_H_INCLUDED

#include "../nack_internal.h"
#include "nack_gfx.h"

/*
 * libnack is C++ now, but its API is C and its tests and examples are C, so
 * these declarations keep C linkage. This is what lets a C program - and
 * anything binding through a C ABI - keep using the library unchanged.
 */
#ifdef __cplusplus
extern "C" {
#endif

#define NACK_MAX_TILESETS 16

/*
 * A tileset is one atlas texture plus the mapping from codepoints to the tiles
 * in it. Fonts and graphical tilesets differ only in how the renderer tints
 * them, which is decided at load time from the image's contents.
 */
/*
 * One codepoint's tile. Declared here rather than inside nack_tileset because
 * C++ scopes a nested type to its enclosing class where C does not, and this
 * header is compiled as both.
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
    int32_t direct[256];
    struct nack_codepoint_map *sparse;
    size_t sparse_count, sparse_capacity;
};

struct nack_console {
    int columns, rows;
    struct nack_cell *cells;
};

struct nack_console_state {
    bool initialized;
    struct nack_window *window;
    struct nack_gl_context *gl;

    struct nack_console *root;
    struct nack_tileset *font;
    struct nack_tileset *builtin_font;

    /* Every tileset ever handed out, so present() can batch by atlas. */
    struct nack_tileset *tilesets[NACK_MAX_TILESETS];
    size_t tileset_count;

    enum nack_scaling scaling;
    struct nack_color letterbox;
    bool auto_resize;
    bool vsync;

    /* Where the console sits inside the window, in framebuffer pixels. */
    int viewport_x, viewport_y, viewport_w, viewport_h;
    int fb_width, fb_height;
    float dpi_scale;

    /* Quad scratch space, reused between frames. */
    float *vertices;
    size_t vertex_capacity;

    double start_time;
    double last_frame_time;
    double delta;

    int mouse_cell_x, mouse_cell_y;
    bool should_close;

    char error[512];
    bool has_error;
};

extern struct nack_console_state nack__c;

/* Sets the error reported by nack_get_error and returns false. */
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
 * frame; the tests turn it on right after nack_init.
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

/* nack_console.c */
struct nack_console *nack__console_resolve(struct nack_console *console);
uint32_t nack__utf8_next(const char **cursor);


#ifdef __cplusplus
}   /* extern "C" */
#endif
#endif /* NACK_CONSOLE_INTERNAL_H_INCLUDED */
