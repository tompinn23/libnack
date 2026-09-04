/* libnack - Wayland backend: registry, surfaces, xdg-shell and the event loop. */
#include "nack_wayland.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

struct nack_wayland_state nack__wl;


/* ------------------------------------------------------------------ */
/* Outputs                                                            */
/* ------------------------------------------------------------------ */

static struct nack_wl_output *nack__wl_find_output(struct wl_output *output)
{
    for (size_t i = 0; i < nack__wl.output_count; ++i) {
        if (nack__wl.outputs[i].output == output)
            return &nack__wl.outputs[i];
    }
    return NULL;
}

static void output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
                            int32_t physical_width, int32_t physical_height,
                            int32_t subpixel, const char *make, const char *model,
                            int32_t transform)
{
    (void)data; (void)x; (void)y; (void)subpixel; (void)make; (void)model;
    (void)transform;
    struct nack_wl_output *entry = nack__wl_find_output(output);
    if (entry) {
        entry->physical_width = physical_width;
        entry->physical_height = physical_height;
    }
}

static void output_mode(void *data, struct wl_output *output, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh)
{
    (void)data; (void)refresh;
    if (!(flags & WL_OUTPUT_MODE_CURRENT))
        return;
    struct nack_wl_output *entry = nack__wl_find_output(output);
    if (entry) {
        entry->width = width;
        entry->height = height;
    }
}

static void output_done(void *data, struct wl_output *output)
{
    (void)data; (void)output;
}

static void output_scale(void *data, struct wl_output *output, int32_t factor)
{
    (void)data;
    struct nack_wl_output *entry = nack__wl_find_output(output);
    if (!entry)
        return;
    entry->scale = factor;
    /* A scale change on an output we are on changes our buffer scale. */
    for (size_t i = 0; i < nack__g.windows.size(); ++i)
        nack__wl_window_update_scale(nack__g.windows[i]);
}

static void output_name(void *data, struct wl_output *output, const char *name)
{
    (void)data; (void)output; (void)name;
}

static void output_description(void *data, struct wl_output *output,
                               const char *description)
{
    (void)data; (void)output; (void)description;
}

static const struct wl_output_listener nack__wl_output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

/* ------------------------------------------------------------------ */
/* Buffer scale                                                       */
/* ------------------------------------------------------------------ */

void nack__wl_resize_egl(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww->egl_window)
        return;

    int fb_width = (int)(w->width * ww->buffer_scale);
    int fb_height = (int)(w->height * ww->buffer_scale);
    if (fb_width < 1) fb_width = 1;
    if (fb_height < 1) fb_height = 1;

    wl_egl_window_resize(ww->egl_window, fb_width, fb_height, 0, 0);
    w->fb_width = fb_width;
    w->fb_height = fb_height;
}

/*
 * Picks the buffer scale from the outputs the surface currently touches.
 * Without the fractional-scale protocol the only correct choice is the
 * largest integer scale among them, which is what wl_surface.set_buffer_scale
 * expects.
 */
void nack__wl_window_update_scale(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww)
        return;
#if defined(NACK_HAS_FRACTIONAL_SCALE)
    if (ww->fractional_scale)
        return;   /* the compositor drives scale through the protocol instead */
#endif

    int32_t scale = 1;
    for (size_t i = 0; i < ww->output_count; ++i) {
        struct nack_wl_output *entry = nack__wl_find_output(ww->outputs[i]);
        if (entry && entry->scale > scale)
            scale = entry->scale;
    }

    if (scale == ww->buffer_scale)
        return;

    ww->buffer_scale = scale;
    wl_surface_set_buffer_scale(ww->surface, scale);
    nack__wl_decor_update_scale(w, scale);
    nack__wl_resize_egl(w);
    nack__emit_scale(w, (float)scale);
    nack__emit_resize(w, w->width, w->height, w->fb_width, w->fb_height);
    nack__wl_load_cursor_theme(scale);
    nack__wl_update_cursor(w);
}

/* ------------------------------------------------------------------ */
/* Surface listeners                                                  */
/* ------------------------------------------------------------------ */

static void surface_enter(void *data, struct wl_surface *surface,
                          struct wl_output *output)
{
    (void)surface;
    struct nack_window *w = (struct nack_window *)data;
    struct nack_wl_window *ww = nack__wl_win(w);
    if (ww->output_count < NACK_WL_MAX_OUTPUTS)
        ww->outputs[ww->output_count++] = output;
    nack__wl_window_update_scale(w);
}

static void surface_leave(void *data, struct wl_surface *surface,
                          struct wl_output *output)
{
    (void)surface;
    struct nack_window *w = (struct nack_window *)data;
    struct nack_wl_window *ww = nack__wl_win(w);
    for (size_t i = 0; i < ww->output_count; ++i) {
        if (ww->outputs[i] == output) {
            ww->outputs[i] = ww->outputs[--ww->output_count];
            break;
        }
    }
    nack__wl_window_update_scale(w);
}

static const struct wl_surface_listener nack__wl_surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
};

#if defined(NACK_HAS_FRACTIONAL_SCALE)
static void fractional_scale_preferred(void *data,
                                       struct wp_fractional_scale_v1 *fractional,
                                       uint32_t scale_8_24)
{
    (void)fractional;
    struct nack_window *w = (struct nack_window *)data;
    struct nack_wl_window *ww = nack__wl_win(w);

    /* The protocol reports scale in 1/120ths. */
    float scale = (float)scale_8_24 / 120.0f;
    if (scale <= 0.0f)
        return;

    int fb_width = (int)((float)w->width * scale + 0.5f);
    int fb_height = (int)((float)w->height * scale + 0.5f);
    if (fb_width < 1) fb_width = 1;
    if (fb_height < 1) fb_height = 1;

    if (ww->egl_window)
        wl_egl_window_resize(ww->egl_window, fb_width, fb_height, 0, 0);

    /* With a fractional buffer the surface has to be scaled back down to its
     * logical size explicitly, which is what wp_viewport is for. */
    if (ww->viewport)
        wp_viewport_set_destination(ww->viewport, w->width, w->height);

    /* Decorations are drawn at an integer scale, so round the fractional one
     * up rather than rendering them at 1x on a scaled display. */
    nack__wl_decor_update_scale(w, (int)(scale + 0.999f));

    nack__emit_scale(w, scale);
    nack__emit_resize(w, w->width, w->height, fb_width, fb_height);
    nack__wl_load_cursor_theme((int)(scale + 0.5f));
    nack__wl_update_cursor(w);
}

static const struct wp_fractional_scale_v1_listener nack__wl_fractional_listener = {
    .preferred_scale = fractional_scale_preferred,
};
#endif

/* ------------------------------------------------------------------ */
/* xdg-shell                                                          */
/* ------------------------------------------------------------------ */

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener nack__wl_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
                                  uint32_t serial)
{
    struct nack_window *w = (struct nack_window *)data;
    struct nack_wl_window *ww = nack__wl_win(w);

    /* Acknowledging here commits the whole configure sequence atomically. */
    xdg_surface_ack_configure(xdg_surface, serial);

    /*
     * A zero width or height means "you choose", in which case the size the
     * caller asked for stands as-is. Only a size the compositor actually
     * proposed gets clamped and snapped: snapping the client's own request
     * would silently shrink the window it just asked for.
     */
    int width = w->width;
    int height = w->height;

    if (ww->pending_width > 0) {
        width = ww->pending_width;
        if (w->min_width > 0 && width < w->min_width) width = w->min_width;
        if (w->max_width > 0 && width > w->max_width) width = w->max_width;
        /* Snap to the cell grid so a terminal never sees a partial column. */
        if (w->inc_width > 1) {
            int base = w->min_width > 0 ? w->min_width : 0;
            width = base + ((width - base) / w->inc_width) * w->inc_width;
            if (width < base + w->inc_width) width = base + w->inc_width;
        }
    }
    if (ww->pending_height > 0) {
        height = ww->pending_height;
        if (w->min_height > 0 && height < w->min_height) height = w->min_height;
        if (w->max_height > 0 && height > w->max_height) height = w->max_height;
        if (w->inc_height > 1) {
            int base = w->min_height > 0 ? w->min_height : 0;
            height = base + ((height - base) / w->inc_height) * w->inc_height;
            if (height < base + w->inc_height) height = base + w->inc_height;
        }
    }

    bool size_changed = (width != w->width || height != w->height);
    w->width = width;
    w->height = height;
    nack__wl_resize_egl(w);
    nack__wl_decor_resize(w);

    if (ww->viewport)
        wp_viewport_set_destination(ww->viewport, w->width, w->height);

    if (size_changed || !ww->configured) {
        struct nack_win_event *ev = nack__event_begin(NACK_WIN_EVENT_WINDOW_RESIZE, w);
        ev->data.size.width = w->width;
        ev->data.size.height = w->height;
        ev->data.size.fb_width = w->fb_width;
        ev->data.size.fb_height = w->fb_height;
        nack__push_event(ev);
    }

    if (ww->pending_maximized != w->maximized) {
        w->maximized = ww->pending_maximized;
        nack__emit_simple(w, w->maximized ? NACK_WIN_EVENT_WINDOW_MAXIMIZE
                                          : NACK_WIN_EVENT_WINDOW_RESTORE);
    }
    w->fullscreen = ww->pending_fullscreen;

    ww->configured = true;
    wl_surface_commit(ww->surface);
}

static const struct xdg_surface_listener nack__wl_xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states)
{
    (void)toplevel;
    struct nack_window *w = (struct nack_window *)data;
    struct nack_wl_window *ww = nack__wl_win(w);

    ww->pending_width = width;
    ww->pending_height = height;
    ww->pending_maximized = false;
    ww->pending_fullscreen = false;
    ww->pending_activated = false;

    /*
     * Spelled out rather than using wl_array_for_each: that macro assigns
     * the array's void* data straight to the loop pointer, which C++ will
     * not do implicitly.
     */
    const uint32_t *state;
    const uint32_t *state_end =
        (const uint32_t *)((const char *)states->data + states->size);
    for (state = (const uint32_t *)states->data; state < state_end; ++state) {
        switch (*state) {
        case XDG_TOPLEVEL_STATE_MAXIMIZED:  ww->pending_maximized = true; break;
        case XDG_TOPLEVEL_STATE_FULLSCREEN: ww->pending_fullscreen = true; break;
        case XDG_TOPLEVEL_STATE_ACTIVATED:  ww->pending_activated = true; break;
        default: break;
        }
    }
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    (void)toplevel;
    struct nack_window *w = (struct nack_window *)data;
    w->should_close = true;
    nack__emit_simple(w, NACK_WIN_EVENT_WINDOW_CLOSE);
}

static void xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel,
                                          int32_t width, int32_t height)
{
    (void)data; (void)toplevel; (void)width; (void)height;
}

static void xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel,
                                         struct wl_array *capabilities)
{
    (void)data; (void)toplevel; (void)capabilities;
}

static const struct xdg_toplevel_listener nack__wl_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
    .configure_bounds = xdg_toplevel_configure_bounds,
    .wm_capabilities = xdg_toplevel_wm_capabilities,
};

/* ------------------------------------------------------------------ */
/* Registry                                                           */
/* ------------------------------------------------------------------ */

static uint32_t nack__min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version)
{
    (void)data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        nack__wl.compositor = (struct wl_compositor *)wl_registry_bind(registry, name, &wl_compositor_interface,
                                               nack__min_u32(version, 4));
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        nack__wl.subcompositor = (struct wl_subcompositor *)wl_registry_bind(registry, name,
                                                  &wl_subcompositor_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        nack__wl.shm = (struct wl_shm *)wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        nack__wl.wm_base = (struct xdg_wm_base *)wl_registry_bind(registry, name, &xdg_wm_base_interface,
                                            nack__min_u32(version, 4));
        xdg_wm_base_add_listener(nack__wl.wm_base, &nack__wl_wm_base_listener, NULL);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        nack__wl_seat_bind(name, nack__min_u32(version, 7));
        /* The seat may arrive after the data device managers, or the other
         * way round; binding is idempotent, so try from both sides. */
        nack__wl_data_device_bind();
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        if (nack__wl.output_count < NACK_WL_MAX_OUTPUTS) {
            struct nack_wl_output *entry = &nack__wl.outputs[nack__wl.output_count++];
            entry->output = (struct wl_output *)wl_registry_bind(registry, name, &wl_output_interface,
                                             nack__min_u32(version, 3));
            entry->name = name;
            entry->scale = 1;
            wl_output_add_listener(entry->output, &nack__wl_output_listener, NULL);
        }
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        nack__wl.decoration_manager =
            (struct zxdg_decoration_manager_v1 *)wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        nack__wl.viewporter = (struct wp_viewporter *)wl_registry_bind(registry, name,
                                               &wp_viewporter_interface, 1);
    } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        nack__wl.data_device_manager =
            (struct wl_data_device_manager *)wl_registry_bind(registry, name, &wl_data_device_manager_interface,
                             nack__min_u32(version, 3));
        nack__wl_data_device_bind();
    } else if (strcmp(interface,
                      zwp_primary_selection_device_manager_v1_interface.name) == 0) {
        nack__wl.primary_manager =
            (struct zwp_primary_selection_device_manager_v1 *)wl_registry_bind(registry, name,
                             &zwp_primary_selection_device_manager_v1_interface, 1);
        nack__wl_data_device_bind();
    } else if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        nack__wl.pointer_constraints =
            (struct zwp_pointer_constraints_v1 *)wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1);
    } else if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        nack__wl.relative_pointer_manager =
            (struct zwp_relative_pointer_manager_v1 *)wl_registry_bind(registry, name,
                             &zwp_relative_pointer_manager_v1_interface, 1);
#if defined(NACK_HAS_FRACTIONAL_SCALE)
    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        nack__wl.fractional_scale_manager =
            (struct wp_fractional_scale_manager_v1 *)wl_registry_bind(
                registry, name, &wp_fractional_scale_manager_v1_interface, 1);
#endif
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name)
{
    (void)data; (void)registry;
    for (size_t i = 0; i < nack__wl.output_count; ++i) {
        if (nack__wl.outputs[i].name != name)
            continue;
        wl_output_destroy(nack__wl.outputs[i].output);
        nack__wl.outputs[i] = nack__wl.outputs[--nack__wl.output_count];
        memset(&nack__wl.outputs[nack__wl.output_count], 0,
               sizeof nack__wl.outputs[0]);
        break;
    }
}

static const struct wl_registry_listener nack__wl_registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* ------------------------------------------------------------------ */
/* Window management                                                  */
/* ------------------------------------------------------------------ */

static void decoration_configure(void *data,
                                 struct zxdg_toplevel_decoration_v1 *decoration,
                                 uint32_t mode)
{
    (void)decoration;
    struct nack_window *w = (struct nack_window *)data;
    if (!w)
        return;

    /* The compositor may honour the server-side request or insist the client
     * draws its own; only the second case needs our fallback. */
    if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE)
        nack__wl_decor_enable(w);
    else
        nack__wl_decor_destroy(w);
}

static const struct zxdg_toplevel_decoration_v1_listener nack__wl_decoration_listener = {
    .configure = decoration_configure,
};

static bool nack__wl_window_create(struct nack_window *w,
                                   const struct nack_window_desc *desc)
{
    if (!nack__wl.compositor || !nack__wl.wm_base)
        return nack__fail(NACK_ERROR_PLATFORM,
                          "compositor is missing wl_compositor or xdg_wm_base");

    struct nack_wl_window *ww = new nack_wl_window{};
    ww->egl_surface = EGL_NO_SURFACE;
    ww->buffer_scale = 1;
    ww->decor_scale = 1;
    ww->decor_hover = NACK_WL_BUTTON_NONE;
    w->native = ww;

    ww->surface = wl_compositor_create_surface(nack__wl.compositor);
    if (!ww->surface) {
        delete ww;
        w->native = NULL;
        return nack__fail(NACK_ERROR_PLATFORM, "wl_compositor.create_surface failed");
    }
    wl_surface_add_listener(ww->surface, &nack__wl_surface_listener, w);

    ww->xdg_surface = xdg_wm_base_get_xdg_surface(nack__wl.wm_base, ww->surface);
    xdg_surface_add_listener(ww->xdg_surface, &nack__wl_xdg_surface_listener, w);

    ww->xdg_toplevel = xdg_surface_get_toplevel(ww->xdg_surface);
    xdg_toplevel_add_listener(ww->xdg_toplevel, &nack__wl_toplevel_listener, w);
    xdg_toplevel_set_title(ww->xdg_toplevel, w->title.c_str());
    xdg_toplevel_set_app_id(ww->xdg_toplevel, nack__g.app_id.c_str());

    if (w->min_width > 0 || w->min_height > 0)
        xdg_toplevel_set_min_size(ww->xdg_toplevel, w->min_width, w->min_height);
    if (w->max_width > 0 || w->max_height > 0)
        xdg_toplevel_set_max_size(ww->xdg_toplevel, w->max_width, w->max_height);
    if (!w->resizable) {
        xdg_toplevel_set_min_size(ww->xdg_toplevel, w->width, w->height);
        xdg_toplevel_set_max_size(ww->xdg_toplevel, w->width, w->height);
    }

    /* NACK_WAYLAND_FORCE_CSD exists so the fallback frame can be exercised on
     * a compositor that does support xdg-decoration. */
    bool force_csd = getenv("NACK_WAYLAND_FORCE_CSD") != NULL;

    /* Ask for server-side decorations; compositors that lack the protocol
     * leave the client undecorated, which callers can detect via the flag. */
    if (nack__wl.decoration_manager && w->decorated && !force_csd) {
        ww->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
            nack__wl.decoration_manager, ww->xdg_toplevel);
        zxdg_toplevel_decoration_v1_add_listener(ww->decoration,
                                                 &nack__wl_decoration_listener, w);
        zxdg_toplevel_decoration_v1_set_mode(
            ww->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    if ((nack__wl.decoration_manager == NULL || force_csd) && w->decorated) {
        /* No xdg-decoration protocol: nothing will draw a frame but us. */
        nack__wl_decor_enable(w);
    }

    if (nack__wl.viewporter)
        ww->viewport = wp_viewporter_get_viewport(nack__wl.viewporter, ww->surface);

#if defined(NACK_HAS_FRACTIONAL_SCALE)
    if (nack__wl.fractional_scale_manager && w->high_dpi) {
        ww->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
            nack__wl.fractional_scale_manager, ww->surface);
        wp_fractional_scale_v1_add_listener(ww->fractional_scale,
                                            &nack__wl_fractional_listener, w);
    }
#endif

    if (nack__egl.initialized) {
        EGLConfig config;
        EGLint visual = 0;
        if (nack__egl_choose_config(&w->framebuffer, NACK__GL_PROFILE_CORE, 3,
                                    &config, &visual)) {
            ww->config = config;
            ww->has_config = true;
        }
    }

    ww->egl_window = wl_egl_window_create(ww->surface, w->width, w->height);
    if (!ww->egl_window)
        nack_log("nack: wl_egl_window_create failed; no OpenGL on this window");

    w->fb_width = w->width;
    w->fb_height = w->height;
    w->scale = 1.0f;

    /* An initial commit without a buffer asks the compositor for the first
     * configure, which is where the real size comes from. */
    wl_surface_commit(ww->surface);
    wl_display_roundtrip(nack__wl.display);

    return true;
}

static void nack__wl_window_destroy(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww)
        return;

    if (nack__wl.pointer_focus == w)  nack__wl.pointer_focus = NULL;
    if (nack__wl.keyboard_focus == w) nack__wl.keyboard_focus = NULL;
    if (nack__wl.repeat_window == w) {
        nack__wl.repeat_window = NULL;
        nack__wl.repeat_key = 0;
    }

    nack__wl_decor_destroy(w);
    nack__wl_shm_buffer_release(&ww->placeholder);

    if (ww->locked_pointer)   zwp_locked_pointer_v1_destroy(ww->locked_pointer);
    if (ww->relative_pointer) zwp_relative_pointer_v1_destroy(ww->relative_pointer);
    if (ww->egl_surface != EGL_NO_SURFACE)
        eglDestroySurface(nack__egl.display, ww->egl_surface);
    if (ww->egl_window)  wl_egl_window_destroy(ww->egl_window);
#if defined(NACK_HAS_FRACTIONAL_SCALE)
    if (ww->fractional_scale) wp_fractional_scale_v1_destroy(ww->fractional_scale);
#endif
    if (ww->viewport)    wp_viewport_destroy(ww->viewport);
    if (ww->decoration)  zxdg_toplevel_decoration_v1_destroy(ww->decoration);
    if (ww->xdg_toplevel) xdg_toplevel_destroy(ww->xdg_toplevel);
    if (ww->xdg_surface)  xdg_surface_destroy(ww->xdg_surface);
    if (ww->surface)      wl_surface_destroy(ww->surface);

    delete ww;
    w->native = NULL;
    wl_display_flush(nack__wl.display);
}

static void nack__wl_window_show(struct nack_window *w, bool show)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (show) {
        /* A surface with no buffer is never mapped, so present a blank frame
         * rather than leaving the window invisible until the client draws. */
        nack__wl_present_placeholder(w);
        wl_surface_commit(ww->surface);
    } else {
        /* Attaching a null buffer unmaps the surface; xdg-shell has no hide. */
        wl_surface_attach(ww->surface, NULL, 0, 0);
        wl_surface_commit(ww->surface);
        ww->configured = false;
    }
    wl_display_flush(nack__wl.display);
}

static void nack__wl_window_set_title(struct nack_window *w, const char *title)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (ww->xdg_toplevel)
        xdg_toplevel_set_title(ww->xdg_toplevel, title);
    nack__wl_decor_redraw(w);
    wl_display_flush(nack__wl.display);
}

static void nack__wl_window_set_size(struct nack_window *w, int width, int height)
{
    /*
     * Wayland clients own their own size: there is no "resize request", the
     * client simply renders at the size it wants and the compositor accepts
     * it. Update the surface and report the change ourselves.
     */
    struct nack_wl_window *ww = nack__wl_win(w);
    w->width = width;
    w->height = height;
    nack__wl_resize_egl(w);
    nack__wl_decor_resize(w);
    if (ww->viewport)
        wp_viewport_set_destination(ww->viewport, width, height);

    struct nack_win_event *ev = nack__event_begin(NACK_WIN_EVENT_WINDOW_RESIZE, w);
    ev->data.size.width = w->width;
    ev->data.size.height = w->height;
    ev->data.size.fb_width = w->fb_width;
    ev->data.size.fb_height = w->fb_height;
    nack__push_event(ev);

    wl_surface_commit(ww->surface);
    wl_display_flush(nack__wl.display);
}

static void nack__wl_apply_size_hints(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww->xdg_toplevel)
        return;
    if (!w->resizable) {
        xdg_toplevel_set_min_size(ww->xdg_toplevel, w->width, w->height);
        xdg_toplevel_set_max_size(ww->xdg_toplevel, w->width, w->height);
    } else {
        xdg_toplevel_set_min_size(ww->xdg_toplevel, w->min_width, w->min_height);
        xdg_toplevel_set_max_size(ww->xdg_toplevel, w->max_width, w->max_height);
    }
    wl_surface_commit(ww->surface);
    wl_display_flush(nack__wl.display);
}

static void nack__wl_window_set_fullscreen(struct nack_window *w, bool fullscreen)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww->xdg_toplevel)
        return;
    if (fullscreen)
        xdg_toplevel_set_fullscreen(ww->xdg_toplevel, NULL);
    else
        xdg_toplevel_unset_fullscreen(ww->xdg_toplevel);
    wl_display_flush(nack__wl.display);
}

static void nack__wl_window_minimize(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (ww->xdg_toplevel)
        xdg_toplevel_set_minimized(ww->xdg_toplevel);
    wl_display_flush(nack__wl.display);
}

static void nack__wl_window_maximize(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (ww->xdg_toplevel)
        xdg_toplevel_set_maximized(ww->xdg_toplevel);
    wl_display_flush(nack__wl.display);
}

static void nack__wl_window_restore(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww->xdg_toplevel)
        return;
    if (w->fullscreen)
        xdg_toplevel_unset_fullscreen(ww->xdg_toplevel);
    if (w->maximized)
        xdg_toplevel_unset_maximized(ww->xdg_toplevel);
    wl_display_flush(nack__wl.display);
}

static void nack__wl_window_request_redraw(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    wl_surface_damage_buffer(ww->surface, 0, 0, INT32_MAX, INT32_MAX);
    nack__emit_simple(w, NACK_WIN_EVENT_WINDOW_EXPOSE);
    wl_display_flush(nack__wl.display);
}

static void nack__wl_window_get_native(const struct nack_window *w,
                                       struct nack_native_window *out)
{
    struct nack_wl_window *ww = (struct nack_wl_window *)w->native;
    out->display = nack__wl.display;
    out->surface = ww ? ww->surface : NULL;
    out->handle = 0;
}

/* ------------------------------------------------------------------ */
/* OpenGL                                                             */
/* ------------------------------------------------------------------ */

static bool nack__wl_ensure_surface(struct nack_window *w, EGLConfig config)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    if (ww->egl_surface != EGL_NO_SURFACE)
        return true;
    if (!ww->egl_window)
        return nack__fail(NACK_ERROR_CONTEXT_CREATION, "no wl_egl_window for surface");

    ww->egl_surface = nack__egl_create_window_surface(config, ww->egl_window, true,
                                                      w->framebuffer.srgb);
    return ww->egl_surface != EGL_NO_SURFACE;
}

static struct nack_gl_context *nack__wl_gl_create(nack_backend_vt *vt,
                                                 struct nack_window *w,
                                                  const struct nack__gl_desc *desc)
{
    if (!nack__egl.initialized) {
        nack__fail(NACK_ERROR_UNSUPPORTED, "EGL is not available");
        return NULL;
    }
    struct nack_wl_window *ww = nack__wl_win(w);
    if (!ww->has_config) {
        nack__fail(NACK_ERROR_NO_PIXEL_FORMAT,
                   "window was created without a usable EGL config");
        return NULL;
    }
    if (!nack__wl_ensure_surface(w, ww->config))
        return NULL;
    return nack__egl_create_context(w, desc, ww->config, vt);
}

static bool nack__wl_gl_make_current(struct nack_window *w, struct nack_gl_context *ctx)
{
    if (!ctx)
        return nack__egl_make_current(EGL_NO_SURFACE, NULL);
    if (!w)
        return nack__fail(NACK_ERROR_INVALID_ARGUMENT,
                          "nack__gl_make_current needs a window for this context");
    struct nack_wl_window *ww = nack__wl_win(w);
    if (ww->egl_surface == EGL_NO_SURFACE &&
        !nack__wl_ensure_surface(w, ((struct nack_egl_context *)ctx->native)->config))
        return false;
    return nack__egl_make_current(ww->egl_surface, ctx);
}

static void nack__wl_gl_swap_buffers(struct nack_window *w)
{
    struct nack_wl_window *ww = nack__wl_win(w);
    nack__egl_swap_buffers(ww->egl_surface);
    if (!ww->presented)
        nack__wl_drop_placeholder(w);
}

/* ------------------------------------------------------------------ */
/* Event loop                                                         */
/* ------------------------------------------------------------------ */

static void nack__wl_wakeup(void)
{
    const char byte = 1;
    ssize_t rc;
    do {
        rc = write(nack__wl.wakeup_pipe[1], &byte, 1);
    } while (rc < 0 && errno == EINTR);
    (void)rc;
}

/*
 * Wayland's dispatch is prepare/read/dispatch rather than a plain fd read, so
 * the wait has to be built around wl_display_prepare_read to avoid losing
 * events queued between the poll and the read.
 */
static void nack__wl_pump_events(double timeout)
{
    struct wl_display *display = nack__wl.display;

    wl_display_dispatch_pending(display);
    nack__wl_pump_key_repeat();

    if (!nack__queue_empty() || timeout == 0.0) {
        wl_display_flush(display);
        return;
    }

    /* Key repeat needs the wait to end when the next repeat is due. */
    double repeat_timeout = nack__wl_next_repeat_timeout();
    if (repeat_timeout >= 0.0 && (timeout < 0.0 || repeat_timeout < timeout))
        timeout = repeat_timeout;

    while (wl_display_prepare_read(display) != 0) {
        if (wl_display_dispatch_pending(display) < 0)
            return;
        if (!nack__queue_empty()) {
            wl_display_flush(display);
            return;
        }
    }

    if (wl_display_flush(display) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(display);
        return;
    }

    struct pollfd fds[2];
    fds[0].fd = wl_display_get_fd(display);
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = nack__wl.wakeup_pipe[0];
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    int ms = -1;
    if (timeout > 0.0) {
        double clamped = timeout * 1000.0;
        if (clamped > (double)INT_MAX)
            clamped = (double)INT_MAX;
        ms = (int)clamped;
    }

    int rc = poll(fds, 2, ms);
    if (rc <= 0) {
        wl_display_cancel_read(display);
        if (rc == 0)
            nack__wl_pump_key_repeat();
        return;
    }

    if (fds[0].revents & POLLIN) {
        if (wl_display_read_events(display) < 0)
            return;
        wl_display_dispatch_pending(display);
    } else {
        wl_display_cancel_read(display);
    }

    if (fds[1].revents & POLLIN) {
        char scratch[64];
        while (read(nack__wl.wakeup_pipe[0], scratch, sizeof scratch) > 0)
            ;
        nack__emit_simple(NULL, NACK_WIN_EVENT_WAKEUP);
    }

    nack__wl_pump_key_repeat();
    wl_display_flush(display);
}

/* ------------------------------------------------------------------ */
/* Init / shutdown                                                    */
/* ------------------------------------------------------------------ */

static bool nack__wl_init(const struct nack_win_init_desc *desc)
{
    (void)desc;
    nack__wl = nack_wayland_state{};
    nack__wl.wakeup_pipe[0] = nack__wl.wakeup_pipe[1] = -1;
    nack__wl.repeat_rate = 25;
    nack__wl.repeat_delay = 400;
    nack__wl.cursor_theme_scale = 1;

    nack__wl.display = wl_display_connect(NULL);
    if (!nack__wl.display)
        return nack__fail(NACK_ERROR_NO_BACKEND,
                          "cannot connect to Wayland display '%s'",
                          getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY")
                                                    : "(unset)");

    if (pipe(nack__wl.wakeup_pipe) != 0) {
        wl_display_disconnect(nack__wl.display);
        nack__wl.display = NULL;
        return nack__fail(NACK_ERROR_PLATFORM, "pipe() failed: %s", strerror(errno));
    }
    for (int i = 0; i < 2; ++i) {
        int flags = fcntl(nack__wl.wakeup_pipe[i], F_GETFL, 0);
        fcntl(nack__wl.wakeup_pipe[i], F_SETFL, flags | O_NONBLOCK);
        flags = fcntl(nack__wl.wakeup_pipe[i], F_GETFD, 0);
        fcntl(nack__wl.wakeup_pipe[i], F_SETFD, flags | FD_CLOEXEC);
    }

    nack__wl.registry = wl_display_get_registry(nack__wl.display);
    wl_registry_add_listener(nack__wl.registry, &nack__wl_registry_listener, NULL);

    /* Two round trips: the first delivers the globals, the second the events
     * those globals emit on bind (seat capabilities, output modes). */
    wl_display_roundtrip(nack__wl.display);
    wl_display_roundtrip(nack__wl.display);

    if (!nack__wl.compositor || !nack__wl.wm_base) {
        const char *missing = !nack__wl.compositor ? "wl_compositor" : "xdg_wm_base";
        wl_display_disconnect(nack__wl.display);
        nack__wl.display = NULL;
        return nack__fail(NACK_ERROR_NO_BACKEND,
                          "compositor does not advertise %s", missing);
    }

    nack__wl_data_device_bind();

    if (nack__wl.shm) {
        const char *size_env = getenv("XCURSOR_SIZE");
        (void)size_env;
        nack__wl_load_cursor_theme(1);
        if (nack__wl.compositor)
            nack__wl.cursor_surface = wl_compositor_create_surface(nack__wl.compositor);
    }

    if (!nack__egl_init(EGL_PLATFORM_WAYLAND_KHR, nack__wl.display, NULL))
        nack_log("nack: EGL unavailable; windows will have no OpenGL support");

    return true;
}

static void nack__wl_shutdown(void)
{
    if (!nack__wl.display)
        return;

    nack__wl_clipboard_shutdown();
    nack__wl_input_shutdown();
    nack__egl_terminate();

    for (int i = 0; i < NACK_CURSOR_SHAPE_COUNT; ++i)
        nack__wl.cursors_loaded[i] = false;
    if (nack__wl.cursor_surface) wl_surface_destroy(nack__wl.cursor_surface);
    if (nack__wl.cursor_theme)   wl_cursor_theme_destroy(nack__wl.cursor_theme);

    for (size_t i = 0; i < nack__wl.output_count; ++i)
        wl_output_destroy(nack__wl.outputs[i].output);

    if (nack__wl.pointer_constraints)
        zwp_pointer_constraints_v1_destroy(nack__wl.pointer_constraints);
    if (nack__wl.relative_pointer_manager)
        zwp_relative_pointer_manager_v1_destroy(nack__wl.relative_pointer_manager);
#if defined(NACK_HAS_FRACTIONAL_SCALE)
    if (nack__wl.fractional_scale_manager)
        wp_fractional_scale_manager_v1_destroy(nack__wl.fractional_scale_manager);
#endif
    if (nack__wl.viewporter)         wp_viewporter_destroy(nack__wl.viewporter);
    if (nack__wl.decoration_manager)
        zxdg_decoration_manager_v1_destroy(nack__wl.decoration_manager);
    if (nack__wl.wm_base)            xdg_wm_base_destroy(nack__wl.wm_base);
    if (nack__wl.shm)                wl_shm_destroy(nack__wl.shm);
    if (nack__wl.subcompositor)      wl_subcompositor_destroy(nack__wl.subcompositor);
    if (nack__wl.compositor)         wl_compositor_destroy(nack__wl.compositor);
    if (nack__wl.registry)           wl_registry_destroy(nack__wl.registry);

    if (nack__wl.wakeup_pipe[0] >= 0) close(nack__wl.wakeup_pipe[0]);
    if (nack__wl.wakeup_pipe[1] >= 0) close(nack__wl.wakeup_pipe[1]);

    wl_display_flush(nack__wl.display);
    wl_display_disconnect(nack__wl.display);
    nack__wl = nack_wayland_state{};
}

/* ------------------------------------------------------------------ */

namespace {

class wayland_backend final : public nack_backend_vt {
public:
    const char *name() const override { return "wayland"; }
    enum nack_backend id() const override { return NACK_BACKEND_WAYLAND; }

    bool init(const struct nack_win_init_desc *desc) override
    {
        return nack__wl_init(desc);
    }
    void shutdown() override
    {
        nack__wl_shutdown();
    }
    bool window_create(struct nack_window *w, const struct nack_window_desc *desc) override
    {
        return nack__wl_window_create(w, desc);
    }
    void window_destroy(struct nack_window *w) override
    {
        nack__wl_window_destroy(w);
    }
    void window_show(struct nack_window *w, bool show) override
    {
        nack__wl_window_show(w, show);
    }
    void window_set_title(struct nack_window *w, const char *title) override
    {
        nack__wl_window_set_title(w, title);
    }
    void window_set_size(struct nack_window *w, int width, int height) override
    {
        nack__wl_window_set_size(w, width, height);
    }
    void window_apply_size_hints(struct nack_window *w) override
    {
        nack__wl_apply_size_hints(w);
    }
    void window_set_fullscreen(struct nack_window *w, bool fullscreen) override
    {
        nack__wl_window_set_fullscreen(w, fullscreen);
    }
    void window_minimize(struct nack_window *w) override
    {
        nack__wl_window_minimize(w);
    }
    void window_maximize(struct nack_window *w) override
    {
        nack__wl_window_maximize(w);
    }
    void window_restore(struct nack_window *w) override
    {
        nack__wl_window_restore(w);
    }
    void window_request_redraw(struct nack_window *w) override
    {
        nack__wl_window_request_redraw(w);
    }
    void window_set_cursor_shape(struct nack_window *w, enum nack_cursor_shape shape) override
    {
        nack__wl_set_cursor_shape(w, shape);
    }
    void window_set_cursor_mode(struct nack_window *w, enum nack_cursor_mode mode) override
    {
        nack__wl_set_cursor_mode(w, mode);
    }
    void window_get_native(const struct nack_window *w, struct nack_native_window *out) override
    {
        nack__wl_window_get_native(w, out);
    }
    void pump_events(double timeout) override
    {
        nack__wl_pump_events(timeout);
    }
    void wakeup() override
    {
        nack__wl_wakeup();
    }
    struct nack_gl_context *gl_create(struct nack_window *w, const struct nack__gl_desc *desc) override
    {
        return nack__wl_gl_create(this, w, desc);
    }
    void gl_destroy(struct nack_gl_context *ctx) override
    {
        nack__egl_destroy_context(ctx);
    }
    bool gl_make_current(struct nack_window *w, struct nack_gl_context *ctx) override
    {
        return nack__wl_gl_make_current(w, ctx);
    }
    void gl_swap_buffers(struct nack_window *w) override
    {
        nack__wl_gl_swap_buffers(w);
    }
    void gl_set_swap_interval(int interval) override
    {
        nack__egl_set_swap_interval(interval);
    }
    void *gl_get_proc_address(const char *name) override
    {
        return nack__egl_get_proc_address(name);
    }
    bool clipboard_set(const char *utf8) override
    {
        return nack__wl_clipboard_set(utf8);
    }
    const char *clipboard_get() override
    {
        return nack__wl_clipboard_get();
    }
    bool primary_set(const char *utf8) override
    {
        return nack__wl_primary_set(utf8);
    }
    const char *primary_get() override
    {
        return nack__wl_primary_get();
    }
};

wayland_backend nack__wl_backend_instance;

}   /* namespace */


nack_backend_vt *nack__backend_wayland(void)
{
    return &nack__wl_backend_instance;
}
