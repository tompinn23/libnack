/* libnack - Win32 backend. */
#include "nack_win32.h"

#include "../nack_scoped.h"

#include <stdio.h>

namespace nack { namespace detail {

nack_win32_state win32;

#define NACK_DEFAULT_DPI 96

/* ------------------------------------------------------------------ */
/* String conversion                                                  */
/* ------------------------------------------------------------------ */

std::optional<std::wstring> win32_utf8_to_wide(const char *utf8)
{
    if (!utf8)
        return std::nullopt;
    int count = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (count <= 0)
        return std::nullopt;
    std::wstring wide((size_t)count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), count))
        return std::nullopt;
    wide.resize((size_t)count - 1);   /* drop the NUL the API wrote in */
    return wide;
}

std::optional<std::string> win32_wide_to_utf8(const WCHAR *wide)
{
    if (!wide)
        return std::nullopt;
    int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (count <= 0)
        return std::nullopt;
    std::string utf8((size_t)count, '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), count, NULL, NULL))
        return std::nullopt;
    utf8.resize((size_t)count - 1);   /* drop the NUL the API wrote in */
    return utf8;
}

/* ------------------------------------------------------------------ */
/* Key mapping                                                        */
/* ------------------------------------------------------------------ */

/*
 * Indexed by set-1 scan code, so the mapping follows the key's position and
 * not the active layout. Extended keys (the 0xE0 prefix) are resolved
 * separately in win32_key_from_message.
 */
static nack_key win32_scancodes[512];

static void win32_build_keycodes(void)
{
    memset(win32_scancodes, 0, sizeof win32_scancodes);

    win32_scancodes[0x01] = NACK_KEY_ESCAPE;
    win32_scancodes[0x02] = NACK_KEY_1;
    win32_scancodes[0x03] = NACK_KEY_2;
    win32_scancodes[0x04] = NACK_KEY_3;
    win32_scancodes[0x05] = NACK_KEY_4;
    win32_scancodes[0x06] = NACK_KEY_5;
    win32_scancodes[0x07] = NACK_KEY_6;
    win32_scancodes[0x08] = NACK_KEY_7;
    win32_scancodes[0x09] = NACK_KEY_8;
    win32_scancodes[0x0A] = NACK_KEY_9;
    win32_scancodes[0x0B] = NACK_KEY_0;
    win32_scancodes[0x0C] = NACK_KEY_MINUS;
    win32_scancodes[0x0D] = NACK_KEY_EQUAL;
    win32_scancodes[0x0E] = NACK_KEY_BACKSPACE;
    win32_scancodes[0x0F] = NACK_KEY_TAB;
    win32_scancodes[0x10] = NACK_KEY_Q;
    win32_scancodes[0x11] = NACK_KEY_W;
    win32_scancodes[0x12] = NACK_KEY_E;
    win32_scancodes[0x13] = NACK_KEY_R;
    win32_scancodes[0x14] = NACK_KEY_T;
    win32_scancodes[0x15] = NACK_KEY_Y;
    win32_scancodes[0x16] = NACK_KEY_U;
    win32_scancodes[0x17] = NACK_KEY_I;
    win32_scancodes[0x18] = NACK_KEY_O;
    win32_scancodes[0x19] = NACK_KEY_P;
    win32_scancodes[0x1A] = NACK_KEY_LEFT_BRACKET;
    win32_scancodes[0x1B] = NACK_KEY_RIGHT_BRACKET;
    win32_scancodes[0x1C] = NACK_KEY_ENTER;
    win32_scancodes[0x1D] = NACK_KEY_LEFT_CTRL;
    win32_scancodes[0x1E] = NACK_KEY_A;
    win32_scancodes[0x1F] = NACK_KEY_S;
    win32_scancodes[0x20] = NACK_KEY_D;
    win32_scancodes[0x21] = NACK_KEY_F;
    win32_scancodes[0x22] = NACK_KEY_G;
    win32_scancodes[0x23] = NACK_KEY_H;
    win32_scancodes[0x24] = NACK_KEY_J;
    win32_scancodes[0x25] = NACK_KEY_K;
    win32_scancodes[0x26] = NACK_KEY_L;
    win32_scancodes[0x27] = NACK_KEY_SEMICOLON;
    win32_scancodes[0x28] = NACK_KEY_APOSTROPHE;
    win32_scancodes[0x29] = NACK_KEY_GRAVE;
    win32_scancodes[0x2A] = NACK_KEY_LEFT_SHIFT;
    win32_scancodes[0x2B] = NACK_KEY_BACKSLASH;
    win32_scancodes[0x2C] = NACK_KEY_Z;
    win32_scancodes[0x2D] = NACK_KEY_X;
    win32_scancodes[0x2E] = NACK_KEY_C;
    win32_scancodes[0x2F] = NACK_KEY_V;
    win32_scancodes[0x30] = NACK_KEY_B;
    win32_scancodes[0x31] = NACK_KEY_N;
    win32_scancodes[0x32] = NACK_KEY_M;
    win32_scancodes[0x33] = NACK_KEY_COMMA;
    win32_scancodes[0x34] = NACK_KEY_PERIOD;
    win32_scancodes[0x35] = NACK_KEY_SLASH;
    win32_scancodes[0x36] = NACK_KEY_RIGHT_SHIFT;
    win32_scancodes[0x37] = NACK_KEY_KP_MULTIPLY;
    win32_scancodes[0x38] = NACK_KEY_LEFT_ALT;
    win32_scancodes[0x39] = NACK_KEY_SPACE;
    win32_scancodes[0x3A] = NACK_KEY_CAPS_LOCK;
    win32_scancodes[0x3B] = NACK_KEY_F1;
    win32_scancodes[0x3C] = NACK_KEY_F2;
    win32_scancodes[0x3D] = NACK_KEY_F3;
    win32_scancodes[0x3E] = NACK_KEY_F4;
    win32_scancodes[0x3F] = NACK_KEY_F5;
    win32_scancodes[0x40] = NACK_KEY_F6;
    win32_scancodes[0x41] = NACK_KEY_F7;
    win32_scancodes[0x42] = NACK_KEY_F8;
    win32_scancodes[0x43] = NACK_KEY_F9;
    win32_scancodes[0x44] = NACK_KEY_F10;
    win32_scancodes[0x45] = NACK_KEY_NUM_LOCK;
    win32_scancodes[0x46] = NACK_KEY_SCROLL_LOCK;
    win32_scancodes[0x47] = NACK_KEY_KP_7;
    win32_scancodes[0x48] = NACK_KEY_KP_8;
    win32_scancodes[0x49] = NACK_KEY_KP_9;
    win32_scancodes[0x4A] = NACK_KEY_KP_SUBTRACT;
    win32_scancodes[0x4B] = NACK_KEY_KP_4;
    win32_scancodes[0x4C] = NACK_KEY_KP_5;
    win32_scancodes[0x4D] = NACK_KEY_KP_6;
    win32_scancodes[0x4E] = NACK_KEY_KP_ADD;
    win32_scancodes[0x4F] = NACK_KEY_KP_1;
    win32_scancodes[0x50] = NACK_KEY_KP_2;
    win32_scancodes[0x51] = NACK_KEY_KP_3;
    win32_scancodes[0x52] = NACK_KEY_KP_0;
    win32_scancodes[0x53] = NACK_KEY_KP_DECIMAL;
    win32_scancodes[0x56] = NACK_KEY_NON_US_BACKSLASH;
    win32_scancodes[0x57] = NACK_KEY_F11;
    win32_scancodes[0x58] = NACK_KEY_F12;
    win32_scancodes[0x64] = NACK_KEY_F13;
    win32_scancodes[0x65] = NACK_KEY_F14;
    win32_scancodes[0x66] = NACK_KEY_F15;
    win32_scancodes[0x67] = NACK_KEY_F16;
    win32_scancodes[0x68] = NACK_KEY_F17;
    win32_scancodes[0x69] = NACK_KEY_F18;
    win32_scancodes[0x6A] = NACK_KEY_F19;
    win32_scancodes[0x6B] = NACK_KEY_F20;
    win32_scancodes[0x6C] = NACK_KEY_F21;
    win32_scancodes[0x6D] = NACK_KEY_F22;
    win32_scancodes[0x6E] = NACK_KEY_F23;
    win32_scancodes[0x76] = NACK_KEY_F24;

    /* Extended keys, stored at 0x100 + scancode. */
    win32_scancodes[0x100 + 0x1C] = NACK_KEY_KP_ENTER;
    win32_scancodes[0x100 + 0x1D] = NACK_KEY_RIGHT_CTRL;
    win32_scancodes[0x100 + 0x35] = NACK_KEY_KP_DIVIDE;
    win32_scancodes[0x100 + 0x37] = NACK_KEY_PRINT_SCREEN;
    win32_scancodes[0x100 + 0x38] = NACK_KEY_RIGHT_ALT;
    win32_scancodes[0x100 + 0x45] = NACK_KEY_NUM_LOCK;
    win32_scancodes[0x100 + 0x46] = NACK_KEY_PAUSE;
    win32_scancodes[0x100 + 0x47] = NACK_KEY_HOME;
    win32_scancodes[0x100 + 0x48] = NACK_KEY_UP;
    win32_scancodes[0x100 + 0x49] = NACK_KEY_PAGE_UP;
    win32_scancodes[0x100 + 0x4B] = NACK_KEY_LEFT;
    win32_scancodes[0x100 + 0x4D] = NACK_KEY_RIGHT;
    win32_scancodes[0x100 + 0x4F] = NACK_KEY_END;
    win32_scancodes[0x100 + 0x50] = NACK_KEY_DOWN;
    win32_scancodes[0x100 + 0x51] = NACK_KEY_PAGE_DOWN;
    win32_scancodes[0x100 + 0x52] = NACK_KEY_INSERT;
    win32_scancodes[0x100 + 0x53] = NACK_KEY_DELETE;
    win32_scancodes[0x100 + 0x5B] = NACK_KEY_LEFT_SUPER;
    win32_scancodes[0x100 + 0x5C] = NACK_KEY_RIGHT_SUPER;
    win32_scancodes[0x100 + 0x5D] = NACK_KEY_APPLICATION;
    win32_scancodes[0x100 + 0x20] = NACK_KEY_MUTE;
    win32_scancodes[0x100 + 0x2E] = NACK_KEY_VOLUME_DOWN;
    win32_scancodes[0x100 + 0x30] = NACK_KEY_VOLUME_UP;
}

static nack_key win32_key_from_message(WPARAM wparam, LPARAM lparam)
{
    UINT scancode = (UINT)((lparam >> 16) & 0x1FF);   /* includes the 0x100 bit */

    /* Pause reports scancode 0x45 without the extended bit; Num Lock reports
     * 0x45 with it. The virtual key disambiguates. */
    if (wparam == VK_PAUSE)
        return NACK_KEY_PAUSE;
    if (wparam == VK_NUMLOCK)
        return NACK_KEY_NUM_LOCK;

    if (scancode < 512 && win32_scancodes[scancode] != NACK_KEY_UNKNOWN)
        return win32_scancodes[scancode];

    /* Fall back to the virtual key for anything the table misses. */
    switch (wparam) {
    case VK_ESCAPE:  return NACK_KEY_ESCAPE;
    case VK_RETURN:  return NACK_KEY_ENTER;
    case VK_TAB:     return NACK_KEY_TAB;
    case VK_BACK:    return NACK_KEY_BACKSPACE;
    case VK_SPACE:   return NACK_KEY_SPACE;
    default: break;
    }
    return NACK_KEY_UNKNOWN;
}

static uint32_t win32_mods(void)
{
    uint32_t mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)   mods |= NACK_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= NACK_MOD_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000)    mods |= NACK_MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        mods |= NACK_MOD_SUPER;
    if (GetKeyState(VK_CAPITAL) & 1)      mods |= NACK_MOD_CAPSLOCK;
    if (GetKeyState(VK_NUMLOCK) & 1)      mods |= NACK_MOD_NUMLOCK;
    return mods;
}

/* ------------------------------------------------------------------ */
/* DPI                                                                */
/* ------------------------------------------------------------------ */

static UINT win32_dpi_for_window(HWND hwnd)
{
    if (win32.GetDpiForWindow_)
        return win32.GetDpiForWindow_(hwnd);

    HDC hdc = GetDC(hwnd);
    UINT dpi = hdc ? (UINT)GetDeviceCaps(hdc, LOGPIXELSX) : NACK_DEFAULT_DPI;
    if (hdc)
        ReleaseDC(hwnd, hdc);
    return dpi ? dpi : NACK_DEFAULT_DPI;
}

static void win32_adjust_rect(nack_window *w, RECT *rect, DWORD style,
                                    DWORD ex_style, UINT dpi)
{
    (void)w;
    if (win32.AdjustWindowRectExForDpi_)
        win32.AdjustWindowRectExForDpi_(rect, style, FALSE, ex_style, dpi);
    else
        AdjustWindowRectEx(rect, style, FALSE, ex_style);
}

static DWORD win32_style(const nack_window *w)
{
    DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    if (!w->decorated)
        return style | WS_POPUP;
    style |= WS_SYSMENU | WS_MINIMIZEBOX | WS_CAPTION;
    if (w->resizable)
        style |= WS_MAXIMIZEBOX | WS_THICKFRAME;
    return style;
}

static DWORD win32_ex_style(const nack_window *w)
{
    DWORD ex_style = WS_EX_APPWINDOW;
    if (w->transparent)
        ex_style |= WS_EX_LAYERED;
    return ex_style;
}

/* ------------------------------------------------------------------ */
/* Cursors                                                            */
/* ------------------------------------------------------------------ */

static const LPCWSTR win32_cursor_ids[NACK_CURSOR_SHAPE_COUNT] = {
    IDC_ARROW, IDC_IBEAM, IDC_CROSS, IDC_HAND,
    IDC_SIZEWE, IDC_SIZENS, IDC_SIZENWSE, IDC_SIZENESW,
    IDC_SIZEALL, IDC_NO, IDC_WAIT,
};

static HCURSOR win32_get_cursor(nack_cursor_shape shape)
{
    if (!win32.cursors_loaded[shape]) {
        win32.cursors[shape] =
            LoadCursorW(NULL, win32_cursor_ids[shape]);
        win32.cursors_loaded[shape] = true;
    }
    return win32.cursors[shape];
}

static void win32_apply_cursor(nack_window *w)
{
    if (w->cursor_mode == NACK_CURSOR_MODE_NORMAL)
        SetCursor(win32_get_cursor(w->cursor_shape));
    else
        SetCursor(NULL);
}

static void win32_clip_cursor(nack_window *w, bool clip)
{
    nack_win32_window *ww = win32_win(w);
    if (clip) {
        RECT rect;
        GetClientRect(ww->hwnd, &rect);
        ClientToScreen(ww->hwnd, (POINT *)&rect.left);
        ClientToScreen(ww->hwnd, (POINT *)&rect.right);
        ClipCursor(&rect);
        ww->captured_center_x = (rect.left + rect.right) / 2;
        ww->captured_center_y = (rect.top + rect.bottom) / 2;
        SetCursorPos(ww->captured_center_x, ww->captured_center_y);
        ww->cursor_clipped = true;
    } else if (ww->cursor_clipped) {
        ClipCursor(NULL);
        ww->cursor_clipped = false;
    }
}

static void win32_set_cursor_mode(nack_window *w,
                                        nack_cursor_mode mode)
{
    win32_clip_cursor(w, mode == NACK_CURSOR_MODE_CAPTURED);
    win32_apply_cursor(w);
}

static void win32_set_cursor_shape(nack_window *w,
                                         nack_cursor_shape shape)
{
    (void)shape;
    win32_apply_cursor(w);
}

/* ------------------------------------------------------------------ */
/* Window procedure                                                   */
/* ------------------------------------------------------------------ */

static void win32_track_mouse_leave(nack_window *w)
{
    nack_win32_window *ww = win32_win(w);
    if (ww->cursor_tracked)
        return;
    TRACKMOUSEEVENT track;
    memset(&track, 0, sizeof track);
    track.cbSize = sizeof track;
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = ww->hwnd;
    TrackMouseEvent(&track);
    ww->cursor_tracked = true;
}

static void win32_update_size(nack_window *w)
{
    nack_win32_window *ww = win32_win(w);
    RECT rect;
    GetClientRect(ww->hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    /* The client area is already in physical pixels, which is what the
     * framebuffer wants; the logical size is that divided by the scale. */
    w->emit_resize(width, height, width, height);
}

static LRESULT CALLBACK win32_wndproc(HWND hwnd, UINT msg, WPARAM wparam,
                                            LPARAM lparam)
{
    nack_window *w = (nack_window *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!w) {
        if (msg == WM_NCCREATE) {
            /* Opt the non-client area into per-monitor DPI scaling before the
             * frame is created, or the caption is sized for the wrong DPI. */
            if (win32.EnableNonClientDpiScaling_)
                win32.EnableNonClientDpiScaling_(hwnd);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    nack_win32_window *ww = win32_win(w);

    switch (msg) {
    case WM_CLOSE:
        w->should_close = true;
        w->emit_simple(NACK_WIN_EVENT_WINDOW_CLOSE);
        return 0;   /* the application decides when to destroy the window */

    case WM_ERASEBKGND:
        return 1;   /* GL owns the client area; erasing it only flickers */

    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        w->emit_simple(NACK_WIN_EVENT_WINDOW_EXPOSE);
        return 0;
    }

    case WM_SETFOCUS:
        w->emit_focus(true);
        if (w->cursor_mode == NACK_CURSOR_MODE_CAPTURED)
            win32_clip_cursor(w, true);
        return 0;

    case WM_KILLFOCUS:
        if (w->cursor_mode == NACK_CURSOR_MODE_CAPTURED)
            win32_clip_cursor(w, false);
        w->emit_focus(false);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            win32_apply_cursor(w);
            return TRUE;
        }
        break;

    case WM_SIZE: {
        bool minimized = (wparam == SIZE_MINIMIZED);
        bool maximized = (wparam == SIZE_MAXIMIZED);
        if (minimized != w->minimized) {
            w->minimized = minimized;
            w->emit_simple(minimized ? NACK_WIN_EVENT_WINDOW_MINIMIZE
                                           : NACK_WIN_EVENT_WINDOW_RESTORE);
        }
        if (maximized != w->maximized) {
            w->maximized = maximized;
            w->emit_simple(maximized ? NACK_WIN_EVENT_WINDOW_MAXIMIZE
                                           : NACK_WIN_EVENT_WINDOW_RESTORE);
        }
        if (!minimized) {
            int width = LOWORD(lparam), height = HIWORD(lparam);
            w->emit_resize(width, height, width, height);
        }
        if (w->cursor_mode == NACK_CURSOR_MODE_CAPTURED)
            win32_clip_cursor(w, true);
        return 0;
    }

    case WM_MOVE: {
        int x = (int)(short)LOWORD(lparam);
        int y = (int)(short)HIWORD(lparam);
        if (x != w->pos_x || y != w->pos_y) {
            w->pos_x = x;
            w->pos_y = y;
            nack_win_event *ev = state.event_begin(NACK_WIN_EVENT_WINDOW_MOVE, w);
            ev->data.move.x = x;
            ev->data.move.y = y;
            state.push_event(ev);
        }
        return 0;
    }

    case WM_ENTERSIZEMOVE:
        ww->in_size_move = true;
        /*
         * A modal move/resize loop does not return to our message pump, so
         * without a timer the window would freeze while being dragged. The
         * timer lets WM_TIMER drive redraws for the duration.
         */
        SetTimer(hwnd, 1, USER_TIMER_MINIMUM, NULL);
        return 0;

    case WM_EXITSIZEMOVE:
        ww->in_size_move = false;
        KillTimer(hwnd, 1);
        return 0;

    case WM_TIMER:
        if (wparam == 1 && ww->in_size_move) {
            win32_update_size(w);
            w->emit_simple(NACK_WIN_EVENT_WINDOW_EXPOSE);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lparam;
        DWORD style = win32_style(w);
        DWORD ex_style = win32_ex_style(w);

        if (w->min_width > 0 || w->min_height > 0) {
            RECT rect = { 0, 0, w->min_width, w->min_height };
            win32_adjust_rect(w, &rect, style, ex_style, ww->dpi);
            info->ptMinTrackSize.x = rect.right - rect.left;
            info->ptMinTrackSize.y = rect.bottom - rect.top;
        }
        if (w->max_width > 0 || w->max_height > 0) {
            RECT rect = { 0, 0,
                          w->max_width > 0 ? w->max_width : 65535,
                          w->max_height > 0 ? w->max_height : 65535 };
            win32_adjust_rect(w, &rect, style, ex_style, ww->dpi);
            info->ptMaxTrackSize.x = rect.right - rect.left;
            info->ptMaxTrackSize.y = rect.bottom - rect.top;
        }
        return 0;
    }

    case WM_SIZING: {
        /*
         * Windows has no resize-increment hint, so snapping to the cell grid
         * has to happen here, on the rectangle the user is dragging.
         */
        if (w->inc_width <= 1 && w->inc_height <= 1)
            break;

        RECT *rect = (RECT *)lparam;
        RECT frame = { 0, 0, 0, 0 };
        win32_adjust_rect(w, &frame, win32_style(w),
                                win32_ex_style(w), ww->dpi);
        int frame_w = frame.right - frame.left;
        int frame_h = frame.bottom - frame.top;

        int client_w = (rect->right - rect->left) - frame_w;
        int client_h = (rect->bottom - rect->top) - frame_h;

        if (w->inc_width > 1) {
            int base = w->min_width > 0 ? w->min_width : 0;
            int snapped = base + ((client_w - base) / w->inc_width) * w->inc_width;
            if (snapped < base + w->inc_width)
                snapped = base + w->inc_width;
            client_w = snapped;
        }
        if (w->inc_height > 1) {
            int base = w->min_height > 0 ? w->min_height : 0;
            int snapped = base + ((client_h - base) / w->inc_height) * w->inc_height;
            if (snapped < base + w->inc_height)
                snapped = base + w->inc_height;
            client_h = snapped;
        }

        /* Grow from whichever edge is not being dragged. */
        switch (wparam) {
        case WMSZ_LEFT: case WMSZ_TOPLEFT: case WMSZ_BOTTOMLEFT:
            rect->left = rect->right - client_w - frame_w;
            break;
        default:
            rect->right = rect->left + client_w + frame_w;
            break;
        }
        switch (wparam) {
        case WMSZ_TOP: case WMSZ_TOPLEFT: case WMSZ_TOPRIGHT:
            rect->top = rect->bottom - client_h - frame_h;
            break;
        default:
            rect->bottom = rect->top + client_h + frame_h;
            break;
        }
        return TRUE;
    }

    case WM_DPICHANGED: {
        UINT dpi = HIWORD(wparam);
        ww->dpi = dpi;
        if (w->high_dpi) {
            /* Windows supplies the rectangle that keeps the window the same
             * physical size on the new monitor. */
            const RECT *suggested = (const RECT *)lparam;
            SetWindowPos(hwnd, HWND_TOP, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
        w->emit_scale((float)dpi / (float)NACK_DEFAULT_DPI);
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        bool repeat = down && ((lparam >> 30) & 1) != 0;
        nack_key key = win32_key_from_message(wparam, lparam);
        UINT scancode = (UINT)((lparam >> 16) & 0x1FF);

        if (wparam == VK_SHIFT && !down) {
            /*
             * Windows does not send a WM_KEYUP for the second shift when both
             * are held, so release whichever ones are no longer down.
             */
            if (state.keys[NACK_KEY_LEFT_SHIFT] &&
                !(GetKeyState(VK_LSHIFT) & 0x8000))
                w->emit_key(NACK_KEY_LEFT_SHIFT, scancode,
                               win32_mods(), false, false);
            if (state.keys[NACK_KEY_RIGHT_SHIFT] &&
                !(GetKeyState(VK_RSHIFT) & 0x8000))
                w->emit_key(NACK_KEY_RIGHT_SHIFT, scancode,
                               win32_mods(), false, false);
            return 0;
        }

        w->emit_key(key, scancode, win32_mods(), down, repeat);

        /* Alt and F10 open the window menu unless we swallow them. */
        if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
            break;
        return 0;
    }

    case WM_CHAR:
    case WM_SYSCHAR: {
        WCHAR unit = (WCHAR)wparam;
        uint32_t codepoint;

        /* WM_CHAR delivers UTF-16, so a non-BMP character arrives as two
         * messages that have to be recombined. */
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            ww->high_surrogate = unit;
            return 0;
        }
        if (unit >= 0xDC00 && unit <= 0xDFFF) {
            if (!ww->high_surrogate)
                return 0;
            codepoint = 0x10000u +
                        (((uint32_t)ww->high_surrogate - 0xD800u) << 10) +
                        ((uint32_t)unit - 0xDC00u);
            ww->high_surrogate = 0;
        } else {
            ww->high_surrogate = 0;
            codepoint = unit;
        }

        if ((win32_mods() & (NACK_MOD_CTRL | NACK_MOD_SUPER)) == 0 &&
            codepoint_is_text(codepoint)) {
            char utf8[5];
            utf8_encode(codepoint, utf8);
            w->emit_text(utf8);
        }
        if (msg == WM_SYSCHAR)
            break;
        return 0;
    }

    case WM_UNICHAR:
        if (wparam == UNICODE_NOCHAR)
            return TRUE;   /* tell the sender we accept WM_UNICHAR */
        if (codepoint_is_text((uint32_t)wparam)) {
            char utf8[5];
            utf8_encode((uint32_t)wparam, utf8);
            w->emit_text(utf8);
        }
        return 0;

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lparam);
        int y = GET_Y_LPARAM(lparam);

        if (!ww->cursor_tracked) {
            win32_track_mouse_leave(w);
            w->emit_simple(NACK_WIN_EVENT_MOUSE_ENTER);
        }

        if (w->cursor_mode == NACK_CURSOR_MODE_CAPTURED) {
            POINT screen = { x, y };
            ClientToScreen(hwnd, &screen);
            int dx = screen.x - ww->captured_center_x;
            int dy = screen.y - ww->captured_center_y;
            if (dx == 0 && dy == 0)
                return 0;   /* this is the recentring move, not user input */

            nack_win_event *ev = state.event_begin(NACK_WIN_EVENT_MOUSE_MOVE, w);
            w->mouse_x += dx;
            w->mouse_y += dy;
            ev->data.motion.x = w->mouse_x;
            ev->data.motion.y = w->mouse_y;
            ev->data.motion.dx = dx;
            ev->data.motion.dy = dy;
            ev->data.motion.mods = win32_mods();
            state.push_event(ev);

            SetCursorPos(ww->captured_center_x, ww->captured_center_y);
            return 0;
        }

        w->emit_mouse_move(x, y, win32_mods());
        return 0;
    }

    case WM_MOUSELEAVE:
        ww->cursor_tracked = false;
        w->emit_simple(NACK_WIN_EVENT_MOUSE_LEAVE);
        return 0;

    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: {
        int button;
        bool down;
        switch (msg) {
        case WM_LBUTTONDOWN: button = NACK_MOUSE_LEFT;   down = true;  break;
        case WM_LBUTTONUP:   button = NACK_MOUSE_LEFT;   down = false; break;
        case WM_RBUTTONDOWN: button = NACK_MOUSE_RIGHT;  down = true;  break;
        case WM_RBUTTONUP:   button = NACK_MOUSE_RIGHT;  down = false; break;
        case WM_MBUTTONDOWN: button = NACK_MOUSE_MIDDLE; down = true;  break;
        case WM_MBUTTONUP:   button = NACK_MOUSE_MIDDLE; down = false; break;
        default:
            button = (GET_XBUTTON_WPARAM(wparam) == XBUTTON1) ? NACK_MOUSE_X1
                                                              : NACK_MOUSE_X2;
            down = (msg == WM_XBUTTONDOWN);
            break;
        }

        /* Capture keeps the release event coming to us even if the pointer
         * leaves the window mid-drag, which selection handling depends on. */
        if (down) {
            if (!state.mouse_buttons[NACK_MOUSE_LEFT] &&
                !state.mouse_buttons[NACK_MOUSE_RIGHT] &&
                !state.mouse_buttons[NACK_MOUSE_MIDDLE])
                SetCapture(hwnd);
        }

        w->emit_mouse_button(button, down, GET_X_LPARAM(lparam),
                                GET_Y_LPARAM(lparam), win32_mods());

        if (!down) {
            if (!state.mouse_buttons[NACK_MOUSE_LEFT] &&
                !state.mouse_buttons[NACK_MOUSE_RIGHT] &&
                !state.mouse_buttons[NACK_MOUSE_MIDDLE])
                ReleaseCapture();
        }

        if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP)
            return TRUE;
        return 0;
    }

    case WM_MOUSEWHEEL:
        w->emit_scroll(0.0,
                          (double)GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA,
                          win32_mods(), false);
        return 0;

    case WM_MOUSEHWHEEL:
        /* Windows reports horizontal wheel with the opposite sign to ours. */
        w->emit_scroll(-(double)GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA,
                      0.0, win32_mods(), false);
        return 0;

    case WM_SYSCOMMAND:
        /* Suppress the screen saver and monitor power-down while running
         * fullscreen, and swallow the Alt key opening the window menu. */
        switch (wparam & 0xFFF0) {
        case SC_SCREENSAVE:
        case SC_MONITORPOWER:
            if (w->fullscreen)
                return 0;
            break;
        case SC_KEYMENU:
            return 0;
        default:
            break;
        }
        break;

    case WM_DESTROY:
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* ------------------------------------------------------------------ */
/* Window management                                                  */
/* ------------------------------------------------------------------ */

static bool win32_window_create(nack_window *w,
                                      const nack_window_desc *desc)
{
    (void)desc;
    nack_win32_window *ww = new nack_win32_window{};
    ww->dpi = NACK_DEFAULT_DPI;
    w->native = ww;

    DWORD style = win32_style(w);
    DWORD ex_style = win32_ex_style(w);

    RECT rect = { 0, 0, w->width, w->height };
    win32_adjust_rect(w, &rect, style, ex_style, NACK_DEFAULT_DPI);

    std::optional<std::wstring> title = win32_utf8_to_wide(w->title.c_str());

    ww->hwnd = CreateWindowExW(ex_style, NACK_WIN32_CLASS_NAME,
                               title ? title->c_str() : L"libnack", style,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               rect.right - rect.left, rect.bottom - rect.top,
                               NULL, NULL, win32.instance, NULL);

    if (!ww->hwnd) {
        delete ww;
        w->native = NULL;
        return state.fail(NACK_ERROR_PLATFORM, "CreateWindowEx failed (error %lu)",
                          GetLastError());
    }

    SetWindowLongPtrW(ww->hwnd, GWLP_USERDATA, (LONG_PTR)w);

    ww->hdc = GetDC(ww->hwnd);
    if (!ww->hdc) {
        DestroyWindow(ww->hwnd);
        delete ww;
        w->native = NULL;
        return state.fail(NACK_ERROR_PLATFORM, "GetDC failed");
    }

    ww->dpi = win32_dpi_for_window(ww->hwnd);
    w->scale = w->high_dpi ? (float)ww->dpi / (float)NACK_DEFAULT_DPI : 1.0f;

    /* Re-apply the size at the real DPI: the window was created before we
     * knew which monitor it would land on. */
    if (w->high_dpi && ww->dpi != NACK_DEFAULT_DPI) {
        RECT scaled = { 0, 0,
                        (int)(w->width * w->scale),
                        (int)(w->height * w->scale) };
        win32_adjust_rect(w, &scaled, style, ex_style, ww->dpi);
        SetWindowPos(ww->hwnd, NULL, 0, 0,
                     scaled.right - scaled.left, scaled.bottom - scaled.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (w->transparent)
        SetLayeredWindowAttributes(ww->hwnd, 0, 255, LWA_ALPHA);

    RECT client;
    GetClientRect(ww->hwnd, &client);
    w->width = client.right - client.left;
    w->height = client.bottom - client.top;
    w->fb_width = w->width;
    w->fb_height = w->height;

    /* The pixel format has to be set on the DC before any GL context uses it,
     * and it can only be set once per window. */
    if (wgl_choose_pixel_format(w, ww->hdc, &ww->pixel_format)) {
        PIXELFORMATDESCRIPTOR pfd;
        memset(&pfd, 0, sizeof pfd);
        pfd.nSize = sizeof pfd;
        pfd.nVersion = 1;
        DescribePixelFormat(ww->hdc, ww->pixel_format, sizeof pfd, &pfd);
        if (!SetPixelFormat(ww->hdc, ww->pixel_format, &pfd))
            nack_log("nack: SetPixelFormat failed (error %lu)", GetLastError());
    }

    return true;
}

static void win32_window_destroy(nack_window *w)
{
    nack_win32_window *ww = win32_win(w);
    if (!ww)
        return;
    if (ww->cursor_clipped)
        ClipCursor(NULL);
    if (ww->hdc)
        ReleaseDC(ww->hwnd, ww->hdc);
    if (ww->hwnd) {
        SetWindowLongPtrW(ww->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(ww->hwnd);
    }
    delete ww;
    w->native = NULL;
}

static void win32_window_show(nack_window *w, bool show)
{
    ShowWindow(win32_win(w)->hwnd, show ? SW_SHOWNA : SW_HIDE);
}

static void win32_window_focus(nack_window *w)
{
    nack_win32_window *ww = win32_win(w);
    BringWindowToTop(ww->hwnd);
    SetForegroundWindow(ww->hwnd);
    SetFocus(ww->hwnd);
}

static void win32_window_set_title(nack_window *w, const char *title)
{
    std::optional<std::wstring> wide = win32_utf8_to_wide(title);
    if (wide)
        SetWindowTextW(win32_win(w)->hwnd, wide->c_str());
}

static void win32_window_set_size(nack_window *w, int width, int height)
{
    nack_win32_window *ww = win32_win(w);
    RECT rect = { 0, 0, width, height };
    win32_adjust_rect(w, &rect, win32_style(w),
                            win32_ex_style(w), ww->dpi);
    SetWindowPos(ww->hwnd, HWND_TOP, 0, 0,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOZORDER);
}

static void win32_window_set_position(nack_window *w, int x, int y)
{
    nack_win32_window *ww = win32_win(w);
    RECT rect = { x, y, x, y };
    win32_adjust_rect(w, &rect, win32_style(w),
                            win32_ex_style(w), ww->dpi);
    SetWindowPos(ww->hwnd, NULL, rect.left, rect.top, 0, 0,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE);
}

static void win32_apply_size_hints(nack_window *w)
{
    /* Limits are enforced from WM_GETMINMAXINFO and WM_SIZING; nudge the
     * window manager into asking again. */
    nack_win32_window *ww = win32_win(w);
    RECT rect;
    GetWindowRect(ww->hwnd, &rect);
    SetWindowPos(ww->hwnd, NULL, rect.left, rect.top,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

static void win32_window_set_fullscreen(nack_window *w, bool fullscreen)
{
    nack_win32_window *ww = win32_win(w);

    if (fullscreen && !w->fullscreen) {
        ww->restore_style = (DWORD)GetWindowLongPtrW(ww->hwnd, GWL_STYLE);
        ww->restore_ex_style = (DWORD)GetWindowLongPtrW(ww->hwnd, GWL_EXSTYLE);
        GetWindowRect(ww->hwnd, &ww->restore_rect);

        MONITORINFO info;
        memset(&info, 0, sizeof info);
        info.cbSize = sizeof info;
        HMONITOR monitor = MonitorFromWindow(ww->hwnd, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfoW(monitor, &info))
            return;

        SetWindowLongPtrW(ww->hwnd, GWL_STYLE,
                          (LONG_PTR)((ww->restore_style & ~WS_OVERLAPPEDWINDOW) |
                                     WS_POPUP));
        SetWindowPos(ww->hwnd, HWND_TOP,
                     info.rcMonitor.left, info.rcMonitor.top,
                     info.rcMonitor.right - info.rcMonitor.left,
                     info.rcMonitor.bottom - info.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        w->fullscreen = true;
    } else if (!fullscreen && w->fullscreen) {
        SetWindowLongPtrW(ww->hwnd, GWL_STYLE, (LONG_PTR)ww->restore_style);
        SetWindowLongPtrW(ww->hwnd, GWL_EXSTYLE, (LONG_PTR)ww->restore_ex_style);
        SetWindowPos(ww->hwnd, HWND_NOTOPMOST,
                     ww->restore_rect.left, ww->restore_rect.top,
                     ww->restore_rect.right - ww->restore_rect.left,
                     ww->restore_rect.bottom - ww->restore_rect.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        w->fullscreen = false;
    }
}

static void win32_window_minimize(nack_window *w)
{
    ShowWindow(win32_win(w)->hwnd, SW_MINIMIZE);
}

static void win32_window_maximize(nack_window *w)
{
    ShowWindow(win32_win(w)->hwnd, SW_MAXIMIZE);
}

static void win32_window_restore(nack_window *w)
{
    if (w->fullscreen)
        win32_window_set_fullscreen(w, false);
    ShowWindow(win32_win(w)->hwnd, SW_RESTORE);
}

static void win32_window_request_attention(nack_window *w)
{
    FLASHWINFO flash;
    memset(&flash, 0, sizeof flash);
    flash.cbSize = sizeof flash;
    flash.hwnd = win32_win(w)->hwnd;
    flash.dwFlags = FLASHW_TRAY;
    flash.uCount = 3;
    FlashWindowEx(&flash);
}

static void win32_window_request_redraw(nack_window *w)
{
    InvalidateRect(win32_win(w)->hwnd, NULL, FALSE);
}

static void win32_window_get_native(const nack_window *w,
                                          nack_native_window *out)
{
    nack_win32_window *ww = (nack_win32_window *)w->native;
    out->display = NULL;
    out->surface = ww ? (void *)ww->hwnd : NULL;
    out->handle = ww ? (uintptr_t)ww->hwnd : 0;
}

/* ------------------------------------------------------------------ */
/* Event loop                                                         */
/* ------------------------------------------------------------------ */

static void win32_drain(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            size_t i;
            state.emit_global(NACK_WIN_EVENT_QUIT);
            for (i = 0; i < state.windows.size(); ++i)
                state.windows[i]->should_close = true;
            continue;
        }
        /*
         * nack_state::wakeup() posts a thread message, which has no target
         * window. DispatchMessageW would silently drop it, so it has to
         * become an event here rather than in the window procedure.
         */
        if (msg.message == NACK_WM_WAKEUP && msg.hwnd == NULL) {
            state.emit_global(NACK_WIN_EVENT_WAKEUP);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void win32_pump_events(double timeout)
{
    win32_drain();
    if (!state.queue.empty() || timeout == 0.0)
        return;

    DWORD ms = INFINITE;
    if (timeout > 0.0) {
        double clamped = timeout * 1000.0;
        if (clamped > (double)(INFINITE - 1))
            clamped = (double)(INFINITE - 1);
        ms = (DWORD)clamped;
    }

    /* Sleep until a message arrives rather than spinning on PeekMessage. */
    MsgWaitForMultipleObjects(0, NULL, FALSE, ms, QS_ALLINPUT);
    win32_drain();
}

static void win32_wakeup(void)
{
    /* Safe from any thread: this only queues a message. */
    PostThreadMessageW(win32.main_thread, NACK_WM_WAKEUP, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Clipboard                                                          */
/* ------------------------------------------------------------------ */

static HWND win32_any_window(void)
{
    for (size_t i = 0; i < state.windows.size(); ++i) {
        nack_win32_window *ww = (nack_win32_window *)state.windows[i]->native;
        if (ww && ww->hwnd)
            return ww->hwnd;
    }
    return NULL;
}

static bool win32_clipboard_set(const char *utf8)
{
    std::optional<std::wstring> wide = win32_utf8_to_wide(utf8);
    if (!wide)
        return state.fail(NACK_ERROR_INVALID_ARGUMENT, "clipboard text is not UTF-8");

    size_t count = wide->size() + 1;
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, count * sizeof(WCHAR));
    if (!handle)
        return state.fail(NACK_ERROR_OUT_OF_MEMORY, "GlobalAlloc failed");

    void *locked = GlobalLock(handle);
    if (!locked) {
        GlobalFree(handle);
        return state.fail(NACK_ERROR_PLATFORM, "GlobalLock failed");
    }
    memcpy(locked, wide->c_str(), count * sizeof(WCHAR));
    GlobalUnlock(handle);

    if (!OpenClipboard(win32_any_window())) {
        GlobalFree(handle);
        return state.fail(NACK_ERROR_PLATFORM, "OpenClipboard failed");
    }
    EmptyClipboard();
    /* Ownership of the handle transfers to the clipboard on success. */
    if (!SetClipboardData(CF_UNICODETEXT, handle)) {
        CloseClipboard();
        GlobalFree(handle);
        return state.fail(NACK_ERROR_PLATFORM, "SetClipboardData failed");
    }
    CloseClipboard();
    return true;
}

static const char *win32_clipboard_get(void)
{
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
        return NULL;
    if (!OpenClipboard(win32_any_window()))
        return NULL;

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle) {
        CloseClipboard();
        return NULL;
    }
    /* GetClipboardData hands back a plain HANDLE; GlobalLock wants the
     * HGLOBAL it really is. C converted between them silently, C++ does not. */
    const WCHAR *wide = (const WCHAR *)GlobalLock((HGLOBAL)handle);
    if (!wide) {
        CloseClipboard();
        return NULL;
    }

    win32.clipboard_text = win32_wide_to_utf8(wide);

    GlobalUnlock((HGLOBAL)handle);
    CloseClipboard();
    return win32.clipboard_text ? win32.clipboard_text->c_str() : NULL;
}

/* ------------------------------------------------------------------ */
/* Init / shutdown                                                    */
/* ------------------------------------------------------------------ */

static void win32_load_dpi_functions(void)
{
    win32.user32 = LoadLibraryA("user32.dll");
    if (win32.user32) {
        win32.GetDpiForWindow_ = (UINT (WINAPI *)(HWND))(void *)
            GetProcAddress(win32.user32, "GetDpiForWindow");
        win32.AdjustWindowRectExForDpi_ =
            (BOOL (WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT))(void *)
                GetProcAddress(win32.user32, "AdjustWindowRectExForDpi");
        win32.EnableNonClientDpiScaling_ = (BOOL (WINAPI *)(HWND))(void *)
            GetProcAddress(win32.user32, "EnableNonClientDpiScaling");
        win32.SetProcessDpiAwarenessContext_ = (BOOL (WINAPI *)(void *))(void *)
            GetProcAddress(win32.user32, "SetProcessDpiAwarenessContext");
    }

    win32.shcore = LoadLibraryA("shcore.dll");
    if (win32.shcore)
        win32.SetProcessDpiAwareness_ = (HRESULT (WINAPI *)(int))(void *)
            GetProcAddress(win32.shcore, "SetProcessDpiAwareness");

    /* Per-monitor v2 where available, then the older APIs, so the window is
     * not bitmap-stretched on a high DPI display. */
    if (win32.SetProcessDpiAwarenessContext_) {
        /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
        win32.SetProcessDpiAwarenessContext_((void *)-4);
    } else if (win32.SetProcessDpiAwareness_) {
        win32.SetProcessDpiAwareness_(2);   /* PROCESS_PER_MONITOR_DPI_AWARE */
    } else {
        SetProcessDPIAware();
    }
}

static bool win32_init(const nack_win_init_desc *desc)
{
    (void)desc;
    win32 = nack_win32_state{};

    win32.instance = GetModuleHandleW(NULL);
    win32.main_thread = GetCurrentThreadId();

    win32_load_dpi_functions();
    win32_build_keycodes();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = win32_wndproc;
    wc.hInstance = win32.instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = NACK_WIN32_CLASS_NAME;
    wc.hIcon = (HICON)LoadImageW(win32.instance, L"NACK_ICON", IMAGE_ICON,
                                 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (!wc.hIcon)
        wc.hIcon = (HICON)LoadImageW(NULL, IDI_APPLICATION, IMAGE_ICON, 0, 0,
                                     LR_DEFAULTSIZE | LR_SHARED);

    if (!RegisterClassExW(&wc))
        return state.fail(NACK_ERROR_PLATFORM, "RegisterClassEx failed (error %lu)",
                          GetLastError());
    win32.class_registered = true;

    wgl_init();   /* soft failure: windows still work without GL */

    return true;
}

static void win32_shutdown(void)
{
    wgl_terminate();

    if (win32.class_registered)
        UnregisterClassW(NACK_WIN32_CLASS_NAME, win32.instance);

    if (win32.user32) FreeLibrary(win32.user32);
    if (win32.shcore) FreeLibrary(win32.shcore);

    win32 = nack_win32_state{};
}

/* ------------------------------------------------------------------ */

namespace {

class win32_backend final : public nack_backend_vt {
public:
    const char *name() const override { return "win32"; }
    nack_backend id() const override { return NACK_BACKEND_WIN32; }

    bool init(const nack_win_init_desc *desc) override
    {
        return win32_init(desc);
    }
    void shutdown() override
    {
        win32_shutdown();
    }
    bool window_create(nack_window *w, const nack_window_desc *desc) override
    {
        return win32_window_create(w, desc);
    }
    void window_destroy(nack_window *w) override
    {
        win32_window_destroy(w);
    }
    void window_show(nack_window *w, bool show) override
    {
        win32_window_show(w, show);
    }
    void window_set_title(nack_window *w, const char *title) override
    {
        win32_window_set_title(w, title);
    }
    void window_set_size(nack_window *w, int width, int height) override
    {
        win32_window_set_size(w, width, height);
    }
    void window_apply_size_hints(nack_window *w) override
    {
        win32_apply_size_hints(w);
    }
    void window_set_fullscreen(nack_window *w, bool fullscreen) override
    {
        win32_window_set_fullscreen(w, fullscreen);
    }
    void window_minimize(nack_window *w) override
    {
        win32_window_minimize(w);
    }
    void window_maximize(nack_window *w) override
    {
        win32_window_maximize(w);
    }
    void window_restore(nack_window *w) override
    {
        win32_window_restore(w);
    }
    void window_request_redraw(nack_window *w) override
    {
        win32_window_request_redraw(w);
    }
    void window_set_cursor_shape(nack_window *w, nack_cursor_shape shape) override
    {
        win32_set_cursor_shape(w, shape);
    }
    void window_set_cursor_mode(nack_window *w, nack_cursor_mode mode) override
    {
        win32_set_cursor_mode(w, mode);
    }
    void window_get_native(const nack_window *w, nack_native_window *out) override
    {
        win32_window_get_native(w, out);
    }
    void window_focus(nack_window *w) override
    {
        win32_window_focus(w);
    }
    void window_set_position(nack_window *w, int x, int y) override
    {
        win32_window_set_position(w, x, y);
    }
    void window_request_attention(nack_window *w) override
    {
        win32_window_request_attention(w);
    }
    void pump_events(double timeout) override
    {
        win32_pump_events(timeout);
    }
    void wakeup() override
    {
        win32_wakeup();
    }
    nack_gl_context *gl_create(nack_window *w,
                                      const gl_desc *desc) override
    {
        return wgl_create_context(w, desc, this);
    }
    void gl_destroy(nack_gl_context *ctx) override
    {
        wgl_destroy_context(ctx);
    }
    bool gl_make_current(nack_window *w, nack_gl_context *ctx) override
    {
        return wgl_make_current(w, ctx);
    }
    void gl_swap_buffers(nack_window *w) override
    {
        wgl_swap_buffers(w);
    }
    void gl_set_swap_interval(int interval) override
    {
        wgl_set_swap_interval(interval);
    }
    void *gl_get_proc_address(const char *name) override
    {
        return wgl_get_proc_address(name);
    }
    bool clipboard_set(const char *utf8) override
    {
        return win32_clipboard_set(utf8);
    }
    const char *clipboard_get() override
    {
        return win32_clipboard_get();
    }
};

win32_backend win32_backend_instance;

}   /* namespace */


nack_backend_vt *backend_win32(void)
{
    return &win32_backend_instance;
}

} }   /* namespace nack::detail */
