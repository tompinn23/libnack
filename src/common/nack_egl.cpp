/* Shared EGL context management for the XCB and Wayland backends. */
#include "nack_egl.h"

#include <dlfcn.h>
#include <stdio.h>
#include <memory>
#include <vector>

nack_egl_state egl;

const char *egl_error_string(EGLint error)
{
    switch (error) {
    case EGL_SUCCESS:             return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:     return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:          return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:           return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:       return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONTEXT:         return "EGL_BAD_CONTEXT";
    case EGL_BAD_CONFIG:          return "EGL_BAD_CONFIG";
    case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:         return "EGL_BAD_DISPLAY";
    case EGL_BAD_SURFACE:         return "EGL_BAD_SURFACE";
    case EGL_BAD_MATCH:           return "EGL_BAD_MATCH";
    case EGL_BAD_PARAMETER:       return "EGL_BAD_PARAMETER";
    case EGL_BAD_NATIVE_PIXMAP:   return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:   return "EGL_BAD_NATIVE_WINDOW";
    case EGL_CONTEXT_LOST:        return "EGL_CONTEXT_LOST";
    default:                      return "unknown EGL error";
    }
}

static bool egl_has_extension(const char *list, const char *name)
{
    if (!list || !name)
        return false;
    size_t len = strlen(name);
    const char *p = list;
    while ((p = strstr(p, name)) != nullptr) {
        char after = p[len];
        if ((p == list || p[-1] == ' ') && (after == ' ' || after == '\0'))
            return true;
        p += len;
    }
    return false;
}

bool egl_init(EGLenum platform, void *native_display, const EGLAttrib *attribs)
{
    if (egl.initialized)
        return true;

    memset(&egl, 0, sizeof egl);

    const char *client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    egl.has_platform_base =
        egl_has_extension(client_exts, "EGL_EXT_platform_base");

    if (egl.has_platform_base) {
        egl.get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
            eglGetProcAddress("eglGetPlatformDisplayEXT");
        egl.create_platform_window_surface =
            (PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC)
                eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
    }

    if (egl.get_platform_display) {
        /* eglGetPlatformDisplayEXT takes EGLint attributes, not EGLAttrib. */
        EGLint int_attribs[9];
        EGLint *ptr = nullptr;
        if (attribs) {
            size_t i = 0;
            for (; attribs[i] != EGL_NONE && i < 8; ++i)
                int_attribs[i] = (EGLint)attribs[i];
            int_attribs[i] = EGL_NONE;
            ptr = int_attribs;
        }
        egl.display = egl.get_platform_display(platform, native_display, ptr);
    }

    if (egl.display == EGL_NO_DISPLAY) {
        /* Legacy path: only valid when the native display is an Xlib Display*
         * or a wl_display*, which is exactly how callers use the fallback. */
        egl.display = eglGetDisplay((EGLNativeDisplayType)native_display);
    }

    if (egl.display == EGL_NO_DISPLAY)
        return state.fail(NACK_ERROR_NO_BACKEND, "eglGetDisplay failed: %s",
                          egl_error_string(eglGetError()));

    if (!eglInitialize(egl.display, &egl.major, &egl.minor)) {
        egl.display = EGL_NO_DISPLAY;
        return state.fail(NACK_ERROR_NO_BACKEND, "eglInitialize failed: %s",
                          egl_error_string(eglGetError()));
    }

    const char *exts = eglQueryString(egl.display, EGL_EXTENSIONS);
    egl.has_khr_create_context =
        egl_has_extension(exts, "EGL_KHR_create_context");
    egl.has_ext_create_context_robustness =
        egl_has_extension(exts, "EGL_EXT_create_context_robustness");
    egl.has_khr_gl_colorspace =
        egl_has_extension(exts, "EGL_KHR_gl_colorspace");
    egl.has_ext_swap_control_tear =
        egl_has_extension(exts, "EGL_EXT_swap_control_tear");

    /* EGL 1.5 guarantees eglGetProcAddress resolves core GL entry points;
     * older implementations do not, so keep a dlopen handle for fallback. */
    egl.gl_library = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!egl.gl_library)
        egl.gl_library = dlopen("libOpenGL.so.0", RTLD_LAZY | RTLD_LOCAL);
    egl.gles_library = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_LOCAL);

    egl.initialized = true;
    nack_log("nack: EGL %d.%d initialized", egl.major, egl.minor);
    return true;
}

void egl_terminate(void)
{
    if (!egl.initialized)
        return;
    eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(egl.display);
    if (egl.gl_library)
        dlclose(egl.gl_library);
    if (egl.gles_library)
        dlclose(egl.gles_library);
    memset(&egl, 0, sizeof egl);
}

bool egl_choose_config(const nack_framebuffer_desc *fb, gl_profile profile,
                             int gl_major, EGLConfig *out_config,
                             EGLint *out_visual_id)
{
    EGLint renderable = EGL_OPENGL_BIT;
    if (profile == NACK__GL_PROFILE_ES)
        renderable = (gl_major >= 3) ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT;

    EGLint attribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, renderable,
        EGL_RED_SIZE,        fb->red_bits,
        EGL_GREEN_SIZE,      fb->green_bits,
        EGL_BLUE_SIZE,       fb->blue_bits,
        EGL_ALPHA_SIZE,      fb->alpha_bits,
        EGL_DEPTH_SIZE,      fb->depth_bits,
        EGL_STENCIL_SIZE,    fb->stencil_bits,
        EGL_SAMPLES,         fb->samples,
        EGL_NONE
    };

    EGLint count = 0;
    if (!eglChooseConfig(egl.display, attribs, nullptr, 0, &count) || count == 0)
        return state.fail(NACK_ERROR_NO_PIXEL_FORMAT,
                          "no EGL config matches the requested framebuffer");

    std::vector<EGLConfig> configs(count);
    eglChooseConfig(egl.display, attribs, configs.data(), count, &count);

    /* eglChooseConfig sorts by "at least as good as requested", which can hand
     * back a config with more bits than asked for. Prefer an exact match on the
     * colour channels so a request for 8/8/8/0 does not silently gain alpha. */
    EGLConfig chosen = configs[0];
    for (EGLint i = 0; i < count; ++i) {
        EGLint r = 0, g = 0, b = 0, a = 0, samples = 0;
        eglGetConfigAttrib(egl.display, configs[i], EGL_RED_SIZE, &r);
        eglGetConfigAttrib(egl.display, configs[i], EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(egl.display, configs[i], EGL_BLUE_SIZE, &b);
        eglGetConfigAttrib(egl.display, configs[i], EGL_ALPHA_SIZE, &a);
        eglGetConfigAttrib(egl.display, configs[i], EGL_SAMPLES, &samples);
        if (r == fb->red_bits && g == fb->green_bits && b == fb->blue_bits &&
            a == fb->alpha_bits && samples == fb->samples) {
            chosen = configs[i];
            break;
        }
    }

    *out_config = chosen;
    if (out_visual_id) {
        *out_visual_id = 0;
        eglGetConfigAttrib(egl.display, chosen, EGL_NATIVE_VISUAL_ID, out_visual_id);
    }
    return true;
}

nack_gl_context *egl_create_context(nack_window *w, const gl_desc *desc,
                                          EGLConfig config, nack_backend_vt *vt)
{
    EGLenum api = (desc->profile == NACK__GL_PROFILE_ES) ? EGL_OPENGL_ES_API
                                                        : EGL_OPENGL_API;
    if (!eglBindAPI(api)) {
        state.fail(NACK_ERROR_CONTEXT_CREATION, "eglBindAPI failed: %s",
                   egl_error_string(eglGetError()));
        return nullptr;
    }

    EGLint attribs[16];
    int n = 0;

    if (egl.has_khr_create_context || egl.major > 1 ||
        (egl.major == 1 && egl.minor >= 5)) {
        if (desc->major > 0) {
            attribs[n++] = EGL_CONTEXT_MAJOR_VERSION;
            attribs[n++] = desc->major;
            attribs[n++] = EGL_CONTEXT_MINOR_VERSION;
            attribs[n++] = desc->minor;
        }
        if (desc->profile != NACK__GL_PROFILE_ES) {
            attribs[n++] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
            attribs[n++] = (desc->profile == NACK__GL_PROFILE_CORE)
                               ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
                               : EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT;
        }
        if (desc->debug) {
            attribs[n++] = EGL_CONTEXT_OPENGL_DEBUG;
            attribs[n++] = EGL_TRUE;
        }
        if (desc->forward_compatible) {
            attribs[n++] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE;
            attribs[n++] = EGL_TRUE;
        }
        if (desc->robust && egl.has_ext_create_context_robustness) {
            attribs[n++] = EGL_CONTEXT_OPENGL_ROBUST_ACCESS;
            attribs[n++] = EGL_TRUE;
        }
    } else if (desc->profile == NACK__GL_PROFILE_ES && desc->major > 0) {
        attribs[n++] = EGL_CONTEXT_CLIENT_VERSION;
        attribs[n++] = desc->major;
    }
    attribs[n] = EGL_NONE;

    EGLContext share = EGL_NO_CONTEXT;
    if (desc->share && desc->share->native)
        share = ((nack_egl_context *)desc->share->native)->context;

    EGLContext egl_ctx = eglCreateContext(egl.display, config, share, attribs);
    if (egl_ctx == EGL_NO_CONTEXT) {
        state.fail(NACK_ERROR_CONTEXT_CREATION,
                   "eglCreateContext failed for GL %d.%d %s: %s",
                   desc->major, desc->minor,
                   desc->profile == NACK__GL_PROFILE_CORE ? "core"
                       : desc->profile == NACK__GL_PROFILE_ES ? "es" : "compat",
                   egl_error_string(eglGetError()));
        return nullptr;
    }

    auto ctx = std::make_unique<nack_gl_context>();
    auto native = std::make_unique<nack_egl_context>();

    native->context = egl_ctx;
    native->config = config;
    eglGetConfigAttrib(egl.display, config, EGL_NATIVE_VISUAL_ID,
                       &native->visual_id);
    native->is_es = (desc->profile == NACK__GL_PROFILE_ES);
    ctx->native = native.release();
    ctx->vt = vt;
    ctx->owner = w;
    return ctx.release();
}

void egl_destroy_context(nack_gl_context *ctx)
{
    if (!ctx)
        return;
    nack_egl_context *native = (nack_egl_context *)ctx->native;
    if (native) {
        if (native->context != EGL_NO_CONTEXT)
            eglDestroyContext(egl.display, native->context);
        delete native;
    }
    delete ctx;
}

EGLSurface egl_create_window_surface(EGLConfig config, void *native_window,
                                           bool use_pointer, bool srgb)
{
    EGLint attribs[5];
    int n = 0;
    if (srgb && egl.has_khr_gl_colorspace) {
        attribs[n++] = EGL_GL_COLORSPACE_KHR;
        attribs[n++] = EGL_GL_COLORSPACE_SRGB_KHR;
    }
    attribs[n] = EGL_NONE;

    EGLSurface surface = EGL_NO_SURFACE;
    if (use_pointer && egl.create_platform_window_surface) {
        surface = egl.create_platform_window_surface(egl.display, config,
                                                           native_window, attribs);
    } else {
        surface = eglCreateWindowSurface(egl.display, config,
                                         (EGLNativeWindowType)(uintptr_t)native_window,
                                         attribs);
    }

    if (surface == EGL_NO_SURFACE)
        state.fail(NACK_ERROR_CONTEXT_CREATION, "eglCreateWindowSurface failed: %s",
                   egl_error_string(eglGetError()));
    return surface;
}

bool egl_make_current(EGLSurface surface, nack_gl_context *ctx)
{
    if (!ctx) {
        eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        return true;
    }
    nack_egl_context *native = (nack_egl_context *)ctx->native;
    eglBindAPI(native->is_es ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
    if (!eglMakeCurrent(egl.display, surface, surface, native->context))
        return state.fail(NACK_ERROR_PLATFORM, "eglMakeCurrent failed: %s",
                          egl_error_string(eglGetError()));
    return true;
}

void egl_swap_buffers(EGLSurface surface)
{
    if (surface != EGL_NO_SURFACE)
        eglSwapBuffers(egl.display, surface);
}

void egl_set_swap_interval(int interval)
{
    if (!egl.initialized)
        return;
    if (interval < 0 && !egl.has_ext_swap_control_tear)
        interval = -interval;   /* no adaptive vsync; fall back to plain vsync */
    eglSwapInterval(egl.display, interval);
}

void *egl_get_proc_address(const char *name)
{
    void *proc = (void *)eglGetProcAddress(name);
    if (proc)
        return proc;
    /* Core entry points before EGL 1.5 may only be available from the
     * client library itself. */
    if (egl.gl_library) {
        proc = dlsym(egl.gl_library, name);
        if (proc)
            return proc;
    }
    if (egl.gles_library)
        proc = dlsym(egl.gles_library, name);
    return proc;
}
