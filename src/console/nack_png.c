/*
 * A self-contained PNG decoder.
 *
 * libnack has no external dependencies and this keeps it that way: tilesets
 * ship as PNGs, so the library needs to read one, and that means a DEFLATE
 * decompressor as well.
 *
 * The inflate implementation follows RFC 1951 directly, decoding Huffman codes
 * a bit at a time. That is not fast, but a tileset is decoded once at load
 * time, and being obviously correct matters more here than throughput.
 */
#include "nack_png.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Bit reader                                                         */
/* ------------------------------------------------------------------ */

struct nack_bits {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t bit_buffer;
    int bit_count;
    bool overrun;
};

static int nack__bits_get(struct nack_bits *bits, int need)
{
    uint32_t value;

    while (bits->bit_count < need) {
        if (bits->position >= bits->size) {
            bits->overrun = true;
            return 0;
        }
        bits->bit_buffer |= (uint32_t)bits->data[bits->position++] << bits->bit_count;
        bits->bit_count += 8;
    }

    value = bits->bit_buffer & ((1u << need) - 1u);
    bits->bit_buffer >>= need;
    bits->bit_count -= need;
    return (int)value;
}

/* ------------------------------------------------------------------ */
/* Huffman                                                            */
/* ------------------------------------------------------------------ */

#define NACK_MAX_SYMBOLS 288

struct nack_huffman {
    uint16_t counts[16];                  /* codes of each length      */
    uint16_t symbols[NACK_MAX_SYMBOLS];   /* symbols in canonical order */
};

static void nack__huffman_build(struct nack_huffman *huffman,
                                const uint8_t *lengths, int count)
{
    uint16_t offsets[16];
    int i, length;

    memset(huffman->counts, 0, sizeof huffman->counts);
    for (i = 0; i < count; ++i)
        huffman->counts[lengths[i]]++;
    huffman->counts[0] = 0;

    offsets[0] = 0;
    for (length = 1; length < 16; ++length)
        offsets[length] = (uint16_t)(offsets[length - 1] +
                                     huffman->counts[length - 1]);

    for (i = 0; i < count; ++i) {
        if (lengths[i])
            huffman->symbols[offsets[lengths[i]]++] = (uint16_t)i;
    }
}

/* Walks the code tree one bit at a time; returns -1 on an invalid code. */
static int nack__huffman_decode(struct nack_bits *bits,
                                const struct nack_huffman *huffman)
{
    int code = 0, first = 0, index = 0, length;

    for (length = 1; length < 16; ++length) {
        code |= nack__bits_get(bits, 1);
        if (bits->overrun)
            return -1;
        {
            int count = huffman->counts[length];
            if (code - first < count)
                return huffman->symbols[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Inflate                                                            */
/* ------------------------------------------------------------------ */

struct nack_out {
    uint8_t *data;
    size_t size;
    size_t capacity;
};

static bool nack__out_reserve(struct nack_out *out, size_t extra)
{
    size_t needed = out->size + extra;
    uint8_t *grown;
    size_t capacity;

    if (needed <= out->capacity)
        return true;

    capacity = out->capacity ? out->capacity : 8192;
    while (capacity < needed)
        capacity *= 2;

    grown = (uint8_t *)realloc(out->data, capacity);
    if (!grown)
        return false;
    out->data = grown;
    out->capacity = capacity;
    return true;
}

static const uint16_t nack__length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t nack__length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t nack__dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
    769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t nack__dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static bool nack__inflate_block(struct nack_bits *bits, struct nack_out *out,
                                const struct nack_huffman *literals,
                                const struct nack_huffman *distances)
{
    for (;;) {
        int symbol = nack__huffman_decode(bits, literals);
        if (symbol < 0)
            return false;

        if (symbol < 256) {
            if (!nack__out_reserve(out, 1))
                return false;
            out->data[out->size++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256)
            return true;   /* end of block */

        symbol -= 257;
        if (symbol >= 29)
            return false;
        {
            int length = nack__length_base[symbol] +
                         nack__bits_get(bits, nack__length_extra[symbol]);
            int dist_symbol = nack__huffman_decode(bits, distances);
            int distance;
            size_t from;

            if (dist_symbol < 0 || dist_symbol >= 30)
                return false;
            distance = nack__dist_base[dist_symbol] +
                       nack__bits_get(bits, nack__dist_extra[dist_symbol]);
            if ((size_t)distance > out->size)
                return false;
            if (!nack__out_reserve(out, (size_t)length))
                return false;

            from = out->size - (size_t)distance;
            while (length-- > 0)
                out->data[out->size++] = out->data[from++];
        }
    }
}

static void nack__fixed_trees(struct nack_huffman *literals,
                              struct nack_huffman *distances)
{
    uint8_t lengths[NACK_MAX_SYMBOLS];
    int i;

    for (i = 0; i < 144; ++i) lengths[i] = 8;
    for (; i < 256; ++i)      lengths[i] = 9;
    for (; i < 280; ++i)      lengths[i] = 7;
    for (; i < 288; ++i)      lengths[i] = 8;
    nack__huffman_build(literals, lengths, 288);

    for (i = 0; i < 30; ++i) lengths[i] = 5;
    nack__huffman_build(distances, lengths, 30);
}

static bool nack__dynamic_trees(struct nack_bits *bits,
                                struct nack_huffman *literals,
                                struct nack_huffman *distances)
{
    static const uint8_t order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    uint8_t lengths[NACK_MAX_SYMBOLS + 32];
    uint8_t code_lengths[19];
    struct nack_huffman code_tree;
    int literal_count, distance_count, code_count, i;

    literal_count = nack__bits_get(bits, 5) + 257;
    distance_count = nack__bits_get(bits, 5) + 1;
    code_count = nack__bits_get(bits, 4) + 4;
    if (literal_count > 286 || distance_count > 30)
        return false;

    memset(code_lengths, 0, sizeof code_lengths);
    for (i = 0; i < code_count; ++i)
        code_lengths[order[i]] = (uint8_t)nack__bits_get(bits, 3);
    nack__huffman_build(&code_tree, code_lengths, 19);

    i = 0;
    while (i < literal_count + distance_count) {
        int symbol = nack__huffman_decode(bits, &code_tree);
        int repeat, value = 0;

        if (symbol < 0)
            return false;
        if (symbol < 16) {
            lengths[i++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 16) {
            if (i == 0)
                return false;
            value = lengths[i - 1];
            repeat = 3 + nack__bits_get(bits, 2);
        } else if (symbol == 17) {
            repeat = 3 + nack__bits_get(bits, 3);
        } else {
            repeat = 11 + nack__bits_get(bits, 7);
        }
        if (i + repeat > literal_count + distance_count)
            return false;
        while (repeat-- > 0)
            lengths[i++] = (uint8_t)value;
    }

    if (lengths[256] == 0)
        return false;   /* no end-of-block code */

    nack__huffman_build(literals, lengths, literal_count);
    nack__huffman_build(distances, lengths + literal_count, distance_count);
    return true;
}

static uint8_t *nack__inflate(const uint8_t *data, size_t size, size_t *out_size)
{
    struct nack_bits bits;
    struct nack_out out;
    int final;

    memset(&bits, 0, sizeof bits);
    memset(&out, 0, sizeof out);
    bits.data = data;
    bits.size = size;

    do {
        int type;

        final = nack__bits_get(&bits, 1);
        type = nack__bits_get(&bits, 2);
        if (bits.overrun)
            goto fail;

        if (type == 0) {
            /* Stored: discard the partial byte, then copy verbatim. */
            uint16_t length, check;
            bits.bit_buffer = 0;
            bits.bit_count = 0;
            if (bits.position + 4 > bits.size)
                goto fail;
            length = (uint16_t)(bits.data[bits.position] |
                                (bits.data[bits.position + 1] << 8));
            check = (uint16_t)(bits.data[bits.position + 2] |
                               (bits.data[bits.position + 3] << 8));
            bits.position += 4;
            if ((uint16_t)~length != check)
                goto fail;
            if (bits.position + length > bits.size)
                goto fail;
            if (!nack__out_reserve(&out, length))
                goto fail;
            memcpy(out.data + out.size, bits.data + bits.position, length);
            out.size += length;
            bits.position += length;
        } else if (type == 1 || type == 2) {
            struct nack_huffman literals, distances;
            if (type == 1)
                nack__fixed_trees(&literals, &distances);
            else if (!nack__dynamic_trees(&bits, &literals, &distances))
                goto fail;
            if (!nack__inflate_block(&bits, &out, &literals, &distances))
                goto fail;
        } else {
            goto fail;   /* reserved block type */
        }
    } while (!final && !bits.overrun);

    if (bits.overrun)
        goto fail;

    *out_size = out.size;
    return out.data;

fail:
    free(out.data);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* PNG                                                                */
/* ------------------------------------------------------------------ */

static uint32_t nack__read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int nack__paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

/* Undoes the per-scanline filters, in place, over the raw inflated stream. */
static bool nack__unfilter(uint8_t *data, size_t size, int width, int height,
                           int bytes_per_pixel, size_t stride)
{
    uint8_t *previous = NULL;
    int y;
    size_t offset = 0;

    (void)width;
    for (y = 0; y < height; ++y) {
        uint8_t filter;
        uint8_t *row;
        size_t i;

        if (offset + 1 + stride > size)
            return false;
        filter = data[offset++];
        row = data + offset;

        switch (filter) {
        case 0:
            break;
        case 1:
            for (i = (size_t)bytes_per_pixel; i < stride; ++i)
                row[i] = (uint8_t)(row[i] + row[i - bytes_per_pixel]);
            break;
        case 2:
            if (previous)
                for (i = 0; i < stride; ++i)
                    row[i] = (uint8_t)(row[i] + previous[i]);
            break;
        case 3:
            for (i = 0; i < stride; ++i) {
                int left = i >= (size_t)bytes_per_pixel
                               ? row[i - bytes_per_pixel] : 0;
                int up = previous ? previous[i] : 0;
                row[i] = (uint8_t)(row[i] + ((left + up) >> 1));
            }
            break;
        case 4:
            for (i = 0; i < stride; ++i) {
                int left = i >= (size_t)bytes_per_pixel
                               ? row[i - bytes_per_pixel] : 0;
                int up = previous ? previous[i] : 0;
                int upleft = (previous && i >= (size_t)bytes_per_pixel)
                                 ? previous[i - bytes_per_pixel] : 0;
                row[i] = (uint8_t)(row[i] + nack__paeth(left, up, upleft));
            }
            break;
        default:
            return false;
        }

        previous = row;
        offset += stride;
    }
    return true;
}

/* Reads one sample of the given bit depth from a packed scanline. */
static unsigned nack__sample(const uint8_t *row, int index, int depth)
{
    switch (depth) {
    case 8:  return row[index];
    case 16: return row[index * 2];   /* take the high byte */
    case 4:  return (row[index / 2] >> (index % 2 ? 0 : 4)) & 0x0F;
    case 2:  return (row[index / 4] >> (6 - 2 * (index % 4))) & 0x03;
    case 1:  return (row[index / 8] >> (7 - (index % 8))) & 0x01;
    default: return 0;
    }
}

uint8_t *nack__png_decode(const void *data, size_t size, int *out_width,
                          int *out_height, const char **error)
{
    static const uint8_t signature[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t *compressed = NULL, *raw = NULL, *rgba = NULL;
    size_t compressed_size = 0, compressed_capacity = 0, raw_size = 0;
    uint8_t palette[256 * 3];
    uint8_t palette_alpha[256];
    int palette_count = 0;
    bool has_trns = false;
    unsigned trns_grey = 0, trns_r = 0, trns_g = 0, trns_b = 0;
    int width = 0, height = 0, depth = 0, colour = 0, interlace = 0;
    int channels, bytes_per_pixel, x, y;
    size_t offset = 8, stride;

    if (error)
        *error = NULL;
    if (!bytes || size < 8 || memcmp(bytes, signature, 8) != 0) {
        if (error) *error = "not a PNG";
        return NULL;
    }

    memset(palette_alpha, 255, sizeof palette_alpha);

    while (offset + 8 <= size) {
        uint32_t chunk_size = nack__read_be32(bytes + offset);
        const uint8_t *type = bytes + offset + 4;
        const uint8_t *body = bytes + offset + 8;

        if (offset + 12 + (size_t)chunk_size > size) {
            if (error) *error = "truncated PNG chunk";
            goto fail;
        }

        if (memcmp(type, "IHDR", 4) == 0) {
            if (chunk_size < 13) {
                if (error) *error = "bad IHDR";
                goto fail;
            }
            width = (int)nack__read_be32(body);
            height = (int)nack__read_be32(body + 4);
            depth = body[8];
            colour = body[9];
            interlace = body[12];
            if (width <= 0 || height <= 0 || width > 65536 || height > 65536) {
                if (error) *error = "implausible PNG dimensions";
                goto fail;
            }
            if (interlace != 0) {
                if (error) *error = "interlaced PNG is not supported";
                goto fail;
            }
        } else if (memcmp(type, "PLTE", 4) == 0) {
            palette_count = (int)(chunk_size / 3);
            if (palette_count > 256)
                palette_count = 256;
            memcpy(palette, body, (size_t)palette_count * 3);
        } else if (memcmp(type, "tRNS", 4) == 0) {
            has_trns = true;
            if (colour == 3) {
                size_t n = chunk_size < 256 ? chunk_size : 256;
                memcpy(palette_alpha, body, n);
            } else if (colour == 0 && chunk_size >= 2) {
                trns_grey = ((unsigned)body[0] << 8) | body[1];
            } else if (colour == 2 && chunk_size >= 6) {
                trns_r = ((unsigned)body[0] << 8) | body[1];
                trns_g = ((unsigned)body[2] << 8) | body[3];
                trns_b = ((unsigned)body[4] << 8) | body[5];
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            /* Image data may be split across any number of IDAT chunks. */
            if (compressed_size + chunk_size > compressed_capacity) {
                size_t capacity = compressed_capacity ? compressed_capacity : 8192;
                uint8_t *grown;
                while (capacity < compressed_size + chunk_size)
                    capacity *= 2;
                grown = (uint8_t *)realloc(compressed, capacity);
                if (!grown) {
                    if (error) *error = "out of memory";
                    goto fail;
                }
                compressed = grown;
                compressed_capacity = capacity;
            }
            memcpy(compressed + compressed_size, body, chunk_size);
            compressed_size += chunk_size;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }

        offset += 12 + chunk_size;
    }

    if (!width || !compressed) {
        if (error) *error = "PNG has no image data";
        goto fail;
    }

    switch (colour) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default:
        if (error) *error = "unsupported PNG colour type";
        goto fail;
    }
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) {
        if (error) *error = "unsupported PNG bit depth";
        goto fail;
    }
    if (colour != 3 && depth < 8) {
        if (error) *error = "unsupported PNG bit depth for this colour type";
        goto fail;
    }

    /* Skip the two-byte zlib header; the checksum trailer is ignored. */
    if (compressed_size < 3) {
        if (error) *error = "PNG image data is too short";
        goto fail;
    }
    raw = nack__inflate(compressed + 2, compressed_size - 2, &raw_size);
    if (!raw) {
        if (error) *error = "PNG image data is corrupt";
        goto fail;
    }

    stride = ((size_t)width * (size_t)channels * (size_t)depth + 7) / 8;
    bytes_per_pixel = (channels * depth + 7) / 8;
    if (bytes_per_pixel < 1)
        bytes_per_pixel = 1;

    if (raw_size < (stride + 1) * (size_t)height) {
        if (error) *error = "PNG image data is short";
        goto fail;
    }
    if (!nack__unfilter(raw, raw_size, width, height, bytes_per_pixel, stride)) {
        if (error) *error = "bad PNG scanline filter";
        goto fail;
    }

    rgba = (uint8_t *)malloc((size_t)width * (size_t)height * 4);
    if (!rgba) {
        if (error) *error = "out of memory";
        goto fail;
    }

    for (y = 0; y < height; ++y) {
        const uint8_t *row = raw + (size_t)y * (stride + 1) + 1;
        uint8_t *dst = rgba + (size_t)y * (size_t)width * 4;

        for (x = 0; x < width; ++x) {
            unsigned r = 0, g = 0, b = 0, a = 255;

            switch (colour) {
            case 0: {
                unsigned v = nack__sample(row, x, depth);
                unsigned max = (1u << (depth == 16 ? 8 : depth)) - 1u;
                r = g = b = depth == 8 || depth == 16 ? v : v * 255u / max;
                if (has_trns && v == (depth == 16 ? trns_grey >> 8 : trns_grey))
                    a = 0;
                break;
            }
            case 2:
                r = nack__sample(row, x * 3 + 0, depth);
                g = nack__sample(row, x * 3 + 1, depth);
                b = nack__sample(row, x * 3 + 2, depth);
                if (has_trns && r == (trns_r & 0xFF) && g == (trns_g & 0xFF) &&
                    b == (trns_b & 0xFF))
                    a = 0;
                break;
            case 3: {
                unsigned index = nack__sample(row, x, depth);
                if ((int)index >= palette_count)
                    index = 0;
                r = palette[index * 3 + 0];
                g = palette[index * 3 + 1];
                b = palette[index * 3 + 2];
                a = palette_alpha[index];
                break;
            }
            case 4:
                r = g = b = nack__sample(row, x * 2 + 0, depth);
                a = nack__sample(row, x * 2 + 1, depth);
                break;
            case 6:
                r = nack__sample(row, x * 4 + 0, depth);
                g = nack__sample(row, x * 4 + 1, depth);
                b = nack__sample(row, x * 4 + 2, depth);
                a = nack__sample(row, x * 4 + 3, depth);
                break;
            default:
                break;
            }

            dst[x * 4 + 0] = (uint8_t)r;
            dst[x * 4 + 1] = (uint8_t)g;
            dst[x * 4 + 2] = (uint8_t)b;
            dst[x * 4 + 3] = (uint8_t)a;
        }
    }

    free(compressed);
    free(raw);
    *out_width = width;
    *out_height = height;
    return rgba;

fail:
    free(compressed);
    free(raw);
    free(rgba);
    return NULL;
}
