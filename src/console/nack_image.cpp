/*
 * Turns the bytes of a tileset file into RGBA pixels.
 *
 * The decoder is not ours: stb_image handles both PNG and JPEG, vendored under
 * third_party/. Decoding an image is parsing untrusted input, which is the
 * last place to prefer something hand-written.
 *
 * The format is worked out from the bytes rather than from a filename, so a
 * tileset embedded in the executable needs no hint. Only PNG and JPEG are
 * compiled in; see third_party/stb/stb_image.c for why.
 */
#include "nack_image.h"

#include "stb_image.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char nack__image_error[256];

static uint8_t *nack__image_fail(const char **error, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(nack__image_error, sizeof nack__image_error, fmt, args);
    va_end(args);
    if (error)
        *error = nack__image_error;
    return nullptr;
}

uint8_t *nack__image_decode(const void *data, size_t size, int *width,
                            int *height, const char **error)
{
    unsigned char *rgba;
    int w = 0, h = 0, channels = 0;

    if (error)
        *error = nullptr;
    if (!data || size == 0)
        return nack__image_fail(error, "the image is empty");
    if (size > (size_t)INT_MAX)
        return nack__image_fail(error, "the image is implausibly large");

    /*
     * Asking for four channels is what makes the rest of the library simple:
     * whatever the file is - palette, greyscale, 16-bit, interlaced, or a
     * JPEG with no alpha at all - it arrives as 8-bit RGBA, opaque where the
     * source had no transparency of its own.
     */
    rgba = stbi_load_from_memory((const unsigned char *)data, (int)size,
                                 &w, &h, &channels, 4);
    if (!rgba) {
        const char *why = stbi_failure_reason();
        return nack__image_fail(error, "%s", why ? why : "cannot be decoded");
    }

    if (w <= 0 || h <= 0) {
        stbi_image_free(rgba);
        return nack__image_fail(error, "image has an unusable size %dx%d",
                                w, h);
    }

    *width = w;
    *height = h;
    return rgba;
}

void nack__image_free(uint8_t *pixels)
{
    /*
     * stb_image allocates through its own macros, so its buffers go back the
     * same way rather than to free(). They are the same allocator today, but
     * that is stb's business to change, not ours to assume.
     */
    stbi_image_free(pixels);
}
