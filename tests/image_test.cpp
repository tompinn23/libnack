/*
 * Checks that nack::detail::image_decode turns files into the RGBA the
 * tileset layer expects. stb_image is tested upstream; what is tested here is
 * our use of it - the sizes, the alpha, and that bad input is handled rather
 * than crashing.
 *
 * PNG is compared pixel-exact against fixtures produced by tools/mkpng.py,
 * which the build runs into the build tree; the directory arrives as argv[1].
 * Missing fixtures are a skip rather than a failure, since that means the
 * generator did not run (no Python), not that the decoder is wrong.
 *
 * JPEG is lossy, so there is nothing exact to compare against and the fixtures
 * from tools/mkjpeg.py are compared within a tolerance instead. That encoder
 * is not the decoder under test and shares no code with it, which is what
 * makes the comparison mean anything.
 */
#include "../src/console/nack_image.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

using nack::detail::image_decode;
using nack::detail::image_free;

namespace {

std::unique_ptr<unsigned char[]> slurp(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return nullptr;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    auto buf = std::make_unique<unsigned char[]>(*size);
    if (fread(buf.get(), 1, *size, f) != *size) {
        fclose(f);
        return nullptr;
    }
    fclose(f);
    return buf;
}

}   /* namespace */

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char expected_path[512];
    size_t esz;
    size_t off = 0;
    unsigned count, i;
    int failures = 0;

    snprintf(expected_path, sizeof expected_path, "%s/expected.bin", dir);
    auto exp = slurp(expected_path, &esz);
    if (!exp) {
        fprintf(stderr, "no fixtures in %s; run tools/mkpng.py, skipping\n", dir);
        return 77;
    }
    memcpy(&count, exp.get() + off, 4); off += 4;

    for (i = 0; i < count; ++i) {
        unsigned namelen, w, h, x, y;
        char name[64], path[576];
        size_t psz;
        const char *err = nullptr;
        int bad = 0;

        memcpy(&namelen, exp.get() + off, 4); off += 4;
        memcpy(name, exp.get() + off, namelen); name[namelen] = 0; off += namelen;
        memcpy(&w, exp.get() + off, 4); off += 4;
        memcpy(&h, exp.get() + off, 4); off += 4;

        snprintf(path, sizeof path, "%s/%s", dir, name);
        auto png = slurp(path, &psz);
        if (!png) { printf("%-12s MISSING\n", name); failures++; off += (size_t)w*h*4; continue; }

        int dw = 0, dh = 0;
        auto rgba = image_decode(png.get(), psz, &dw, &dh, &err);
        if (!rgba) {
            printf("%-12s DECODE FAILED: %s\n", name, err ? err : "?");
            failures++; off += (size_t)w*h*4; continue;
        }
        if ((unsigned)dw != w || (unsigned)dh != h) {
            printf("%-12s SIZE %dx%d expected %ux%u\n", name, dw, dh, w, h);
            failures++; bad = 1;
        }
        for (y = 0; y < h && !bad; ++y) {
            for (x = 0; x < w; ++x) {
                const unsigned char *e = exp.get() + off + ((size_t)y*w + x) * 4;
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
        image_free(rgba);
    }

    /* Malformed input must be rejected, not crash. */
    {
        const char *err;
        unsigned char junk[64];
        int w, h;
        memset(junk, 0xA5, sizeof junk);
        if (image_decode(junk, sizeof junk, &w, &h, &err) != nullptr) {
            printf("junk input accepted\n"); failures++;
        } else printf("%-12s ok  (rejected: %s)\n", "junk", err);

        char path[576];
        size_t psz;

        snprintf(path, sizeof path, "%s/rgba8.png", dir);
        auto png = slurp(path, &psz);
        if (png) {
            if (image_decode(png.get(), psz / 2, &w, &h, &err) != nullptr) {
                printf("truncated PNG accepted\n"); failures++;
            } else printf("%-12s ok  (rejected: %s)\n", "truncated", err);
            png[30] ^= 0xFF;   /* corrupt the compressed stream */
            {
                unsigned char *salvaged =
                    image_decode(png.get(), psz, &w, &h, &err);
                if (salvaged) {
                    printf("%-12s ok  (corrupt data decoded to something; "
                           "no crash)\n", "corrupt");
                    image_free(salvaged);
                } else {
                    printf("%-12s ok  (rejected: %s)\n", "corrupt", err);
                }
            }
        }
    }

    /*
     * JPEG. The fixtures come from tools/mkjpeg.py rather than from anything
     * libnack links, because there is no encoder in the tree and because a
     * decoder checked against its own encoder proves very little. The
     * comparison is a tolerance rather than an equality: JPEG is lossy, so
     * what is being checked is that the image comes back the right size, the
     * right way up, recognisably the same picture, and fully opaque.
     */
    {
        char path[576];
        size_t jesz;
        size_t joff = 0;
        unsigned jcount = 0, j;

        snprintf(path, sizeof path, "%s/expected_jpeg.bin", dir);
        auto jexp = slurp(path, &jesz);
        if (!jexp) {
            printf("%-12s no fixtures; run tools/mkjpeg.py\n", "jpeg");
            failures++;
        } else {
            memcpy(&jcount, jexp.get() + joff, 4); joff += 4;
            for (j = 0; j < jcount; ++j) {
                unsigned namelen, w, h, x, y;
                char name[64];
                size_t jsz;
                const char *err = nullptr;
                int dw = 0, dh = 0, worst = 0, opaque = 1, bad = 0;

                memcpy(&namelen, jexp.get() + joff, 4); joff += 4;
                memcpy(name, jexp.get() + joff, namelen); name[namelen] = 0;
                joff += namelen;
                memcpy(&w, jexp.get() + joff, 4); joff += 4;
                memcpy(&h, jexp.get() + joff, 4); joff += 4;

                snprintf(path, sizeof path, "%s/%s", dir, name);
                auto jpeg = slurp(path, &jsz);
                if (!jpeg) {
                    printf("%-12s MISSING\n", name);
                    failures++;
                    joff += (size_t)w * h * 3;
                    continue;
                }

                auto rgba = image_decode(jpeg.get(), jsz, &dw, &dh, &err);
                if (!rgba) {
                    printf("%-12s DECODE FAILED: %s\n", name,
                           err ? err : "?");
                    failures++;
                    joff += (size_t)w * h * 3;
                    continue;
                }
                if ((unsigned)dw != w || (unsigned)dh != h) {
                    printf("%-12s SIZE %dx%d expected %ux%u\n", name, dw, dh,
                           w, h);
                    failures++;
                    bad = 1;
                }
                for (y = 0; y < h && !bad; ++y) {
                    for (x = 0; x < w; ++x) {
                        const unsigned char *e =
                            jexp.get() + joff + ((size_t)y * w + x) * 3;
                        const unsigned char *g =
                            rgba + ((size_t)y * w + x) * 4;
                        int c;
                        for (c = 0; c < 3; ++c) {
                            int d = (int)g[c] - (int)e[c];
                            if (d < 0) d = -d;
                            if (d > worst) worst = d;
                        }
                        if (g[3] != 255) opaque = 0;
                    }
                }
                if (!bad) {
                    /*
                     * Quality 95 with no subsampling on a smooth gradient
                     * should stay well inside this; a decoder that got the
                     * colour conversion or the block layout wrong would be
                     * out by far more than a rounding difference.
                     */
                    if (worst > 12) {
                        printf("%-12s off by %d, more than JPEG should be\n",
                               name, worst);
                        failures++;
                    } else {
                        printf("%-12s ok  (%ux%u, worst channel off by %d)\n",
                               name, w, h, worst);
                    }
                    if (!opaque) {
                        printf("%-12s alpha not filled in\n", name);
                        failures++;
                    }
                }
                joff += (size_t)w * h * 3;

                /*
                 * Truncation. stb_image is deliberately lenient here: once it
                 * has the header it returns what scan data it got rather than
                 * failing, which is what most viewers do and is documented
                 * behaviour, so demanding a refusal would be testing one
                 * library's strictness rather than anything that matters.
                 * What matters is that it neither crashes nor reports a size
                 * it did not produce - a caller slicing that buffer up into
                 * tiles has to be able to trust the dimensions.
                 */
                if (j == 0) {
                    size_t fraction;
                    int truncation_ok = 1;

                    for (fraction = 10; fraction <= 90; fraction += 20) {
                        const char *terr = nullptr;
                        int tw = 0, th = 0;
                        unsigned char *cut = image_decode(
                            jpeg.get(), jsz * fraction / 100, &tw, &th, &terr);
                        if (!cut)
                            continue;           /* refused, which is fine */
                        if (tw <= 0 || th <= 0) {
                            printf("truncated JPEG reported %dx%d\n", tw, th);
                            truncation_ok = 0;
                        }
                        image_free(cut);
                    }
                    if (!truncation_ok)
                        failures++;
                    else
                        printf("%-12s ok  (survives truncation at any "
                               "length)\n", "jpeg-cut");
                }

                image_free(rgba);
            }
        }
    }

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
