/* libnack - Cocoa backend. */
#include "nack_cocoa.h"

#import <Carbon/Carbon.h>             /* kVK_* virtual key constants */
#import <CoreGraphics/CoreGraphics.h> /* CGAssociateMouseAndMouseCursorPosition */

#include <stdio.h>

struct nack_cocoa_state nack__cocoa;

/* ------------------------------------------------------------------ */
/* Key mapping                                                        */
/* ------------------------------------------------------------------ */

/*
 * macOS virtual key codes describe physical positions on an ANSI keyboard,
 * which is exactly the mapping we want; the character produced comes
 * separately through the text input client.
 */
static void nack__cocoa_build_keycodes(void)
{
    memset(nack__cocoa.keycodes, 0, sizeof nack__cocoa.keycodes);

    nack__cocoa.keycodes[kVK_ANSI_A] = NACK_KEY_A;
    nack__cocoa.keycodes[kVK_ANSI_B] = NACK_KEY_B;
    nack__cocoa.keycodes[kVK_ANSI_C] = NACK_KEY_C;
    nack__cocoa.keycodes[kVK_ANSI_D] = NACK_KEY_D;
    nack__cocoa.keycodes[kVK_ANSI_E] = NACK_KEY_E;
    nack__cocoa.keycodes[kVK_ANSI_F] = NACK_KEY_F;
    nack__cocoa.keycodes[kVK_ANSI_G] = NACK_KEY_G;
    nack__cocoa.keycodes[kVK_ANSI_H] = NACK_KEY_H;
    nack__cocoa.keycodes[kVK_ANSI_I] = NACK_KEY_I;
    nack__cocoa.keycodes[kVK_ANSI_J] = NACK_KEY_J;
    nack__cocoa.keycodes[kVK_ANSI_K] = NACK_KEY_K;
    nack__cocoa.keycodes[kVK_ANSI_L] = NACK_KEY_L;
    nack__cocoa.keycodes[kVK_ANSI_M] = NACK_KEY_M;
    nack__cocoa.keycodes[kVK_ANSI_N] = NACK_KEY_N;
    nack__cocoa.keycodes[kVK_ANSI_O] = NACK_KEY_O;
    nack__cocoa.keycodes[kVK_ANSI_P] = NACK_KEY_P;
    nack__cocoa.keycodes[kVK_ANSI_Q] = NACK_KEY_Q;
    nack__cocoa.keycodes[kVK_ANSI_R] = NACK_KEY_R;
    nack__cocoa.keycodes[kVK_ANSI_S] = NACK_KEY_S;
    nack__cocoa.keycodes[kVK_ANSI_T] = NACK_KEY_T;
    nack__cocoa.keycodes[kVK_ANSI_U] = NACK_KEY_U;
    nack__cocoa.keycodes[kVK_ANSI_V] = NACK_KEY_V;
    nack__cocoa.keycodes[kVK_ANSI_W] = NACK_KEY_W;
    nack__cocoa.keycodes[kVK_ANSI_X] = NACK_KEY_X;
    nack__cocoa.keycodes[kVK_ANSI_Y] = NACK_KEY_Y;
    nack__cocoa.keycodes[kVK_ANSI_Z] = NACK_KEY_Z;

    nack__cocoa.keycodes[kVK_ANSI_0] = NACK_KEY_0;
    nack__cocoa.keycodes[kVK_ANSI_1] = NACK_KEY_1;
    nack__cocoa.keycodes[kVK_ANSI_2] = NACK_KEY_2;
    nack__cocoa.keycodes[kVK_ANSI_3] = NACK_KEY_3;
    nack__cocoa.keycodes[kVK_ANSI_4] = NACK_KEY_4;
    nack__cocoa.keycodes[kVK_ANSI_5] = NACK_KEY_5;
    nack__cocoa.keycodes[kVK_ANSI_6] = NACK_KEY_6;
    nack__cocoa.keycodes[kVK_ANSI_7] = NACK_KEY_7;
    nack__cocoa.keycodes[kVK_ANSI_8] = NACK_KEY_8;
    nack__cocoa.keycodes[kVK_ANSI_9] = NACK_KEY_9;

    nack__cocoa.keycodes[kVK_Return]              = NACK_KEY_ENTER;
    nack__cocoa.keycodes[kVK_Escape]              = NACK_KEY_ESCAPE;
    nack__cocoa.keycodes[kVK_Delete]              = NACK_KEY_BACKSPACE;
    nack__cocoa.keycodes[kVK_ForwardDelete]       = NACK_KEY_DELETE;
    nack__cocoa.keycodes[kVK_Tab]                 = NACK_KEY_TAB;
    nack__cocoa.keycodes[kVK_Space]               = NACK_KEY_SPACE;
    nack__cocoa.keycodes[kVK_ANSI_Minus]          = NACK_KEY_MINUS;
    nack__cocoa.keycodes[kVK_ANSI_Equal]          = NACK_KEY_EQUAL;
    nack__cocoa.keycodes[kVK_ANSI_LeftBracket]    = NACK_KEY_LEFT_BRACKET;
    nack__cocoa.keycodes[kVK_ANSI_RightBracket]   = NACK_KEY_RIGHT_BRACKET;
    nack__cocoa.keycodes[kVK_ANSI_Backslash]      = NACK_KEY_BACKSLASH;
    nack__cocoa.keycodes[kVK_ANSI_Semicolon]      = NACK_KEY_SEMICOLON;
    nack__cocoa.keycodes[kVK_ANSI_Quote]          = NACK_KEY_APOSTROPHE;
    nack__cocoa.keycodes[kVK_ANSI_Grave]          = NACK_KEY_GRAVE;
    nack__cocoa.keycodes[kVK_ANSI_Comma]          = NACK_KEY_COMMA;
    nack__cocoa.keycodes[kVK_ANSI_Period]         = NACK_KEY_PERIOD;
    nack__cocoa.keycodes[kVK_ANSI_Slash]          = NACK_KEY_SLASH;
    nack__cocoa.keycodes[kVK_CapsLock]            = NACK_KEY_CAPS_LOCK;
    nack__cocoa.keycodes[kVK_ISO_Section]         = NACK_KEY_NON_US_BACKSLASH;

    nack__cocoa.keycodes[kVK_F1]  = NACK_KEY_F1;
    nack__cocoa.keycodes[kVK_F2]  = NACK_KEY_F2;
    nack__cocoa.keycodes[kVK_F3]  = NACK_KEY_F3;
    nack__cocoa.keycodes[kVK_F4]  = NACK_KEY_F4;
    nack__cocoa.keycodes[kVK_F5]  = NACK_KEY_F5;
    nack__cocoa.keycodes[kVK_F6]  = NACK_KEY_F6;
    nack__cocoa.keycodes[kVK_F7]  = NACK_KEY_F7;
    nack__cocoa.keycodes[kVK_F8]  = NACK_KEY_F8;
    nack__cocoa.keycodes[kVK_F9]  = NACK_KEY_F9;
    nack__cocoa.keycodes[kVK_F10] = NACK_KEY_F10;
    nack__cocoa.keycodes[kVK_F11] = NACK_KEY_F11;
    nack__cocoa.keycodes[kVK_F12] = NACK_KEY_F12;
    nack__cocoa.keycodes[kVK_F13] = NACK_KEY_F13;
    nack__cocoa.keycodes[kVK_F14] = NACK_KEY_F14;
    nack__cocoa.keycodes[kVK_F15] = NACK_KEY_F15;
    nack__cocoa.keycodes[kVK_F16] = NACK_KEY_F16;
    nack__cocoa.keycodes[kVK_F17] = NACK_KEY_F17;
    nack__cocoa.keycodes[kVK_F18] = NACK_KEY_F18;
    nack__cocoa.keycodes[kVK_F19] = NACK_KEY_F19;
    nack__cocoa.keycodes[kVK_F20] = NACK_KEY_F20;

    nack__cocoa.keycodes[kVK_Home]        = NACK_KEY_HOME;
    nack__cocoa.keycodes[kVK_End]         = NACK_KEY_END;
    nack__cocoa.keycodes[kVK_PageUp]      = NACK_KEY_PAGE_UP;
    nack__cocoa.keycodes[kVK_PageDown]    = NACK_KEY_PAGE_DOWN;
    nack__cocoa.keycodes[kVK_LeftArrow]   = NACK_KEY_LEFT;
    nack__cocoa.keycodes[kVK_RightArrow]  = NACK_KEY_RIGHT;
    nack__cocoa.keycodes[kVK_UpArrow]     = NACK_KEY_UP;
    nack__cocoa.keycodes[kVK_DownArrow]   = NACK_KEY_DOWN;

    nack__cocoa.keycodes[kVK_ANSI_Keypad0]        = NACK_KEY_KP_0;
    nack__cocoa.keycodes[kVK_ANSI_Keypad1]        = NACK_KEY_KP_1;
    nack__cocoa.keycodes[kVK_ANSI_Keypad2]        = NACK_KEY_KP_2;
    nack__cocoa.keycodes[kVK_ANSI_Keypad3]        = NACK_KEY_KP_3;
    nack__cocoa.keycodes[kVK_ANSI_Keypad4]        = NACK_KEY_KP_4;
    nack__cocoa.keycodes[kVK_ANSI_Keypad5]        = NACK_KEY_KP_5;
    nack__cocoa.keycodes[kVK_ANSI_Keypad6]        = NACK_KEY_KP_6;
    nack__cocoa.keycodes[kVK_ANSI_Keypad7]        = NACK_KEY_KP_7;
    nack__cocoa.keycodes[kVK_ANSI_Keypad8]        = NACK_KEY_KP_8;
    nack__cocoa.keycodes[kVK_ANSI_Keypad9]        = NACK_KEY_KP_9;
    nack__cocoa.keycodes[kVK_ANSI_KeypadDecimal]  = NACK_KEY_KP_DECIMAL;
    nack__cocoa.keycodes[kVK_ANSI_KeypadDivide]   = NACK_KEY_KP_DIVIDE;
    nack__cocoa.keycodes[kVK_ANSI_KeypadMultiply] = NACK_KEY_KP_MULTIPLY;
    nack__cocoa.keycodes[kVK_ANSI_KeypadMinus]    = NACK_KEY_KP_SUBTRACT;
    nack__cocoa.keycodes[kVK_ANSI_KeypadPlus]     = NACK_KEY_KP_ADD;
    nack__cocoa.keycodes[kVK_ANSI_KeypadEnter]    = NACK_KEY_KP_ENTER;
    nack__cocoa.keycodes[kVK_ANSI_KeypadEquals]   = NACK_KEY_KP_EQUAL;

    nack__cocoa.keycodes[kVK_Shift]        = NACK_KEY_LEFT_SHIFT;
    nack__cocoa.keycodes[kVK_RightShift]   = NACK_KEY_RIGHT_SHIFT;
    nack__cocoa.keycodes[kVK_Control]      = NACK_KEY_LEFT_CTRL;
    nack__cocoa.keycodes[kVK_RightControl] = NACK_KEY_RIGHT_CTRL;
    nack__cocoa.keycodes[kVK_Option]       = NACK_KEY_LEFT_ALT;
    nack__cocoa.keycodes[kVK_RightOption]  = NACK_KEY_RIGHT_ALT;
    nack__cocoa.keycodes[kVK_Command]      = NACK_KEY_LEFT_SUPER;
    nack__cocoa.keycodes[kVK_RightCommand] = NACK_KEY_RIGHT_SUPER;
    nack__cocoa.keycodes[kVK_Function]     = NACK_KEY_UNKNOWN;
}

static uint32_t nack__cocoa_mods(NSEventModifierFlags flags)
{
    uint32_t mods = 0;
    if (flags & NSEventModifierFlagShift)    mods |= NACK_MOD_SHIFT;
    if (flags & NSEventModifierFlagControl)  mods |= NACK_MOD_CTRL;
    if (flags & NSEventModifierFlagOption)   mods |= NACK_MOD_ALT;
    if (flags & NSEventModifierFlagCommand)  mods |= NACK_MOD_SUPER;
    if (flags & NSEventModifierFlagCapsLock) mods |= NACK_MOD_CAPSLOCK;
    return mods;
}

static enum nack_key nack__cocoa_key(unsigned short keycode)
{
    return keycode < 256 ? nack__cocoa.keycodes[keycode] : NACK_KEY_UNKNOWN;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                           */
/* ------------------------------------------------------------------ */

void nack__cocoa_update_size(struct nack_window *w)
{
    struct nack_cocoa_window *cw = nack__cocoa_win(w);
    if (!cw || !cw->view)
        return;

    const NSRect bounds = [cw->view bounds];
    const NSRect backing = [cw->view convertRectToBacking:bounds];

    float scale = (float)[cw->window backingScaleFactor];
    nack__emit_scale(w, scale);

    /* The logical size is in points; the framebuffer is in backing pixels,
     * which on a Retina display is twice as many. */
    nack__emit_resize(w, (int)bounds.size.width, (int)bounds.size.height,
                      (int)backing.size.width, (int)backing.size.height);
    nack__nsgl_update(w);
}

/* ------------------------------------------------------------------ */
/* Window delegate                                                    */
/* ------------------------------------------------------------------ */

@implementation NackWindowDelegate

- (instancetype)initWithWindow:(struct nack_window *)window
{
    self = [super init];
    if (self)
        _nackWindow = window;
    return self;
}

- (BOOL)windowShouldClose:(id)sender
{
    (void)sender;
    _nackWindow->should_close = true;
    nack__emit_simple(_nackWindow, NACK_WIN_EVENT_WINDOW_CLOSE);
    /* Never close the window here: the application decides when to destroy
     * it, exactly as on the other backends. */
    return NO;
}

- (void)windowDidResize:(NSNotification *)notification
{
    (void)notification;
    nack__cocoa_update_size(_nackWindow);
}

- (void)windowDidMove:(NSNotification *)notification
{
    (void)notification;
    struct nack_cocoa_window *cw = nack__cocoa_win(_nackWindow);
    const NSRect frame = [cw->window frame];
    const NSRect content = [cw->window contentRectForFrameRect:frame];
    /* Cocoa's origin is bottom-left; report top-left like everywhere else. */
    const CGFloat screen_height = [[cw->window screen] frame].size.height;
    int x = (int)content.origin.x;
    int y = (int)(screen_height - content.origin.y - content.size.height);

    if (x != _nackWindow->pos_x || y != _nackWindow->pos_y) {
        _nackWindow->pos_x = x;
        _nackWindow->pos_y = y;
        struct nack_win_event *ev = nack__event_begin(NACK_WIN_EVENT_WINDOW_MOVE, _nackWindow);
        ev->data.move.x = x;
        ev->data.move.y = y;
        nack__push_event(ev);
    }
}

- (void)windowDidBecomeKey:(NSNotification *)notification
{
    (void)notification;
    nack__emit_focus(_nackWindow, true);
}

- (void)windowDidResignKey:(NSNotification *)notification
{
    (void)notification;
    nack__emit_focus(_nackWindow, false);
}

- (void)windowDidMiniaturize:(NSNotification *)notification
{
    (void)notification;
    _nackWindow->minimized = true;
    nack__emit_simple(_nackWindow, NACK_WIN_EVENT_WINDOW_MINIMIZE);
}

- (void)windowDidDeminiaturize:(NSNotification *)notification
{
    (void)notification;
    _nackWindow->minimized = false;
    nack__emit_simple(_nackWindow, NACK_WIN_EVENT_WINDOW_RESTORE);
}

- (void)windowDidChangeBackingProperties:(NSNotification *)notification
{
    (void)notification;
    /* Fires when the window moves between displays of different densities. */
    nack__cocoa_update_size(_nackWindow);
}

- (void)windowDidEnterFullScreen:(NSNotification *)notification
{
    (void)notification;
    _nackWindow->fullscreen = true;
}

- (void)windowDidExitFullScreen:(NSNotification *)notification
{
    (void)notification;
    _nackWindow->fullscreen = false;
}

@end

/* ------------------------------------------------------------------ */
/* Content view                                                       */
/* ------------------------------------------------------------------ */

@implementation NackContentView

- (instancetype)initWithWindow:(struct nack_window *)window
{
    self = [super initWithFrame:NSMakeRect(0, 0, window->width, window->height)];
    if (self) {
        _nackWindow = window;
        _nackMarkedText = [[NSMutableAttributedString alloc] init];
        [self updateTrackingAreas];
    }
    return self;
}

- (void)dealloc
{
    [_nackTrackingArea release];
    [_nackMarkedText release];
    [super dealloc];
}

- (BOOL)isOpaque                 { return YES; }
- (BOOL)canBecomeKeyView         { return YES; }
- (BOOL)acceptsFirstResponder    { return YES; }
- (BOOL)wantsUpdateLayer         { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void)event; return YES; }

- (void)updateTrackingAreas
{
    if (_nackTrackingArea) {
        [self removeTrackingArea:_nackTrackingArea];
        [_nackTrackingArea release];
    }
    const NSTrackingAreaOptions options =
        NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow |
        NSTrackingCursorUpdate | NSTrackingInVisibleRect |
        NSTrackingEnabledDuringMouseDrag;
    _nackTrackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
                                                options:options
                                                  owner:self
                                               userInfo:nil];
    [self addTrackingArea:_nackTrackingArea];
    [super updateTrackingAreas];
}

- (void)drawRect:(NSRect)rect
{
    (void)rect;
    nack__emit_simple(_nackWindow, NACK_WIN_EVENT_WINDOW_EXPOSE);
}

/* ---- Mouse ---- */

- (NSPoint)nackMouseLocation:(NSEvent *)event
{
    const NSRect content = [self frame];
    const NSPoint point = [event locationInWindow];
    const NSPoint local = [self convertPoint:point fromView:nil];
    /* Flip to a top-left origin so coordinates match the other backends. */
    return NSMakePoint(local.x, content.size.height - local.y);
}

- (void)nackHandleMouseButton:(NSEvent *)event button:(int)button down:(BOOL)down
{
    const NSPoint location = [self nackMouseLocation:event];
    nack__emit_mouse_button(_nackWindow, button, down, location.x, location.y,
                            nack__cocoa_mods([event modifierFlags]));
}

- (void)mouseDown:(NSEvent *)event
{ [self nackHandleMouseButton:event button:NACK_MOUSE_LEFT down:YES]; }
- (void)mouseUp:(NSEvent *)event
{ [self nackHandleMouseButton:event button:NACK_MOUSE_LEFT down:NO]; }
- (void)rightMouseDown:(NSEvent *)event
{ [self nackHandleMouseButton:event button:NACK_MOUSE_RIGHT down:YES]; }
- (void)rightMouseUp:(NSEvent *)event
{ [self nackHandleMouseButton:event button:NACK_MOUSE_RIGHT down:NO]; }

- (void)otherMouseDown:(NSEvent *)event
{
    int button = ([event buttonNumber] == 2) ? NACK_MOUSE_MIDDLE
                                             : (int)[event buttonNumber];
    [self nackHandleMouseButton:event button:button down:YES];
}

- (void)otherMouseUp:(NSEvent *)event
{
    int button = ([event buttonNumber] == 2) ? NACK_MOUSE_MIDDLE
                                             : (int)[event buttonNumber];
    [self nackHandleMouseButton:event button:button down:NO];
}

- (void)nackHandleMouseMove:(NSEvent *)event
{
    struct nack_cocoa_window *cw = nack__cocoa_win(_nackWindow);

    if (_nackWindow->cursor_mode == NACK_CURSOR_MODE_CAPTURED) {
        /* In captured mode the deltas are authoritative; the pointer itself
         * is associated away from the mouse, so its position is meaningless. */
        const double dx = [event deltaX];
        const double dy = [event deltaY];
        cw->virtual_x += dx;
        cw->virtual_y += dy;

        struct nack_win_event *ev = nack__event_begin(NACK_WIN_EVENT_MOUSE_MOVE, _nackWindow);
        ev->data.motion.x = cw->virtual_x;
        ev->data.motion.y = cw->virtual_y;
        ev->data.motion.dx = dx;
        ev->data.motion.dy = dy;
        ev->data.motion.mods = nack__cocoa_mods([event modifierFlags]);
        nack__push_event(ev);
        return;
    }

    const NSPoint location = [self nackMouseLocation:event];
    nack__emit_mouse_move(_nackWindow, location.x, location.y,
                          nack__cocoa_mods([event modifierFlags]));
}

- (void)mouseMoved:(NSEvent *)event        { [self nackHandleMouseMove:event]; }
- (void)mouseDragged:(NSEvent *)event      { [self nackHandleMouseMove:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self nackHandleMouseMove:event]; }
- (void)otherMouseDragged:(NSEvent *)event { [self nackHandleMouseMove:event]; }

- (void)mouseEntered:(NSEvent *)event
{
    (void)event;
    nack__emit_simple(_nackWindow, NACK_WIN_EVENT_MOUSE_ENTER);
}

- (void)mouseExited:(NSEvent *)event
{
    (void)event;
    nack__emit_simple(_nackWindow, NACK_WIN_EVENT_MOUSE_LEAVE);
}

- (void)cursorUpdate:(NSEvent *)event
{
    (void)event;
    struct nack_cocoa_window *cw = nack__cocoa_win(_nackWindow);
    if (cw->cursor)
        [cw->cursor set];
}

- (void)scrollWheel:(NSEvent *)event
{
    double dx = [event scrollingDeltaX];
    double dy = [event scrollingDeltaY];
    const bool precise = [event hasPreciseScrollingDeltas];

    if (precise) {
        /* Trackpad deltas are in points; scale them to wheel-detent units so
         * consumers can treat both the same way. */
        dx *= 0.1;
        dy *= 0.1;
    }

    nack__emit_scroll(_nackWindow, dx, dy, nack__cocoa_mods([event modifierFlags]),
                      precise);
}

/* ---- Keyboard ---- */

- (void)keyDown:(NSEvent *)event
{
    const enum nack_key key = nack__cocoa_key([event keyCode]);
    const uint32_t mods = nack__cocoa_mods([event modifierFlags]);

    nack__emit_key(_nackWindow, key, [event keyCode], mods, true, [event isARepeat]);

    /*
     * Command chords are menu shortcuts, not text. Routing them through the
     * input client would both insert text and beep.
     */
    if (!(mods & NACK_MOD_SUPER))
        [self interpretKeyEvents:@[event]];
}

- (void)keyUp:(NSEvent *)event
{
    nack__emit_key(_nackWindow, nack__cocoa_key([event keyCode]), [event keyCode],
                   nack__cocoa_mods([event modifierFlags]), false, false);
}

- (void)flagsChanged:(NSEvent *)event
{
    /*
     * macOS reports modifiers as a flags word rather than as press and
     * release events, so the transition has to be derived by comparing
     * against the previous state.
     */
    const uint32_t mods = nack__cocoa_mods([event modifierFlags]);
    const enum nack_key key = nack__cocoa_key([event keyCode]);

    if (key == NACK_KEY_UNKNOWN)
        return;

    bool down;
    if (key == NACK_KEY_CAPS_LOCK) {
        /* Caps Lock is a toggle: the flag reflects the lock, not the key. */
        down = (mods & NACK_MOD_CAPSLOCK) != 0;
    } else {
        down = !nack__g.keys[key];
    }

    nack__cocoa.modifier_flags = mods;
    nack__emit_key(_nackWindow, key, [event keyCode], mods, down, false);
}

/* ---- NSTextInputClient ----
 *
 * Implementing this protocol is what makes dead keys, Option chords and IME
 * composition produce correct text; -insertText: is where committed text
 * arrives.
 */

- (BOOL)hasMarkedText                { return [_nackMarkedText length] > 0; }
- (NSRange)markedRange
{
    return [_nackMarkedText length] > 0 ? NSMakeRange(0, [_nackMarkedText length] - 1)
                                    : NSMakeRange(NSNotFound, 0);
}
- (NSRange)selectedRange             { return NSMakeRange(NSNotFound, 0); }

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange
{
    (void)selectedRange; (void)replacementRange;
    [_nackMarkedText release];
    if ([string isKindOfClass:[NSAttributedString class]])
        _nackMarkedText = [[NSMutableAttributedString alloc] initWithAttributedString:string];
    else
        _nackMarkedText = [[NSMutableAttributedString alloc] initWithString:string];
}

- (void)unmarkText                   { [[_nackMarkedText mutableString] setString:@""]; }
- (NSArray *)validAttributesForMarkedText { return @[]; }

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range
                                                actualRange:(NSRangePointer)actualRange
{
    (void)range; (void)actualRange;
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point { (void)point; return 0; }

- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actualRange
{
    (void)range; (void)actualRange;
    /* Where an IME candidate window should appear. Without per-cell caret
     * tracking the window origin is the best available answer. */
    const NSRect frame = [self frame];
    return [[self window] convertRectToScreen:
        [self convertRect:frame toView:nil]];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    (void)replacementRange;

    NSString *characters = [string isKindOfClass:[NSAttributedString class]]
                               ? [string string]
                               : (NSString *)string;

    NSUInteger length = [characters length];
    for (NSUInteger i = 0; i < length; ++i) {
        uint32_t codepoint = (uint32_t)[characters characterAtIndex:i];

        /* Recombine UTF-16 surrogate pairs into one code point. */
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && i + 1 < length) {
            uint32_t low = (uint32_t)[characters characterAtIndex:i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) +
                            (low - 0xDC00u);
                ++i;
            }
        }

        /* Function keys arrive here as private-use code points; those are
         * key events, not text. */
        if (!nack__codepoint_is_text(codepoint))
            continue;

        char utf8[5];
        nack__utf8_encode(codepoint, utf8);
        nack__emit_text(_nackWindow, utf8);
    }
}

- (void)doCommandBySelector:(SEL)selector
{
    (void)selector;
    /* Swallow the command so AppKit does not beep at unhandled keys such as
     * Escape or the arrows, which we already delivered as key events. */
}

@end

/* ------------------------------------------------------------------ */
/* Application delegate                                               */
/* ------------------------------------------------------------------ */

@interface NackApplicationDelegate : NSObject <NSApplicationDelegate>
@end

@implementation NackApplicationDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;
    for (size_t i = 0; i < nack__g.window_count; ++i)
        nack__g.windows[i]->should_close = true;
    nack__emit_simple(NULL, NACK_WIN_EVENT_QUIT);
    /* Let the application shut down in its own event loop rather than having
     * AppKit tear the process down underneath it. */
    return NSTerminateCancel;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    nack__cocoa.finished_launching = true;

    /*
     * -stop: only takes effect after the next event is processed, so post a
     * dummy one; otherwise the priming -run never returns.
     */
    [NSApp stop:nil];
    NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSMakePoint(0, 0)
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:event atStart:YES];
}

- (void)applicationDidChangeScreenParameters:(NSNotification *)notification
{
    (void)notification;
    for (size_t i = 0; i < nack__g.window_count; ++i)
        nack__cocoa_update_size(nack__g.windows[i]);
}

@end

/* ------------------------------------------------------------------ */
/* Window management                                                  */
/* ------------------------------------------------------------------ */

static bool nack__cocoa_window_create(struct nack_window *w,
                                      const struct nack_window_desc *desc)
{
    (void)desc;
    struct nack_cocoa_window *cw = (struct nack_cocoa_window *)nack__calloc(1, sizeof *cw);
    if (!cw)
        return false;
    w->native = cw;

    NSWindowStyleMask style = 0;
    if (w->decorated) {
        style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                NSWindowStyleMaskMiniaturizable;
        if (w->resizable)
            style |= NSWindowStyleMaskResizable;
    } else {
        style = NSWindowStyleMaskBorderless;
        if (w->resizable)
            style |= NSWindowStyleMaskResizable;
    }

    const NSRect content = NSMakeRect(0, 0, w->width, w->height);
    cw->window = [[NSWindow alloc] initWithContentRect:content
                                            styleMask:style
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    if (!cw->window) {
        free(cw);
        w->native = NULL;
        return nack__fail(NACK_ERROR_PLATFORM, "failed to create NSWindow");
    }

    cw->view = [[NackContentView alloc] initWithWindow:w];
    cw->delegate = [[NackWindowDelegate alloc] initWithWindow:w];

    [cw->window setContentView:cw->view];
    [cw->window setDelegate:cw->delegate];
    [cw->window makeFirstResponder:cw->view];
    [cw->window setAcceptsMouseMovedEvents:YES];
    [cw->window setRestorable:NO];
    [cw->window center];

    /* Opting the view into high resolution is what makes the backing store
     * match the display; without it the framebuffer stays at point size and
     * text renders soft on a Retina panel. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [cw->view setWantsBestResolutionOpenGLSurface:w->high_dpi ? YES : NO];
#pragma clang diagnostic pop

    if (w->transparent) {
        [cw->window setOpaque:NO];
        [cw->window setBackgroundColor:[NSColor clearColor]];
    }

    if (w->resizable)
        [cw->window setCollectionBehavior:
            NSWindowCollectionBehaviorFullScreenPrimary];

    NSString *title = [NSString stringWithUTF8String:w->title];
    [cw->window setTitle:title];

    if (w->min_width > 0 || w->min_height > 0)
        [cw->window setContentMinSize:NSMakeSize(w->min_width, w->min_height)];
    if (w->max_width > 0 || w->max_height > 0)
        [cw->window setContentMaxSize:
            NSMakeSize(w->max_width > 0 ? w->max_width : CGFLOAT_MAX,
                       w->max_height > 0 ? w->max_height : CGFLOAT_MAX)];
    if (w->inc_width > 0 || w->inc_height > 0)
        [cw->window setContentResizeIncrements:
            NSMakeSize(w->inc_width > 0 ? w->inc_width : 1,
                       w->inc_height > 0 ? w->inc_height : 1)];

    cw->cursor = [NSCursor arrowCursor];
    w->scale = (float)[cw->window backingScaleFactor];

    const NSRect backing = [cw->view convertRectToBacking:[cw->view bounds]];
    w->fb_width = (int)backing.size.width;
    w->fb_height = (int)backing.size.height;

    return true;
}

static void nack__cocoa_window_destroy(struct nack_window *w)
{
    struct nack_cocoa_window *cw = nack__cocoa_win(w);
    if (!cw)
        return;

    if (cw->cursor_hidden) {
        [NSCursor unhide];
        CGAssociateMouseAndMouseCursorPosition(true);
    }
    if (cw->window) {
        [cw->window setDelegate:nil];
        [cw->window close];
        [cw->window release];
    }
    [cw->delegate release];
    [cw->view release];

    free(cw);
    w->native = NULL;
}

static void nack__cocoa_window_show(struct nack_window *w, bool show)
{
    struct nack_cocoa_window *cw = nack__cocoa_win(w);
    if (show) {
        [cw->window orderFront:nil];
        nack__cocoa_update_size(w);
    } else {
        [cw->window orderOut:nil];
    }
}

static void nack__cocoa_window_focus(struct nack_window *w)
{
    [NSApp activateIgnoringOtherApps:YES];
    [nack__cocoa_win(w)->window makeKeyAndOrderFront:nil];
}

static void nack__cocoa_window_set_title(struct nack_window *w, const char *title)
{
    NSString *string = [NSString stringWithUTF8String:title];
    if (string)
        [nack__cocoa_win(w)->window setTitle:string];
}

static void nack__cocoa_window_set_size(struct nack_window *w, int width, int height)
{
    [nack__cocoa_win(w)->window setContentSize:NSMakeSize(width, height)];
}

static void nack__cocoa_window_set_position(struct nack_window *w, int x, int y)
{
    struct nack_cocoa_window *cw = nack__cocoa_win(w);
    const NSRect content = [cw->window contentRectForFrameRect:[cw->window frame]];
    const CGFloat screen_height = [[cw->window screen] frame].size.height;
    /* Convert our top-left origin back to Cocoa's bottom-left one. */
    const NSRect target = NSMakeRect(x, screen_height - y - content.size.height,
                                     content.size.width, content.size.height);
    [cw->window setFrameOrigin:
        [cw->window frameRectForContentRect:target].origin];
}

static void nack__cocoa_apply_size_hints(struct nack_window *w)
{
    struct nack_cocoa_window *cw = nack__cocoa_win(w);
    [cw->window setContentMinSize:
        NSMakeSize(w->min_width > 0 ? w->min_width : 1,
                   w->min_height > 0 ? w->min_height : 1)];
    [cw->window setContentMaxSize:
        NSMakeSize(w->max_width > 0 ? w->max_width : CGFLOAT_MAX,
                   w->max_height > 0 ? w->max_height : CGFLOAT_MAX)];
    [cw->window setContentResizeIncrements:
        NSMakeSize(w->inc_width > 0 ? w->inc_width : 1,
                   w->inc_height > 0 ? w->inc_height : 1)];
}

static void nack__cocoa_window_set_fullscreen(struct nack_window *w, bool fullscreen)
{
    struct nack_cocoa_window *cw = nack__cocoa_win(w);
    const bool is_fullscreen =
        ([cw->window styleMask] & NSWindowStyleMaskFullScreen) != 0;
    if (fullscreen != is_fullscreen)
        [cw->window toggleFullScreen:nil];
}

static void nack__cocoa_window_minimize(struct nack_window *w)
{
    [nack__cocoa_win(w)->window miniaturize:nil];
}

static void nack__cocoa_window_maximize(struct nack_window *w)
{
    struct nack_cocoa_window *cw = nack__cocoa_win(w);
    if (![cw->window isZoomed])
        [cw->window zoom:nil];
}

static void nack__cocoa_window_restore(struct nack_window *w)
{
    struct nack_cocoa_window *cw = nack__cocoa_win(w);
    if ([cw->window isMiniaturized])
        [cw->window deminiaturize:nil];
    else if ([cw->window isZoomed])
        [cw->window zoom:nil];
}

static void nack__cocoa_window_request_attention(struct nack_window *w)
{
    (void)w;
    [NSApp requestUserAttention:NSInformationalRequest];
}

static void nack__cocoa_window_request_redraw(struct nack_window *w)
{
    [nack__cocoa_win(w)->view setNeedsDisplay:YES];
}

static void nack__cocoa_window_get_native(const struct nack_window *w,
                                          struct nack_native_window *out)
{
    struct nack_cocoa_window *cw = (struct nack_cocoa_window *)w->native;
    out->display = NULL;
    out->surface = cw ? (void *)cw->window : NULL;
    out->view = cw ? (void *)cw->view : NULL;
    out->handle = 0;
}

/* ------------------------------------------------------------------ */
/* Cursor                                                             */
/* ------------------------------------------------------------------ */

static NSCursor *nack__cocoa_cursor_for(enum nack_cursor_shape shape)
{
    switch (shape) {
    case NACK_CURSOR_IBEAM:        return [NSCursor IBeamCursor];
    case NACK_CURSOR_CROSSHAIR:    return [NSCursor crosshairCursor];
    case NACK_CURSOR_HAND:         return [NSCursor pointingHandCursor];
    case NACK_CURSOR_RESIZE_H:     return [NSCursor resizeLeftRightCursor];
    case NACK_CURSOR_RESIZE_V:     return [NSCursor resizeUpDownCursor];
    case NACK_CURSOR_NOT_ALLOWED:  return [NSCursor operationNotAllowedCursor];
    case NACK_CURSOR_RESIZE_ALL:   return [NSCursor closedHandCursor];
    /* macOS ships no public diagonal resize or wait cursor; the arrow is a
     * better answer than a private API that may vanish. */
    case NACK_CURSOR_RESIZE_NWSE:
    case NACK_CURSOR_RESIZE_NESW:
    case NACK_CURSOR_WAIT:
    case NACK_CURSOR_ARROW:
    default:                       return [NSCursor arrowCursor];
    }
}

static void nack__cocoa_set_cursor_shape(struct nack_window *w,
                                         enum nack_cursor_shape shape)
{
    struct nack_cocoa_window *cw = nack__cocoa_win(w);
    cw->cursor = nack__cocoa_cursor_for(shape);
    if (w->cursor_mode == NACK_CURSOR_MODE_NORMAL && w->focused)
        [cw->cursor set];
}

static void nack__cocoa_set_cursor_mode(struct nack_window *w,
                                        enum nack_cursor_mode mode)
{
    struct nack_cocoa_window *cw = nack__cocoa_win(w);

    const bool should_hide = (mode != NACK_CURSOR_MODE_NORMAL);
    if (should_hide != cw->cursor_hidden) {
        if (should_hide)
            [NSCursor hide];
        else
            [NSCursor unhide];
        cw->cursor_hidden = should_hide;
    }

    /* Disassociating the pointer from the cursor is what turns absolute
     * motion into pure deltas; there is no pointer warping to do. */
    CGAssociateMouseAndMouseCursorPosition(mode != NACK_CURSOR_MODE_CAPTURED);

    if (mode == NACK_CURSOR_MODE_CAPTURED) {
        cw->virtual_x = w->mouse_x;
        cw->virtual_y = w->mouse_y;
    } else if (cw->cursor) {
        [cw->cursor set];
    }
}

/* ------------------------------------------------------------------ */
/* Event loop                                                         */
/* ------------------------------------------------------------------ */

/* Marks the dummy event nack__cocoa_wakeup posts as ours. */
#define NACK_COCOA_WAKEUP_SUBTYPE 0x4E41   /* 'NA' */

static void nack__cocoa_drain(NSDate *deadline)
{
    for (;;) {
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:deadline
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (!event)
            break;
        /*
         * Our own wakeup event becomes a queued NACK_WIN_EVENT_WAKEUP rather
         * than going to AppKit, which has nothing to do with it. Breaking the
         * wait is only half of what nack__win_wakeup promises; the caller is
         * waiting for the event itself.
         */
        if ([event type] == NSEventTypeApplicationDefined &&
            [event subtype] == (short)NACK_COCOA_WAKEUP_SUBTYPE) {
            nack__emit_simple(NULL, NACK_WIN_EVENT_WAKEUP);
        } else {
            [NSApp sendEvent:event];
        }
        /* Only the first iteration may block; drain the rest immediately. */
        deadline = [NSDate distantPast];
    }
}

static void nack__cocoa_pump_events(double timeout)
{
    @autoreleasepool {
        nack__cocoa_drain([NSDate distantPast]);

        if (!nack__queue_empty() || timeout == 0.0)
            return;

        NSDate *deadline = (timeout < 0.0)
                               ? [NSDate distantFuture]
                               : [NSDate dateWithTimeIntervalSinceNow:timeout];
        nack__cocoa_drain(deadline);
    }
}

static void nack__cocoa_wakeup(void)
{
    @autoreleasepool {
        /*
         * Posting a dummy event is the documented way to break
         * nextEventMatchingMask out of a blocking wait, and it is safe from
         * any thread.
         */
        NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                            location:NSMakePoint(0, 0)
                                       modifierFlags:0
                                           timestamp:0
                                        windowNumber:0
                                             context:nil
                                             subtype:(short)NACK_COCOA_WAKEUP_SUBTYPE
                                               data1:0
                                               data2:0];
        [NSApp postEvent:event atStart:YES];
    }
}

/* ------------------------------------------------------------------ */
/* Clipboard                                                          */
/* ------------------------------------------------------------------ */

static bool nack__cocoa_clipboard_set(const char *utf8)
{
    @autoreleasepool {
        NSString *string = [NSString stringWithUTF8String:utf8];
        if (!string)
            return nack__fail(NACK_ERROR_INVALID_ARGUMENT,
                              "clipboard text is not valid UTF-8");
        NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        if (![pasteboard setString:string forType:NSPasteboardTypeString])
            return nack__fail(NACK_ERROR_PLATFORM, "NSPasteboard setString failed");
        nack__cocoa.clipboard_change_count = [pasteboard changeCount];
        return true;
    }
}

static const char *nack__cocoa_clipboard_get(void)
{
    @autoreleasepool {
        NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
        NSString *string = [pasteboard stringForType:NSPasteboardTypeString];
        if (!string)
            return NULL;

        const char *utf8 = [string UTF8String];
        if (!utf8)
            return NULL;

        free(nack__cocoa.clipboard_text);
        nack__cocoa.clipboard_text = nack__strdup(utf8);
        return nack__cocoa.clipboard_text;
    }
}

/* ------------------------------------------------------------------ */
/* Init / shutdown                                                    */
/* ------------------------------------------------------------------ */

static void nack__cocoa_create_menu_bar(void)
{
    /*
     * A process launched outside a bundle has no menu bar, and without at
     * least an application menu the standard Command shortcuts (including
     * Command-Q) do nothing.
     */
    NSMenu *bar = [[NSMenu alloc] init];
    [NSApp setMainMenu:bar];

    NSMenuItem *app_item = [bar addItemWithTitle:@"" action:NULL keyEquivalent:@""];
    NSMenu *app_menu = [[NSMenu alloc] init];
    [app_item setSubmenu:app_menu];

    NSString *name = [[NSProcessInfo processInfo] processName];

    [app_menu addItemWithTitle:[NSString stringWithFormat:@"About %@", name]
                        action:@selector(orderFrontStandardAboutPanel:)
                 keyEquivalent:@""];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItemWithTitle:[NSString stringWithFormat:@"Hide %@", name]
                        action:@selector(hide:)
                 keyEquivalent:@"h"];
    [[app_menu addItemWithTitle:@"Hide Others"
                         action:@selector(hideOtherApplications:)
                  keyEquivalent:@"h"]
        setKeyEquivalentModifierMask:NSEventModifierFlagOption |
                                     NSEventModifierFlagCommand];
    [app_menu addItemWithTitle:@"Show All"
                        action:@selector(unhideAllApplications:)
                 keyEquivalent:@""];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItemWithTitle:[NSString stringWithFormat:@"Quit %@", name]
                        action:@selector(terminate:)
                 keyEquivalent:@"q"];

    [app_menu release];
    [bar release];
}

static bool nack__cocoa_init(const struct nack_win_init_desc *desc)
{
    (void)desc;
    memset(&nack__cocoa, 0, sizeof nack__cocoa);

    @autoreleasepool {
        [NSApplication sharedApplication];

        /*
         * Regular activation policy gives an unbundled binary a Dock icon and
         * the ability to take keyboard focus, which it otherwise cannot.
         */
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        nack__cocoa.app_delegate = [[NackApplicationDelegate alloc] init];
        [NSApp setDelegate:nack__cocoa.app_delegate];

        if (![NSApp mainMenu])
            nack__cocoa_create_menu_bar();

        /* Run once so AppKit finishes launching; the delegate stops it. */
        if (![NSApp isRunning])
            [NSApp run];

        [NSApp activateIgnoringOtherApps:YES];

        nack__cocoa_build_keycodes();
    }
    return true;
}

static void nack__cocoa_shutdown(void)
{
    @autoreleasepool {
        nack__nsgl_terminate();
        [NSApp setDelegate:nil];
        [nack__cocoa.app_delegate release];
        free(nack__cocoa.clipboard_text);
        memset(&nack__cocoa, 0, sizeof nack__cocoa);
    }
}

/* ------------------------------------------------------------------ */

static const struct nack_backend_vt nack__cocoa_vt_template = {
    .name = "cocoa",
    .id = NACK_BACKEND_COCOA,
    .init = nack__cocoa_init,
    .shutdown = nack__cocoa_shutdown,
    .window_create = nack__cocoa_window_create,
    .window_destroy = nack__cocoa_window_destroy,
    .window_show = nack__cocoa_window_show,
    .window_focus = nack__cocoa_window_focus,
    .window_set_title = nack__cocoa_window_set_title,
    .window_set_size = nack__cocoa_window_set_size,
    .window_set_position = nack__cocoa_window_set_position,
    .window_apply_size_hints = nack__cocoa_apply_size_hints,
    .window_set_fullscreen = nack__cocoa_window_set_fullscreen,
    .window_minimize = nack__cocoa_window_minimize,
    .window_maximize = nack__cocoa_window_maximize,
    .window_restore = nack__cocoa_window_restore,
    .window_request_attention = nack__cocoa_window_request_attention,
    .window_request_redraw = nack__cocoa_window_request_redraw,
    .window_set_cursor_shape = nack__cocoa_set_cursor_shape,
    .window_set_cursor_mode = nack__cocoa_set_cursor_mode,
    .window_get_native = nack__cocoa_window_get_native,
    .pump_events = nack__cocoa_pump_events,
    .wakeup = nack__cocoa_wakeup,
    .gl_create = NULL,          /* patched in by nack__backend_cocoa */
    .gl_destroy = nack__nsgl_destroy_context,
    .gl_make_current = nack__nsgl_make_current,
    .gl_swap_buffers = nack__nsgl_swap_buffers,
    .gl_set_swap_interval = nack__nsgl_set_swap_interval,
    .gl_get_proc_address = nack__nsgl_get_proc_address,
    .clipboard_set = nack__cocoa_clipboard_set,
    .clipboard_get = nack__cocoa_clipboard_get,
    .primary_set = NULL,   /* macOS has no primary selection */
    .primary_get = NULL,
};

static struct nack_gl_context *nack__cocoa_gl_create(struct nack_window *w,
                                              const struct nack__gl_desc *desc);

const struct nack_backend_vt *nack__backend_cocoa(void)
{
    /* gl_create needs the vtable's own address, so it is patched in on first
     * use rather than written as a static initialiser. */
    static struct nack_backend_vt vt;
    static bool ready = false;
    if (!ready) {
        vt = nack__cocoa_vt_template;
        vt.gl_create = nack__cocoa_gl_create;
        ready = true;
    }
    return &vt;
}

static struct nack_gl_context *nack__cocoa_gl_create(struct nack_window *w,
                                              const struct nack__gl_desc *desc)
{
    return nack__nsgl_create_context(w, desc, nack__backend_cocoa());
}
