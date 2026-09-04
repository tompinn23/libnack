#ifndef NACK_WIN32_H_INCLUDED
#define NACK_WIN32_H_INCLUDED

#include "../nack_internal.h"
#include <optional>
#include <string>

#if defined(NACK_WIN32_USE_SDK_HEADERS)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef UNICODE
#    define UNICODE 1
#  endif
#  ifndef _UNICODE
#    define _UNICODE 1
#  endif
#  include <windows.h>
#  include <windowsx.h>
#else
#  include "nack_win32_api.h"
#endif

#define NACK_WIN32_CLASS_NAME L"nack_window_class"

/* WM_ codes above WM_APP are ours to use. */
#define NACK_WM_WAKEUP (WM_APP + 1)

struct nack_win32_window {
    HWND hwnd;
    HDC hdc;
    HGLRC glrc;                 /* set once a context is made current here  */
    int pixel_format;
    bool cursor_tracked;        /* TrackMouseEvent armed for WM_MOUSELEAVE  */
    bool in_size_move;          /* inside a modal move/resize loop          */
    bool frame_action;
    UINT dpi;
    RECT restore_rect;
    DWORD restore_style;
    DWORD restore_ex_style;
    WCHAR high_surrogate;       /* pending half of a UTF-16 pair from WM_CHAR */
    int captured_center_x, captured_center_y;
    bool cursor_clipped;
};

struct nack_win32_state {
    HINSTANCE instance;
    DWORD main_thread;
    HCURSOR cursors[NACK_CURSOR_SHAPE_COUNT];
    bool cursors_loaded[NACK_CURSOR_SHAPE_COUNT];
    std::optional<std::string> clipboard_text;
    bool class_registered;

    /* Per-monitor DPI entry points, resolved at run time so the library still
     * loads on Windows versions that predate them. */
    HMODULE user32;
    HMODULE shcore;
    UINT (WINAPI *GetDpiForWindow_)(HWND);
    BOOL (WINAPI *AdjustWindowRectExForDpi_)(LPRECT, DWORD, BOOL, DWORD, UINT);
    BOOL (WINAPI *EnableNonClientDpiScaling_)(HWND);
    HRESULT (WINAPI *SetProcessDpiAwareness_)(int);
    BOOL (WINAPI *SetProcessDpiAwarenessContext_)(void *);
};

namespace nack { namespace detail {

extern nack_win32_state win32;

static inline nack_win32_window *win32_win(nack_window *w)
{
    return (nack_win32_window *)w->native;
}

/* nack_wgl.c */
bool  wgl_init(void);
void  wgl_terminate(void);
bool  wgl_choose_pixel_format(nack_window *w, HDC hdc, int *out_format);
nack_gl_context *wgl_create_context(nack_window *w, const gl_desc *desc,
                                          nack_backend_vt *vt);
void  wgl_destroy_context(nack_gl_context *ctx);
bool  wgl_make_current(nack_window *w, nack_gl_context *ctx);
void  wgl_swap_buffers(nack_window *w);
void  wgl_set_swap_interval(int interval);
void *wgl_get_proc_address(const char *name);

/* Shared helpers */
std::optional<std::wstring> win32_utf8_to_wide(const char *utf8);
std::optional<std::string> win32_wide_to_utf8(const WCHAR *wide);

} }   /* namespace nack::detail */

#endif /* NACK_WIN32_H_INCLUDED */
