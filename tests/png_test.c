/* Decodes generated PNGs and compares against known pixel data.
 * Run tools/mkpng.py first to produce /tmp/pngs. */
/* Decodes the generated PNGs and compares against the known pixel data. */
#include "console/nack_png.h"
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

int main(void)
{
    size_t esz;
    unsigned char *exp = slurp("/tmp/pngs/expected.bin", &esz);
    size_t off = 0;
    unsigned count, i;
    int failures = 0;

    if (!exp) { fprintf(stderr, "no expected.bin\n"); return 1; }
    memcpy(&count, exp + off, 4); off += 4;

    for (i = 0; i < count; ++i) {
        unsigned namelen, w, h, x, y;
        char name[64], path[128];
        unsigned char *png, *rgba;
        size_t psz;
        const char *err = NULL;
        int bad = 0;

        memcpy(&namelen, exp + off, 4); off += 4;
        memcpy(name, exp + off, namelen); name[namelen] = 0; off += namelen;
        memcpy(&w, exp + off, 4); off += 4;
        memcpy(&h, exp + off, 4); off += 4;

        snprintf(path, sizeof path, "/tmp/pngs/%s", name);
        png = slurp(path, &psz);
        if (!png) { printf("%-12s MISSING\n", name); failures++; off += (size_t)w*h*4; continue; }

        int dw = 0, dh = 0;
        rgba = nack__png_decode(png, psz, &dw, &dh, &err);
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
        if (nack__png_decode(junk, sizeof junk, &w, &h, &err) != NULL) {
            printf("junk input accepted\n"); failures++;
        } else printf("%-12s ok  (rejected: %s)\n", "junk", err);

        size_t psz; unsigned char *png = slurp("/tmp/pngs/rgba8.png", &psz);
        if (png) {
            if (nack__png_decode(png, psz / 2, &w, &h, &err) != NULL) {
                printf("truncated PNG accepted\n"); failures++;
            } else printf("%-12s ok  (rejected: %s)\n", "truncated", err);
            png[30] ^= 0xFF;   /* corrupt the compressed stream */
            if (nack__png_decode(png, psz, &w, &h, &err) != NULL)
                printf("%-12s ok  (corrupt data decoded to something; no crash)\n", "corrupt");
            else
                printf("%-12s ok  (rejected: %s)\n", "corrupt", err);
            free(png);
        }
    }

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
