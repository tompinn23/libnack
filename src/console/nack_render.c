/*
 * The console renderer.
 *
 * One quad per cell, built into a vertex buffer each frame and drawn in a
 * handful of passes: backgrounds first, then one pass per tileset in use so
 * that a console mixing font glyphs with graphical tiles still costs only a
 * couple of draw calls. A console is a few thousand cells, so rebuilding the
 * buffer outright is cheaper than tracking which cells changed.
 */
#include "nack_console_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* position.xy, uv.xy, fg.rgba, bg.rgba */
#define NACK_FLOATS_PER_VERTEX 12
#define NACK_VERTICES_PER_CELL 6

static const char *nack__vertex_source =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_position;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_fg;\n"
    "layout(location = 3) in vec4 a_bg;\n"
    "uniform vec2 u_viewport;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_fg;\n"
    "out vec4 v_bg;\n"
    "void main() {\n"
    "    vec2 ndc = (a_position / u_viewport) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_fg = a_fg;\n"
    "    v_bg = a_bg;\n"
    "}\n";

static const char *nack__fragment_source =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_fg;\n"
    "in vec4 v_bg;\n"
    "uniform sampler2D u_atlas;\n"
    "uniform int u_mode;\n"          /* 0 background, 1 glyph */
    "out vec4 frag_colour;\n"
    "void main() {\n"
    "    if (u_mode == 0) {\n"
    "        frag_colour = v_bg;\n"
    "        return;\n"
    "    }\n"
    /* A font atlas is white with coverage in alpha, so the multiply tints it.
     * A colour tileset carries its own rgb and the tint is usually white. */
    "    vec4 texel = texture(u_atlas, v_uv);\n"
    "    frag_colour = vec4(v_fg.rgb * texel.rgb, v_fg.a * texel.a);\n"
    "    if (frag_colour.a <= 0.0) discard;\n"
    "}\n";

static nack_gluint nack__compile(nack_glenum stage, const char *source)
{
    nack_gluint shader = glCreateShader(stage);
    nack_glint status = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        nack__error("console shader failed to compile: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool nack__render_init(void)
{
    nack_gluint vertex, fragment;
    nack_glint status = 0;

    vertex = nack__compile(GL_VERTEX_SHADER, nack__vertex_source);
    if (!vertex)
        return false;
    fragment = nack__compile(GL_FRAGMENT_SHADER, nack__fragment_source);
    if (!fragment) {
        glDeleteShader(vertex);
        return false;
    }

    nack__c.program = glCreateProgram();
    glAttachShader(nack__c.program, vertex);
    glAttachShader(nack__c.program, fragment);
    glLinkProgram(nack__c.program);
    glGetProgramiv(nack__c.program, GL_LINK_STATUS, &status);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!status) {
        char log[1024];
        glGetProgramInfoLog(nack__c.program, sizeof log, NULL, log);
        glDeleteProgram(nack__c.program);
        nack__c.program = 0;
        return nack__error("console shader failed to link: %s", log);
    }

    nack__c.uniform_viewport = glGetUniformLocation(nack__c.program, "u_viewport");
    nack__c.uniform_atlas = glGetUniformLocation(nack__c.program, "u_atlas");
    nack__c.uniform_mode = glGetUniformLocation(nack__c.program, "u_mode");

    glGenVertexArrays(1, &nack__c.vao);
    glBindVertexArray(nack__c.vao);
    glGenBuffers(1, &nack__c.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, nack__c.vbo);

    {
        const nack_glsizei stride = NACK_FLOATS_PER_VERTEX * (nack_glsizei)sizeof(float);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(0 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(4 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(8 * sizeof(float)));
        glEnableVertexAttribArray(3);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

void nack__render_shutdown(void)
{
    if (nack__c.vbo) glDeleteBuffers(1, &nack__c.vbo);
    if (nack__c.vao) glDeleteVertexArrays(1, &nack__c.vao);
    if (nack__c.program) glDeleteProgram(nack__c.program);
    free(nack__c.vertices);
    nack__c.vbo = nack__c.vao = nack__c.program = 0;
    nack__c.vertices = NULL;
    nack__c.vertex_capacity = 0;
}

/*
 * Works out where the console sits in the window. Integer scaling keeps the
 * tiles pixel-exact, which matters more for pixel art than filling the window
 * does; the remainder becomes letterbox.
 */
void nack__render_update_viewport(void)
{
    const struct nack_console *console = nack__c.root;
    const struct nack_tileset *font = nack__c.font;
    int console_w, console_h;

    if (!console || !font)
        return;

    console_w = console->columns * font->tile_width;
    console_h = console->rows * font->tile_height;
    if (console_w < 1 || console_h < 1)
        return;

    switch (nack__c.scaling) {
    case NACK_SCALE_STRETCH:
        nack__c.viewport_x = 0;
        nack__c.viewport_y = 0;
        nack__c.viewport_w = nack__c.fb_width;
        nack__c.viewport_h = nack__c.fb_height;
        break;

    case NACK_SCALE_FIT: {
        double sx = (double)nack__c.fb_width / console_w;
        double sy = (double)nack__c.fb_height / console_h;
        double scale = sx < sy ? sx : sy;
        nack__c.viewport_w = (int)(console_w * scale);
        nack__c.viewport_h = (int)(console_h * scale);
        nack__c.viewport_x = (nack__c.fb_width - nack__c.viewport_w) / 2;
        nack__c.viewport_y = (nack__c.fb_height - nack__c.viewport_h) / 2;
        break;
    }

    case NACK_SCALE_INTEGER:
    default: {
        int sx = nack__c.fb_width / console_w;
        int sy = nack__c.fb_height / console_h;
        int scale = sx < sy ? sx : sy;
        if (scale < 1)
            scale = 1;   /* smaller than one tile per pixel is unreadable */
        nack__c.viewport_w = console_w * scale;
        nack__c.viewport_h = console_h * scale;
        nack__c.viewport_x = (nack__c.fb_width - nack__c.viewport_w) / 2;
        nack__c.viewport_y = (nack__c.fb_height - nack__c.viewport_h) / 2;
        break;
    }
    }
}

static bool nack__reserve_vertices(size_t cells)
{
    size_t needed = cells * NACK_VERTICES_PER_CELL * NACK_FLOATS_PER_VERTEX;
    float *grown;

    if (needed <= nack__c.vertex_capacity)
        return true;
    grown = (float *)realloc(nack__c.vertices, needed * sizeof *grown);
    if (!grown)
        return nack__error("out of memory building the console vertex buffer");
    nack__c.vertices = grown;
    nack__c.vertex_capacity = needed;
    return true;
}

static void nack__emit_quad(float **cursor, float x0, float y0, float x1,
                            float y1, float u0, float v0, float u1, float v1,
                            struct nack_color fg, struct nack_color bg)
{
    const float fr = fg.r / 255.0f, fg_ = fg.g / 255.0f;
    const float fb = fg.b / 255.0f, fa = fg.a / 255.0f;
    const float br = bg.r / 255.0f, bg_ = bg.g / 255.0f;
    const float bb = bg.b / 255.0f, ba = bg.a / 255.0f;
    float *out = *cursor;
    int i;

    const float corners[6][4] = {
        { x0, y0, u0, v0 },
        { x1, y0, u1, v0 },
        { x1, y1, u1, v1 },
        { x0, y0, u0, v0 },
        { x1, y1, u1, v1 },
        { x0, y1, u0, v1 },
    };

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

static void nack__draw_batch(size_t vertex_count)
{
    if (vertex_count == 0)
        return;
    glBufferData(GL_ARRAY_BUFFER,
                 (nack_glsizeiptr)(vertex_count * NACK_FLOATS_PER_VERTEX *
                                   sizeof(float)),
                 nack__c.vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (nack_glsizei)vertex_count);
}

void nack__render_console(const struct nack_console *console)
{
    const struct nack_tileset *font = nack__c.font;
    float cell_w, cell_h;
    size_t cell_count, pass;
    int x, y;

    if (!console || !font || !nack__c.program)
        return;

    cell_count = (size_t)console->columns * (size_t)console->rows;
    if (!nack__reserve_vertices(cell_count))
        return;

    cell_w = (float)nack__c.viewport_w / (float)console->columns;
    cell_h = (float)nack__c.viewport_h / (float)console->rows;

    glViewport(nack__c.viewport_x, nack__c.viewport_y,
               nack__c.viewport_w, nack__c.viewport_h);
    glUseProgram(nack__c.program);
    glUniform2f(nack__c.uniform_viewport, (float)nack__c.viewport_w,
                (float)nack__c.viewport_h);
    glUniform1i(nack__c.uniform_atlas, 0);
    glBindVertexArray(nack__c.vao);
    glBindBuffer(GL_ARRAY_BUFFER, nack__c.vbo);

    /* Pass one: every cell's background, in a single draw. */
    {
        float *cursor = nack__c.vertices;
        for (y = 0; y < console->rows; ++y) {
            for (x = 0; x < console->columns; ++x) {
                const struct nack_cell *cell =
                    &console->cells[(size_t)y * console->columns + x];
                if (cell->bg.a == 0)
                    continue;
                nack__emit_quad(&cursor, x * cell_w, y * cell_h,
                                (x + 1) * cell_w, (y + 1) * cell_h,
                                0.0f, 0.0f, 0.0f, 0.0f, cell->fg, cell->bg);
            }
        }
        glUniform1i(nack__c.uniform_mode, 0);
        nack__draw_batch((size_t)(cursor - nack__c.vertices) /
                         NACK_FLOATS_PER_VERTEX);
    }

    /* Then one pass per atlas, so mixed glyph and tile consoles stay cheap. */
    glUniform1i(nack__c.uniform_mode, 1);
    glActiveTexture(GL_TEXTURE0);

    for (pass = 0; pass < nack__c.tileset_count; ++pass) {
        const struct nack_tileset *atlas = nack__c.tilesets[pass];
        float *cursor = nack__c.vertices;
        float du, dv;

        if (!atlas || atlas->count <= 0)
            continue;
        du = 1.0f / (float)atlas->columns;
        dv = 1.0f / (float)atlas->rows;

        for (y = 0; y < console->rows; ++y) {
            for (x = 0; x < console->columns; ++x) {
                const struct nack_cell *cell =
                    &console->cells[(size_t)y * console->columns + x];
                const struct nack_tileset *source =
                    cell->tileset ? cell->tileset : font;
                int index;
                float u0, v0;

                if (source != atlas)
                    continue;
                if (cell->fg.a == 0)
                    continue;

                if (cell->tileset) {
                    index = (int)cell->glyph;   /* already a tile index */
                } else {
                    if (cell->glyph == ' ' || cell->glyph == 0)
                        continue;
                    index = nack__tileset_index_for(atlas, cell->glyph);
                    if (index < 0)
                        index = nack__tileset_index_for(atlas, '?');
                    if (index < 0)
                        continue;
                }
                if (index < 0 || index >= atlas->count)
                    continue;

                u0 = (float)(index % atlas->columns) * du;
                v0 = (float)(index / atlas->columns) * dv;

                nack__emit_quad(&cursor, x * cell_w, y * cell_h,
                                (x + 1) * cell_w, (y + 1) * cell_h,
                                u0, v0, u0 + du, v0 + dv,
                                cell->fg, cell->bg);
            }
        }

        glBindTexture(GL_TEXTURE_2D, atlas->texture);
        nack__draw_batch((size_t)(cursor - nack__c.vertices) /
                         NACK_FLOATS_PER_VERTEX);
    }
}
