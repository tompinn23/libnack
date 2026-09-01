/*
 * Turns the bytes of a tileset file into RGBA pixels.
 *
 * Neither decoder is ours: LodePNG handles PNG and TurboJPEG handles JPEG,
 * both vendored under third_party/. Image decoding is parsing untrusted input,
 * which is the last place to prefer something hand-written over code that has
 * been attacked for twenty years.
 */
#include "nack_image.h"

#include "lodepng.h"
#include "turbojpeg.h"

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
    return NULL;
}

static bool nack__is_png(const uint8_t *b, size_t size)
{
    static const uint8_t signature[8] =
        { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    return size >= sizeof signature &&
           memcmp(b, signature, sizeof signature) == 0;
}

static bool nack__is_jpeg(const uint8_t *b, size_t size)
{
    /* SOI followed by the first marker of any JFIF, Exif or raw JPEG. */
    return size >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF;
}

static uint8_t *nack__decode_png(const uint8_t *bytes, size_t size, int *width,
                                 int *height, const char **error)
{
    unsigned char *rgba = NULL;
    unsigned w = 0, h = 0;
    unsigned status;

    /*
     * decode32 converts whatever the file actually is - palette, greyscale,
     * 16-bit, interlaced - into 8-bit RGBA, which is the only thing the
     * tileset layer wants to think about.
     */
    status = lodepng_decode32(&rgba, &w, &h, bytes, size);
    if (status != 0)
        return nack__image_fail(error, "PNG: %s", lodepng_error_text(status));

    if (w == 0 || h == 0 || w > INT_MAX || h > INT_MAX) {
        free(rgba);
        return nack__image_fail(error, "PNG has an unusable size %ux%u", w, h);
    }

    *width = (int)w;
    *height = (int)h;
    return rgba;
}

static uint8_t *nack__decode_jpeg(const uint8_t *bytes, size_t size,
                                  int *width, int *height, const char **error)
{
    tjhandle handle;
    uint8_t *rgba;
    int w, h;

    handle = tj3Init(TJINIT_DECOMPRESS);
    if (!handle)
        return nack__image_fail(error, "JPEG: cannot start the decoder");

    if (tj3DecompressHeader(handle, bytes, size) != 0) {
        nack__image_fail(error, "JPEG: %s", tj3GetErrorStr(handle));
        tj3Destroy(handle);
        return NULL;
    }

    w = tj3Get(handle, TJPARAM_JPEGWIDTH);
    h = tj3Get(handle, TJPARAM_JPEGHEIGHT);
    if (w <= 0 || h <= 0) {
        nack__image_fail(error, "JPEG has an unusable size %dx%d", w, h);
        tj3Destroy(handle);
        return NULL;
    }

    rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4);
    if (!rgba) {
        nack__image_fail(error, "out of memory decoding a %dx%d JPEG", w, h);
        tj3Destroy(handle);
        return NULL;
    }

    /*
     * TJPF_RGBA writes the alpha byte but leaves it undefined, so it is set
     * afterwards rather than trusted. A JPEG carries no transparency; a
     * tileset that needs some has to be a PNG.
     */
    if (tj3Decompress8(handle, bytes, size, rgba, 0, TJPF_RGBA) != 0) {
        nack__image_fail(error, "JPEG: %s", tj3GetErrorStr(handle));
        tj3Destroy(handle);
        free(rgba);
        return NULL;
    }
    tj3Destroy(handle);

    {
        size_t i, pixels = (size_t)w * (size_t)h;
        for (i = 0; i < pixels; ++i)
            rgba[i * 4 + 3] = 255;
    }

    *width = w;
    *height = h;
    return rgba;
}

uint8_t *nack__image_decode(const void *data, size_t size, int *width,
                            int *height, const char **error)
{
    const uint8_t *bytes = (const uint8_t *)data;

    if (error)
        *error = NULL;
    if (!bytes || size == 0)
        return nack__image_fail(error, "the image is empty");

    if (nack__is_png(bytes, size))
        return nack__decode_png(bytes, size, width, height, error);
    if (nack__is_jpeg(bytes, size))
        return nack__decode_jpeg(bytes, size, width, height, error);

    return nack__image_fail(error,
                            "not a PNG or a JPEG (it starts %02X %02X %02X)",
                            bytes[0], size > 1 ? bytes[1] : 0,
                            size > 2 ? bytes[2] : 0);
}
