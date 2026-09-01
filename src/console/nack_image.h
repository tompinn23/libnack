#ifndef NACK_IMAGE_H_INCLUDED
#define NACK_IMAGE_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Decodes a PNG or a JPEG to 8-bit RGBA. The format is taken from the bytes
 * themselves rather than from a filename, so a tileset loaded from memory
 * needs no hint. Returns a buffer the caller frees, or NULL on failure with
 * *error describing why.
 *
 * PNG goes through LodePNG and JPEG through TurboJPEG, both vendored under
 * third_party/. Between them that covers every colour type, bit depth and
 * interlacing a tileset might ship as; a JPEG has no alpha, so it decodes
 * fully opaque.
 *
 * The error string lives in a buffer belonging to this module and is
 * overwritten by the next failure, which matches the rest of the library:
 * everything here runs on one thread.
 */
uint8_t *nack__image_decode(const void *data, size_t size, int *width,
                            int *height, const char **error);

#endif /* NACK_IMAGE_H_INCLUDED */
