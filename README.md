# libnack

A small cross-platform windowing and OpenGL context library in C23, built to
sit under a terminal emulator or a libtcod/BearLibTerminal-style console.

| Platform | Backend | Contexts |
| --- | --- | --- |
| Linux, BSD | XCB | EGL |
| Linux, BSD | Wayland (xdg-shell) | EGL |
| Windows | Win32 | WGL |
| macOS | Cocoa | NSOpenGL |

On Unix the backend is chosen at run time, so one binary runs on both Wayland
and X11. `NACK_BACKEND=wayland` or `NACK_BACKEND=x11` overrides the choice.

## Why the API looks like this

Most windowing libraries assume a game loop: run flat out, redraw every frame.
A terminal is the opposite. It is idle almost always, wakes on a keystroke or
on output from a child process, redraws once, and goes back to sleep. The API
is shaped around that:

- **`nack_wait_event` blocks.** An idle window costs no CPU. There is also
  `nack_wait_event_timeout` for cursor blink, and `nack_wakeup`, which is safe
  to call from another thread when the PTY has produced output.
- **Text is separate from keys.** `NACK_EVENT_TEXT` carries UTF-8 that has
  already been through the platform's compose and IME machinery. Ctrl and
  Command chords produce key events and no text, so a terminal can turn them
  into control bytes without having to filter stray characters.
- **Keys are physical.** `nack_key` values are USB HID usage codes describing
  the position of a key, not the symbol on it, so a keybinding does not move
  when the layout changes.
- **Size increments are first class.** `nack_window_set_size_increments` makes
  the window snap to whole cells, so a resize never leaves a half column.
- **Logical and physical sizes are distinct.** The cell grid is reasoned about
  in logical pixels; the framebuffer is sized in physical ones, so glyphs stay
  crisp on a HiDPI display.

## Building

```sh
cmake -S . -B build
cmake --build build
```

Dependencies on Linux:

```sh
sudo apt install libxcb1-dev libx11-xcb-dev libxcb-xkb-dev libxcb-cursor-dev \
                 libxkbcommon-dev libxkbcommon-x11-dev \
                 libwayland-dev wayland-protocols libegl-dev libgl-dev
```

Either backend can be turned off with `-DNACK_ENABLE_XCB=OFF` or
`-DNACK_ENABLE_WAYLAND=OFF`.

Use it from CMake with `add_subdirectory(libnack)` and link `nack::nack`.

## Usage

```c
#include "nack/nack.h"

nack_init(&(nack_init_desc){ .app_id = "my.terminal" });

nack_window_desc desc;
nack_window_desc_defaults(&desc);
desc.title = "terminal";
desc.width  = 80 * cell_width;
desc.height = 25 * cell_height;
desc.width_increment  = cell_width;    /* snap to whole cells */
desc.height_increment = cell_height;

nack_window *window = nack_window_create(&desc);

nack_gl_desc gl;
nack_gl_desc_defaults(&gl);             /* 3.3 core */
nack_gl_context *ctx = nack_gl_context_create(window, &gl);
nack_gl_make_current(window, ctx);

while (!nack_window_should_close(window)) {
    nack_event event;
    if (!nack_wait_event(&event))       /* sleeps until something happens */
        break;
    do {
        switch (event.type) {
        case NACK_EVENT_TEXT:           /* already composed UTF-8 */
            pty_write(event.text.utf8);
            break;
        case NACK_EVENT_KEY_DOWN:
            if (event.key.mods & NACK_MOD_CTRL && event.key.key == NACK_KEY_C)
                pty_write("\003");
            break;
        case NACK_EVENT_WINDOW_RESIZE:
            reflow(event.size.width / cell_width,
                   event.size.height / cell_height);
            break;
        default:
            break;
        }
    } while (nack_poll_event(&event));  /* drain the rest without blocking */

    render();
    nack_gl_swap_buffers(window);
}
```

### Loading OpenGL

libnack ships no loader. `nack_gl_get_proc_address` is a plain getter meant to
be handed to whichever one you already use:

```c
gladLoadGLLoader((GLADloadproc)nack_gl_get_proc_address);
```

Lookups are memoised, so resolving several hundred names at startup is cheap
and a second load costs almost nothing.

One caveat that is not libnack's to fix: on any driver using libglvnd,
`eglGetProcAddress` returns a non-NULL dispatch stub for *every* `gl`-prefixed
name, whether the function exists or not. A non-NULL pointer therefore proves
nothing. Gate optional functionality on the context version or on
`nack_gl_extension_supported`, never on the pointer alone.

## Platform notes

**A window is not visible until you present a frame.** On Wayland a surface
with no committed buffer is never mapped, so a window that never draws simply
does not appear. X11 and Win32 show an empty window instead, which makes this
easy to miss until you test on Wayland.

**X11 goes through XCB, not Xlib.** Xlib is still linked for one reason: EGL on
the proprietary NVIDIA driver requires an Xlib `Display*`, and
`EGL_EXT_platform_xcb` is Mesa-only. Where that extension is present the
connection stays pure XCB; where it is not, an XCB connection is borrowed from
an Xlib display rather than running two connections. Building with
`-DNACK_XCB_XLIB_FALLBACK=OFF` drops the Xlib dependency entirely, at the cost
of EGL on NVIDIA. GLX is not used at all.

**Wayland decorations.** xdg-decoration is optional and some compositors never
implement it (Mutter, and WSLg among others). Where it is missing, or where the
compositor asks the client to draw, libnack draws a plain title bar, close
button and resize borders itself so the window is still usable. It is
deliberately minimal — no fonts, no theming. `NACK_WAYLAND_FORCE_CSD=1`
exercises that path on a compositor that does support the protocol.

**Key repeat** is generated by the client on Wayland, as the protocol requires,
using the compositor's advertised rate and delay. X11 uses detectable
auto-repeat so a held key does not produce spurious release events.

**Clipboard.** The X11 backend implements both directions of ICCCM including
INCR, so pasting more than a screenful of text works. Wayland uses
`wl_data_device`, with the primary selection where the compositor offers it.
Windows and macOS have no primary selection, so `nack_primary_*` returns
false/NULL there.

## What is not implemented

- Drag and drop.
- Multi-window IME preedit display (dead keys and compose work; a candidate
  window for CJK input does not).
- Monitor enumeration and video mode switching; fullscreen uses the monitor the
  window is already on.
- Gamepad or touch input.

## Examples

- `hello_window` — window creation and the event stream.
- `gl_triangle` — a 3.3 core context and a shader.
- `text_grid` — a character-cell grid with cell snapping, UTF-8 input,
  clipboard, and a blocking event loop. This is the one that shows the shape a
  terminal would take.

```sh
./build/examples/text_grid
```

## Testing

The XCB and Wayland backends are exercised against Xvfb, sway and Weston:
window creation, GL 3.3 core contexts with pixel readback, event-loop timeouts,
thread wakeups, clipboard round trips including a 200 KB payload over INCR, and
key events with text separated from Ctrl chords. The Win32 backend is
cross-compiled with MinGW. CI additionally builds on Windows and macOS.

## Licence

MIT.
