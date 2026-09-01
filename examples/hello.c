/* The smallest useful libnack program. */
#include "nack/nack.h"

#include <stdio.h>

int main(void)
{
    struct nack_config config;
    int frame = 0;

    nack_config_defaults(&config);
    config.title = "libnack - hello";
    config.columns = 60;
    config.rows = 20;

    if (!nack_init(&config)) {
        fprintf(stderr, "nack_init failed: %s\n", nack_get_error());
        return 1;
    }

    while (!nack_should_close()) {
        struct nack_event event;

        while (nack_poll_event(&event)) {
            if (event.type == NACK_EVENT_QUIT ||
                (event.type == NACK_EVENT_KEY_DOWN &&
                 event.data.key.key == NACK_KEY_ESCAPE))
                nack_set_should_close(true);
        }

        nack_clear(NULL);
        nack_draw_box(NULL, 0, 0, 60, 20, NACK_GREY, NACK_BLACK, "libnack");
        nack_print(NULL, 3, 3, NACK_WHITE, NACK_BLACK, "Hello from the console.");
        nack_print(NULL, 3, 5, NACK_YELLOW, NACK_BLACK, "@");
        nack_print(NULL, 5, 5, NACK_GREY, NACK_BLACK, "<- that is you, probably");
        nack_printf(NULL, 3, 8, NACK_CYAN, NACK_BLACK, "frame %d", frame++);
        nack_printf(NULL, 3, 9, NACK_CYAN, NACK_BLACK, "%.1f ms",
                    nack_delta_time() * 1000.0);
        nack_print(NULL, 3, 17, NACK_DARK_GREY, NACK_BLACK, "escape to quit");

        nack_present();
    }

    nack_shutdown();
    return 0;
}
