/* Tileset loading, atlas upload and codepoint mapping. */
#include "nack_console_internal.h"
#include "nack_image.h"
#include "nack_font8x8.h"

#include "../nack_scoped.h"
#include "nack_guard.h"

#include <algorithm>
#include <memory>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The codepoints CP437's 256 slots correspond to. Entry n is the Unicode
 * codepoint drawn by tile n, which is what lets a CP437 sheet render box
 * drawing characters written as UTF-8 in source.
 */
static const uint16_t nack__cp437[256] = {
    0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
    0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x2302,
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

/* ------------------------------------------------------------------ */
/* Codepoint mapping                                                  */
/* ------------------------------------------------------------------ */

static bool nack__codepoint_before(const struct nack_codepoint_map &entry,
                                   uint32_t codepoint)
{
    return entry.codepoint < codepoint;
}

static bool nack__sparse_set(struct nack_tileset *tileset, uint32_t codepoint,
                             int index)
{
    auto at = std::lower_bound(tileset->sparse.begin(), tileset->sparse.end(),
                               codepoint, nack__codepoint_before);

    if (at != tileset->sparse.end() && at->codepoint == codepoint) {
        at->index = index;
        return true;
    }
    tileset->sparse.insert(at, { codepoint, index });
    return true;
}

int nack_tileset::index_for(uint32_t codepoint) const
{
    if (codepoint < 256) {
        int index = direct[codepoint];
        return index >= 0 ? index : -1;
    }

    auto at = std::lower_bound(sparse.begin(), sparse.end(), codepoint,
                               nack__codepoint_before);
    if (at != sparse.end() && at->codepoint == codepoint)
        return at->index;
    return -1;
}

bool nack_tileset::map(uint32_t codepoint, int index)
{
    if (index < 0 || index >= count)
        return nack__c.set_error("tile index %d is outside the tileset", index);
    if (codepoint < 256) {
        direct[codepoint] = index;
        return true;
    }
    return nack::guarded("cannot map a codepoint",
                         [&] { return nack__sparse_set(this, codepoint, index); },
                         false);
}

bool nack_tileset::map_range(uint32_t first, uint32_t last, int first_index)
{
    uint32_t codepoint;
    int index = first_index;

    if (last < first)
        return nack__c.set_error("bad codepoint range");
    for (codepoint = first; codepoint <= last; ++codepoint, ++index) {
        if (!map(codepoint, index))
            return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Atlas construction                                                 */
/* ------------------------------------------------------------------ */

/*
 * A sheet that is only ever white, black or transparent is a font: the glyph
 * shape is all it carries, so the cell's foreground colour supplies the rest.
 * Anything with real colour in it is artwork and is drawn as it is.
 */
static bool nack__looks_like_font(const uint8_t *rgba, int width, int height)
{
    int i, count = width * height;
    for (i = 0; i < count; ++i) {
        uint8_t r = rgba[i * 4 + 0];
        uint8_t g = rgba[i * 4 + 1];
        uint8_t b = rgba[i * 4 + 2];
        if (r != g || g != b)
            return false;     /* a real hue: this is artwork */
    }
    return true;
}

/*
 * Normalises a font sheet to white with coverage in alpha, so the shader can
 * treat every font the same way whether it arrived as black-on-white,
 * white-on-black or white-on-transparent.
 */
static void nack__normalize_font(uint8_t *rgba, int width, int height)
{
    int i, count = width * height;
    bool has_alpha = false;

    for (i = 0; i < count; ++i) {
        if (rgba[i * 4 + 3] != 255) {
            has_alpha = true;
            break;
        }
    }

    for (i = 0; i < count; ++i) {
        uint8_t luminance = rgba[i * 4 + 0];
        uint8_t alpha = rgba[i * 4 + 3];
        uint8_t coverage = has_alpha ? alpha : luminance;
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = coverage;
    }
}

/*
 * Everything a tileset owns beyond its own memory: the texture. The codepoint
 * map is a member and destroys itself; the texture does not, so this is the
 * one thing the destructor has to do by hand.
 */
nack_tileset::~nack_tileset()
{
    nack__gfx_texture_destroy(texture);
}

struct nack_tileset *nack_tileset::from_rgba(uint8_t *rgba, int width,
                                             int height, int tile_width,
                                             int tile_height,
                                             enum nack_tileset_layout layout)
{
    int i;

    if (tile_width <= 0 || tile_height <= 0) {
        /* No size given: assume the usual 16 by 16 sheet. */
        tile_width = width / 16;
        tile_height = height / 16;
    }
    if (tile_width <= 0 || tile_height <= 0 ||
        width % tile_width || height % tile_height) {
        nack__c.set_error(
            "tileset is %dx%d, which does not divide into %dx%d tiles",
            width, height, tile_width, tile_height);
        return NULL;
    }

    std::unique_ptr<nack_tileset> owner(new nack_tileset{});
    nack_tileset *tileset = owner.get();

    tileset->tile_width = tile_width;
    tileset->tile_height = tile_height;
    tileset->columns = width / tile_width;
    tileset->rows = height / tile_height;
    tileset->count = tileset->columns * tileset->rows;
    tileset->is_font = nack__looks_like_font(rgba, width, height);

    if (tileset->is_font)
        nack__normalize_font(rgba, width, height);

    tileset->direct.fill(-1);

    /* Seed the codepoint map from the sheet's declared ordering. */
    if (layout == NACK_LAYOUT_CP437) {
        for (i = 0; i < 256 && i < tileset->count; ++i) {
            int index = i;
            if (layout == NACK_LAYOUT_TCOD)
                index = (i % 16) * 16 + i / 16;
            tileset->map(nack__cp437[i], index);
        }
        /* ASCII is also reachable by its own codepoint, which is the same
         * thing for CP437 but keeps plain ASCII working on odd sheets. */
        for (i = 32; i < 127; ++i)
            if (tileset->direct[i] < 0 && i < tileset->count)
                tileset->direct[i] = i;
    } else if (layout == NACK_LAYOUT_TCOD) {
        /* libtcod sheets run down the columns rather than across the rows. */
        for (i = 0; i < 256 && i < tileset->count; ++i) {
            int index = (i % tileset->rows) * tileset->columns + (i / tileset->rows);
            if (index < tileset->count)
                tileset->map(nack__cp437[i], index);
        }
    } else {
        for (i = 0; i < 256 && i < tileset->count; ++i)
            tileset->direct[i] = i;
    }

    tileset->texture = nack__gfx_texture_create(rgba, width, height);
    if (!tileset->texture)
        return NULL;

    tileset->track();
    return owner.release();
}

/* ------------------------------------------------------------------ */
/* Built-in font                                                      */
/* ------------------------------------------------------------------ */

struct nack_tileset *nack_tileset::builtin()
{
    /* Expand the 1-bit font into a 16x16 sheet of 8x8 glyphs. */
    const int tile = 8, across = 16;
    const int width = tile * across, height = tile * across;
    nack::c_ptr<uint8_t> owner(
        (uint8_t *)calloc((size_t)width * height, 4));
    uint8_t *rgba;
    int glyph;

    if (!owner) {
        nack__c.set_error("out of memory");
        return NULL;
    }
    rgba = owner.get();

    for (glyph = 0; glyph < NACK_BUILTIN_FONT_COUNT; ++glyph) {
        int ox = (glyph % across) * tile;
        int oy = (glyph / across) * tile;
        int y;
        for (y = 0; y < tile; ++y) {
            uint8_t bits = nack__builtin_font[glyph][y];
            int x;
            for (x = 0; x < tile; ++x) {
                uint8_t on = (bits & (1u << x)) ? 255 : 0;
                size_t p = ((size_t)(oy + y) * width + (ox + x)) * 4;
                rgba[p + 0] = 255;
                rgba[p + 1] = 255;
                rgba[p + 2] = 255;
                rgba[p + 3] = on;
            }
        }
    }

    return nack_tileset::from_rgba(rgba, width, height, tile, tile,
                                   NACK_LAYOUT_CP437);
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                */
/* ------------------------------------------------------------------ */

struct nack_tileset *nack_tileset::load_memory(const void *data, size_t size,
                                               int tile_width, int tile_height,
                                               enum nack_tileset_layout layout)
{
    const char *error = NULL;
    int width = 0, height = 0;

    if (!nack__c.initialized) {
        nack__c.set_error("nack_init has not been called");
        return NULL;
    }

    nack::owned<uint8_t, nack__image_free> rgba(
        nack__image_decode(data, size, &width, &height, &error));
    if (!rgba) {
        nack__c.set_error("cannot decode tileset: %s",
                          error ? error : "unknown");
        return NULL;
    }

    return nack_tileset::from_rgba(rgba.get(), width, height, tile_width,
                                   tile_height, layout);
}

struct nack_tileset *nack_tileset::load(const char *path, int tile_width,
                                        int tile_height,
                                        enum nack_tileset_layout layout)
{
    long size;

    if (!path) {
        nack__c.set_error("no tileset path given");
        return NULL;
    }

    nack::file_ptr file(fopen(path, "rb"));
    if (!file) {
        nack__c.set_error("cannot open tileset '%s'", path);
        return NULL;
    }
    fseek(file.get(), 0, SEEK_END);
    size = ftell(file.get());
    fseek(file.get(), 0, SEEK_SET);
    if (size <= 0) {
        nack__c.set_error("tileset '%s' is empty", path);
        return NULL;
    }

    nack::c_ptr<char> data((char *)malloc((size_t)size));
    if (!data) {
        nack__c.set_error("out of memory");
        return NULL;
    }
    if (fread(data.get(), 1, (size_t)size, file.get()) != (size_t)size) {
        nack__c.set_error("cannot read tileset '%s'", path);
        return NULL;
    }

    return nack_tileset::load_memory(data.get(), (size_t)size, tile_width,
                                     tile_height, layout);
}

void nack_tileset::destroy(struct nack_tileset *tileset)
{
    if (!tileset || tileset == nack__c.builtin_font)
        return;
    tileset->untrack();
    delete tileset;
}

void nack_tileset::track()
{
    nack__c.tilesets.push_back(this);
}

void nack_tileset::untrack()
{
    auto at = std::find(nack__c.tilesets.begin(), nack__c.tilesets.end(),
                        this);
    if (at != nack__c.tilesets.end())
        nack__c.tilesets.erase(at);
}
