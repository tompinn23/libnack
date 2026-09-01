/*
 * Creates an OpenGL 3.3 core context and draws a triangle.
 *
 * Shows the render-loop shape: poll everything queued, then draw once.
 */
#include "nack/nack.h"

#include <stdio.h>
#include <stdlib.h>

#include "gl_loader.h"

static const char *vertex_source =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_position;\n"
    "layout(location = 1) in vec3 a_colour;\n"
    "out vec3 v_colour;\n"
    "void main() {\n"
    "    v_colour = a_colour;\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "}\n";

static const char *fragment_source =
    "#version 330 core\n"
    "in vec3 v_colour;\n"
    "out vec4 frag_colour;\n"
    "void main() {\n"
    "    frag_colour = vec4(v_colour, 1.0);\n"
    "}\n";

int main(void)
{
    if (!nack_init(&(nack_init_desc){ .app_id = "nack.triangle" })) {
        const char *message = NULL;
        nack_get_error(&message);
        fprintf(stderr, "nack_init failed: %s\n", message);
        return 1;
    }

    nack_window_desc window_desc;
    nack_window_desc_defaults(&window_desc);
    window_desc.title = "libnack - triangle";
    window_desc.width = 800;
    window_desc.height = 600;

    nack_window *window = nack_window_create(&window_desc);
    if (!window) {
        const char *message = NULL;
        nack_get_error(&message);
        fprintf(stderr, "window creation failed: %s\n", message);
        nack_shutdown();
        return 1;
    }

    nack_gl_desc gl_desc;
    nack_gl_desc_defaults(&gl_desc);
    nack_gl_context *context = nack_gl_context_create(window, &gl_desc);
    if (!context) {
        const char *message = NULL;
        nack_get_error(&message);
        fprintf(stderr, "GL context creation failed: %s\n", message);
        nack_window_destroy(window);
        nack_shutdown();
        return 1;
    }

    nack_gl_make_current(window, context);
    nack_gl_set_swap_interval(1);

    if (!nack_example_load_gl()) {
        fprintf(stderr, "failed to load OpenGL entry points\n");
        nack_gl_context_destroy(context);
        nack_window_destroy(window);
        nack_shutdown();
        return 1;
    }

    printf("backend:  %s\n", nack_backend_name(nack_get_backend()));
    printf("renderer: %s\n", (const char *)glGetString(GL_RENDERER));
    printf("version:  %s\n", (const char *)glGetString(GL_VERSION));

    GLuint program = nack_example_program(vertex_source, fragment_source);
    if (!program) {
        nack_gl_context_destroy(context);
        nack_window_destroy(window);
        nack_shutdown();
        return 1;
    }

    const GLfloat vertices[] = {
        /* position     colour        */
         0.0f,  0.6f,   1.0f, 0.2f, 0.3f,
        -0.6f, -0.4f,   0.2f, 0.8f, 0.4f,
         0.6f, -0.4f,   0.3f, 0.4f, 1.0f,
    };

    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                          (const void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                          (const void *)(2 * sizeof(GLfloat)));
    glEnableVertexAttribArray(1);

    while (!nack_window_should_close(window)) {
        nack_event event;
        while (nack_poll_event(&event)) {
            if (event.type == NACK_EVENT_KEY_DOWN &&
                event.key.key == NACK_KEY_ESCAPE)
                nack_window_set_should_close(window, true);
        }

        int width = 0, height = 0;
        nack_window_get_framebuffer_size(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.09f, 0.11f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        nack_gl_swap_buffers(window);
    }

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    nack_gl_context_destroy(context);
    nack_window_destroy(window);
    nack_shutdown();
    return 0;
}
