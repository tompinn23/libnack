#ifndef NACK_COCOA_H_INCLUDED
#define NACK_COCOA_H_INCLUDED

#include "../nack_internal.h"
#include <optional>
#include <string>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

@class NackWindowDelegate;
@class NackContentView;

struct nack_cocoa_window {
    NSWindow *window;
    NackWindowDelegate *delegate;
    NackContentView *view;
    void *gl_context;          /* NSOpenGLContext, retained                */
    NSCursor *cursor;
    NSRect restore_frame;
    bool cursor_hidden;
    bool warp_pending;
    double virtual_x, virtual_y;
};

struct nack_cocoa_state {
    id app_delegate;
    bool finished_launching;
    std::optional<std::string> clipboard_text;
    NSInteger clipboard_change_count;
    nack_key keycodes[256];
    /* Modifier state has to be tracked by hand: macOS reports modifiers as a
     * flags word, not as press and release events. */
    uint32_t modifier_flags;
};

extern nack_cocoa_state nack__cocoa;

/*
 * Instance variables carry a nack prefix rather than the obvious _window and
 * friends: AppKit has historically used such names for its own private ivars,
 * and a collision in a subclass is a compile error.
 */
@interface NackWindowDelegate : NSObject <NSWindowDelegate>
{
    nack_window *_nackWindow;
}
- (instancetype)initWithWindow:(nack_window *)window;
@end

@interface NackContentView : NSView <NSTextInputClient>
{
    nack_window *_nackWindow;
    NSTrackingArea *_nackTrackingArea;
    NSMutableAttributedString *_nackMarkedText;
}
- (instancetype)initWithWindow:(nack_window *)window;
@end

static inline nack_cocoa_window *nack__cocoa_win(nack_window *w)
{
    return (nack_cocoa_window *)w->native;
}

/* nack_nsgl.m */
nack_gl_context *nack__nsgl_create_context(nack_window *w, const nack__gl_desc *desc,
                                           nack_backend_vt *vt);
void  nack__nsgl_destroy_context(nack_gl_context *ctx);
bool  nack__nsgl_make_current(nack_window *w, nack_gl_context *ctx);
void  nack__nsgl_swap_buffers(nack_window *w);
void  nack__nsgl_set_swap_interval(int interval);
void *nack__nsgl_get_proc_address(const char *name);
void  nack__nsgl_update(nack_window *w);
void  nack__nsgl_terminate(void);

/* nack_cocoa.m */
void nack__cocoa_update_size(nack_window *w);

#endif /* NACK_COCOA_H_INCLUDED */
