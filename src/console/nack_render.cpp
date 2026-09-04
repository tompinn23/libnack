/*
 * Turns a console into quads and hands them to the graphics backend.
 *
 * Backgrounds go in one pass, then one pass per atlas, so a console mixing
 * font glyphs with graphical tiles still costs a couple of draws. Nothing here
 * knows whether OpenGL or Metal is underneath.
 */
#include "nack_console_internal.h"
#include "nack_gfx.h"

#include "nack_guard.h"

#include <stdlib.h>
#include <string.h>

namespace nack { namespace detail {

static bool reserve_vertices(size_t cells)
{
    size_t needed = cells * NACK_VERTICES_PER_CELL * NACK_FLOATS_PER_VERTEX;

    if (needed <= console_state.vertices.size())
        return true;
    return nack::guarded("cannot size the console vertex buffer",
                         [&] { console_state.vertices.resize(needed); return true; },
                         false);
}

static void emit_quad(float **cursor, float x0, float y0, float x1,
                            float y1, float u0, float v0, float u1, float v1,
                            nack_color fg, nack_color bg)
{
    const float fr = fg.r / 255.0f, fg_ = fg.g / 255.0f;
    const float fb = fg.b / 255.0f, fa = fg.a / 255.0f;
    const float br = bg.r / 255.0f, bg_ = bg.g / 255.0f;
    const float bb = bg.b / 255.0f, ba = bg.a / 255.0f;
    const float corners[6][4] = {
        { x0, y0, u0, v0 },
        { x1, y0, u1, v0 },
        { x1, y1, u1, v1 },
        { x0, y0, u0, v0 },
        { x1, y1, u1, v1 },
        { x0, y1, u0, v1 },
    };
    float *out = *cursor;
    int i;

    for (i = 0; i < 6; ++i) {
        *out++ = corners[i][0];
        *out++ = corners[i][1];
        *out++ = corners[i][2];
        *out++ = corners[i][3];
        *out++ = fr; *out++ = fg_; *out++ = fb; *out++ = fa;
        *out++ = br; *out++ = bg_; *out++ = bb; *out++ = ba;
    }
    *cursor = out;
}

/*
 * Works out where the console sits in the window. Integer scaling keeps the
 * tiles pixel-exact, which matters more for pixel art than filling the window
 * does; the remainder becomes letterbox.
 */
void render_update_viewport(void)
{
    const nack_console *console = console_state.root;
    const nack_tileset *font = console_state.font;
    int console_w, console_h;

    if (!console || !font)
        return;

    console_w = console->columns * font->tile_width;
    console_h = console->rows * font->tile_height;
    if (console_w < 1 || console_h < 1)
        return;

    switch (console_state.scaling) {
    case NACK_SCALE_STRETCH:
        console_state.viewport_x = 0;
        console_state.viewport_y = 0;
        console_state.viewport_w = console_state.fb_width;
        console_state.viewport_h = console_state.fb_height;
        break;

    case NACK_SCALE_FIT: {
        double sx = (double)console_state.fb_width / console_w;
        double sy = (double)console_state.fb_height / console_h;
        double scale = sx < sy ? sx : sy;
        console_state.viewport_w = (int)(console_w * scale);
        console_state.viewport_h = (int)(console_h * scale);
        console_state.viewport_x = (console_state.fb_width - console_state.viewport_w) / 2;
        console_state.viewport_y = (console_state.fb_height - console_state.viewport_h) / 2;
        break;
    }

    case NACK_SCALE_INTEGER:
    default: {
        int sx = console_state.fb_width / console_w;
        int sy = console_state.fb_height / console_h;
        int scale = sx < sy ? sx : sy;
        if (scale < 1)
            scale = 1;   /* smaller than one tile per pixel is unreadable */
        console_state.viewport_w = console_w * scale;
        console_state.viewport_h = console_h * scale;
        console_state.viewport_x = (console_state.fb_width - console_state.viewport_w) / 2;
        console_state.viewport_y = (console_state.fb_height - console_state.viewport_h) / 2;
        break;
    }
    }
}

void render_console(const nack_console *console)
{
    const nack_tileset *font = console_state.font;
    float cell_w, cell_h;
    size_t cell_count, pass;
    int x, y;

    if (!console || !font)
        return;

    cell_count = (size_t)console->columns * (size_t)console->rows;
    if (!reserve_vertices(cell_count))
        return;

    cell_w = (float)console_state.viewport_w / (float)console->columns;
    cell_h = (float)console_state.viewport_h / (float)console->rows;

    /* Pass one: every cell's background, in a single draw. */
    {
        float *cursor = console_state.vertices.data();
        for (y = 0; y < console->rows; ++y) {
            for (x = 0; x < console->columns; ++x) {
                const nack_cell *cell =
                    &console->cells[(size_t)y * console->columns + x];
                if (cell->bg.a == 0)
                    continue;
                emit_quad(&cursor, x * cell_w, y * cell_h,
                                (x + 1) * cell_w, (y + 1) * cell_h,
                                0.0f, 0.0f, 0.0f, 0.0f, cell->fg, cell->bg);
            }
        }
        gfx_draw(console_state.vertices.data(),
                       (size_t)(cursor - console_state.vertices.data()) /
                           NACK_FLOATS_PER_VERTEX,
                       0, nullptr);
    }

    /* Then one pass per atlas, so mixed glyph and tile consoles stay cheap. */
    for (pass = 0; pass < console_state.tilesets.size(); ++pass) {
        nack_tileset *atlas = console_state.tilesets[pass];
        float *cursor = console_state.vertices.data();
        float du, dv;

        if (!atlas || atlas->count <= 0 || !atlas->texture)
            continue;
        du = 1.0f / (float)atlas->columns;
        dv = 1.0f / (float)atlas->rows;

        for (y = 0; y < console->rows; ++y) {
            for (x = 0; x < console->columns; ++x) {
                const nack_cell *cell =
                    &console->cells[(size_t)y * console->columns + x];
                const nack_tileset *source =
                    cell->tileset ? cell->tileset : font;
                int index;
                float u0, v0;

                if (source != atlas || cell->fg.a == 0)
                    continue;

                if (cell->tileset) {
                    index = (int)cell->glyph;   /* already a tile index */
                } else {
                    if (cell->glyph == ' ' || cell->glyph == 0)
                        continue;
                    index = atlas->index_for(cell->glyph);
                    if (index < 0)
                        index = atlas->index_for('?');
                    if (index < 0)
                        continue;
                }
                if (index < 0 || index >= atlas->count)
                    continue;

                u0 = (float)(index % atlas->columns) * du;
                v0 = (float)(index / atlas->columns) * dv;

                emit_quad(&cursor, x * cell_w, y * cell_h,
                                (x + 1) * cell_w, (y + 1) * cell_h,
                                u0, v0, u0 + du, v0 + dv, cell->fg, cell->bg);
            }
        }

        gfx_draw(console_state.vertices.data(),
                       (size_t)(cursor - console_state.vertices.data()) /
                           NACK_FLOATS_PER_VERTEX,
                       1, atlas->texture);
    }
}

} }   /* namespace nack::detail */
