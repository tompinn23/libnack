/*
 * Exercises the console API end to end: init, tilesets, every drawing
 * operation, offscreen consoles and blitting, the event loop, and a real
 * frame presented through OpenGL with the result read back.
 *
 * Most of this goes through <nack/nack.hpp>, the only API there is now. A
 * handful of sections reach into the engine directly - the internal headers
 * are fair game for the project's own tests - because what they are
 * checking (a texture that fails to build mid-tileset, a fixed-size
 * registry, a vsnprintf that used to fail silently before it was deleted
 * along with the C-varargs printf path it belonged to) has no public
 * surface a normal caller would ever reach.
 */
#include <nack/nack.hpp>

#include "console/nack_console_internal.h"
#include "console/nack_gfx.h"
#include "nack_window.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(NACK_X11_SELECTION_PEER)
#  include "x11_selection_payload.h"
#  include <poll.h>
#  include <unistd.h>
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
static bool nack__test_peer_line(const nack::app &app, FILE *pipe, char *out,
                                 size_t size, double timeout)
{
    int fd = fileno(pipe);
    size_t filled = 0;
    double deadline = app.time() + timeout;

    while (app.time() < deadline) {
        struct pollfd pfd;
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
                return true;
            continue;
        }

        /* Servicing the connection is what lets the peer make progress. */
        app.wait_for(0.01);
    }

    out[filled] = '\0';
    return filled > 0;
}

#endif /* NACK_X11_SELECTION_PEER */

static void check(bool ok, const char *what)
{
    std::printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

/*
 * Same, but says what the pixel actually was. These are the checks most
 * likely to fail on a machine nobody here can run, and "FAIL" on its own
 * would leave nothing to go on.
 */
static void checkpx(bool ok, const char *what, const uint8_t rgba[4],
                    const char *expected)
{
    std::printf("%-52s %s", what, ok ? "ok" : "FAIL");
    if (!ok)
        std::printf("  (got %u,%u,%u,%u; wanted %s)", rgba[0], rgba[1],
                    rgba[2], rgba[3], expected);
    std::printf("\n");
    if (!ok)
        failures++;
}

int main()
{
    nack::config config = nack::default_config();
    config.title = "nack console smoke";
    config.columns = 40;
    config.rows = 20;
    config.vsync = false;

    auto app = nack::app::try_create(config);
    if (!app) {
        std::string why(nack::last_error());
        if (why.empty())
            why = "no reason given";
        /*
         * What the machine cannot provide is reported to ctest as a skip: no
         * display at all, or a driver with no OpenGL 3.3 in it (a Windows CI
         * runner with no GPU has only the 1.1 software path). Everything else
         * - a shader that will not compile, missing entry points a 3.3
         * driver is required to have - is our bug and has to fail, because
         * this is the only test that runs the whole stack.
         */
        if (why.rfind("cannot open a window", 0) == 0 ||
            why.rfind("cannot create a window", 0) == 0 ||
            why.find("cannot create an OpenGL 3.3 context") != std::string::npos) {
            std::printf("skipping: %s\n", why.c_str());
            return 77;
        }
        std::fprintf(stderr, "nack::app failed: %s\n", why.c_str());
        return 1;
    }
    std::printf("rendering with the %s backend\n", nack__gfx_name());
    /* Frames are gone once presented unless the renderer is told to keep one. */
    nack__debug_capture_frames(true);
    check(true, "app constructed with the built-in font");
    /*
     * Under NACK_RENDERER=test-fail the preferred renderer refuses to start,
     * which is how the fallback gets exercised on a machine with only one
     * real one. Either way, what ends up active must be a renderer that
     * works.
     */
    check(std::strcmp(nack__gfx_name(), "test-fail") != 0,
          "a working renderer is active");

    auto [columns, rows] = app->console().size();
    check(columns == 40 && rows == 20, "root console has the requested size");

    {
        int tw = 0, th = 0, count = 0;
        nack__tileset_size(nack__app_get_font(), &tw, &th, &count);
        check(tw == 8 && th == 8 && count == 256,
              "built-in font is 256 8x8 tiles");
    }

    /* Drawing */
    app->console().clear();
    app->console().put(2, 3, 'A', nack::red, nack::blue);
    nack::cell cell = app->console().at(2, 3);
    check(cell.glyph == 'A' && cell.fg.r == 200 && cell.bg.b == 220,
          "put and at round trip");

    check(app->console().print(0, 0, nack::white, nack::black, "hello") == 5,
          "print returns the cell count");
    check(app->console().at(4, 0).glyph == 'o', "print wrote the last cell");

    /*
     * console_view::get() cannot return null the way the old C API's
     * console pointer could - it always names a real console - so the
     * "drawing through a null console" boundary that used to be tested here
     * no longer has anything to probe: a console_view is only ever built
     * from a console that already exists.
     */

    /* UTF-8 must survive as codepoints, not bytes. */
    app->console().print(0, 1, nack::white, nack::black, "\xE2\x94\x80");   /* U+2500 */
    check(app->console().at(0, 1).glyph == 0x2500,
          "UTF-8 decoded to a codepoint");

    app->console().print(0, 2, nack::white, nack::black, "{}-{}", 42, "x");
    check(app->console().at(0, 2).glyph == '4' &&
          app->console().at(3, 2).glyph == 'x',
          "fmt formats into cells");

    /*
     * The old C API's nack_printf went through vsnprintf, which can fail
     * part way through a conversion and leave the buffer holding whatever
     * it wrote before the failure - "%ls" with an unconvertible wide string
     * does it on glibc - and printing that anyway drew a truncated line and
     * reported it as a success. print()'s argument is a std::string that
     * fmt::format() either finishes completely or never returns at all
     * (an exception unwinds before print() is called), so there is no
     * partial-buffer state left for anything to draw. The C-varargs printf
     * path this used to test doesn't exist any more - nothing formats
     * through vsnprintf now - so this failure mode is gone architecturally,
     * not merely handled.
     */

    /*
     * A console size no allocator can serve has to come back as an error.
     * std::vector refuses it by throwing; the internal entry point still
     * catches that and returns NULL, which is what every internal caller
     * (and the public nack::console constructor, through detail::fail)
     * relies on rather than an exception unwinding somewhere unexpected.
     */
    {
        struct nack_console *huge = nack__console_new(INT_MAX, INT_MAX);
        check(huge == NULL, "an impossible console size is refused");
        check(!nack::last_error().empty(),
              "and says why rather than terminating");
        nack__console_free(huge);
    }
    check(!nack::console::try_create(INT_MAX, INT_MAX).has_value(),
          "and the public constructor reports it the same way");

    /* Clipping rather than corrupting memory. */
    app->console().put(-5, -5, 'X', nack::white, nack::black);
    app->console().put(9999, 9999, 'X', nack::white, nack::black);
    check(true, "out of bounds writes are ignored");

    app->console().fill(10, 10, 5, 3, '#', nack::green, nack::black);
    check(app->console().at(12, 11).glyph == '#',
          "fill covers its rectangle");

    app->console().draw_box(20, 5, 10, 6, nack::grey, nack::black, "hi");
    check(app->console().at(20, 5).glyph == 0x250C &&
          app->console().at(29, 10).glyph == 0x2518,
          "draw_box uses box drawing corners");

    check(app->console().measure_wrapped(10, "the quick brown fox jumps") >= 3,
          "print_wrapped measures without drawing");

    /* Offscreen consoles and blitting */
    {
        auto offscreen = nack::console::try_create(8, 4);
        check(offscreen.has_value(), "offscreen console created");
        offscreen->clear(nack::white, nack::red);
        offscreen->print(0, 0, nack::yellow, nack::red, "panel");
        offscreen->blit_to(app->console(), 1, 15);
        check(app->console().at(1, 15).glyph == 'p', "blit copied glyphs");
        check(app->console().at(1, 15).bg.r == 200, "blit copied backgrounds");
    }

    /* Resizing keeps what still fits. */
    {
        nack::console offscreen(4, 4);
        offscreen.put(1, 1, 'Z', nack::white, nack::black);
        offscreen.resize(8, 8);
        check(offscreen.at(1, 1).glyph == 'Z', "resize preserves contents");
    }

    /*
     * A tileset seeds its codepoint map before it creates its atlas texture,
     * so a texture that cannot be made leaves a tileset owning a map and
     * nothing else. Releasing only the struct loses that map. Nothing is
     * asserted here beyond the failure being reported - the leak itself is
     * what LSan sees, in the sanitiser job.
     */
    {
        uint8_t sheet[16 * 16 * 4];
        std::memset(sheet, 0, sizeof sheet);
        nack__debug_fail_next_textures(1);
        struct nack_tileset *doomed = nack__tileset_from_rgba(
            sheet, 16, 16, 8, 8, NACK_LAYOUT_CP437);
        check(doomed == NULL, "a tileset with no texture is not handed out");
        nack__tileset_free(doomed);
    }

    /*
     * A big sheet has more tiles than a 16-bit index can hold: 2048x2048 of
     * 8x8 tiles is 65536 of them. Mapping a codepoint to a high tile has to
     * survive the round trip rather than wrap into a negative slot and
     * silently map to nothing.
     */
    {
        std::vector<uint8_t> big(2048u * 2048u * 4, 0);
        struct nack_tileset *wide = nack__tileset_from_rgba(
            big.data(), 2048, 2048, 8, 8, NACK_LAYOUT_ROW_MAJOR);
        check(wide != NULL, "a 65536 tile sheet loads");
        if (wide) {
            check(nack__tileset_map(wide, 'Q', 40000),
                  "a codepoint maps to a tile past 32767");
            check(nack__tileset_index_for(wide, 'Q') == 40000,
                  "and reads back as the tile it was given");
        }
        nack__tileset_free(wide);
    }

    /* Codepoints the font has no tile for must not draw garbage. */
    app->console().put(0, 19, 0x4E2D, nack::white, nack::black);   /* CJK */
    check(true, "unmapped codepoint accepted");

    /*
     * A real frame through OpenGL, verified by reading pixels back rather
     * than by present() merely returning.
     */
    app->console().clear(nack::white, nack::black);
    app->console().fill(0, 0, 40, 20, ' ', nack::white, nack::rgb(20, 40, 80));
    app->present();
    {
        uint8_t pixel[4] = { 0, 0, 0, 0 };
        check(nack__debug_read_pixel(5, 5, pixel), "framebuffer readable");
        checkpx(pixel[0] > 10 && pixel[0] < 32 && pixel[2] > 64 && pixel[2] < 96,
                "cell background rendered to the framebuffer", pixel,
                "r 10..32, b 64..96");
    }

    /* A glyph must actually put foreground pixels on screen. */
    app->console().clear(nack::white, nack::black);
    app->console().fill(0, 0, 40, 20, 0x2588, nack::rgb(0, 255, 0), nack::black);
    app->present();
    {
        uint8_t pixel[4] = { 0, 0, 0, 0 };
        nack__debug_read_pixel(10, 10, pixel);
        checkpx(pixel[1] > 200 && pixel[0] < 60,
                "full block glyph rendered in its foreground colour", pixel,
                "g > 200, r < 60");
    }

    /* And a blank cell must leave the background showing. */
    app->console().clear(nack::white, nack::rgb(255, 0, 0));
    app->present();
    {
        uint8_t pixel[4] = { 0, 0, 0, 0 };
        nack__debug_read_pixel(20, 10, pixel);
        checkpx(pixel[0] > 200 && pixel[1] < 60,
                "blank cell shows its background", pixel, "r > 200, g < 60");
    }

    /*
     * Tilesets used to live in a fixed array of 16. The seventeenth was
     * created and handed back like any other, but silently left out of the
     * registry present() walks to batch by atlas - so every cell drawn with
     * it came out empty, with no error anywhere to say why.
     */
    {
        uint8_t plain[16 * 16 * 4];
        uint8_t red[16 * 16 * 4];
        struct nack_tileset *many[20];
        uint8_t pixel[4] = { 0, 0, 0, 0 };
        int made = 0;

        std::memset(plain, 0, sizeof plain);
        for (int i = 0; i < 16 * 16; ++i) {
            red[i * 4 + 0] = 255;   /* a real hue, so it loads as artwork */
            red[i * 4 + 1] = 0;
            red[i * 4 + 2] = 0;
            red[i * 4 + 3] = 255;
        }

        for (int i = 0; i < 20; ++i) {
            many[i] = nack__tileset_from_rgba(i == 19 ? red : plain, 16, 16,
                                              8, 8, NACK_LAYOUT_ROW_MAJOR);
            if (many[i])
                made++;
        }
        check(made == 20, "twenty tilesets all load");

        app->console().clear();
        if (many[19])
            nack__console_put_tile(app->console().get(), 20, 10, many[19], 0,
                                   nack_color{ 255, 255, 255, 255 },
                                   nack_color{ 0, 0, 0, 255 });
        app->present();
        nack__debug_read_pixel(20, 10, pixel);
        checkpx(pixel[0] > 200 && pixel[1] < 60,
                "and the twentieth one actually draws", pixel,
                "r > 200, g < 60");

        for (int i = 0; i < 20; ++i)
            nack__tileset_free(many[i]);
    }

    check(true, "frames presented");
    app->present();
    check(app->delta_time() >= 0.0, "delta time is sane");
    check(app->time() > 0.0, "elapsed time advances");

    /* Event loop: a timeout must actually wait. */
    while (app->poll())
        ;
    double t0 = app->time();
    app->wait_for(0.2);
    double elapsed = app->time() - t0;
    check(elapsed >= 0.15, "wait_event_timeout blocks for the requested time");

    app->wake();
    {
        bool woke = false;
        for (int i = 0; i < 32; ++i) {
            auto ev = app->wait_for(0.3);
            if (!ev)
                break;
            if (std::holds_alternative<nack::wakeup_event>(*ev)) {
                woke = true;
                break;
            }
        }
        check(woke, "wake() delivers the wakeup_event alternative");
    }

    check(!app->should_close(), "not asked to close");
    app->set_should_close(true);
    check(app->should_close(), "should_close is settable");

    /*
     * The clipboard, in both directions and at a size that has to be sent in
     * chunks. Nothing exercised any of this before: the INCR code is the
     * most intricate in the X11 backend and had never been run.
     *
     * Not every backend can do it. A headless Wayland compositor has no
     * wl_seat, and a selection needs one, so there the library refuses and
     * says why - which is an answer, not a failure, and this reports it as
     * a skip rather than pretending the machine is broken.
     */
    if (!app->set_clipboard("hello clipboard")) {
        std::printf("%-52s skipped (%.*s)\n", "clipboard",
                    (int)nack::last_error().size(), nack::last_error().data());
    } else {
        std::string back = app->clipboard();

        check(true, "clipboard accepts text");
        check(back == "hello clipboard",
              "clipboard round trips a short string");

#if defined(NACK_X11_SELECTION_PEER)
        /*
         * The peer is an X client, so it is only any use when X11 is the
         * backend actually running - the XCB backend being compiled in says
         * nothing about which one was chosen.
         */
        if (nack__win_get_backend() != NACK_BACKEND_X11) {
            std::printf("%-52s skipped (not X11)\n", "INCR transfers both ways");
        } else {
            std::vector<char> payload(NACK_PEER_PAYLOAD_BYTES);
            char line[256];

            nack__peer_payload(payload.data(), payload.size());

            /*
             * Reading: the peer owns the selection and serves it in 4K
             * chunks, so what comes back has crossed the wire and been
             * reassembled rather than being handed over from our own copy.
             * The payload is unlike anything we put on the clipboard
             * ourselves, so a read served locally would not match it.
             */
            FILE *peer = nack__test_peer_start("own");
            check(peer != NULL, "the selection peer starts");
            if (peer) {
                check(nack__test_peer_line(*app, peer, line, sizeof line, 5.0) &&
                      std::strncmp(line, "ready", 5) == 0,
                      "another client owns the clipboard");

                back = app->clipboard();
                check(back.size() == payload.size() - 1,
                      "an INCR selection arrives at its full length");
                check(std::memcmp(back.data(), payload.data(), payload.size() - 1) == 0,
                      "and every chunk landed in the right place");
                pclose(peer);
            }

            /*
             * Writing: the same size back out, read by the peer. This is
             * the serving half of INCR, which announces the transfer and
             * then has to answer each property delete with the next chunk.
             */
            check(app->set_clipboard(payload.data()), "clipboard accepts 300K");
            peer = nack__test_peer_start("read");
            check(peer != NULL, "the selection peer starts again");
            if (peer) {
                unsigned long length = 0;
                unsigned sum = 0;
                int incremental = 0;
                bool parsed = nack__test_peer_line(*app, peer, line,
                                                   sizeof line, 10.0) &&
                              sscanf(line, "len=%lu sum=%x incr=%d", &length,
                                     &sum, &incremental) == 3;

                check(parsed, "another client reads the clipboard back");
                check(parsed && incremental,
                      "and is served incrementally, not in one property");
                check(parsed && length == payload.size() - 1 &&
                      sum == nack__peer_checksum(payload.data(),
                                                 payload.size() - 1),
                      "with the bytes intact");
                pclose(peer);
            }
        }
#else
        std::printf("%-52s skipped (no X11)\n", "INCR transfers both ways");
#endif
    }

    app->set_title("renamed");
    check(true, "set_title survived");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
