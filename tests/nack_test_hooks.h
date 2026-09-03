/*
 * The internals the tests reach into, declared for C.
 *
 * The library's own internal headers are C++ now - their structs hold
 * std::vector and std::string - but the tests are deliberately C, because
 * compiling them as C is what proves the public API is still usable from C.
 * They only ever call these functions; none of them touches a struct field.
 */
#ifndef NACK_TEST_HOOKS_H_INCLUDED
#define NACK_TEST_HOOKS_H_INCLUDED

#include "nack/nack.h"
#include "nack_backend_id.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* src/console/nack_gfx.h */
const char *nack__gfx_name(void);

/* src/console/nack_console_internal.h */
void nack__debug_capture_frames(bool capture);
bool nack__debug_read_pixel(int cell_x, int cell_y, uint8_t rgba[4]);
void nack__debug_fail_next_textures(int count);
struct nack_tileset *nack__tileset_from_rgba(uint8_t *rgba, int width,
                                             int height, int tile_width,
                                             int tile_height,
                                             enum nack_tileset_layout layout);
int nack__tileset_index_for(const struct nack_tileset *tileset,
                            uint32_t codepoint);

/* src/console/nack_image.h */
uint8_t *nack__image_decode(const void *data, size_t size, int *width,
                            int *height, const char **error);
void nack__image_free(uint8_t *pixels);

/* src/nack_window.h (the enum comes from src/nack_backend_id.h) */
enum nack_backend nack__win_get_backend(void);

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* NACK_TEST_HOOKS_H_INCLUDED */
