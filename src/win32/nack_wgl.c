/*
 * WGL context creation.
 *
 * Choosing a modern pixel format and creating a versioned context both need
 * WGL extensions, and those can only be resolved from a context that already
 * exists. The usual bootstrap therefore applies: create a throwaway window
 * with a legacy context, resolve the extensions from it, then discard it.
 */
#include "nack_win32.h"

#include <stdio.h>

/* WGL_ARB_pixel_format */
#define WGL_NUMBER_PIXEL_FORMATS_ARB      0x2000
#define WGL_DRAW_TO_WINDOW_ARB            0x2001
#define WGL_ACCELERATION_ARB              0x2003
#define WGL_SUPPORT_OPENGL_ARB            0x2010
#define WGL_DOUBLE_BUFFER_ARB             0x2011
#define WGL_PIXEL_TYPE_ARB                0x2013
#define WGL_COLOR_BITS_ARB                0x2014
#define WGL_RED_BITS_ARB                  0x2015
#define WGL_GREEN_BITS_ARB                0x2017
#define WGL_BLUE_BITS_ARB                 0x2019
#define WGL_ALPHA_BITS_ARB                0x201B
#define WGL_DEPTH_BITS_ARB                0x2022
#define WGL_STENCIL_BITS_ARB              0x2023
#define WGL_FULL_ACCELERATION_ARB         0x2027
#define WGL_TYPE_RGBA_ARB                 0x202B
#define WGL_SAMPLE_BUFFERS_ARB            0x2041
#define WGL_SAMPLES_ARB                   0x2042
#define WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB  0x20A9

/* WGL_ARB_create_context */
#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_FLAGS_ARB             0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_DEBUG_BIT_ARB         0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x0002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#define WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB 0x00000004
#define WGL_LOSE_CONTEXT_ON_RESET_ARB     0x8252
#define WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB 0x8256

typedef HGLRC (WINAPI *nack_pfn_wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
typedef BOOL  (WINAPI *nack_pfn_wglChoosePixelFormatARB)(HDC, const int *,
                                                         const FLOAT *, UINT,
                                                         int *, UINT *);
typedef const char *(WINAPI *nack_pfn_wglGetExtensionsStringARB)(HDC);
typedef const char *(WINAPI *nack_pfn_wglGetExtensionsStringEXT)(void);
typedef BOOL  (WINAPI *nack_pfn_wglSwapIntervalEXT)(int);

struct nack_wgl_state {
    HMODULE opengl32;
    HGLRC (WINAPI *CreateContext)(HDC);
    BOOL  (WINAPI *DeleteContext)(HGLRC);
    PROC  (WINAPI *GetProcAddress_)(LPCSTR);
    BOOL  (WINAPI *MakeCurrent)(HDC, HGLRC);
    BOOL  (WINAPI *ShareLists)(HGLRC, HGLRC);

    nack_pfn_wglCreateContextAttribsARB CreateContextAttribsARB;
    nack_pfn_wglChoosePixelFormatARB ChoosePixelFormatARB;
    nack_pfn_wglGetExtensionsStringARB GetExtensionsStringARB;
    nack_pfn_wglGetExtensionsStringEXT GetExtensionsStringEXT;
    nack_pfn_wglSwapIntervalEXT SwapIntervalEXT;

    bool has_create_context;
    bool has_create_context_profile;
    bool has_create_context_robustness;
    bool has_pixel_format;
    bool has_multisample;
    bool has_srgb;
    bool has_swap_control;
    bool has_swap_control_tear;
    bool initialized;
};

struct nack_wgl_context {
    HGLRC glrc;
};

static struct nack_wgl_state nack__wgl;

/* ------------------------------------------------------------------ */
/* Extension bootstrap                                                */
/* ------------------------------------------------------------------ */

static bool nack__wgl_has_extension(const char *list, const char *name)
{
    if (!list || !name)
        return false;
    size_t len = strlen(name);
    const char *p = list;
    while ((p = strstr(p, name)) != NULL) {
        char after = p[len];
        if ((p == list || p[-1] == ' ') && (after == ' ' || after == '\0'))
            return true;
        p += len;
    }
    return false;
}

/*
 * Creates a hidden window with a legacy pixel format, makes a legacy context
 * current on it, and resolves the WGL extension entry points. The window is
 * separate from any real one because a window's pixel format can only be set
 * once, and the legacy format is not the one we want to keep.
 */
static bool nack__wgl_bootstrap(void)
{
    HWND hwnd = CreateWindowExW(0, NACK_WIN32_CLASS_NAME, L"nack wgl bootstrap",
                                WS_OVERLAPPEDWINDOW, 0, 0, 1, 1,
                                NULL, NULL, nack__win32.instance, NULL);
    if (!hwnd)
        return nack__fail(NACK_ERROR_PLATFORM, "failed to create WGL helper window");

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        DestroyWindow(hwnd);
        return nack__fail(NACK_ERROR_PLATFORM, "GetDC failed for WGL helper window");
    }

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;

    int format = ChoosePixelFormat(hdc, &pfd);
    if (!format || !SetPixelFormat(hdc, format, &pfd)) {
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return nack__fail(NACK_ERROR_NO_PIXEL_FORMAT,
                          "no legacy pixel format available for WGL bootstrap");
    }

    HGLRC glrc = nack__wgl.CreateContext(hdc);
    if (!glrc) {
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return nack__fail(NACK_ERROR_CONTEXT_CREATION,
                          "failed to create WGL bootstrap context");
    }

    HDC previous_dc = wglGetCurrentDC();
    HGLRC previous_glrc = wglGetCurrentContext();

    if (!nack__wgl.MakeCurrent(hdc, glrc)) {
        nack__wgl.DeleteContext(glrc);
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return nack__fail(NACK_ERROR_CONTEXT_CREATION,
                          "failed to make WGL bootstrap context current");
    }

    nack__wgl.GetExtensionsStringARB = (nack_pfn_wglGetExtensionsStringARB)(void *)
        nack__wgl.GetProcAddress_("wglGetExtensionsStringARB");
    nack__wgl.GetExtensionsStringEXT = (nack_pfn_wglGetExtensionsStringEXT)(void *)
        nack__wgl.GetProcAddress_("wglGetExtensionsStringEXT");

    const char *extensions = NULL;
    if (nack__wgl.GetExtensionsStringARB)
        extensions = nack__wgl.GetExtensionsStringARB(hdc);
    else if (nack__wgl.GetExtensionsStringEXT)
        extensions = nack__wgl.GetExtensionsStringEXT();

    nack__wgl.has_create_context =
        nack__wgl_has_extension(extensions, "WGL_ARB_create_context");
    nack__wgl.has_create_context_profile =
        nack__wgl_has_extension(extensions, "WGL_ARB_create_context_profile");
    nack__wgl.has_create_context_robustness =
        nack__wgl_has_extension(extensions, "WGL_ARB_create_context_robustness");
    nack__wgl.has_pixel_format =
        nack__wgl_has_extension(extensions, "WGL_ARB_pixel_format");
    nack__wgl.has_multisample =
        nack__wgl_has_extension(extensions, "WGL_ARB_multisample");
    nack__wgl.has_srgb =
        nack__wgl_has_extension(extensions, "WGL_ARB_framebuffer_sRGB") ||
        nack__wgl_has_extension(extensions, "WGL_EXT_framebuffer_sRGB");
    nack__wgl.has_swap_control =
        nack__wgl_has_extension(extensions, "WGL_EXT_swap_control");
    nack__wgl.has_swap_control_tear =
        nack__wgl_has_extension(extensions, "WGL_EXT_swap_control_tear");

    if (nack__wgl.has_create_context)
        nack__wgl.CreateContextAttribsARB =
            (nack_pfn_wglCreateContextAttribsARB)(void *)
                nack__wgl.GetProcAddress_("wglCreateContextAttribsARB");
    if (nack__wgl.has_pixel_format)
        nack__wgl.ChoosePixelFormatARB = (nack_pfn_wglChoosePixelFormatARB)(void *)
            nack__wgl.GetProcAddress_("wglChoosePixelFormatARB");
    if (nack__wgl.has_swap_control)
        nack__wgl.SwapIntervalEXT = (nack_pfn_wglSwapIntervalEXT)(void *)
            nack__wgl.GetProcAddress_("wglSwapIntervalEXT");

    nack__wgl.MakeCurrent(previous_dc, previous_glrc);
    nack__wgl.DeleteContext(glrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return true;
}

bool nack__wgl_init(void)
{
    if (nack__wgl.initialized)
        return true;
    memset(&nack__wgl, 0, sizeof nack__wgl);

    nack__wgl.opengl32 = LoadLibraryA("opengl32.dll");
    if (!nack__wgl.opengl32)
        return nack__fail(NACK_ERROR_NO_BACKEND, "cannot load opengl32.dll");

#define NACK_WGL_LOAD(field, name, type)                                       \
    nack__wgl.field = (type)(void *)GetProcAddress(nack__wgl.opengl32, name);  \
    if (!nack__wgl.field) {                                                    \
        FreeLibrary(nack__wgl.opengl32);                                       \
        nack__wgl.opengl32 = NULL;                                             \
        return nack__fail(NACK_ERROR_NO_BACKEND, "opengl32.dll lacks %s", name); \
    }
    NACK_WGL_LOAD(CreateContext, "wglCreateContext", HGLRC (WINAPI *)(HDC))
    NACK_WGL_LOAD(DeleteContext, "wglDeleteContext", BOOL (WINAPI *)(HGLRC))
    NACK_WGL_LOAD(GetProcAddress_, "wglGetProcAddress", PROC (WINAPI *)(LPCSTR))
    NACK_WGL_LOAD(MakeCurrent, "wglMakeCurrent", BOOL (WINAPI *)(HDC, HGLRC))
    NACK_WGL_LOAD(ShareLists, "wglShareLists", BOOL (WINAPI *)(HGLRC, HGLRC))
#undef NACK_WGL_LOAD

    if (!nack__wgl_bootstrap()) {
        FreeLibrary(nack__wgl.opengl32);
        nack__wgl.opengl32 = NULL;
        return false;
    }

    nack__wgl.initialized = true;
    return true;
}

void nack__wgl_terminate(void)
{
    if (nack__wgl.opengl32)
        FreeLibrary(nack__wgl.opengl32);
    memset(&nack__wgl, 0, sizeof nack__wgl);
}

/* ------------------------------------------------------------------ */
/* Pixel format                                                       */
/* ------------------------------------------------------------------ */

bool nack__wgl_choose_pixel_format(struct nack_window *w, HDC hdc, int *out_format)
{
    if (!nack__wgl.initialized)
        return false;

    const struct nack_framebuffer_desc *fb = &w->framebuffer;

    if (nack__wgl.ChoosePixelFormatARB) {
        int attribs[32];
        int n = 0;
        attribs[n++] = WGL_DRAW_TO_WINDOW_ARB;  attribs[n++] = TRUE;
        attribs[n++] = WGL_SUPPORT_OPENGL_ARB;  attribs[n++] = TRUE;
        attribs[n++] = WGL_ACCELERATION_ARB;    attribs[n++] = WGL_FULL_ACCELERATION_ARB;
        attribs[n++] = WGL_PIXEL_TYPE_ARB;      attribs[n++] = WGL_TYPE_RGBA_ARB;
        attribs[n++] = WGL_DOUBLE_BUFFER_ARB;   attribs[n++] = fb->double_buffer;
        attribs[n++] = WGL_RED_BITS_ARB;        attribs[n++] = fb->red_bits;
        attribs[n++] = WGL_GREEN_BITS_ARB;      attribs[n++] = fb->green_bits;
        attribs[n++] = WGL_BLUE_BITS_ARB;       attribs[n++] = fb->blue_bits;
        attribs[n++] = WGL_ALPHA_BITS_ARB;      attribs[n++] = fb->alpha_bits;
        attribs[n++] = WGL_DEPTH_BITS_ARB;      attribs[n++] = fb->depth_bits;
        attribs[n++] = WGL_STENCIL_BITS_ARB;    attribs[n++] = fb->stencil_bits;
        if (fb->samples > 0 && nack__wgl.has_multisample) {
            attribs[n++] = WGL_SAMPLE_BUFFERS_ARB; attribs[n++] = 1;
            attribs[n++] = WGL_SAMPLES_ARB;        attribs[n++] = fb->samples;
        }
        if (fb->srgb && nack__wgl.has_srgb) {
            attribs[n++] = WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB;
            attribs[n++] = TRUE;
        }
        attribs[n] = 0;

        int format = 0;
        UINT count = 0;
        if (nack__wgl.ChoosePixelFormatARB(hdc, attribs, NULL, 1, &format, &count) &&
            count > 0) {
            *out_format = format;
            return true;
        }
        /* Fall through to the legacy chooser rather than failing outright. */
    }

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    if (fb->double_buffer)
        pfd.dwFlags |= PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = (BYTE)(fb->red_bits + fb->green_bits + fb->blue_bits);
    pfd.cAlphaBits = (BYTE)fb->alpha_bits;
    pfd.cDepthBits = (BYTE)fb->depth_bits;
    pfd.cStencilBits = (BYTE)fb->stencil_bits;

    int format = ChoosePixelFormat(hdc, &pfd);
    if (!format)
        return nack__fail(NACK_ERROR_NO_PIXEL_FORMAT,
                          "no pixel format matches the requested framebuffer");
    *out_format = format;
    return true;
}

/* ------------------------------------------------------------------ */
/* Contexts                                                           */
/* ------------------------------------------------------------------ */

struct nack_gl_context *nack__wgl_create_context(struct nack_window *w, const struct nack__gl_desc *desc,
                                          const struct nack_backend_vt *vt)
{
    if (!nack__wgl.initialized) {
        nack__fail(NACK_ERROR_UNSUPPORTED, "WGL is not available");
        return NULL;
    }

    struct nack_win32_window *ww = nack__win32_win(w);
    if (!ww || !ww->hdc) {
        nack__fail(NACK_ERROR_INVALID_ARGUMENT, "window has no device context");
        return NULL;
    }

    if (desc->profile == NACK__GL_PROFILE_ES) {
        nack__fail(NACK_ERROR_UNSUPPORTED,
                   "OpenGL ES needs an EGL implementation such as ANGLE; "
                   "WGL provides desktop OpenGL only");
        return NULL;
    }

    HGLRC share = NULL;
    if (desc->share && desc->share->native)
        share = ((struct nack_wgl_context *)desc->share->native)->glrc;

    HGLRC glrc = NULL;

    if (nack__wgl.CreateContextAttribsARB) {
        int attribs[16];
        int n = 0;
        int flags = 0;
        int mask = 0;

        if (desc->major > 0) {
            attribs[n++] = WGL_CONTEXT_MAJOR_VERSION_ARB; attribs[n++] = desc->major;
            attribs[n++] = WGL_CONTEXT_MINOR_VERSION_ARB; attribs[n++] = desc->minor;
        }
        if (desc->debug)
            flags |= WGL_CONTEXT_DEBUG_BIT_ARB;
        if (desc->forward_compatible)
            flags |= WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
        if (desc->robust && nack__wgl.has_create_context_robustness)
            flags |= WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB;

        if (nack__wgl.has_create_context_profile)
            mask = (desc->profile == NACK__GL_PROFILE_CORE)
                       ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB
                       : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;

        if (flags) {
            attribs[n++] = WGL_CONTEXT_FLAGS_ARB;
            attribs[n++] = flags;
        }
        if (mask) {
            attribs[n++] = WGL_CONTEXT_PROFILE_MASK_ARB;
            attribs[n++] = mask;
        }
        attribs[n] = 0;

        glrc = nack__wgl.CreateContextAttribsARB(ww->hdc, share, attribs);
    }

    if (!glrc && desc->major == 0) {
        /*
         * No version was asked for, so the legacy context is what was wanted.
         * When one was asked for, falling back here would hand back a 1.1
         * context that fails much later with a missing entry point, blaming
         * the loader for what is really a machine with no modern OpenGL on
         * it - which is exactly what the Microsoft GDI generic renderer, the
         * only one a GPU-less Windows box has, does.
         */
        glrc = nack__wgl.CreateContext(ww->hdc);
        if (glrc && share && !nack__wgl.ShareLists(share, glrc))
            nack__log("nack: wglShareLists failed; contexts will not share objects");
    }

    if (!glrc) {
        if (desc->major > 0 && !nack__wgl.CreateContextAttribsARB)
            nack__fail(NACK_ERROR_CONTEXT_CREATION,
                       "this OpenGL driver has no WGL_ARB_create_context, so "
                       "no GL %d.%d context can be made; it is most likely "
                       "the software renderer Windows falls back to with no "
                       "graphics driver installed",
                       desc->major, desc->minor);
        else
            nack__fail(NACK_ERROR_CONTEXT_CREATION,
                       "failed to create a GL %d.%d context (error %lu)",
                       desc->major, desc->minor, GetLastError());
        return NULL;
    }

    struct nack_gl_context *ctx = (struct nack_gl_context *)nack__calloc(1, sizeof *ctx);
    struct nack_wgl_context *native = (struct nack_wgl_context *)nack__calloc(1, sizeof *native);
    if (!ctx || !native) {
        nack__wgl.DeleteContext(glrc);
        free(ctx);
        free(native);
        return NULL;
    }

    native->glrc = glrc;
    ctx->native = native;
    ctx->vt = vt;
    ctx->owner = w;
    return ctx;
}

void nack__wgl_destroy_context(struct nack_gl_context *ctx)
{
    if (!ctx)
        return;
    struct nack_wgl_context *native = (struct nack_wgl_context *)ctx->native;
    if (native) {
        if (native->glrc) {
            if (wglGetCurrentContext() == native->glrc)
                nack__wgl.MakeCurrent(NULL, NULL);
            nack__wgl.DeleteContext(native->glrc);
        }
        free(native);
    }
    free(ctx);
}

bool nack__wgl_make_current(struct nack_window *w, struct nack_gl_context *ctx)
{
    if (!ctx)
        return nack__wgl.MakeCurrent(NULL, NULL) != FALSE;
    if (!w)
        return nack__fail(NACK_ERROR_INVALID_ARGUMENT,
                          "nack__gl_make_current needs a window for this context");

    struct nack_win32_window *ww = nack__win32_win(w);
    struct nack_wgl_context *native = (struct nack_wgl_context *)ctx->native;
    if (!nack__wgl.MakeCurrent(ww->hdc, native->glrc))
        return nack__fail(NACK_ERROR_PLATFORM, "wglMakeCurrent failed (error %lu)",
                          GetLastError());
    ww->glrc = native->glrc;
    return true;
}

void nack__wgl_swap_buffers(struct nack_window *w)
{
    struct nack_win32_window *ww = nack__win32_win(w);
    if (ww && ww->hdc)
        SwapBuffers(ww->hdc);
}

void nack__wgl_set_swap_interval(int interval)
{
    if (!nack__wgl.SwapIntervalEXT)
        return;
    if (interval < 0 && !nack__wgl.has_swap_control_tear)
        interval = -interval;   /* no adaptive vsync; use plain vsync */
    nack__wgl.SwapIntervalEXT(interval);
}

void *nack__wgl_get_proc_address(const char *name)
{
    /*
     * wglGetProcAddress only resolves extension entry points; the OpenGL 1.1
     * core that ships in opengl32.dll has to come from the module itself.
     */
    if (nack__wgl.GetProcAddress_) {
        PROC proc = nack__wgl.GetProcAddress_(name);
        /* Some drivers return these sentinels instead of NULL on failure. */
        if (proc && proc != (PROC)1 && proc != (PROC)2 && proc != (PROC)3 &&
            proc != (PROC)-1)
            return (void *)proc;
    }
    if (nack__wgl.opengl32)
        return (void *)GetProcAddress(nack__wgl.opengl32, name);
    return NULL;
}
