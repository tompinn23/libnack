/*
 * Exercises the console API end to end: init, tilesets, every drawing
 * operation, offscreen consoles and blitting, the event loop, and a real
 * frame presented through OpenGL with the result read back.
 */
#include "nack/nack.h"
#include "console/nack_console_internal.h"
#include "console/nack_gfx.h"
#include "nack_window.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if defined(NACK_X11_SELECTION_PEER)
#  include <poll.h>
#  include <unistd.h>
#  include "x11_selection_payload.h"
#endif

static int failures;

/*
 * Talking to the selection peer.
 *
 * A selection this process owns is served from libnack's own copy without
 * touching the server, so neither INCR path is reachable from here alone.
 * tests/x11_selection_peer.c is the other end; these run it and wait on its
 * output rather than sleeping, so the test is not a race.
 */
#if defined(NACK_X11_SELECTION_PEER)

static FILE *nack__test_peer_start(const char *mode)
{
    char command[4096];

    /* Quoted: the build directory is wherever the user put it. */
    snprintf(command, sizeof command, "\"%s\" %s", NACK_X11_SELECTION_PEER,
             mode);
    return popen(command, "r");
}

/*
 * Reads a line from the peer while keeping our own event loop turning. The
 * peer is waiting on us for the whole transfer, so a blocking read here would
 * deadlock: it cannot finish until we answer, and we would not be answering.
 */
static int nack__test_peer_line(FILE *pipe, char *out, size_t size,
                                double timeout)
{
    int fd = fileno(pipe);
    size_t filled = 0;
    double deadline = nack_time() + timeout;

    while (nack_time() < deadline) {
        struct pollfd pfd;
        struct nack_event ignored;
        ssize_t got;

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
            got = read(fd, out + filled, size - filled - 1);
            if (got <= 0)
                break;                 /* the peer exited */
            filled += (size_t)got;
            out[filled] = '\0';
            if (strchr(out, '\n') || filled + 1 >= size)
                return 1;
            continue;
        }

        /* Servicing the connection is what lets the peer make progress. */
        nack_wait_event_timeout(&ignored, 0.01);
    }

    out[filled] = '\0';
    return filled > 0;
}

#endif /* NACK_X11_SELECTION_PEER */

static void check(int ok, const char *what)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

/*
 * Same, but says what the pixel actually was. These are the checks most
 * likely to fail on a machine nobody here can run, and "FAIL" on its own
 * would leave nothing to go on.
 */
static void checkpx(int ok, const char *what, const uint8_t rgba[4],
                    const char *expected)
{
    printf("%-52s %s", what, ok ? "ok" : "FAIL");
    if (!ok)
        printf("  (got %u,%u,%u,%u; wanted %s)", rgba[0], rgba[1], rgba[2],
               rgba[3], expected);
    printf("\n");
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
    /* Frames are gone once presented unless the renderer is told to keep one. */
    nack__debug_capture_frames(true);
    check(1, "nack_init with the built-in font");
    /*
     * Under NACK_RENDERER=test-fail the preferred renderer refuses to start,
     * which is how the fallback gets exercised on a machine with only one real
     * one. Either way, what ends up active must be a renderer that works.
     */
    check(strcmp(nack__gfx_name(), "test-fail") != 0,
          "a working renderer is active");

    nack_console_size(nack_root(), &columns, &rows);
    check(columns == 40 && rows == 20, "root console has the requested size");

    nack_tileset_size(nack_get_font(), &tw, &th, &count);
    check(tw == 8 && th == 8 && count == 256, "built-in font is 256 8x8 tiles");

    /* Drawing */
    nack_clear(nack_root());
    nack_put(nack_root(), 2, 3, 'A', NACK_RED, NACK_BLUE);
    cell = nack_get(nack_root(), 2, 3);
    check(cell.glyph == 'A' && cell.fg.r == 200 && cell.bg.b == 220,
          "put and get round trip");

    check(nack_print(nack_root(), 0, 0, NACK_WHITE, NACK_BLACK, "hello") == 5,
          "print returns the cell count");
    check(nack_get(nack_root(), 4, 0).glyph == 'o', "print wrote the last cell");

    /*
     * A null console must not fall back to the root. This is the whole reason
     * the root is spelled nack_root(): a console that failed to allocate used
     * to draw to the screen, and the caller never found out.
     */
    nack_put(nack_root(), 30, 18, 'Z', NACK_WHITE, NACK_BLACK);
    nack_put(NULL, 30, 18, 'Q', NACK_RED, NACK_BLUE);
    check(nack_get(nack_root(), 30, 18).glyph == 'Z',
          "a null console does not draw to the root");
    check(nack_get(NULL, 30, 18).glyph == 0,
          "reading a null console yields nothing");
    check(nack_print(NULL, 0, 0, NACK_WHITE, NACK_BLACK, "x") == 0,
          "printing to a null console writes nothing");
    {
        const char *why = nack_get_error();
        check(why != NULL && strstr(why, "nack_root") != NULL,
              "and says what to use instead");
    }
    nack_put(nack_root(), 30, 18, ' ', NACK_WHITE, NACK_BLACK);

    /* UTF-8 must survive as codepoints, not bytes. */
    nack_print(nack_root(), 0, 1, NACK_WHITE, NACK_BLACK, "\xE2\x94\x80");   /* U+2500 */
    check(nack_get(nack_root(), 0, 1).glyph == 0x2500, "UTF-8 decoded to a codepoint");

    nack_printf(nack_root(), 0, 2, NACK_WHITE, NACK_BLACK, "%d-%s", 42, "x");
    check(nack_get(nack_root(), 0, 2).glyph == '4' && nack_get(nack_root(), 3, 2).glyph == 'x',
          "printf formats into cells");

    /*
     * vsnprintf can fail part way through - "%ls" with an unconvertible wide
     * string does it on glibc - returning a negative count after writing the
     * text that came before the failing conversion. Printing that buffer
     * anyway puts a truncated line on screen and reports it as a success, so
     * the caller never learns the text it asked for is not the text there.
     */
    {
        char probe[16];
        wchar_t unconvertible[2];
        unconvertible[0] = (wchar_t)0xDFFF;   /* a lone surrogate */
        unconvertible[1] = 0;

        if (snprintf(probe, sizeof probe, "ab%ls", unconvertible) >= 0) {
            printf("%-52s skipped (vsnprintf accepts it here)\n",
                   "a failed vsnprintf prints nothing");
        } else {
            int printed;
            nack_clear(nack_root());
            printed = nack_printf(nack_root(), 0, 3, NACK_WHITE, NACK_BLACK,
                                  "ab%ls", unconvertible);
            check(printed == 0, "a failed vsnprintf prints nothing");
            check(nack_get(nack_root(), 0, 3).glyph == ' ',
                  "and leaves the cells alone");
        }
    }

    /*
     * A console whose cell count cannot be addressed has to be refused. Where
     * size_t is 64 bits calloc catches this itself, so this only pins the
     * behaviour; the check in nack_console_new is for 32-bit builds, where
     * the multiply wraps and calloc is handed a plausible small number.
     */
    {
        struct nack_console *huge = nack_console_new(INT_MAX, INT_MAX);
        check(huge == NULL, "an unaddressable console size is refused");
        nack_console_free(huge);
    }

    /* Clipping rather than corrupting memory. */
    nack_put(nack_root(), -5, -5, 'X', NACK_WHITE, NACK_BLACK);
    nack_put(nack_root(), 9999, 9999, 'X', NACK_WHITE, NACK_BLACK);
    check(1, "out of bounds writes are ignored");

    nack_fill(nack_root(), 10, 10, 5, 3, '#', NACK_GREEN, NACK_BLACK);
    check(nack_get(nack_root(), 12, 11).glyph == '#', "fill covers its rectangle");

    nack_draw_box(nack_root(), 20, 5, 10, 6, NACK_GREY, NACK_BLACK, "hi");
    check(nack_get(nack_root(), 20, 5).glyph == 0x250C &&
          nack_get(nack_root(), 29, 10).glyph == 0x2518,
          "draw_box uses box drawing corners");

    check(nack_print_wrapped(nack_root(), 0, 14, 10, 0,
                             NACK_WHITE, NACK_BLACK,
                             "the quick brown fox jumps") >= 3,
          "print_wrapped measures without drawing");

    /* Offscreen consoles and blitting */
    offscreen = nack_console_new(8, 4);
    check(offscreen != NULL, "offscreen console created");
    nack_clear_to(offscreen, NACK_WHITE, NACK_RED);
    nack_print(offscreen, 0, 0, NACK_YELLOW, NACK_RED, "panel");
    nack_blit(offscreen, 0, 0, 8, 4, nack_root(), 1, 15, 1.0f, 1.0f);
    check(nack_get(nack_root(), 1, 15).glyph == 'p', "blit copied glyphs");
    check(nack_get(nack_root(), 1, 15).bg.r == 200, "blit copied backgrounds");
    nack_console_free(offscreen);

    /* Resizing keeps what still fits. */
    offscreen = nack_console_new(4, 4);
    nack_put(offscreen, 1, 1, 'Z', NACK_WHITE, NACK_BLACK);
    nack_console_resize(offscreen, 8, 8);
    check(nack_get(offscreen, 1, 1).glyph == 'Z', "resize preserves contents");
    nack_console_free(offscreen);

    /*
     * A tileset seeds its codepoint map before it creates its atlas texture,
     * so a texture that cannot be made leaves a tileset owning a map and
     * nothing else. Releasing only the struct loses that map. Nothing is
     * asserted here beyond the failure being reported - the leak itself is
     * what LSan sees, in the sanitiser job.
     */
    {
        uint8_t sheet[16 * 16 * 4];
        struct nack_tileset *doomed;

        memset(sheet, 0, sizeof sheet);
        nack__debug_fail_next_textures(1);
        doomed = nack__tileset_from_rgba(sheet, 16, 16, 8, 8,
                                         NACK_LAYOUT_CP437);
        check(doomed == NULL, "a tileset with no texture is not handed out");
        nack_tileset_free(doomed);
    }

    /*
     * A big sheet has more tiles than a 16-bit index can hold: 2048x2048 of
     * 8x8 tiles is 65536 of them. Mapping a codepoint to a high tile has to
     * survive the round trip rather than wrap into a negative slot and
     * silently map to nothing.
     */
    {
        uint8_t *big = (uint8_t *)calloc(2048u * 2048u, 4);
        struct nack_tileset *wide;

        if (!big) {
            check(0, "out of memory building the wide tileset");
        } else {
            wide = nack__tileset_from_rgba(big, 2048, 2048, 8, 8,
                                           NACK_LAYOUT_ROW_MAJOR);
            check(wide != NULL, "a 65536 tile sheet loads");
            if (wide) {
                check(nack_tileset_map(wide, 'Q', 40000),
                      "a codepoint maps to a tile past 32767");
                check(nack__tileset_index_for(wide, 'Q') == 40000,
                      "and reads back as the tile it was given");
            }
            nack_tileset_free(wide);
            free(big);
        }
    }

    /* Codepoints the font has no tile for must not draw garbage. */
    nack_put(nack_root(), 0, 19, 0x4E2D, NACK_WHITE, NACK_BLACK);   /* CJK */
    check(1, "unmapped codepoint accepted");

    /*
     * A real frame through OpenGL, verified by reading pixels back rather
     * than by present() merely returning.
     */
    nack_clear_to(nack_root(), NACK_WHITE, NACK_BLACK);
    nack_fill(nack_root(), 0, 0, 40, 20, ' ', NACK_WHITE, NACK_RGB(20, 40, 80));
    nack_present();
    {
        uint8_t pixel[4] = { 0, 0, 0, 0 };
        check(nack__debug_read_pixel(5, 5, pixel), "framebuffer readable");
        checkpx(pixel[0] > 10 && pixel[0] < 32 && pixel[2] > 64 && pixel[2] < 96,
                "cell background rendered to the framebuffer", pixel,
                "r 10..32, b 64..96");
    }

    /* A glyph must actually put foreground pixels on screen. */
    nack_clear_to(nack_root(), NACK_WHITE, NACK_BLACK);
    nack_fill(nack_root(), 0, 0, 40, 20, 0x2588, NACK_RGB(0, 255, 0), NACK_BLACK);
    nack_present();
    {
        uint8_t pixel[4] = { 0, 0, 0, 0 };
        nack__debug_read_pixel(10, 10, pixel);
        checkpx(pixel[1] > 200 && pixel[0] < 60,
                "full block glyph rendered in its foreground colour", pixel,
                "g > 200, r < 60");
    }

    /* And a blank cell must leave the background showing. */
    nack_clear_to(nack_root(), NACK_WHITE, NACK_RGB(255, 0, 0));
    nack_present();
    {
        uint8_t pixel[4] = { 0, 0, 0, 0 };
        nack__debug_read_pixel(20, 10, pixel);
        checkpx(pixel[0] > 200 && pixel[1] < 60,
                "blank cell shows its background", pixel, "r > 200, g < 60");
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

    /*
     * The clipboard, in both directions and at a size that has to be sent in
     * chunks. Nothing exercised any of this before: the INCR code is the most
     * intricate in the X11 backend and had never been run.
     *
     * Not every backend can do it. A headless Wayland compositor has no
     * wl_seat, and a selection needs one, so there the library refuses and
     * says why - which is an answer, not a failure, and this reports it as a
     * skip rather than pretending the machine is broken.
     */
    if (!nack_clipboard_set("hello clipboard")) {
        printf("%-52s skipped (%s)\n", "clipboard", nack_get_error());
    } else {
        const char *back = nack_clipboard_get();

        check(1, "clipboard accepts text");
        check(back && strcmp(back, "hello clipboard") == 0,
              "clipboard round trips a short string");

#if defined(NACK_X11_SELECTION_PEER)
        /*
         * The peer is an X client, so it is only any use when X11 is the
         * backend actually running - the XCB backend being compiled in says
         * nothing about which one was chosen.
         */
        if (nack__win_get_backend() != NACK_BACKEND_X11) {
            printf("%-52s skipped (not X11)\n", "INCR transfers both ways");
        } else {
            char *payload = (char *)malloc(NACK_PEER_PAYLOAD_BYTES);
            char line[256];
            FILE *peer;

            if (!payload) {
                check(0, "out of memory building the clipboard payload");
            } else {
                nack__peer_payload(payload, NACK_PEER_PAYLOAD_BYTES);

                /*
                 * Reading: the peer owns the selection and serves it in 4K
                 * chunks, so what comes back has crossed the wire and been
                 * reassembled rather than being handed over from our own
                 * copy. The payload is unlike anything we put on the
                 * clipboard ourselves, so a read served locally would not
                 * match it.
                 */
                peer = nack__test_peer_start("own");
                check(peer != NULL, "the selection peer starts");
                if (peer) {
                    check(nack__test_peer_line(peer, line, sizeof line, 5.0) &&
                          strncmp(line, "ready", 5) == 0,
                          "another client owns the clipboard");

                    back = nack_clipboard_get();
                    check(back != NULL && strlen(back) == strlen(payload),
                          "an INCR selection arrives at its full length");
                    check(back != NULL && strcmp(back, payload) == 0,
                          "and every chunk landed in the right place");
                    pclose(peer);
                }

                /*
                 * Writing: the same size back out, read by the peer. This is
                 * the serving half of INCR, which announces the transfer and
                 * then has to answer each property delete with the next
                 * chunk.
                 */
                check(nack_clipboard_set(payload), "clipboard accepts 300K");
                peer = nack__test_peer_start("read");
                check(peer != NULL, "the selection peer starts again");
                if (peer) {
                    unsigned long length = 0;
                    unsigned sum = 0;
                    int incremental = 0;
                    int parsed = nack__test_peer_line(peer, line, sizeof line,
                                                      10.0) &&
                                 sscanf(line, "len=%lu sum=%x incr=%d",
                                        &length, &sum, &incremental) == 3;

                    check(parsed, "another client reads the clipboard back");
                    check(parsed && incremental,
                          "and is served incrementally, not in one property");
                    check(parsed && length == strlen(payload) &&
                          sum == nack__peer_checksum(payload, strlen(payload)),
                          "with the bytes intact");
                    pclose(peer);
                }
                free(payload);
            }
        }
#else
        printf("%-52s skipped (no X11)\n", "INCR transfers both ways");
#endif
    }

    nack_set_title("renamed");
    check(1, "set_title survived");

    nack_shutdown();
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
