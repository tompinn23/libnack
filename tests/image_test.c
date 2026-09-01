/*
 * Checks that nack__image_decode turns files into the RGBA the tileset layer
 * expects. LodePNG and TurboJPEG are tested upstream; what is tested here is
 * our use of them - the sniffing, the sizes, the alpha, and that bad input is
 * refused rather than crashing.
 *
 * PNG is compared pixel-exact against fixtures produced by tools/mkpng.py,
 * which the build runs into the build tree; the directory arrives as argv[1].
 * Missing fixtures are a skip rather than a failure, since that means the
 * generator did not run (no Python), not that the decoder is wrong.
 *
 * JPEG has no fixtures because it is lossy and there is nothing exact to
 * compare against. Instead a known image is compressed with the vendored
 * encoder and decoded back, which exercises the same path a real tileset
 * takes.
 */
#include "console/nack_image.h"
#include "turbojpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(*size);
    if (fread(buf, 1, *size, f) != *size) { free(buf); fclose(f); return NULL; }
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char expected_path[512];
    size_t esz;
    unsigned char *exp;
    size_t off = 0;
    unsigned count, i;
    int failures = 0;

    snprintf(expected_path, sizeof expected_path, "%s/expected.bin", dir);
    exp = slurp(expected_path, &esz);
    if (!exp) {
        fprintf(stderr, "no fixtures in %s; run tools/mkpng.py, skipping\n", dir);
        return 77;
    }
    memcpy(&count, exp + off, 4); off += 4;

    for (i = 0; i < count; ++i) {
        unsigned namelen, w, h, x, y;
        char name[64], path[576];
        unsigned char *png, *rgba;
        size_t psz;
        const char *err = NULL;
        int bad = 0;

        memcpy(&namelen, exp + off, 4); off += 4;
        memcpy(name, exp + off, namelen); name[namelen] = 0; off += namelen;
        memcpy(&w, exp + off, 4); off += 4;
        memcpy(&h, exp + off, 4); off += 4;

        snprintf(path, sizeof path, "%s/%s", dir, name);
        png = slurp(path, &psz);
        if (!png) { printf("%-12s MISSING\n", name); failures++; off += (size_t)w*h*4; continue; }

        int dw = 0, dh = 0;
        rgba = nack__image_decode(png, psz, &dw, &dh, &err);
        if (!rgba) {
            printf("%-12s DECODE FAILED: %s\n", name, err ? err : "?");
            failures++; free(png); off += (size_t)w*h*4; continue;
        }
        if ((unsigned)dw != w || (unsigned)dh != h) {
            printf("%-12s SIZE %dx%d expected %ux%u\n", name, dw, dh, w, h);
            failures++; bad = 1;
        }
        for (y = 0; y < h && !bad; ++y) {
            for (x = 0; x < w; ++x) {
                const unsigned char *e = exp + off + ((size_t)y*w + x) * 4;
                const unsigned char *g = rgba + ((size_t)y*w + x) * 4;
                if (memcmp(e, g, 4) != 0) {
                    printf("%-12s PIXEL MISMATCH at %u,%u: got %u,%u,%u,%u want %u,%u,%u,%u\n",
                           name, x, y, g[0],g[1],g[2],g[3], e[0],e[1],e[2],e[3]);
                    failures++; bad = 1; break;
                }
            }
        }
        if (!bad) printf("%-12s ok  (%ux%u)\n", name, w, h);
        off += (size_t)w * h * 4;
        free(rgba); free(png);
    }

    /* Malformed input must be rejected, not crash. */
    {
        const char *err;
        unsigned char junk[64];
        int w, h;
        memset(junk, 0xA5, sizeof junk);
        if (nack__image_decode(junk, sizeof junk, &w, &h, &err) != NULL) {
            printf("junk input accepted\n"); failures++;
        } else printf("%-12s ok  (rejected: %s)\n", "junk", err);

        char path[576];
        size_t psz;
        unsigned char *png;

        snprintf(path, sizeof path, "%s/rgba8.png", dir);
        png = slurp(path, &psz);
        if (png) {
            if (nack__image_decode(png, psz / 2, &w, &h, &err) != NULL) {
                printf("truncated PNG accepted\n"); failures++;
            } else printf("%-12s ok  (rejected: %s)\n", "truncated", err);
            png[30] ^= 0xFF;   /* corrupt the compressed stream */
            if (nack__image_decode(png, psz, &w, &h, &err) != NULL)
                printf("%-12s ok  (corrupt data decoded to something; no crash)\n", "corrupt");
            else
                printf("%-12s ok  (rejected: %s)\n", "corrupt", err);
            free(png);
        }
    }

    /*
     * JPEG, round-tripped through the vendored encoder. The comparison is a
     * tolerance rather than an equality because JPEG is lossy; what is being
     * checked is that the image comes back the right size, the right way up,
     * recognisably the same picture, and fully opaque.
     */
    {
        enum { JW = 64, JH = 48 };
        unsigned char *source = (unsigned char *)malloc(JW * JH * 4);
        unsigned char *jpeg = NULL;
        size_t jpeg_size = 0;
        tjhandle encoder;
        int x, y;

        for (y = 0; y < JH; ++y) {
            for (x = 0; x < JW; ++x) {
                unsigned char *p = source + ((size_t)y * JW + x) * 4;
                /* Smooth gradients: sharp edges are what JPEG blurs most. */
                p[0] = (unsigned char)(x * 4);
                p[1] = (unsigned char)(y * 5);
                p[2] = (unsigned char)(128);
                p[3] = 255;
            }
        }

        encoder = tj3Init(TJINIT_COMPRESS);
        if (!encoder || tj3Set(encoder, TJPARAM_QUALITY, 95) != 0 ||
            tj3Set(encoder, TJPARAM_SUBSAMP, TJSAMP_444) != 0 ||
            tj3Compress8(encoder, source, JW, 0, JH, TJPF_RGBA, &jpeg,
                         &jpeg_size) != 0) {
            printf("%-12s FAILED to build a test JPEG\n", "jpeg");
            failures++;
        } else {
            const char *err = NULL;
            int dw = 0, dh = 0;
            unsigned char *back =
                nack__image_decode(jpeg, jpeg_size, &dw, &dh, &err);

            if (!back) {
                printf("%-12s DECODE FAILED: %s\n", "jpeg", err ? err : "?");
                failures++;
            } else {
                int worst = 0, opaque = 1;
                if (dw != JW || dh != JH) {
                    printf("%-12s SIZE %dx%d expected %dx%d\n", "jpeg",
                           dw, dh, JW, JH);
                    failures++;
                } else {
                    size_t i;
                    for (i = 0; i < (size_t)JW * JH * 4; ++i) {
                        int d = (int)back[i] - (int)source[i];
                        if (i % 4 == 3) {
                            if (back[i] != 255) opaque = 0;
                            continue;
                        }
                        if (d < 0) d = -d;
                        if (d > worst) worst = d;
                    }
                    if (worst > 8) {
                        printf("%-12s off by %d, more than JPEG should be\n",
                               "jpeg", worst);
                        failures++;
                    } else {
                        printf("%-12s ok  (%dx%d, worst channel off by %d)\n",
                               "jpeg", dw, dh, worst);
                    }
                    if (!opaque) {
                        printf("%-12s alpha not filled in\n", "jpeg");
                        failures++;
                    }
                }
                free(back);
            }
            /* A JPEG cut in half must be refused, not read past its end. */
            {
                const char *terr = NULL;
                int tw, th;
                if (nack__image_decode(jpeg, jpeg_size / 2, &tw, &th, &terr)
                        != NULL) {
                    printf("truncated JPEG accepted\n");
                    failures++;
                } else {
                    printf("%-12s ok  (rejected: %s)\n", "jpeg-cut",
                           terr ? terr : "?");
                }
            }
        }
        tj3Free(jpeg);
        if (encoder) tj3Destroy(encoder);
        free(source);
    }

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
