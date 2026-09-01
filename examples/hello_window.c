/*
 * Minimal libnack example: open a window and report events.
 *
 * Note the context and the clear. On Wayland a surface is not mapped until a
 * buffer has been committed to it, so a window that never presents a frame is
 * never visible at all. X11 and Win32 will show an empty window; Wayland
 * shows nothing. Presenting at least one frame is therefore not optional.
 */
#include "nack/nack.h"

#include <stdio.h>

#define GL_COLOR_BUFFER_BIT 0x00004000

typedef void (*pfn_clear)(unsigned int);
typedef void (*pfn_clear_colour)(float, float, float, float);
typedef void (*pfn_viewport)(int, int, int, int);

static void log_message(const char *message, void *user_data)
{
    (void)user_data;
    fprintf(stderr, "[nack] %s\n", message);
}

int main(void)
{
    struct nack_init_desc init = { .app_id = "nack.hello", .log_fn = log_message };
    if (!nack_init(&init)) {
        const char *message = NULL;
        nack_get_error(&message);
        fprintf(stderr, "nack_init failed: %s\n", message);
        return 1;
    }

    struct nack_window_desc desc;
    nack_window_desc_defaults(&desc);
    desc.title = "libnack - hello";
    desc.width = 960;
    desc.height = 600;

    struct nack_window *window = nack_window_create(&desc);
    if (!window) {
        const char *message = NULL;
        nack_get_error(&message);
        fprintf(stderr, "window creation failed: %s\n", message);
        nack_shutdown();
        return 1;
    }

    printf("backend: %s\n", nack_backend_name(nack_get_backend()));

    struct nack_gl_desc gl_desc;
    nack_gl_desc_defaults(&gl_desc);
    struct nack_gl_context *context = nack_gl_context_create(window, &gl_desc);
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

    pfn_clear gl_clear = (pfn_clear)nack_gl_get_proc_address("glClear");
    pfn_clear_colour gl_clear_colour =
        (pfn_clear_colour)nack_gl_get_proc_address("glClearColor");
    pfn_viewport gl_viewport = (pfn_viewport)nack_gl_get_proc_address("glViewport");
    if (!gl_clear || !gl_clear_colour || !gl_viewport) {
        fprintf(stderr, "could not resolve the OpenGL entry points\n");
        nack_gl_context_destroy(context);
        nack_window_destroy(window);
        nack_shutdown();
        return 1;
    }

    bool needs_redraw = true;

    while (!nack_window_should_close(window)) {
        struct nack_event event;
        if (!nack_wait_event(&event))
            break;

        do {
            switch (event.type) {
            case NACK_EVENT_WINDOW_RESIZE:
                printf("resize %dx%d (framebuffer %dx%d)\n",
                       event.data.size.width, event.data.size.height,
                       event.data.size.fb_width, event.data.size.fb_height);
                needs_redraw = true;
                break;
            case NACK_EVENT_WINDOW_EXPOSE:
                needs_redraw = true;
                break;
            case NACK_EVENT_WINDOW_SCALE:
                printf("content scale is now %.2f\n", (double)event.data.scale.scale);
                needs_redraw = true;
                break;
            case NACK_EVENT_KEY_DOWN:
                printf("key down: %s%s\n", nack_key_get_name(event.data.key.key),
                       event.data.key.repeat ? " (repeat)" : "");
                if (event.data.key.key == NACK_KEY_ESCAPE)
                    nack_window_set_should_close(window, true);
                break;
            case NACK_EVENT_TEXT:
                printf("text: %s\n", event.data.text.utf8);
                break;
            case NACK_EVENT_MOUSE_DOWN:
                printf("mouse %d down at %.0f,%.0f (%d clicks)\n",
                       event.data.button.button, event.data.button.x, event.data.button.y,
                       event.data.button.click_count);
                break;
            case NACK_EVENT_MOUSE_SCROLL:
                printf("scroll %.2f,%.2f\n", event.data.scroll.dx, event.data.scroll.dy);
                break;
            case NACK_EVENT_WINDOW_FOCUS:
                printf("focused\n");
                break;
            case NACK_EVENT_WINDOW_CLOSE:
                printf("close requested\n");
                break;
            default:
                break;
            }
        } while (nack_poll_event(&event));

        if (!needs_redraw)
            continue;
        needs_redraw = false;

        int width = 0, height = 0;
        nack_window_get_framebuffer_size(window, &width, &height);
        gl_viewport(0, 0, width, height);
        gl_clear_colour(0.11f, 0.13f, 0.17f, 1.0f);
        gl_clear(GL_COLOR_BUFFER_BIT);
        nack_gl_swap_buffers(window);
    }

    nack_gl_context_destroy(context);
    nack_window_destroy(window);
    nack_shutdown();
    return 0;
}
