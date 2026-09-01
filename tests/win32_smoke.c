/*
 * Exercises the Win32 backend end to end through the hand-rolled API
 * declarations: window creation, the message pump, the blocking wait, the
 * cross-thread wakeup and the clipboard.
 *
 * Runs under Wine as well as on Windows, which is how the thread-message
 * wakeup bug was found.
 */
#include "nack/nack.h"
#include <stdio.h>
#include <string.h>

static void lg(const char *m, void *u) { (void)u; fprintf(stderr, "[nack] %s\n", m); }
static int failures = 0;
static void check(int ok, const char *what)
{
    fprintf(stderr, "%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

int main(void)
{
    struct nack_init_desc init;
    struct nack_window_desc d;
    struct nack_window *w;
    struct nack_event e;
    struct nack_native_window native;
    int width = 0, height = 0;
    double t0, t1;

    memset(&init, 0, sizeof init);
    init.app_id = "nack.winsmoke";
    init.log_fn = lg;
    check(nack_init(&init), "nack_init");
    if (!nack_is_initialized()) return 1;

    check(nack_get_backend() == NACK_BACKEND_WIN32, "backend is win32");

    /* Monotonic clock goes through QueryPerformanceCounter. */
    t0 = nack_time_seconds();
    check(t0 > 0.0, "nack_time_seconds is positive");

    nack_window_desc_defaults(&d);
    d.title = "nack win32 smoke";
    d.width = 640; d.height = 400;
    d.width_increment = 8; d.height_increment = 16;
    w = nack_window_create(&d);
    check(w != NULL, "window created");
    if (!w) { nack_shutdown(); return 1; }

    nack_window_get_size(w, &width, &height);
    check(width == 640 && height == 400, "window reports requested size");

    nack_window_get_native(w, &native);
    check(native.handle != 0, "HWND exposed");

    nack_window_set_title(w, "renamed");
    nack_window_set_cursor_shape(w, NACK_CURSOR_IBEAM);
    nack_window_set_size_increments(w, 9, 17);
    check(1, "title/cursor/size-hint calls survived");

    /* Pumping messages exercises PeekMessageW/DispatchMessageW and the
     * window procedure, including WM_SIZE and WM_PAINT. */
    while (nack_poll_event(&e))
        ;
    check(1, "message pump ran");

    t0 = nack_time_seconds();
    nack_wait_event_timeout(&e, 0.2);
    t1 = nack_time_seconds();
    check(t1 - t0 >= 0.15, "MsgWaitForMultipleObjects waited");

    nack_wakeup();
    {
        int woke = 0, i;
        for (i = 0; i < 32 && nack_wait_event_timeout(&e, 0.3); ++i)
            if (e.type == NACK_EVENT_WAKEUP) { woke = 1; break; }
        check(woke, "PostThreadMessageW wakeup delivered");
    }

    check(nack_clipboard_set_text("hand rolled"), "clipboard set");
    {
        const char *got = nack_clipboard_get_text();
        check(got && strcmp(got, "hand rolled") == 0, "clipboard round trip");
    }

    nack_window_destroy(w);
    nack_shutdown();
    fprintf(stderr, "\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
