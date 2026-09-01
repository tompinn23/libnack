#ifndef NACK_PNG_H_INCLUDED
#define NACK_PNG_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Decodes a PNG to 8-bit RGBA. Returns a buffer the caller frees, or NULL on
 * failure with *error pointing at a static description.
 *
 * Supports the colour types and bit depths tilesets actually ship as:
 * greyscale, greyscale+alpha, RGB, RGBA and palette, at 1, 2, 4, 8 and 16 bits
 * per sample, with tRNS transparency. Interlaced images are rejected rather
 * than silently mangled.
 */
uint8_t *nack__png_decode(const void *data, size_t size, int *width, int *height,
                          const char **error);

#endif /* NACK_PNG_H_INCLUDED */
