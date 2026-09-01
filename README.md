# libnack

A cell console for roguelikes, in C99. You get a grid of cells to draw glyphs
and tiles into, in the spirit of libtcod and BearLibTerminal.

libnack owns the window, the OpenGL context and the renderer. None of that is
exposed, because none of it is what a roguelike wants to think about. The whole
API is the console.

```c
#include "nack/nack.h"

int main(void)
{
    struct nack_config config;
    nack_config_defaults(&config);
    config.title = "my roguelike";
    config.columns = 80;
    config.rows = 50;
    nack_init(&config);

    while (!nack_should_close()) {
        nack_clear(NULL);
        nack_put(NULL, x, y, '@', NACK_WHITE, NACK_BLACK);
        nack_print(NULL, 0, 49, NACK_GREY, NACK_BLACK, "you are in a maze");
        nack_present();

        struct nack_event event;
        while (nack_poll_event(&event)) {
            if (event.type == NACK_EVENT_KEY_DOWN &&
                event.data.key.key == NACK_KEY_H)
                x--;
        }
    }
    nack_shutdown();
}
```

| Platform | Windowing | Renderer |
| --- | --- | --- |
| Linux, BSD | Wayland (xdg-shell) or XCB, chosen at run time | OpenGL 3.3 via EGL |
| Windows | Win32 | OpenGL 3.3 via WGL |
| macOS | Cocoa | Metal |

The library is C99 throughout, with no C++ anywhere — not even `extern "C"`
guards in the headers, since it is not meant to be included from C++ without
the caller saying so themselves.

## Why it looks like this

- **A console, not a terminal.** Cells are addressed directly and redrawn as
  you like. There is no scrollback, no reflow and no PTY; it is a grid you
  paint, which is what a roguelike wants.
- **Turn-based games can sleep.** `nack_wait_event` blocks until the player
  does something, so a game waiting on input costs no CPU at all. Real-time
  games call `nack_poll_event` in a loop and present every frame instead. Both
  work; neither is privileged.
- **The mouse arrives in cells.** `event.data.mouse.x` is a column, not a
  pixel, already through the letterbox and scale transform. Pixel positions are
  there too if you want them.
- **Keys are physical.** `nack_key` values are USB HID usage codes describing
  the position of a key, so a movement binding does not move when the player
  changes layout. The numeric keypad is distinct from the number row, because
  eight-way movement needs it. Text, for entering a character's name, arrives
  separately as `NACK_EVENT_TEXT` after the platform's dead-key and IME
  handling; Ctrl chords produce key events and no text.
- **Whole-pixel scaling by default.** The console is scaled to the window by a
  whole number and letterboxed, so tiles stay crisp. `NACK_SCALE_FIT` and
  `NACK_SCALE_STRETCH` are there if you would rather fill the window.

## Tilesets

The built-in 8x8 CP437 font means a game runs before it ships any assets,
including box drawing and block characters. Beyond that:

```c
/* A CP437 sheet: the classic roguelike tileset layout. */
struct nack_tileset *font =
    nack_tileset_load("terminal16x16.png", 16, 16, NACK_LAYOUT_CP437);
nack_set_font(font);

/* Graphical tiles, drawn by index rather than by codepoint. */
struct nack_tileset *tiles =
    nack_tileset_load("creatures.png", 32, 32, NACK_LAYOUT_ROW_MAJOR);
nack_put_tile(NULL, x, y, tiles, ORC_TILE, NACK_WHITE, NACK_BLACK);

/* Unicode past CP437's 256 slots. */
nack_tileset_map_range(font, 0x4E00, 0x4E7F, 256);
```

A sheet that only contains greys is treated as a font and tinted by each cell's
foreground colour; a sheet with real colour in it is treated as artwork and
drawn as it is, tinted only if you ask. Foreground and background are 24-bit
with alpha, per cell.

PNG decoding is built in, so there is no image library to link.

## Consoles

Every drawing call takes a console, and `NULL` means the root console — the one
that gets presented. Offscreen consoles are for composing:

```c
struct nack_console *panel = nack_console_new(20, 10);
nack_draw_box(panel, 0, 0, 20, 10, NACK_GREY, NACK_BLACK, "inventory");
nack_print(panel, 2, 2, NACK_WHITE, NACK_BLACK, "a) rusty sword");
nack_blit(panel, 0, 0, 20, 10, NULL, 30, 20, 1.0f, 0.9f);
```

The last two arguments are foreground and background alpha, so a translucent
overlay is a blit with a low background alpha.

## Building

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

On Linux:

```sh
sudo apt install libxcb1-dev libx11-xcb-dev libxcb-xkb-dev libxcb-cursor-dev \
                 libxkbcommon-dev libxkbcommon-x11-dev \
                 libwayland-dev wayland-protocols libegl-dev libgl-dev
```

Use it from CMake with `add_subdirectory(libnack)` and link `nack::nack`.
Either Unix backend can be turned off with `-DNACK_ENABLE_XCB=OFF` or
`-DNACK_ENABLE_WAYLAND=OFF`; `NACK_BACKEND=wayland` or `x11` in the environment
overrides the run-time choice.

## Examples

- `hello` — the smallest useful program.
- `roguelike` — a playable fragment: generated dungeon, field of view with
  remembered terrain, eight-way movement from vi keys, arrows and the keypad,
  a sidebar composed offscreen and blitted, a message log, and a blocking event
  loop that idles at zero CPU.

## Platform notes

**No windows.h.** The Win32 backend declares the seventy-odd functions and
structures it uses itself, rather than dragging in `windows.h` and its
namespace pollution. Those declarations are checked rather than trusted:
`tests/win32_abi_check` compares every structure size, member offset and
constant against the real SDK and fails the build if any of the 226 facts
disagree. `-DNACK_WIN32_USE_SDK_HEADERS=ON` falls back to `windows.h`.

**X11 goes through XCB, not Xlib.** Xlib is linked for one reason: EGL on the
proprietary NVIDIA driver needs an Xlib `Display*`, and `EGL_EXT_platform_xcb`
is Mesa-only. Where that extension exists the connection stays pure XCB.
`-DNACK_XCB_XLIB_FALLBACK=OFF` drops the dependency, at the cost of EGL on
NVIDIA. GLX is not used.

**macOS renders with Metal.** Apple deprecated OpenGL in 10.14 and capped it
at 4.1, so the console draws through Metal there: a `CAMetalLayer` on the
window's view, one pipeline built from Metal Shading Language at startup, and
the same quads the OpenGL backend gets. `-DNACK_MACOS_USE_OPENGL=ON` selects
the OpenGL renderer instead, and CI builds both. The renderer sits behind an
internal interface (`src/console/nack_gfx.h`), so the console layer is unaware
of which one it is talking to.

**Wayland decorations.** xdg-decoration is optional and some compositors never
implement it (Mutter and WSLg among them). Where it is missing, libnack draws
its own frame: title bar with the window title, minimise, maximise and close
buttons, resize borders with the right cursors, drag to move, double-click to
maximise, following the display's scale factor. `NACK_WAYLAND_FORCE_CSD=1`
exercises it on a compositor that does support the protocol.

## What is not implemented

- Drag and drop.
- IME candidate windows (dead keys and compose work; CJK input methods do not
  show their candidate list).
- Monitor enumeration and video mode switching; fullscreen uses the monitor the
  window is already on.
- Gamepad and touch input.
- Sound. libnack draws; it does not make noise.

## Testing

`tests/console_smoke.c` drives the console API and verifies frames by reading
pixels back out of the framebuffer, not merely by checking that `nack_present`
returned. `tests/png_test.c` decodes generated PNGs across every colour type,
bit depth and row filter and compares them pixel for pixel against known data.
`tests/win32_abi_check` compares the hand-rolled Win32 declarations with the
SDK.

Run here against Xvfb, sway and Weston, and cross-compiled for Windows with
MinGW and exercised under Wine. The Cocoa window layer and the Metal renderer
are built by CI, in both the Metal and OpenGL configurations, but neither has
been run on hardware.

## Licence

MIT. The built-in font derives from Daniel Hepper's font8x8, which is public
domain.
