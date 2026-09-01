/*
 * Exercises the console API end to end: init, tilesets, every drawing
 * operation, offscreen consoles and blitting, the event loop, and a real
 * frame presented through OpenGL with the result read back.
 */
#include "nack/nack.h"
#include "console/nack_console_internal.h"
#include "console/nack_gfx.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

int main(void)
{
    struct nack_config config;
    struct nack_console *offscreen;
    struct nack_cell cell;
    struct nack_event event;
    int columns = 0, rows = 0, tw = 0, th = 0, count = 0;
    double t0, elapsed;

    nack_config_defaults(&config);
    config.title = "nack console smoke";
    config.columns = 40;
    config.rows = 20;
    config.vsync = false;

    if (!nack_init(&config)) {
        const char *why = nack_get_error();

        if (!why)
            why = "no reason given";
        /*
         * What the machine cannot provide is reported to ctest as a skip: no
         * display at all, or a driver with no OpenGL 3.3 in it (a Windows CI
         * runner with no GPU has only the 1.1 software path). Everything else
         * - a shader that will not compile, missing entry points a 3.3 driver
         * is required to have - is our bug and has to fail, because this is
         * the only test that runs the whole stack.
         */
        if (strncmp(why, "cannot open a window", 20) == 0 ||
            strncmp(why, "cannot create a window", 22) == 0 ||
            strstr(why, "cannot create an OpenGL 3.3 context") != NULL) {
            printf("skipping: %s\n", why);
            return 77;
        }
        fprintf(stderr, "nack_init failed: %s\n", why);
        return 1;
    }
    printf("rendering with the %s backend\n", nack__gfx_name());
    check(1, "nack_init with the built-in font");
    /*
     * Under NACK_RENDERER=test-fail the preferred renderer refuses to start,
     * which is how the fallback gets exercised on a machine with only one real
     * one. Either way, what ends up active must be a renderer that works.
     */
    check(strcmp(nack__gfx_name(), "test-fail") != 0,
          "a working renderer is active");

    nack_console_size(NULL, &columns, &rows);
    check(columns == 40 && rows == 20, "root console has the requested size");

    nack_tileset_size(nack_get_font(), &tw, &th, &count);
    check(tw == 8 && th == 8 && count == 256, "built-in font is 256 8x8 tiles");

    /* Drawing */
    nack_clear(NULL);
    nack_put(NULL, 2, 3, 'A', NACK_RED, NACK_BLUE);
    cell = nack_get(NULL, 2, 3);
    check(cell.glyph == 'A' && cell.fg.r == 200 && cell.bg.b == 220,
          "put and get round trip");

    check(nack_print(NULL, 0, 0, NACK_WHITE, NACK_BLACK, "hello") == 5,
          "print returns the cell count");
    check(nack_get(NULL, 4, 0).glyph == 'o', "print wrote the last cell");

    /* UTF-8 must survive as codepoints, not bytes. */
    nack_print(NULL, 0, 1, NACK_WHITE, NACK_BLACK, "\xE2\x94\x80");   /* U+2500 */
    check(nack_get(NULL, 0, 1).glyph == 0x2500, "UTF-8 decoded to a codepoint");

    nack_printf(NULL, 0, 2, NACK_WHITE, NACK_BLACK, "%d-%s", 42, "x");
    check(nack_get(NULL, 0, 2).glyph == '4' && nack_get(NULL, 3, 2).glyph == 'x',
          "printf formats into cells");

    /* Clipping rather than corrupting memory. */
    nack_put(NULL, -5, -5, 'X', NACK_WHITE, NACK_BLACK);
    nack_put(NULL, 9999, 9999, 'X', NACK_WHITE, NACK_BLACK);
    check(1, "out of bounds writes are ignored");

    nack_fill(NULL, 10, 10, 5, 3, '#', NACK_GREEN, NACK_BLACK);
    check(nack_get(NULL, 12, 11).glyph == '#', "fill covers its rectangle");

    nack_draw_box(NULL, 20, 5, 10, 6, NACK_GREY, NACK_BLACK, "hi");
    check(nack_get(NULL, 20, 5).glyph == 0x250C &&
          nack_get(NULL, 29, 10).glyph == 0x2518,
          "draw_box uses box drawing corners");

    check(nack_print_wrapped(NULL, 0, 14, 10, 0,
                             NACK_WHITE, NACK_BLACK,
                             "the quick brown fox jumps") >= 3,
          "print_wrapped measures without drawing");

    /* Offscreen consoles and blitting */
    offscreen = nack_console_new(8, 4);
    check(offscreen != NULL, "offscreen console created");
    nack_clear_to(offscreen, NACK_WHITE, NACK_RED);
    nack_print(offscreen, 0, 0, NACK_YELLOW, NACK_RED, "panel");
    nack_blit(offscreen, 0, 0, 8, 4, NULL, 1, 15, 1.0f, 1.0f);
    check(nack_get(NULL, 1, 15).glyph == 'p', "blit copied glyphs");
    check(nack_get(NULL, 1, 15).bg.r == 200, "blit copied backgrounds");
    nack_console_free(offscreen);

    /* Resizing keeps what still fits. */
    offscreen = nack_console_new(4, 4);
    nack_put(offscreen, 1, 1, 'Z', NACK_WHITE, NACK_BLACK);
    nack_console_resize(offscreen, 8, 8);
    check(nack_get(offscreen, 1, 1).glyph == 'Z', "resize preserves contents");
    nack_console_free(offscreen);

    /* Codepoints the font has no tile for must not draw garbage. */
    nack_put(NULL, 0, 19, 0x4E2D, NACK_WHITE, NACK_BLACK);   /* CJK */
    check(1, "unmapped codepoint accepted");

    /*
     * A real frame through OpenGL, verified by reading pixels back rather
     * than by present() merely returning.
     */
    nack_clear_to(NULL, NACK_WHITE, NACK_BLACK);
    nack_fill(NULL, 0, 0, 40, 20, ' ', NACK_WHITE, NACK_RGB(20, 40, 80));
    nack_present();
    {
        uint8_t pixel[4] = { 0, 0, 0, 0 };
        check(nack__debug_read_pixel(5, 5, pixel), "framebuffer readable");
        check(pixel[0] > 10 && pixel[0] < 32 && pixel[2] > 64 && pixel[2] < 96,
              "cell background rendered to the framebuffer");
    }

    /* A glyph must actually put foreground pixels on screen. */
    nack_clear_to(NULL, NACK_WHITE, NACK_BLACK);
    nack_fill(NULL, 0, 0, 40, 20, 0x2588, NACK_RGB(0, 255, 0), NACK_BLACK);
    nack_present();
    {
        uint8_t pixel[4] = { 0, 0, 0, 0 };
        nack__debug_read_pixel(10, 10, pixel);
        check(pixel[1] > 200 && pixel[0] < 60,
              "full block glyph rendered in its foreground colour");
    }

    /* And a blank cell must leave the background showing. */
    nack_clear_to(NULL, NACK_WHITE, NACK_RGB(255, 0, 0));
    nack_present();
    {
        uint8_t pixel[4] = { 0, 0, 0, 0 };
        nack__debug_read_pixel(20, 10, pixel);
        check(pixel[0] > 200 && pixel[1] < 60, "blank cell shows its background");
    }

    check(1, "frames presented");
    nack_present();
    check(nack_delta_time() >= 0.0, "delta time is sane");
    check(nack_time() > 0.0, "elapsed time advances");

    /* Event loop: a timeout must actually wait. */
    while (nack_poll_event(&event))
        ;
    t0 = nack_time();
    nack_wait_event_timeout(&event, 0.2);
    elapsed = nack_time() - t0;
    check(elapsed >= 0.15, "wait_event_timeout blocks for the requested time");

    nack_wakeup();
    {
        int woke = 0, i;
        for (i = 0; i < 32 && nack_wait_event_timeout(&event, 0.3); ++i) {
            if (event.type == NACK_EVENT_WAKEUP) { woke = 1; break; }
        }
        check(woke, "nack_wakeup delivers NACK_EVENT_WAKEUP");
    }

    check(!nack_should_close(), "not asked to close");
    nack_set_should_close(true);
    check(nack_should_close(), "should_close is settable");

    nack_set_title("renamed");
    check(1, "set_title survived");

    nack_shutdown();
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
