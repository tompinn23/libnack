#ifndef NACK_COCOA_H_INCLUDED
#define NACK_COCOA_H_INCLUDED

#include "../nack_internal.h"

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
    char *clipboard_text;
    NSInteger clipboard_change_count;
    enum nack_key keycodes[256];
    /* Modifier state has to be tracked by hand: macOS reports modifiers as a
     * flags word, not as press and release events. */
    uint32_t modifier_flags;
};

extern struct nack_cocoa_state nack__cocoa;

@interface NackWindowDelegate : NSObject <NSWindowDelegate>
{
    struct nack_window *_window;
}
- (instancetype)initWithWindow:(struct nack_window *)window;
@end

@interface NackContentView : NSView <NSTextInputClient>
{
    struct nack_window *_window;
    NSTrackingArea *_trackingArea;
    NSMutableAttributedString *_markedText;
}
- (instancetype)initWithWindow:(struct nack_window *)window;
@end

static inline struct nack_cocoa_window *nack__cocoa_win(struct nack_window *w)
{
    return (struct nack_cocoa_window *)w->native;
}

/* nack_nsgl.m */
struct nack_gl_context *nack__nsgl_create_context(struct nack_window *w, const struct nack_gl_desc *desc,
                                           const struct nack_backend_vt *vt);
void  nack__nsgl_destroy_context(struct nack_gl_context *ctx);
bool  nack__nsgl_make_current(struct nack_window *w, struct nack_gl_context *ctx);
void  nack__nsgl_swap_buffers(struct nack_window *w);
void  nack__nsgl_set_swap_interval(int interval);
void *nack__nsgl_get_proc_address(const char *name);
void  nack__nsgl_update(struct nack_window *w);
void  nack__nsgl_terminate(void);

/* nack_cocoa.m */
void nack__cocoa_update_size(struct nack_window *w);

#endif /* NACK_COCOA_H_INCLUDED */
