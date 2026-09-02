/* Console buffers and the drawing operations on them. */
#include "nack_console_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Every drawing call comes through here.
 *
 * NULL used to mean the root console, which made a null pointer
 * indistinguishable from a mistake: a console that failed to allocate drew to
 * the screen instead of failing, and the caller never found out. The root is
 * spelled nack_root() now, so NULL can mean what it looks like.
 */
struct nack_console *nack__console_resolve(struct nack_console *console)
{
    if (!console) {
        nack__error("no console given (the root console is nack_root())");
        return NULL;
    }
    return console;
}

/* Decodes one codepoint and advances the cursor. Invalid bytes yield U+FFFD
 * and consume one byte, so bad input cannot stall the loop. */
uint32_t nack__utf8_next(const char **cursor)
{
    const unsigned char *p = (const unsigned char *)*cursor;
    uint32_t codepoint;
    int extra, i;

    if (p[0] < 0x80) {
        *cursor = (const char *)(p + 1);
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0) { codepoint = p[0] & 0x1Fu; extra = 1; }
    else if ((p[0] & 0xF0) == 0xE0) { codepoint = p[0] & 0x0Fu; extra = 2; }
    else if ((p[0] & 0xF8) == 0xF0) { codepoint = p[0] & 0x07u; extra = 3; }
    else { *cursor = (const char *)(p + 1); return 0xFFFD; }

    for (i = 1; i <= extra; ++i) {
        if ((p[i] & 0xC0) != 0x80) {
            *cursor = (const char *)(p + 1);
            return 0xFFFD;
        }
        codepoint = (codepoint << 6) | (p[i] & 0x3Fu);
    }
    *cursor = (const char *)(p + extra + 1);
    return codepoint;
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                           */
/* ------------------------------------------------------------------ */

struct nack_console *nack_console_new(int columns, int rows)
{
    struct nack_console *console;

    if (columns < 1 || rows < 1) {
        nack__error("console size %dx%d is not positive", columns, rows);
        return NULL;
    }

    console = (struct nack_console *)calloc(1, sizeof *console);
    if (!console) {
        nack__error("out of memory");
        return NULL;
    }
    console->cells = (struct nack_cell *)calloc((size_t)columns * (size_t)rows,
                                                sizeof *console->cells);
    if (!console->cells) {
        free(console);
        nack__error("out of memory");
        return NULL;
    }
    console->columns = columns;
    console->rows = rows;
    nack_clear(console);
    return console;
}

void nack_console_free(struct nack_console *console)
{
    if (!console || console == nack__c.root)
        return;
    free(console->cells);
    free(console);
}

void nack_console_size(const struct nack_console *console, int *columns, int *rows)
{
    const struct nack_console *c =
        nack__console_resolve((struct nack_console *)console);
    if (columns) *columns = c ? c->columns : 0;
    if (rows)    *rows = c ? c->rows : 0;
}

bool nack_console_resize(struct nack_console *console, int columns, int rows)
{
    struct nack_console *c = nack__console_resolve(console);
    struct nack_cell *cells;
    int y, copy_w, copy_h;

    if (!c)
        return false;
    if (columns < 1 || rows < 1)
        return nack__error("console size %dx%d is not positive", columns, rows);
    if (c->columns == columns && c->rows == rows)
        return true;

    cells = (struct nack_cell *)calloc((size_t)columns * (size_t)rows,
                                       sizeof *cells);
    if (!cells)
        return nack__error("out of memory");

    /* Keep whatever still fits, so a resize does not blank the screen. */
    copy_w = columns < c->columns ? columns : c->columns;
    copy_h = rows < c->rows ? rows : c->rows;
    for (y = 0; y < copy_h; ++y)
        memcpy(cells + (size_t)y * columns, c->cells + (size_t)y * c->columns,
               (size_t)copy_w * sizeof *cells);

    free(c->cells);
    c->cells = cells;
    c->columns = columns;
    c->rows = rows;
    return true;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static bool nack__in_bounds(const struct nack_console *c, int x, int y)
{
    return c && x >= 0 && y >= 0 && x < c->columns && y < c->rows;
}

static struct nack_cell *nack__cell_at(struct nack_console *c, int x, int y)
{
    return &c->cells[(size_t)y * (size_t)c->columns + (size_t)x];
}

void nack_clear_to(struct nack_console *console, struct nack_color fg,
                   struct nack_color bg)
{
    struct nack_console *c = nack__console_resolve(console);
    size_t i, count;

    if (!c)
        return;
    count = (size_t)c->columns * (size_t)c->rows;
    for (i = 0; i < count; ++i) {
        c->cells[i].glyph = ' ';
        c->cells[i].tileset = NULL;
        c->cells[i].fg = fg;
        c->cells[i].bg = bg;
    }
}

void nack_clear(struct nack_console *console)
{
    struct nack_color fg = NACK_WHITE;
    struct nack_color bg = NACK_BLACK;
    nack_clear_to(console, fg, bg);
}

void nack_put(struct nack_console *console, int x, int y, uint32_t codepoint,
              struct nack_color fg, struct nack_color bg)
{
    struct nack_console *c = nack__console_resolve(console);
    struct nack_cell *cell;

    if (!nack__in_bounds(c, x, y))
        return;
    cell = nack__cell_at(c, x, y);
    cell->glyph = codepoint;
    cell->tileset = NULL;
    cell->fg = fg;
    cell->bg = bg;
}

void nack_put_tile(struct nack_console *console, int x, int y,
                   struct nack_tileset *tileset, int index,
                   struct nack_color tint, struct nack_color bg)
{
    struct nack_console *c = nack__console_resolve(console);
    struct nack_cell *cell;

    if (!nack__in_bounds(c, x, y) || !tileset)
        return;
    if (index < 0 || index >= tileset->count)
        return;
    cell = nack__cell_at(c, x, y);
    cell->glyph = (uint32_t)index;
    cell->tileset = tileset;
    cell->fg = tint;
    cell->bg = bg;
}

void nack_set_glyph(struct nack_console *console, int x, int y,
                    uint32_t codepoint)
{
    struct nack_console *c = nack__console_resolve(console);
    struct nack_cell *cell;

    if (!nack__in_bounds(c, x, y))
        return;
    cell = nack__cell_at(c, x, y);
    cell->glyph = codepoint;
    cell->tileset = NULL;
}

void nack_set_fg(struct nack_console *console, int x, int y, struct nack_color fg)
{
    struct nack_console *c = nack__console_resolve(console);
    if (nack__in_bounds(c, x, y))
        nack__cell_at(c, x, y)->fg = fg;
}

void nack_set_bg(struct nack_console *console, int x, int y, struct nack_color bg)
{
    struct nack_console *c = nack__console_resolve(console);
    if (nack__in_bounds(c, x, y))
        nack__cell_at(c, x, y)->bg = bg;
}

struct nack_cell nack_get(const struct nack_console *console, int x, int y)
{
    const struct nack_console *c =
        nack__console_resolve((struct nack_console *)console);
    struct nack_cell empty;

    memset(&empty, 0, sizeof empty);
    if (!nack__in_bounds(c, x, y))
        return empty;
    return c->cells[(size_t)y * (size_t)c->columns + (size_t)x];
}

int nack_print(struct nack_console *console, int x, int y, struct nack_color fg,
               struct nack_color bg, const char *utf8)
{
    struct nack_console *c = nack__console_resolve(console);
    const char *cursor = utf8;
    int written = 0;

    if (!c || !utf8)
        return 0;
    while (*cursor) {
        uint32_t codepoint = nack__utf8_next(&cursor);
        if (codepoint == '\n') {
            /* Printing is single-line; a newline ends it rather than
             * silently wrapping into whatever is below. */
            break;
        }
        if (x + written >= c->columns)
            break;
        nack_put(c, x + written, y, codepoint, fg, bg);
        written++;
    }
    return written;
}

int nack_vprintf(struct nack_console *console, int x, int y,
                 struct nack_color fg, struct nack_color bg, const char *fmt,
                 va_list args)
{
    char stack_buffer[512];
    char *buffer = stack_buffer;
    int needed, result;
    va_list copy;

    va_copy(copy, args);
    needed = vsnprintf(stack_buffer, sizeof stack_buffer, fmt, copy);
    va_end(copy);

    if (needed >= (int)sizeof stack_buffer) {
        buffer = (char *)malloc((size_t)needed + 1);
        if (!buffer)
            return 0;
        vsnprintf(buffer, (size_t)needed + 1, fmt, args);
    }

    result = nack_print(console, x, y, fg, bg, buffer);
    if (buffer != stack_buffer)
        free(buffer);
    return result;
}

int nack_printf(struct nack_console *console, int x, int y, struct nack_color fg,
                struct nack_color bg, const char *fmt, ...)
{
    va_list args;
    int result;

    va_start(args, fmt);
    result = nack_vprintf(console, x, y, fg, bg, fmt, args);
    va_end(args);
    return result;
}

/*
 * Wraps on whitespace, breaking mid-word only when a word cannot fit on a line
 * of its own. Passing height 0 measures instead of drawing, which is how you
 * size a message log before deciding where to put it.
 */
int nack_print_wrapped(struct nack_console *console, int x, int y, int width,
                       int height, struct nack_color fg, struct nack_color bg,
                       const char *utf8)
{
    struct nack_console *c = nack__console_resolve(console);
    const char *cursor = utf8;
    int row = 0, column = 0;
    bool measure = (height <= 0);

    if (!c || !utf8 || width < 1)
        return 0;

    while (*cursor) {
        const char *word = cursor;
        int word_length = 0;
        const char *scan;

        if (*cursor == '\n') {
            cursor++;
            row++;
            column = 0;
            continue;
        }
        if (*cursor == ' ') {
            cursor++;
            if (column > 0 && column < width)
                column++;
            continue;
        }

        /* Measure the next word in codepoints, not bytes. */
        scan = cursor;
        while (*scan && *scan != ' ' && *scan != '\n') {
            nack__utf8_next(&scan);
            word_length++;
        }

        if (column > 0 && column + word_length > width) {
            row++;
            column = 0;
        }

        while (word < scan) {
            uint32_t codepoint = nack__utf8_next(&word);
            if (column >= width) {
                row++;
                column = 0;
            }
            if (!measure && row < height)
                nack_put(c, x + column, y + row, codepoint, fg, bg);
            column++;
        }
        cursor = scan;
    }

    return row + 1;
}

void nack_fill(struct nack_console *console, int x, int y, int width, int height,
               uint32_t codepoint, struct nack_color fg, struct nack_color bg)
{
    struct nack_console *c = nack__console_resolve(console);
    int ix, iy;

    if (!c)
        return;
    for (iy = y; iy < y + height; ++iy)
        for (ix = x; ix < x + width; ++ix)
            nack_put(c, ix, iy, codepoint, fg, bg);
}

void nack_draw_box(struct nack_console *console, int x, int y, int width,
                   int height, struct nack_color fg, struct nack_color bg,
                   const char *title)
{
    struct nack_console *c = nack__console_resolve(console);
    int i;

    if (!c || width < 2 || height < 2)
        return;

    for (i = 1; i < width - 1; ++i) {
        nack_put(c, x + i, y, 0x2500, fg, bg);
        nack_put(c, x + i, y + height - 1, 0x2500, fg, bg);
    }
    for (i = 1; i < height - 1; ++i) {
        nack_put(c, x, y + i, 0x2502, fg, bg);
        nack_put(c, x + width - 1, y + i, 0x2502, fg, bg);
    }
    nack_put(c, x, y, 0x250C, fg, bg);
    nack_put(c, x + width - 1, y, 0x2510, fg, bg);
    nack_put(c, x, y + height - 1, 0x2514, fg, bg);
    nack_put(c, x + width - 1, y + height - 1, 0x2518, fg, bg);

    if (title && *title) {
        /* Centred on the top edge, with a space either side of the text. */
        int length = 0;
        const char *scan = title;
        int start;

        while (*scan) {
            nack__utf8_next(&scan);
            length++;
        }
        if (length > width - 4)
            length = width - 4;
        if (length > 0) {
            start = x + (width - length - 2) / 2;
            nack_put(c, start, y, ' ', fg, bg);
            nack_print(c, start + 1, y, fg, bg, title);
            nack_put(c, start + length + 1, y, ' ', fg, bg);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Blitting                                                           */
/* ------------------------------------------------------------------ */

static struct nack_color nack__blend(struct nack_color dst,
                                     struct nack_color src, float alpha)
{
    struct nack_color out;
    float a = alpha * ((float)src.a / 255.0f);

    if (a <= 0.0f)
        return dst;
    if (a >= 1.0f)
        return src;

    out.r = (uint8_t)((float)dst.r + ((float)src.r - (float)dst.r) * a);
    out.g = (uint8_t)((float)dst.g + ((float)src.g - (float)dst.g) * a);
    out.b = (uint8_t)((float)dst.b + ((float)src.b - (float)dst.b) * a);
    out.a = (uint8_t)((float)dst.a + ((float)src.a - (float)dst.a) * alpha);
    return out;
}

void nack_blit(const struct nack_console *src, int src_x, int src_y, int width,
               int height, struct nack_console *dst, int dst_x, int dst_y,
               float fg_alpha, float bg_alpha)
{
    const struct nack_console *s =
        nack__console_resolve((struct nack_console *)src);
    struct nack_console *d = nack__console_resolve(dst);
    int x, y;

    if (!s || !d || s == d)
        return;
    if (width <= 0) width = s->columns;
    if (height <= 0) height = s->rows;

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            int sx = src_x + x, sy = src_y + y;
            int tx = dst_x + x, ty = dst_y + y;
            const struct nack_cell *from;
            struct nack_cell *to;

            if (!nack__in_bounds(s, sx, sy) || !nack__in_bounds(d, tx, ty))
                continue;

            from = &s->cells[(size_t)sy * (size_t)s->columns + (size_t)sx];
            to = nack__cell_at(d, tx, ty);

            to->bg = nack__blend(to->bg, from->bg, bg_alpha);
            if (fg_alpha > 0.0f) {
                /* A blank source cell contributes background only, so an
                 * overlay does not punch holes in the text underneath. */
                if (from->glyph != ' ' && from->glyph != 0) {
                    to->glyph = from->glyph;
                    to->tileset = from->tileset;
                    to->fg = nack__blend(to->fg, from->fg, fg_alpha);
                }
            }
        }
    }
}
