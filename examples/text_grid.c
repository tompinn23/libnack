/*
 * A character-cell grid: the shape a terminal emulator or a libtcod-style
 * console sits on top of.
 *
 * What it demonstrates, in the order it matters for a terminal:
 *
 *   - The window snaps to whole cells, so a resize never leaves a half column.
 *   - nack_wait_event blocks, so an idle terminal costs no CPU at all. It only
 *     redraws when something actually changed.
 *   - Text input arrives already composed as UTF-8, separately from the
 *     physical key events used for control chords.
 *   - Clipboard paste and copy, including the primary selection on Unix.
 *   - The framebuffer is sized in physical pixels, so glyphs stay crisp on a
 *     HiDPI display while the cell grid is reasoned about in logical ones.
 *
 * Rendering is one textured quad per cell out of an 8x8 bitmap font. That is
 * the least interesting part and a real terminal would want a proper shaper,
 * but it makes the plumbing visible.
 */
#include "nack/nack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "gl_loader.h"
#include "font8x8_basic.h"   /* public domain, see the header for provenance */

#define CELL_W 8
#define CELL_H 8
#define SCALE  2             /* logical pixels per font pixel */

#define CELL_PIXELS_W (CELL_W * SCALE)
#define CELL_PIXELS_H (CELL_H * SCALE)

#define MIN_COLS 20
#define MIN_ROWS 5
#define MAX_COLS 400
#define MAX_ROWS 200

struct cell {
    unsigned char glyph;
    unsigned char colour;
};

struct grid {
    int cols, rows;
    struct cell *cells;
    int cursor_x, cursor_y;
};

static const float palette[8][3] = {
    { 0.85f, 0.87f, 0.91f },   /* default text */
    { 0.60f, 0.76f, 0.47f },   /* green        */
    { 0.92f, 0.80f, 0.55f },   /* yellow       */
    { 0.51f, 0.63f, 0.76f },   /* blue         */
    { 0.71f, 0.56f, 0.68f },   /* magenta      */
    { 0.53f, 0.75f, 0.82f },   /* cyan         */
    { 0.75f, 0.38f, 0.42f },   /* red          */
    { 0.40f, 0.44f, 0.52f },   /* dim          */
};

/* ------------------------------------------------------------------ */
/* Grid                                                               */
/* ------------------------------------------------------------------ */

static bool grid_resize(struct grid *g, int cols, int rows)
{
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    struct cell *cells =
        (struct cell *)calloc((size_t)cols * (size_t)rows, sizeof *cells);
    if (!cells)
        return false;

    /* Preserve as much of the old contents as still fits. */
    if (g->cells) {
        int copy_cols = cols < g->cols ? cols : g->cols;
        int copy_rows = rows < g->rows ? rows : g->rows;
        for (int y = 0; y < copy_rows; ++y)
            memcpy(cells + (size_t)y * cols, g->cells + (size_t)y * g->cols,
                   (size_t)copy_cols * sizeof *cells);
        free(g->cells);
    }

    g->cells = cells;
    g->cols = cols;
    g->rows = rows;
    if (g->cursor_x >= cols) g->cursor_x = cols - 1;
    if (g->cursor_y >= rows) g->cursor_y = rows - 1;
    return true;
}

static void grid_scroll(struct grid *g)
{
    memmove(g->cells, g->cells + g->cols,
            (size_t)(g->rows - 1) * (size_t)g->cols * sizeof *g->cells);
    memset(g->cells + (size_t)(g->rows - 1) * (size_t)g->cols, 0,
           (size_t)g->cols * sizeof *g->cells);
}

static void grid_newline(struct grid *g)
{
    g->cursor_x = 0;
    if (++g->cursor_y >= g->rows) {
        g->cursor_y = g->rows - 1;
        grid_scroll(g);
    }
}

static void grid_put(struct grid *g, unsigned char glyph, unsigned char colour)
{
    if (g->cursor_x >= g->cols)
        grid_newline(g);
    g->cells[(size_t)g->cursor_y * g->cols + g->cursor_x].glyph = glyph;
    g->cells[(size_t)g->cursor_y * g->cols + g->cursor_x].colour = colour;
    g->cursor_x++;
}

static void grid_backspace(struct grid *g)
{
    if (g->cursor_x > 0) {
        g->cursor_x--;
    } else if (g->cursor_y > 0) {
        g->cursor_y--;
        g->cursor_x = g->cols - 1;
    }
    g->cells[(size_t)g->cursor_y * g->cols + g->cursor_x].glyph = 0;
}

static void grid_write(struct grid *g, const char *text, unsigned char colour)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '\n')
            grid_newline(g);
        else if (*p < 0x80)
            grid_put(g, *p, colour);
        else
            grid_put(g, '?', colour);   /* the 8x8 font is ASCII only */
    }
}

/* ------------------------------------------------------------------ */
/* Font atlas                                                         */
/* ------------------------------------------------------------------ */

/* Unpacks the 1-bit font into a 128x128 R8 texture of 16x16 glyphs. */
static GLuint build_font_texture(void)
{
    unsigned char atlas[128 * 128];
    memset(atlas, 0, sizeof atlas);

    for (int glyph = 0; glyph < 128; ++glyph) {
        const int origin_x = (glyph % 16) * CELL_W;
        const int origin_y = (glyph / 16) * CELL_H;
        for (int y = 0; y < CELL_H; ++y) {
            const unsigned char bits = (unsigned char)font8x8_basic[glyph][y];
            for (int x = 0; x < CELL_W; ++x) {
                /* font8x8 stores each row least-significant bit leftmost. */
                if (bits & (1u << x))
                    atlas[(origin_y + y) * 128 + (origin_x + x)] = 255;
            }
        }
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 128, 128, 0, GL_RED,
                 GL_UNSIGNED_BYTE, atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

/* ------------------------------------------------------------------ */
/* Shaders                                                            */
/* ------------------------------------------------------------------ */

static const char *vertex_source =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_position;\n"   /* in cell pixels               */
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec3 a_colour;\n"
    "uniform vec2 u_viewport;\n"
    "out vec2 v_uv;\n"
    "out vec3 v_colour;\n"
    "void main() {\n"
    "    vec2 ndc = (a_position / u_viewport) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_colour = a_colour;\n"
    "}\n";

static const char *fragment_source =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec3 v_colour;\n"
    "uniform sampler2D u_font;\n"
    "out vec4 frag_colour;\n"
    "void main() {\n"
    "    float coverage = texture(u_font, v_uv).r;\n"
    "    if (coverage < 0.5) discard;\n"
    "    frag_colour = vec4(v_colour, 1.0);\n"
    "}\n";

/* ------------------------------------------------------------------ */
/* Vertex building                                                    */
/* ------------------------------------------------------------------ */

struct vertex {
    float x, y;
    float u, v;
    float r, g, b;
};

static size_t build_vertices(const struct grid *g, struct vertex *out, double time_seconds)
{
    size_t count = 0;
    const float glyph_uv = 1.0f / 16.0f;

    for (int row = 0; row < g->rows; ++row) {
        for (int col = 0; col < g->cols; ++col) {
            const struct cell *c = &g->cells[(size_t)row * g->cols + col];

            unsigned char glyph = c->glyph;
            unsigned char colour = c->colour;

            /* Draw the cursor as a blinking solid block. */
            bool is_cursor = (row == g->cursor_y && col == g->cursor_x);
            if (is_cursor && fmod(time_seconds, 1.0) < 0.5) {
                glyph = 219;          /* not in the ASCII font, so use '_' */
                glyph = '_';
                colour = 2;
            }
            if (glyph == 0 || glyph >= 128)
                continue;

            const float x0 = (float)(col * CELL_PIXELS_W);
            const float y0 = (float)(row * CELL_PIXELS_H);
            const float x1 = x0 + CELL_PIXELS_W;
            const float y1 = y0 + CELL_PIXELS_H;

            const float u0 = (float)(glyph % 16) * glyph_uv;
            const float v0 = (float)(glyph / 16) * glyph_uv;
            const float u1 = u0 + glyph_uv;
            const float v1 = v0 + glyph_uv;

            const float r = palette[colour % 8][0];
            const float g_ = palette[colour % 8][1];
            const float b = palette[colour % 8][2];

            const struct vertex quad[6] = {
                { x0, y0, u0, v0, r, g_, b },
                { x1, y0, u1, v0, r, g_, b },
                { x1, y1, u1, v1, r, g_, b },
                { x0, y0, u0, v0, r, g_, b },
                { x1, y1, u1, v1, r, g_, b },
                { x0, y1, u0, v1, r, g_, b },
            };
            memcpy(out + count, quad, sizeof quad);
            count += 6;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    if (!nack_init(&(struct nack_init_desc){ .app_id = "nack.textgrid" })) {
        const char *message = NULL;
        nack_get_error(&message);
        fprintf(stderr, "nack_init failed: %s\n", message);
        return 1;
    }

    struct grid g = { 0 };
    if (!grid_resize(&g, 80, 25)) {
        nack_shutdown();
        return 1;
    }

    struct nack_window_desc window_desc;
    nack_window_desc_defaults(&window_desc);
    window_desc.title = "libnack - text grid";
    window_desc.width = g.cols * CELL_PIXELS_W;
    window_desc.height = g.rows * CELL_PIXELS_H;
    /* The two hints that make a window behave like a terminal. */
    window_desc.width_increment = CELL_PIXELS_W;
    window_desc.height_increment = CELL_PIXELS_H;
    window_desc.min_width = MIN_COLS * CELL_PIXELS_W;
    window_desc.min_height = MIN_ROWS * CELL_PIXELS_H;

    struct nack_window *window = nack_window_create(&window_desc);
    if (!window) {
        const char *message = NULL;
        nack_get_error(&message);
        fprintf(stderr, "window creation failed: %s\n", message);
        free(g.cells);
        nack_shutdown();
        return 1;
    }

    nack_window_set_cursor_shape(window, NACK_CURSOR_IBEAM);

    struct nack_gl_desc gl_desc;
    nack_gl_desc_defaults(&gl_desc);
    struct nack_gl_context *context = nack_gl_context_create(window, &gl_desc);
    if (!context || !nack_gl_make_current(window, context) ||
        !nack_example_load_gl()) {
        const char *message = NULL;
        nack_get_error(&message);
        fprintf(stderr, "OpenGL setup failed: %s\n", message ? message : "");
        nack_window_destroy(window);
        free(g.cells);
        nack_shutdown();
        return 1;
    }
    nack_gl_set_swap_interval(1);

    GLuint program = nack_example_program(vertex_source, fragment_source);
    GLuint font = build_font_texture();
    if (!program) {
        nack_gl_context_destroy(context);
        nack_window_destroy(window);
        free(g.cells);
        nack_shutdown();
        return 1;
    }

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                          (const void *)offsetof(struct vertex, x));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                          (const void *)offsetof(struct vertex, u));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                          (const void *)offsetof(struct vertex, r));
    glEnableVertexAttribArray(2);

    const GLint viewport_uniform = glGetUniformLocation(program, "u_viewport");
    const GLint font_uniform = glGetUniformLocation(program, "u_font");

    struct vertex *vertices = (struct vertex *)malloc((size_t)MAX_COLS * MAX_ROWS * 6 *
                                        sizeof *vertices);
    if (!vertices) {
        nack_gl_context_destroy(context);
        nack_window_destroy(window);
        free(g.cells);
        nack_shutdown();
        return 1;
    }

    grid_write(&g, "libnack text grid\n", 5);
    grid_write(&g, "type; ctrl+v pastes, ctrl+c copies, esc quits\n\n", 7);

    bool dirty = true;

    while (!nack_window_should_close(window)) {
        /*
         * Block until something happens, with a timeout only because the
         * cursor blinks. A terminal with no cursor animation would pass -1
         * and use exactly zero CPU while idle.
         */
        struct nack_event event;
        if (nack_wait_event_timeout(&event, 0.5)) {
            do {
                switch (event.type) {
                case NACK_EVENT_WINDOW_RESIZE: {
                    int cols = event.data.size.width / CELL_PIXELS_W;
                    int rows = event.data.size.height / CELL_PIXELS_H;
                    if (cols > MAX_COLS) cols = MAX_COLS;
                    if (rows > MAX_ROWS) rows = MAX_ROWS;
                    if (cols != g.cols || rows != g.rows) {
                        grid_resize(&g, cols, rows);
                        char note[64];
                        snprintf(note, sizeof note, "\n[resized to %dx%d]\n",
                                 cols, rows);
                        grid_write(&g, note, 3);
                    }
                    dirty = true;
                    break;
                }

                case NACK_EVENT_TEXT:
                    grid_write(&g, event.data.text.utf8, 0);
                    dirty = true;
                    break;

                case NACK_EVENT_KEY_DOWN:
                    switch (event.data.key.key) {
                    case NACK_KEY_ESCAPE:
                        nack_window_set_should_close(window, true);
                        break;
                    case NACK_KEY_ENTER:
                    case NACK_KEY_KP_ENTER:
                        grid_newline(&g);
                        break;
                    case NACK_KEY_BACKSPACE:
                        grid_backspace(&g);
                        break;
                    case NACK_KEY_V:
                        if (event.data.key.mods & NACK_MOD_CTRL) {
                            const char *text = nack_clipboard_get_text();
                            if (text)
                                grid_write(&g, text, 1);
                            else
                                grid_write(&g, "[clipboard empty]\n", 6);
                        }
                        break;
                    case NACK_KEY_C:
                        if (event.data.key.mods & NACK_MOD_CTRL) {
                            nack_clipboard_set_text("copied from libnack");
                            grid_write(&g, "\n[copied to clipboard]\n", 1);
                        }
                        break;
                    default:
                        break;
                    }
                    dirty = true;
                    break;

                case NACK_EVENT_MOUSE_DOWN: {
                    int col = (int)event.data.button.x / CELL_PIXELS_W;
                    int row = (int)event.data.button.y / CELL_PIXELS_H;
                    char note[64];
                    snprintf(note, sizeof note, "\n[click cell %d,%d x%d]\n",
                             col, row, event.data.button.click_count);
                    grid_write(&g, note, 4);
                    dirty = true;
                    break;
                }

                case NACK_EVENT_WINDOW_EXPOSE:
                case NACK_EVENT_WINDOW_SCALE:
                    dirty = true;
                    break;

                default:
                    break;
                }
            } while (nack_poll_event(&event));
        }

        /* The cursor blink alone is reason enough to redraw. */
        const double now = nack_time_seconds();
        static double last_blink = 0.0;
        if (fmod(now, 0.5) < 0.05 && now - last_blink > 0.4) {
            last_blink = now;
            dirty = true;
        }

        if (!dirty)
            continue;
        dirty = false;

        int fb_width = 0, fb_height = 0;
        nack_window_get_framebuffer_size(window, &fb_width, &fb_height);
        int width = 0, height = 0;
        nack_window_get_size(window, &width, &height);

        glViewport(0, 0, fb_width, fb_height);
        glClearColor(0.11f, 0.13f, 0.17f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const size_t count = build_vertices(&g, vertices, now);
        if (count > 0) {
            glUseProgram(program);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(count * sizeof *vertices), vertices,
                         GL_DYNAMIC_DRAW);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, font);
            glUniform1i(font_uniform, 0);
            /* Geometry is in logical pixels, so the viewport uniform is the
             * logical size even though glViewport used the framebuffer one. */
            glUniform2f(viewport_uniform, (float)width, (float)height);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)count);
        }

        nack_gl_swap_buffers(window);
    }

    free(vertices);
    glDeleteTextures(1, &font);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    nack_gl_context_destroy(context);
    nack_window_destroy(window);
    free(g.cells);
    nack_shutdown();
    return 0;
}
