/* Shared EGL context management for the XCB and Wayland backends. */
#ifndef NACK_EGL_H_INCLUDED
#define NACK_EGL_H_INCLUDED

#include "../nack_internal.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

struct nack_egl_state {
    EGLDisplay display;
    EGLint major, minor;
    bool initialized;

    bool has_khr_create_context;
    bool has_ext_create_context_robustness;
    bool has_khr_gl_colorspace;
    bool has_ext_swap_control_tear;
    bool has_platform_base;

    /* dlopen handle for libGL/libGLESv2, used as a fallback for entry points
     * eglGetProcAddress declines to return (legal for core symbols before
     * EGL 1.5). */
    void *gl_library;
    void *gles_library;

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
    PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create_platform_window_surface;
};

struct nack_egl_context {
    EGLContext context;
    EGLConfig config;
    EGLint visual_id;
    bool is_es;
};

extern struct nack_egl_state nack__egl;

/* platform: EGL_PLATFORM_XCB_EXT / EGL_PLATFORM_X11_KHR / EGL_PLATFORM_WAYLAND_KHR.
 * attribs may be NULL. Falls back to eglGetDisplay when the platform base
 * extension is unavailable. */
bool nack__egl_init(EGLenum platform, void *native_display, const EGLAttrib *attribs);
void nack__egl_terminate(void);

bool nack__egl_choose_config(const struct nack_framebuffer_desc *fb, enum nack__gl_profile profile,
                             int gl_major, EGLConfig *out_config,
                             EGLint *out_visual_id);

/* Creates the EGLContext. The window's surface is created separately so the
 * backend can pick the right native window representation. */
struct nack_gl_context *nack__egl_create_context(struct nack_window *w, const struct nack__gl_desc *desc,
                                          EGLConfig config, nack_backend_vt *vt);
void nack__egl_destroy_context(struct nack_gl_context *ctx);

EGLSurface nack__egl_create_window_surface(EGLConfig config, void *native_window,
                                           bool use_pointer, bool srgb);

bool  nack__egl_make_current(EGLSurface surface, struct nack_gl_context *ctx);
void  nack__egl_swap_buffers(EGLSurface surface);
void  nack__egl_set_swap_interval(int interval);
void *nack__egl_get_proc_address(const char *name);

const char *nack__egl_error_string(EGLint error);

#endif /* NACK_EGL_H_INCLUDED */
