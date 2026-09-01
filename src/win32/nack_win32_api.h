/*
 * Hand-rolled declarations for the Win32 API that libnack uses.
 *
 * windows.h is enormous and pollutes the namespace aggressively - it defines
 * near, far, min, max, small and CreateWindow among many others, which is how
 * a local variable called "near" once stopped this library compiling. Only
 * around seventy functions and a handful of structures are actually needed, so
 * they are declared here instead.
 *
 * Everything in this file mirrors an external ABI, so it necessarily uses the
 * SDK's own names and typedefs rather than the library's usual style; the
 * backend code below it reads like ordinary Win32 code.
 *
 * The layouts are checked against the real SDK rather than trusted: see
 * tests/win32_abi_check.c, which compares every structure size, member offset
 * and constant value declared here against <windows.h>. Configure with
 * -DNACK_WIN32_USE_SDK_HEADERS=ON to fall back to windows.h if a future SDK
 * ever diverges.
 */
#ifndef NACK_WIN32_API_H_INCLUDED
#define NACK_WIN32_API_H_INCLUDED

#if defined(_WINDOWS_) && !defined(NACK_WIN32_ABI_CHECK)
#  error "windows.h was included before nack_win32_api.h; \
build with -DNACK_WIN32_USE_SDK_HEADERS=ON to use the SDK headers instead"
#endif

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Calling convention and linkage                                     */
/* ------------------------------------------------------------------ */

/*
 * Every Win32 entry point is __stdcall. That is a no-op on x86-64, where
 * there is only one calling convention, but it is load bearing on 32-bit.
 */
#if defined(_M_IX86) || defined(__i386__)
#  define WINAPI __stdcall
#else
#  define WINAPI
#endif
#define CALLBACK WINAPI

/* Matching the import libraries avoids an indirection through a thunk. */
#define NACK_WINIMPORT __declspec(dllimport)

/* ------------------------------------------------------------------ */
/* Base types                                                         */
/* ------------------------------------------------------------------ */

/*
 * Windows is LLP64: long stays 32 bits even on 64-bit targets, which is why
 * LONG is long and the pointer-sized types are spelled out separately.
 */
typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef unsigned long       DWORD;
typedef long                LONG;
typedef unsigned int        UINT;
typedef int                 INT;
typedef float               FLOAT;
typedef wchar_t             WCHAR;
typedef char                CHAR;
typedef void               *LPVOID;
typedef const void         *LPCVOID;
typedef WCHAR              *LPWSTR;
typedef const WCHAR        *LPCWSTR;
typedef CHAR               *LPSTR;
typedef const CHAR         *LPCSTR;
typedef intptr_t            LONG_PTR;
typedef uintptr_t           ULONG_PTR;
typedef uintptr_t           UINT_PTR;
typedef ULONG_PTR           DWORD_PTR;
typedef UINT_PTR            WPARAM;
typedef LONG_PTR            LPARAM;
typedef LONG_PTR            LRESULT;
typedef long                HRESULT;
typedef unsigned short      ATOM;

#define TRUE  1
#define FALSE 0
#ifndef NULL
#  define NULL ((void *)0)
#endif

/*
 * Handles are pointers to undefined structures in the SDK too, which keeps
 * them distinct types rather than interchangeable void pointers.
 */
#define NACK_DECLARE_HANDLE(name) struct name##__; typedef struct name##__ *name
NACK_DECLARE_HANDLE(HWND);
NACK_DECLARE_HANDLE(HDC);
NACK_DECLARE_HANDLE(HGLRC);
NACK_DECLARE_HANDLE(HINSTANCE);
NACK_DECLARE_HANDLE(HICON);
NACK_DECLARE_HANDLE(HBRUSH);
NACK_DECLARE_HANDLE(HMENU);
NACK_DECLARE_HANDLE(HMONITOR);
NACK_DECLARE_HANDLE(HGLOBAL);
#undef NACK_DECLARE_HANDLE

/* The SDK aliases these rather than giving them their own tags. */
typedef HICON      HCURSOR;
typedef HINSTANCE  HMODULE;
typedef void      *HANDLE;

typedef LONG_PTR (WINAPI *PROC)(void);
typedef LONG_PTR (WINAPI *FARPROC)(void);

/* ------------------------------------------------------------------ */
/* Structures                                                         */
/* ------------------------------------------------------------------ */

typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT, *LPPOINT;

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *LPRECT;

typedef struct tagMSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
    DWORD  lPrivate;   /* present in current SDKs; unused by us */
} MSG, *LPMSG;

typedef LRESULT (CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagWNDCLASSEXW {
    UINT      cbSize;
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCWSTR   lpszMenuName;
    LPCWSTR   lpszClassName;
    HICON     hIconSm;
} WNDCLASSEXW;

typedef struct tagPAINTSTRUCT {
    HDC  hdc;
    BOOL fErase;
    RECT rcPaint;
    BOOL fRestore;
    BOOL fIncUpdate;
    BYTE rgbReserved[32];
} PAINTSTRUCT;

typedef struct tagTRACKMOUSEEVENT {
    DWORD cbSize;
    DWORD dwFlags;
    HWND  hwndTrack;
    DWORD dwHoverTime;
} TRACKMOUSEEVENT;

typedef struct tagMINMAXINFO {
    POINT ptReserved;
    POINT ptMaxSize;
    POINT ptMaxPosition;
    POINT ptMinTrackSize;
    POINT ptMaxTrackSize;
} MINMAXINFO;

typedef struct tagMONITORINFO {
    DWORD cbSize;
    RECT  rcMonitor;
    RECT  rcWork;
    DWORD dwFlags;
} MONITORINFO;

typedef struct tagFLASHWINFO {
    UINT  cbSize;
    HWND  hwnd;
    DWORD dwFlags;
    UINT  uCount;
    DWORD dwTimeout;
} FLASHWINFO;

typedef struct tagPIXELFORMATDESCRIPTOR {
    WORD  nSize;
    WORD  nVersion;
    DWORD dwFlags;
    BYTE  iPixelType;
    BYTE  cColorBits;
    BYTE  cRedBits;
    BYTE  cRedShift;
    BYTE  cGreenBits;
    BYTE  cGreenShift;
    BYTE  cBlueBits;
    BYTE  cBlueShift;
    BYTE  cAlphaBits;
    BYTE  cAlphaShift;
    BYTE  cAccumBits;
    BYTE  cAccumRedBits;
    BYTE  cAccumGreenBits;
    BYTE  cAccumBlueBits;
    BYTE  cAccumAlphaBits;
    BYTE  cDepthBits;
    BYTE  cStencilBits;
    BYTE  cAuxBuffers;
    BYTE  iLayerType;
    BYTE  bReserved;
    DWORD dwLayerMask;
    DWORD dwVisibleMask;
    DWORD dwDamageMask;
} PIXELFORMATDESCRIPTOR;

/* ------------------------------------------------------------------ */
/* Messages                                                           */
/* ------------------------------------------------------------------ */

#define WM_DESTROY          0x0002
#define WM_MOVE             0x0003
#define WM_SIZE             0x0005
#define WM_SETFOCUS         0x0007
#define WM_KILLFOCUS        0x0008
#define WM_PAINT            0x000F
#define WM_CLOSE            0x0010
#define WM_QUIT             0x0012
#define WM_ERASEBKGND       0x0014
#define WM_SETCURSOR        0x0020
#define WM_GETMINMAXINFO    0x0024
#define WM_NCCREATE         0x0081
#define WM_KEYDOWN          0x0100
#define WM_KEYUP            0x0101
#define WM_CHAR             0x0102
#define WM_SYSKEYDOWN       0x0104
#define WM_SYSKEYUP         0x0105
#define WM_SYSCHAR          0x0106
#define WM_UNICHAR          0x0109
#define WM_SYSCOMMAND       0x0112
#define WM_TIMER            0x0113
#define WM_MOUSEMOVE        0x0200
#define WM_LBUTTONDOWN      0x0201
#define WM_LBUTTONUP        0x0202
#define WM_RBUTTONDOWN      0x0204
#define WM_RBUTTONUP        0x0205
#define WM_MBUTTONDOWN      0x0207
#define WM_MBUTTONUP        0x0208
#define WM_MOUSEWHEEL       0x020A
#define WM_XBUTTONDOWN      0x020B
#define WM_XBUTTONUP        0x020C
#define WM_MOUSEHWHEEL      0x020E
#define WM_SIZING           0x0214
#define WM_ENTERSIZEMOVE    0x0231
#define WM_EXITSIZEMOVE     0x0232
#define WM_MOUSELEAVE       0x02A3
#define WM_DPICHANGED       0x02E0
#define WM_APP              0x8000

/* WM_SIZE */
#define SIZE_MINIMIZED      1
#define SIZE_MAXIMIZED      2

/* WM_SYSCOMMAND */
#define SC_SCREENSAVE       0xF140
#define SC_MONITORPOWER     0xF170
#define SC_KEYMENU          0xF100

/* WM_SIZING */
#define WMSZ_LEFT           1
#define WMSZ_RIGHT          2
#define WMSZ_TOP            3
#define WMSZ_TOPLEFT        4
#define WMSZ_TOPRIGHT       5
#define WMSZ_BOTTOM         6
#define WMSZ_BOTTOMLEFT     7
#define WMSZ_BOTTOMRIGHT    8

#define HTCLIENT            1
#define XBUTTON1            0x0001
#define XBUTTON2            0x0002
#define WHEEL_DELTA         120
#define UNICODE_NOCHAR      0xFFFF

/* ------------------------------------------------------------------ */
/* Window styles                                                      */
/* ------------------------------------------------------------------ */

#define WS_OVERLAPPED       0x00000000UL
#define WS_POPUP            0x80000000UL
#define WS_CAPTION          0x00C00000UL
#define WS_SYSMENU          0x00080000UL
#define WS_THICKFRAME       0x00040000UL
#define WS_MINIMIZEBOX      0x00020000UL
#define WS_MAXIMIZEBOX      0x00010000UL
#define WS_CLIPSIBLINGS     0x04000000UL
#define WS_CLIPCHILDREN     0x02000000UL
#define WS_OVERLAPPEDWINDOW 0x00CF0000UL

#define WS_EX_APPWINDOW     0x00040000UL
#define WS_EX_LAYERED       0x00080000UL

#define CS_VREDRAW          0x0001
#define CS_HREDRAW          0x0002
#define CS_OWNDC            0x0020

#define CW_USEDEFAULT       ((int)0x80000000)

/* ShowWindow */
#define SW_HIDE             0
#define SW_MAXIMIZE         3
#define SW_MINIMIZE         6
#define SW_SHOWNA           8
#define SW_RESTORE          9

/* SetWindowPos */
#define SWP_NOSIZE          0x0001
#define SWP_NOMOVE          0x0002
#define SWP_NOZORDER        0x0004
#define SWP_NOACTIVATE      0x0010
#define SWP_FRAMECHANGED    0x0020
#define SWP_NOOWNERZORDER   0x0200
#define HWND_TOP            ((HWND)0)
#define HWND_NOTOPMOST      ((HWND)-2)

/* GetWindowLongPtr / SetWindowLongPtr */
#define GWL_STYLE           (-16)
#define GWL_EXSTYLE         (-20)
#define GWLP_USERDATA       (-21)

#define LWA_ALPHA           0x00000002
#define USER_TIMER_MINIMUM  0x0000000A
#define FLASHW_TRAY         0x00000002
#define TME_LEAVE           0x00000002
#define MONITOR_DEFAULTTONEAREST 0x00000002

#define PM_REMOVE           0x0001
#define QS_ALLINPUT         0x1CFF
#define INFINITE            0xFFFFFFFFUL

/* ------------------------------------------------------------------ */
/* Resources and cursors                                              */
/* ------------------------------------------------------------------ */

#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))

#define IDC_ARROW      MAKEINTRESOURCEW(32512)
#define IDC_IBEAM      MAKEINTRESOURCEW(32513)
#define IDC_WAIT       MAKEINTRESOURCEW(32514)
#define IDC_CROSS      MAKEINTRESOURCEW(32515)
#define IDC_SIZENWSE   MAKEINTRESOURCEW(32642)
#define IDC_SIZENESW   MAKEINTRESOURCEW(32643)
#define IDC_SIZEWE     MAKEINTRESOURCEW(32644)
#define IDC_SIZENS     MAKEINTRESOURCEW(32645)
#define IDC_SIZEALL    MAKEINTRESOURCEW(32646)
#define IDC_NO         MAKEINTRESOURCEW(32648)
#define IDC_HAND       MAKEINTRESOURCEW(32649)
#define IDI_APPLICATION MAKEINTRESOURCEW(32512)

#define IMAGE_ICON     1
#define LR_DEFAULTSIZE 0x0040
#define LR_SHARED      0x8000

/* ------------------------------------------------------------------ */
/* Virtual keys                                                       */
/* ------------------------------------------------------------------ */

#define VK_BACK     0x08
#define VK_TAB      0x09
#define VK_RETURN   0x0D
#define VK_SHIFT    0x10
#define VK_CONTROL  0x11
#define VK_MENU     0x12
#define VK_PAUSE    0x13
#define VK_CAPITAL  0x14
#define VK_ESCAPE   0x1B
#define VK_SPACE    0x20
#define VK_LWIN     0x5B
#define VK_RWIN     0x5C
#define VK_NUMLOCK  0x90
#define VK_LSHIFT   0xA0
#define VK_RSHIFT   0xA1

/* ------------------------------------------------------------------ */
/* GDI and pixel formats                                              */
/* ------------------------------------------------------------------ */

#define PFD_TYPE_RGBA       0
#define PFD_DOUBLEBUFFER    0x00000001
#define PFD_DRAW_TO_WINDOW  0x00000004
#define PFD_SUPPORT_OPENGL  0x00000020
#define LOGPIXELSX          88

/* Clipboard and memory */
#define CF_UNICODETEXT      13
#define GMEM_MOVEABLE       0x0002
#define CP_UTF8             65001

/* ------------------------------------------------------------------ */
/* Message parameter accessors                                        */
/* ------------------------------------------------------------------ */

#define LOWORD(l)  ((WORD)(((DWORD_PTR)(l)) & 0xFFFF))
#define HIWORD(l)  ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xFFFF))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define GET_WHEEL_DELTA_WPARAM(wp)  ((short)HIWORD(wp))
#define GET_XBUTTON_WPARAM(wp)      (HIWORD(wp))

/* ------------------------------------------------------------------ */
/* Functions                                                          */
/* ------------------------------------------------------------------ */

/* kernel32 */
NACK_WINIMPORT HMODULE WINAPI GetModuleHandleW(LPCWSTR);
NACK_WINIMPORT HMODULE WINAPI LoadLibraryA(LPCSTR);
NACK_WINIMPORT BOOL    WINAPI FreeLibrary(HMODULE);
NACK_WINIMPORT FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR);
NACK_WINIMPORT DWORD   WINAPI GetLastError(void);
NACK_WINIMPORT DWORD   WINAPI GetCurrentThreadId(void);
NACK_WINIMPORT HGLOBAL WINAPI GlobalAlloc(UINT, size_t);
NACK_WINIMPORT HGLOBAL WINAPI GlobalFree(HGLOBAL);
NACK_WINIMPORT LPVOID  WINAPI GlobalLock(HGLOBAL);
NACK_WINIMPORT BOOL    WINAPI GlobalUnlock(HGLOBAL);
NACK_WINIMPORT int     WINAPI MultiByteToWideChar(UINT, DWORD, LPCSTR, int,
                                                  LPWSTR, int);
NACK_WINIMPORT int     WINAPI WideCharToMultiByte(UINT, DWORD, LPCWSTR, int,
                                                  LPSTR, int, LPCSTR, BOOL *);
/*
 * LARGE_INTEGER is a union whose only member we use is a 64-bit integer, so
 * the pointer is declared as such directly.
 */
NACK_WINIMPORT BOOL    WINAPI QueryPerformanceCounter(int64_t *);
NACK_WINIMPORT BOOL    WINAPI QueryPerformanceFrequency(int64_t *);

/* user32 */
NACK_WINIMPORT ATOM    WINAPI RegisterClassExW(const WNDCLASSEXW *);
NACK_WINIMPORT BOOL    WINAPI UnregisterClassW(LPCWSTR, HINSTANCE);
NACK_WINIMPORT HWND    WINAPI CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD,
                                              int, int, int, int, HWND, HMENU,
                                              HINSTANCE, LPVOID);
NACK_WINIMPORT BOOL    WINAPI DestroyWindow(HWND);
NACK_WINIMPORT LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
NACK_WINIMPORT BOOL    WINAPI PeekMessageW(LPMSG, HWND, UINT, UINT, UINT);
NACK_WINIMPORT BOOL    WINAPI TranslateMessage(const MSG *);
NACK_WINIMPORT LRESULT WINAPI DispatchMessageW(const MSG *);
NACK_WINIMPORT BOOL    WINAPI PostThreadMessageW(DWORD, UINT, WPARAM, LPARAM);
NACK_WINIMPORT DWORD   WINAPI MsgWaitForMultipleObjects(DWORD, const HANDLE *,
                                                        BOOL, DWORD, DWORD);
NACK_WINIMPORT HDC     WINAPI GetDC(HWND);
NACK_WINIMPORT int     WINAPI ReleaseDC(HWND, HDC);
NACK_WINIMPORT HDC     WINAPI BeginPaint(HWND, PAINTSTRUCT *);
NACK_WINIMPORT BOOL    WINAPI EndPaint(HWND, const PAINTSTRUCT *);
NACK_WINIMPORT BOOL    WINAPI InvalidateRect(HWND, const RECT *, BOOL);
NACK_WINIMPORT BOOL    WINAPI ShowWindow(HWND, int);
NACK_WINIMPORT BOOL    WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);
NACK_WINIMPORT BOOL    WINAPI GetClientRect(HWND, LPRECT);
NACK_WINIMPORT BOOL    WINAPI GetWindowRect(HWND, LPRECT);
NACK_WINIMPORT BOOL    WINAPI AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD);
NACK_WINIMPORT BOOL    WINAPI ClientToScreen(HWND, LPPOINT);
NACK_WINIMPORT BOOL    WINAPI SetWindowTextW(HWND, LPCWSTR);
NACK_WINIMPORT LONG_PTR WINAPI GetWindowLongPtrW(HWND, int);
NACK_WINIMPORT LONG_PTR WINAPI SetWindowLongPtrW(HWND, int, LONG_PTR);
NACK_WINIMPORT BOOL    WINAPI BringWindowToTop(HWND);
NACK_WINIMPORT BOOL    WINAPI SetForegroundWindow(HWND);
NACK_WINIMPORT HWND    WINAPI SetFocus(HWND);
NACK_WINIMPORT BOOL    WINAPI FlashWindowEx(const FLASHWINFO *);
NACK_WINIMPORT BOOL    WINAPI SetLayeredWindowAttributes(HWND, DWORD, BYTE, DWORD);
NACK_WINIMPORT UINT_PTR WINAPI SetTimer(HWND, UINT_PTR, UINT, void *);
NACK_WINIMPORT BOOL    WINAPI KillTimer(HWND, UINT_PTR);
NACK_WINIMPORT HCURSOR WINAPI LoadCursorW(HINSTANCE, LPCWSTR);
NACK_WINIMPORT HANDLE  WINAPI LoadImageW(HINSTANCE, LPCWSTR, UINT, int, int, UINT);
NACK_WINIMPORT HCURSOR WINAPI SetCursor(HCURSOR);
NACK_WINIMPORT BOOL    WINAPI SetCursorPos(int, int);
NACK_WINIMPORT BOOL    WINAPI ClipCursor(const RECT *);
NACK_WINIMPORT HWND    WINAPI SetCapture(HWND);
NACK_WINIMPORT BOOL    WINAPI ReleaseCapture(void);
NACK_WINIMPORT short   WINAPI GetKeyState(int);
NACK_WINIMPORT BOOL    WINAPI TrackMouseEvent(TRACKMOUSEEVENT *);
NACK_WINIMPORT HMONITOR WINAPI MonitorFromWindow(HWND, DWORD);
NACK_WINIMPORT BOOL    WINAPI GetMonitorInfoW(HMONITOR, MONITORINFO *);
NACK_WINIMPORT BOOL    WINAPI SetProcessDPIAware(void);
NACK_WINIMPORT BOOL    WINAPI OpenClipboard(HWND);
NACK_WINIMPORT BOOL    WINAPI CloseClipboard(void);
NACK_WINIMPORT BOOL    WINAPI EmptyClipboard(void);
NACK_WINIMPORT HANDLE  WINAPI SetClipboardData(UINT, HANDLE);
NACK_WINIMPORT HANDLE  WINAPI GetClipboardData(UINT);
NACK_WINIMPORT BOOL    WINAPI IsClipboardFormatAvailable(UINT);

/* gdi32 */
NACK_WINIMPORT int  WINAPI GetDeviceCaps(HDC, int);
NACK_WINIMPORT int  WINAPI ChoosePixelFormat(HDC, const PIXELFORMATDESCRIPTOR *);
NACK_WINIMPORT BOOL WINAPI SetPixelFormat(HDC, int, const PIXELFORMATDESCRIPTOR *);
NACK_WINIMPORT int  WINAPI DescribePixelFormat(HDC, int, UINT,
                                               PIXELFORMATDESCRIPTOR *);
NACK_WINIMPORT BOOL WINAPI SwapBuffers(HDC);

/* opengl32 */
NACK_WINIMPORT HGLRC WINAPI wglGetCurrentContext(void);
NACK_WINIMPORT HDC   WINAPI wglGetCurrentDC(void);

#endif /* NACK_WIN32_API_H_INCLUDED */
