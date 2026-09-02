#ifndef NACK_IMAGE_H_INCLUDED
#define NACK_IMAGE_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * libnack is C++ now, but its API is C and its tests and examples are C, so
 * these declarations keep C linkage. This is what lets a C program - and
 * anything binding through a C ABI - keep using the library unchanged.
 */
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decodes a PNG or a JPEG to 8-bit RGBA. The format is taken from the bytes
 * themselves rather than from a filename, so a tileset loaded from memory
 * needs no hint. Returns a buffer the caller frees, or NULL on failure with
 * *error describing why.
 *
 * Both go through stb_image, vendored under third_party/. Between them that
 * covers every colour type, bit depth and interlacing a tileset might ship
 * as, baseline and progressive JPEG included; a JPEG has no alpha, so it
 * decodes fully opaque.
 *
 * The error string lives in a buffer belonging to this module and is
 * overwritten by the next failure, which matches the rest of the library:
 * everything here runs on one thread.
 */
uint8_t *nack__image_decode(const void *data, size_t size, int *width,
                            int *height, const char **error);

/* Returns a decoded image. Not interchangeable with free(). */
void nack__image_free(uint8_t *pixels);


#ifdef __cplusplus
}   /* extern "C" */
#endif
#endif /* NACK_IMAGE_H_INCLUDED */
