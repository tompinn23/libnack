/* Minimal libnack example: open a window and report events. */
#include "nack/nack.h"

#include <stdio.h>

static void log_message(const char *message, void *user_data)
{
    (void)user_data;
    fprintf(stderr, "[nack] %s\n", message);
}

int main(void)
{
    nack_init_desc init = { .app_id = "nack.hello", .log_fn = log_message };
    if (!nack_init(&init)) {
        const char *message = NULL;
        nack_get_error(&message);
        fprintf(stderr, "nack_init failed: %s\n", message);
        return 1;
    }

    nack_window_desc desc;
    nack_window_desc_defaults(&desc);
    desc.title = "libnack - hello";
    desc.width = 960;
    desc.height = 600;

    nack_window *window = nack_window_create(&desc);
    if (!window) {
        const char *message = NULL;
        nack_get_error(&message);
        fprintf(stderr, "window creation failed: %s\n", message);
        nack_shutdown();
        return 1;
    }

    printf("backend: %s\n", nack_backend_name(nack_get_backend()));

    while (!nack_window_should_close(window)) {
        nack_event event;
        if (!nack_wait_event(&event))
            break;

        switch (event.type) {
        case NACK_EVENT_WINDOW_RESIZE:
            printf("resize %dx%d (framebuffer %dx%d)\n",
                   event.size.width, event.size.height,
                   event.size.fb_width, event.size.fb_height);
            break;
        case NACK_EVENT_KEY_DOWN:
            printf("key down: %s%s\n", nack_key_get_name(event.key.key),
                   event.key.repeat ? " (repeat)" : "");
            if (event.key.key == NACK_KEY_ESCAPE)
                nack_window_set_should_close(window, true);
            break;
        case NACK_EVENT_TEXT:
            printf("text: %s\n", event.text.utf8);
            break;
        case NACK_EVENT_MOUSE_DOWN:
            printf("mouse %d down at %.0f,%.0f (%d clicks)\n",
                   event.button.button, event.button.x, event.button.y,
                   event.button.click_count);
            break;
        case NACK_EVENT_MOUSE_SCROLL:
            printf("scroll %.2f,%.2f\n", event.scroll.dx, event.scroll.dy);
            break;
        case NACK_EVENT_WINDOW_CLOSE:
            printf("close requested\n");
            break;
        default:
            break;
        }
    }

    nack_window_destroy(window);
    nack_shutdown();
    return 0;
}
