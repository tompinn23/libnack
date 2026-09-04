/*
 * NSOpenGL context creation.
 *
 * macOS deprecated OpenGL in 10.14 but still ships it. The core profile is
 * capped at 4.1, and asking for a version above that fails rather than
 * downgrading, so the requested version is mapped onto the two profile
 * versions the platform actually offers.
 */
#include "nack_cocoa.h"

/*
 * Apple deprecated OpenGL wholesale in 10.14 but still ships it, so every
 * entry point below is marked deprecated. Silencing it here keeps real
 * warnings visible instead of burying them.
 */
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>

#include <dlfcn.h>
#include <memory>
#include <stdio.h>

namespace nack { namespace detail {

struct nack_nsgl_context {
    NSOpenGLContext *context;
    NSOpenGLPixelFormat *pixel_format;
};

static void *nsgl_framework;

nack_gl_context *nsgl_create_context(nack_window *w, const gl_desc *desc,
                                           nack_backend_vt *vt)
{
    @autoreleasepool {
        if (desc->profile == NACK__GL_PROFILE_ES) {
            state.fail(NACK_ERROR_UNSUPPORTED,
                       "macOS has no OpenGL ES; use ANGLE or a Metal backend");
            return NULL;
        }

        /*
         * NSOpenGLProfileVersion4_1Core covers 3.2 to 4.1; the legacy profile
         * is 2.1. There is nothing in between, so anything above 3.2 maps to
         * the 4.1 profile and a compatibility request maps to legacy.
         */
        NSOpenGLPixelFormatAttribute profile;
        if (desc->profile == NACK__GL_PROFILE_COMPAT) {
            if (desc->major > 2) {
                state.fail(NACK_ERROR_UNSUPPORTED,
                           "macOS offers no compatibility profile above 2.1; "
                           "request a core profile instead");
                return NULL;
            }
            profile = NSOpenGLProfileVersionLegacy;
        } else if (desc->major == 0 || desc->major > 3 ||
                   (desc->major == 3 && desc->minor >= 2)) {
            profile = NSOpenGLProfileVersion4_1Core;
        } else if (desc->major == 3) {
            /* 3.0 and 3.1 core do not exist here; 3.2 core is the floor. */
            profile = NSOpenGLProfileVersion4_1Core;
        } else {
            profile = NSOpenGLProfileVersionLegacy;
        }

        if (desc->major > 4 || (desc->major == 4 && desc->minor > 1)) {
            state.fail(NACK_ERROR_UNSUPPORTED,
                       "macOS caps OpenGL at 4.1; %d.%d is not available",
                       desc->major, desc->minor);
            return NULL;
        }

        const nack_framebuffer_desc *fb = &w->framebuffer;

        NSOpenGLPixelFormatAttribute attribs[40];
        int n = 0;

        attribs[n++] = NSOpenGLPFAAccelerated;
        attribs[n++] = NSOpenGLPFAClosestPolicy;
        attribs[n++] = NSOpenGLPFAOpenGLProfile;
        attribs[n++] = profile;
        attribs[n++] = NSOpenGLPFAColorSize;
        attribs[n++] = (NSOpenGLPixelFormatAttribute)(fb->red_bits + fb->green_bits +
                                                      fb->blue_bits);
        attribs[n++] = NSOpenGLPFAAlphaSize;
        attribs[n++] = (NSOpenGLPixelFormatAttribute)fb->alpha_bits;
        attribs[n++] = NSOpenGLPFADepthSize;
        attribs[n++] = (NSOpenGLPixelFormatAttribute)fb->depth_bits;
        attribs[n++] = NSOpenGLPFAStencilSize;
        attribs[n++] = (NSOpenGLPixelFormatAttribute)fb->stencil_bits;

        if (fb->double_buffer)
            attribs[n++] = NSOpenGLPFADoubleBuffer;

        if (fb->samples > 0) {
            attribs[n++] = NSOpenGLPFAMultisample;
            attribs[n++] = NSOpenGLPFASampleBuffers;
            attribs[n++] = 1;
            attribs[n++] = NSOpenGLPFASamples;
            attribs[n++] = (NSOpenGLPixelFormatAttribute)fb->samples;
        }

        attribs[n] = 0;

        NSOpenGLPixelFormat *pixel_format =
            [[NSOpenGLPixelFormat alloc] initWithAttributes:attribs];
        if (!pixel_format) {
            state.fail(NACK_ERROR_NO_PIXEL_FORMAT,
                       "no NSOpenGLPixelFormat matches the requested framebuffer");
            return NULL;
        }

        NSOpenGLContext *share = nil;
        if (desc->share && desc->share->native)
            share = ((nack_nsgl_context *)desc->share->native)->context;

        NSOpenGLContext *context =
            [[NSOpenGLContext alloc] initWithFormat:pixel_format shareContext:share];
        if (!context) {
            [pixel_format release];
            state.fail(NACK_ERROR_CONTEXT_CREATION, "failed to create NSOpenGLContext");
            return NULL;
        }

        nack_cocoa_window *cw = cocoa_win(w);

        if (w->high_dpi)
            [cw->view setWantsBestResolutionOpenGLSurface:YES];

        [context setView:cw->view];

        auto ctx = std::make_unique<nack_gl_context>();
        auto native = std::make_unique<nack_nsgl_context>();

        native->context = context;
        native->pixel_format = pixel_format;
        ctx->native = native.release();
        ctx->vt = vt;
        ctx->owner = w;
        cw->gl_context = context;
        return ctx.release();
    }
}

void nsgl_destroy_context(nack_gl_context *ctx)
{
    if (!ctx)
        return;
    @autoreleasepool {
        nack_nsgl_context *native = (nack_nsgl_context *)ctx->native;
        if (native) {
            if (ctx->owner) {
                nack_cocoa_window *cw = (nack_cocoa_window *)ctx->owner->native;
                if (cw && cw->gl_context == native->context)
                    cw->gl_context = NULL;
            }
            if ([NSOpenGLContext currentContext] == native->context)
                [NSOpenGLContext clearCurrentContext];
            [native->context release];
            [native->pixel_format release];
            delete native;
        }
        delete ctx;
    }
}

bool nsgl_make_current(nack_window *w, nack_gl_context *ctx)
{
    @autoreleasepool {
        if (!ctx) {
            [NSOpenGLContext clearCurrentContext];
            return true;
        }
        nack_nsgl_context *native = (nack_nsgl_context *)ctx->native;
        if (w) {
            nack_cocoa_window *cw = cocoa_win(w);
            if ([native->context view] != cw->view)
                [native->context setView:cw->view];
        }
        [native->context makeCurrentContext];
        return true;
    }
}

void nsgl_swap_buffers(nack_window *w)
{
    @autoreleasepool {
        nack_cocoa_window *cw = cocoa_win(w);
        if (cw && cw->gl_context)
            [(NSOpenGLContext *)cw->gl_context flushBuffer];
    }
}

void nsgl_set_swap_interval(int interval)
{
    @autoreleasepool {
        NSOpenGLContext *context = [NSOpenGLContext currentContext];
        if (!context)
            return;
        /* NSOpenGL has no adaptive vsync, so a negative interval becomes
         * ordinary vsync rather than an error. */
        GLint value = interval < 0 ? -interval : interval;
        [context setValues:&value forParameter:NSOpenGLContextParameterSwapInterval];
    }
}

void nsgl_update(nack_window *w)
{
    @autoreleasepool {
        nack_cocoa_window *cw = cocoa_win(w);
        if (!cw || !cw->gl_context)
            return;
        /*
         * The context caches the view's backing geometry, so it has to be
         * told whenever the view is resized or moves to a display with a
         * different backing scale factor.
         */
        [(NSOpenGLContext *)cw->gl_context update];
    }
}

void *nsgl_get_proc_address(const char *name)
{
    if (!nsgl_framework) {
        nsgl_framework =
            dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL",
                   RTLD_LAZY | RTLD_LOCAL);
        if (!nsgl_framework)
            return NULL;
    }
    return dlsym(nsgl_framework, name);
}

void nsgl_terminate(void)
{
    if (nsgl_framework) {
        dlclose(nsgl_framework);
        nsgl_framework = NULL;
    }
}

} }   /* namespace nack::detail */
