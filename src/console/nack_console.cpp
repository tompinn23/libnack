/* Console buffers and the drawing operations on them. */
#include "nack_console_internal.h"

#include "nack_guard.h"

#include <algorithm>
#include <memory>
#include <string>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace nack { namespace detail {

/* Decodes one codepoint and advances the cursor. Invalid bytes yield U+FFFD
 * and consume one byte, so bad input cannot stall the loop. */
uint32_t utf8_next(const char **cursor)
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

} }   /* namespace nack::detail */

/* ------------------------------------------------------------------ */
/* Lifetime                                                           */
/* ------------------------------------------------------------------ */

nack_console *nack_console::create(int columns, int rows)
{
    if (columns < 1 || rows < 1) {
        console_state.set_error("console size %dx%d is not positive", columns, rows);
        return nullptr;
    }
    return nack::guarded("cannot create a console", [&] {
        auto console = std::make_unique<nack_console>();
        console->cells.resize((size_t)columns * (size_t)rows);
        console->columns = columns;
        console->rows = rows;
        console->clear();
        return console.release();
    }, (nack_console *)nullptr);
}

void nack_console::destroy(nack_console *console)
{
    if (!console || console == console_state.root)
        return;
    delete console;
}

bool nack_console::resize(int new_columns, int new_rows)
{
    if (new_columns < 1 || new_rows < 1)
        return console_state.set_error("console size %dx%d is not positive",
                                 new_columns, new_rows);
    if (columns == new_columns && rows == new_rows)
        return true;

    return nack::guarded("cannot resize the console", [&] {
        std::vector<nack_cell> new_cells((size_t)new_columns *
                                                (size_t)new_rows);
        /* Keep whatever still fits, so a resize does not blank the screen. */
        int copy_w = new_columns < columns ? new_columns : columns;
        int copy_h = new_rows < rows ? new_rows : rows;

        for (int y = 0; y < copy_h; ++y)
            std::copy_n(cells.begin() + (size_t)y * columns, copy_w,
                        new_cells.begin() + (size_t)y * new_columns);

        cells = std::move(new_cells);
        columns = new_columns;
        rows = new_rows;
        return true;
    }, false);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static bool in_bounds(const nack_console *c, int x, int y)
{
    return x >= 0 && y >= 0 && x < c->columns && y < c->rows;
}

static nack_cell *cell_at(nack_console *c, int x, int y)
{
    return &c->cells[(size_t)y * (size_t)c->columns + (size_t)x];
}

void nack_console::clear_to(nack_color fg, nack_color bg)
{
    size_t i, count = (size_t)columns * (size_t)rows;
    for (i = 0; i < count; ++i) {
        cells[i].glyph = ' ';
        cells[i].tileset = nullptr;
        cells[i].fg = fg;
        cells[i].bg = bg;
    }
}

void nack_console::clear()
{
    nack_color fg = NACK_WHITE;
    nack_color bg = NACK_BLACK;
    clear_to(fg, bg);
}

void nack_console::put(int x, int y, uint32_t codepoint, nack_color fg,
                       nack_color bg)
{
    nack_cell *cell;

    if (!in_bounds(this, x, y))
        return;
    cell = cell_at(this, x, y);
    cell->glyph = codepoint;
    cell->tileset = nullptr;
    cell->fg = fg;
    cell->bg = bg;
}

void nack_console::put_tile(int x, int y, nack_tileset *tileset,
                            int index, nack_color tint,
                            nack_color bg)
{
    nack_cell *cell;

    if (!in_bounds(this, x, y) || !tileset)
        return;
    if (index < 0 || index >= tileset->count)
        return;
    cell = cell_at(this, x, y);
    cell->glyph = (uint32_t)index;
    cell->tileset = tileset;
    cell->fg = tint;
    cell->bg = bg;
}

void nack_console::set_glyph(int x, int y, uint32_t codepoint)
{
    nack_cell *cell;

    if (!in_bounds(this, x, y))
        return;
    cell = cell_at(this, x, y);
    cell->glyph = codepoint;
    cell->tileset = nullptr;
}

void nack_console::set_fg(int x, int y, nack_color fg)
{
    if (in_bounds(this, x, y))
        cell_at(this, x, y)->fg = fg;
}

void nack_console::set_bg(int x, int y, nack_color bg)
{
    if (in_bounds(this, x, y))
        cell_at(this, x, y)->bg = bg;
}

nack_cell nack_console::get(int x, int y) const
{
    nack_cell empty;

    memset(&empty, 0, sizeof empty);
    if (!in_bounds(this, x, y))
        return empty;
    return cells[(size_t)y * (size_t)columns + (size_t)x];
}

int nack_console::print(int x, int y, nack_color fg,
                        nack_color bg, const char *utf8)
{
    const char *cursor = utf8;
    int written = 0;

    if (!utf8)
        return 0;
    while (*cursor) {
        uint32_t codepoint = utf8_next(&cursor);
        if (codepoint == '\n') {
            /* Printing is single-line; a newline ends it rather than
             * silently wrapping into whatever is below. */
            break;
        }
        if (x + written >= columns)
            break;
        put(x + written, y, codepoint, fg, bg);
        written++;
    }
    return written;
}

/*
 * Wraps on whitespace, breaking mid-word only when a word cannot fit on a line
 * of its own. Passing height 0 measures instead of drawing, which is how you
 * size a message log before deciding where to put it.
 */
int nack_console::print_wrapped(int x, int y, int width, int height,
                                nack_color fg, nack_color bg,
                                const char *utf8)
{
    const char *cursor = utf8;
    int row = 0, column = 0;
    bool measure = (height <= 0);

    if (!utf8 || width < 1)
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
            utf8_next(&scan);
            word_length++;
        }

        if (column > 0 && column + word_length > width) {
            row++;
            column = 0;
        }

        while (word < scan) {
            uint32_t codepoint = utf8_next(&word);
            if (column >= width) {
                row++;
                column = 0;
            }
            if (!measure && row < height)
                put(x + column, y + row, codepoint, fg, bg);
            column++;
        }
        cursor = scan;
    }

    return row + 1;
}

void nack_console::fill(int x, int y, int width, int height,
                        uint32_t codepoint, nack_color fg,
                        nack_color bg)
{
    int ix, iy;

    for (iy = y; iy < y + height; ++iy)
        for (ix = x; ix < x + width; ++ix)
            put(ix, iy, codepoint, fg, bg);
}

void nack_console::draw_box(int x, int y, int width, int height,
                            nack_color fg, nack_color bg,
                            const char *title)
{
    int i;

    if (width < 2 || height < 2)
        return;

    for (i = 1; i < width - 1; ++i) {
        put(x + i, y, 0x2500, fg, bg);
        put(x + i, y + height - 1, 0x2500, fg, bg);
    }
    for (i = 1; i < height - 1; ++i) {
        put(x, y + i, 0x2502, fg, bg);
        put(x + width - 1, y + i, 0x2502, fg, bg);
    }
    put(x, y, 0x250C, fg, bg);
    put(x + width - 1, y, 0x2510, fg, bg);
    put(x, y + height - 1, 0x2514, fg, bg);
    put(x + width - 1, y + height - 1, 0x2518, fg, bg);

    if (title && *title) {
        /* Centred on the top edge, with a space either side of the text. */
        int length = 0;
        const char *scan = title;
        int start;

        while (*scan) {
            utf8_next(&scan);
            length++;
        }
        if (length > width - 4)
            length = width - 4;
        if (length > 0) {
            start = x + (width - length - 2) / 2;
            put(start, y, ' ', fg, bg);
            print(start + 1, y, fg, bg, title);
            put(start + length + 1, y, ' ', fg, bg);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Blitting                                                           */
/* ------------------------------------------------------------------ */

static nack_color blend(nack_color dst,
                                     nack_color src, float alpha)
{
    nack_color out;
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

void nack_console::blit_to(nack_console *dst, int src_x, int src_y,
                           int width, int height, int dst_x, int dst_y,
                           float fg_alpha, float bg_alpha) const
{
    int x, y;

    if (!dst || dst == this)
        return;
    if (width <= 0) width = columns;
    if (height <= 0) height = rows;

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            int sx = src_x + x, sy = src_y + y;
            int tx = dst_x + x, ty = dst_y + y;
            const nack_cell *from;
            nack_cell *to;

            if (!in_bounds(this, sx, sy) || !in_bounds(dst, tx, ty))
                continue;

            from = &cells[(size_t)sy * (size_t)columns + (size_t)sx];
            to = cell_at(dst, tx, ty);

            to->bg = blend(to->bg, from->bg, bg_alpha);
            if (fg_alpha > 0.0f) {
                /* A blank source cell contributes background only, so an
                 * overlay does not punch holes in the text underneath. */
                if (from->glyph != ' ' && from->glyph != 0) {
                    to->glyph = from->glyph;
                    to->tileset = from->tileset;
                    to->fg = blend(to->fg, from->fg, fg_alpha);
                }
            }
        }
    }
}
