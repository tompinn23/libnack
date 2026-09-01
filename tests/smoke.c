/*
 * Backend-agnostic smoke test: window, GL context, a real clear read back out
 * of the framebuffer, event loop timeouts, cross-thread wakeup and clipboard
 * round trips including a payload large enough to need X11's INCR protocol.
 */
#include "nack/nack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*pfn_clear_color)(float, float, float, float);
typedef void (*pfn_clear)(unsigned int);
typedef const unsigned char *(*pfn_get_string)(unsigned int);
typedef void (*pfn_viewport)(int, int, int, int);
typedef void (*pfn_read_pixels)(int, int, int, int, unsigned, unsigned, void *);

static void logger(const char *m, void *u) { (void)u; fprintf(stderr, "[nack] %s\n", m); }

static int failures = 0;
static void check(int cond, const char *what)
{
    printf("%-46s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) failures++;
}

int main(void)
{
    struct nack_init_desc init = { .app_id = "nack.smoke", .log_fn = logger };
    if (!nack_init(&init)) {
        const char *m = NULL; nack_get_error(&m);
        fprintf(stderr, "nack_init failed: %s\n", m);
        return 1;
    }
    printf("backend: %s\n", nack_backend_name(nack_get_backend()));

    struct nack_window_desc desc;
    nack_window_desc_defaults(&desc);
    desc.title = "smoke";
    desc.width = 640; desc.height = 400;
    desc.min_width = 100; desc.min_height = 60;
    desc.width_increment = 8; desc.height_increment = 16;

    struct nack_window *w = nack_window_create(&desc);
    check(w != NULL, "window created");
    if (!w) { nack_shutdown(); return 1; }

    int width = 0, height = 0;
    nack_window_get_size(w, &width, &height);
    check(width == 640 && height == 400, "window reports requested size");

    struct nack_gl_desc gl;
    nack_gl_desc_defaults(&gl);
    struct nack_gl_context *ctx = nack_gl_context_create(w, &gl);
    if (!ctx) {
        const char *m = NULL; nack_get_error(&m);
        printf("GL 3.3 core unavailable (%s); retrying without a version request\n", m);
        gl.major = 0; gl.minor = 0; gl.profile = NACK_GL_PROFILE_COMPAT;
        ctx = nack_gl_context_create(w, &gl);
    }
    check(ctx != NULL, "GL context created");
    if (!ctx) { nack_window_destroy(w); nack_shutdown(); return 1; }

    check(nack_gl_make_current(w, ctx), "context made current");

    pfn_get_string gl_get_string = (pfn_get_string)nack_gl_get_proc_address("glGetString");
    check(gl_get_string != NULL, "glGetString resolved");
    if (gl_get_string) {
        printf("  GL_VERSION:  %s\n", (const char *)gl_get_string(0x1F02));
        printf("  GL_RENDERER: %s\n", (const char *)gl_get_string(0x1F01));
    }

    /* The cache must hand back an identical pointer without re-resolving. */
    void *a = nack_gl_get_proc_address("glClear");
    void *b = nack_gl_get_proc_address("glClear");
    check(a != NULL && a == b, "proc address cached and stable");
    /* A non-NULL pointer proves nothing: libglvnd returns a dispatch stub for
     * any gl-prefixed name. Only a name it does not recognise at all is NULL. */
    check(nack_gl_get_proc_address("totallyBogusName") == NULL,
          "unrecognised proc resolves to NULL");

    pfn_clear_color gl_clear_color =
        (pfn_clear_color)nack_gl_get_proc_address("glClearColor");
    pfn_clear gl_clear = (pfn_clear)nack_gl_get_proc_address("glClear");
    pfn_viewport gl_viewport = (pfn_viewport)nack_gl_get_proc_address("glViewport");
    pfn_read_pixels gl_read_pixels =
        (pfn_read_pixels)nack_gl_get_proc_address("glReadPixels");

    if (gl_clear_color && gl_clear && gl_viewport) {
        int fbw = 0, fbh = 0;
        nack_window_get_framebuffer_size(w, &fbw, &fbh);
        gl_viewport(0, 0, fbw, fbh);
        gl_clear_color(0.25f, 0.5f, 0.75f, 1.0f);
        gl_clear(0x00004000 /* GL_COLOR_BUFFER_BIT */);

        unsigned char pixel[4] = { 0, 0, 0, 0 };
        if (gl_read_pixels) {
            gl_read_pixels(fbw / 2, fbh / 2, 1, 1, 0x1908 /* GL_RGBA */,
                           0x1401 /* GL_UNSIGNED_BYTE */, pixel);
            printf("  centre pixel: %u,%u,%u\n", pixel[0], pixel[1], pixel[2]);
            check(pixel[0] > 50 && pixel[0] < 80 && pixel[2] > 175 && pixel[2] < 210,
                  "cleared colour read back from framebuffer");
        }
        nack_gl_swap_buffers(w);
        check(1, "buffers swapped without crashing");
    }

    /* Wakeup must break a blocking wait from the same thread's perspective. */
    nack_wakeup();
    struct nack_event event;
    bool woke = false;
    for (int i = 0; i < 32 && nack_wait_event_timeout(&event, 0.5); ++i) {
        if (event.type == NACK_EVENT_WAKEUP) { woke = true; break; }
    }
    check(woke, "nack_wakeup delivers NACK_EVENT_WAKEUP");

    /* Drain anything still queued (configures, decoration events) so the
     * timeout below measures a genuinely idle wait. */
    while (nack_poll_event(&event))
        ;

    /*
     * A tiling compositor keeps configuring the window after it is mapped, so
     * allow a few rounds: what matters is that a wait with nothing pending
     * blocks for the requested time rather than spinning or returning early.
     */
    bool timeout_ok = false;
    for (int attempt = 0; attempt < 6 && !timeout_ok; ++attempt) {
        while (nack_poll_event(&event))
            ;
        double before = nack_time_seconds();
        bool timed_out = !nack_wait_event_timeout(&event, 0.2);
        double elapsed = nack_time_seconds() - before;
        if (timed_out)
            timeout_ok = (elapsed >= 0.15 && elapsed < 1.0);
        else
            printf("  (woken early by event type %d after %.3fs; retrying)\n",
                   (int)event.type, elapsed);
    }
    check(timeout_ok, "wait_event_timeout blocks for the requested time");

    check(nack_clipboard_set_text("libnack clipboard \xe2\x9c\x93"),
          "clipboard ownership taken");
    const char *pasted = nack_clipboard_get_text();
    check(pasted && strcmp(pasted, "libnack clipboard \xe2\x9c\x93") == 0,
          "clipboard round trip");

    /* A large payload has to survive the INCR path. */
    char *big = malloc(200000);
    memset(big, 'x', 199999);
    big[199999] = '\0';
    nack_clipboard_set_text(big);
    const char *back = nack_clipboard_get_text();
    check(back && strlen(back) == 199999, "large clipboard payload round trip");
    free(big);

after_clipboard:
    nack_window_set_title(w, "smoke (renamed)");
    nack_window_set_size_increments(w, 9, 17);
    nack_window_set_cursor_shape(w, NACK_CURSOR_IBEAM);
    check(1, "title/size-hint/cursor calls survived");

    struct nack_native_window native;
    nack_window_get_native(w, &native);
    /* X11 identifies a window by XID; Wayland by wl_surface pointer. */
    bool native_ok = native.display != NULL &&
                     (native.backend == NACK_BACKEND_WAYLAND ? native.surface != NULL
                                                             : native.handle != 0);
    check(native_ok, "native handles exposed");

    nack_gl_context_destroy(ctx);
    nack_window_destroy(w);
    nack_shutdown();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
