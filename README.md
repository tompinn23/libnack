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
        nack_clear(nack_root());
        nack_put(nack_root(), x, y, '@', NACK_WHITE, NACK_BLACK);
        nack_print(nack_root(), 0, 49, NACK_GREY, NACK_BLACK, "you are in a maze");
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
nack_put_tile(nack_root(), x, y, tiles, ORC_TILE, NACK_WHITE, NACK_BLACK);

/* Unicode past CP437's 256 slots. */
nack_tileset_map_range(font, 0x4E00, 0x4E7F, 256);
```

A sheet that only contains greys is treated as a font and tinted by each cell's
foreground colour; a sheet with real colour in it is treated as artwork and
drawn as it is, tinted only if you ask. Foreground and background are 24-bit
with alpha, per cell.

Tilesets can be PNG or JPEG; the format is taken from the bytes rather than
the filename, so an embedded tileset needs no hint. Decoding goes through
stb_image, vendored under `third_party/` and built from source, so there is
still no image library to install. Decoding an image is parsing untrusted
input, and that is the last place to prefer something hand-written over code
that has been attacked for years. Only PNG and JPEG are compiled in, so the
other seven formats stb_image knows are not reachable from a file the program
did not write. See `third_party/VENDORING.md`.

## C++

`<nack/nack.hpp>` is an optional header-only C++17 face on the same library.
It is a wrapper and nothing else — every call forwards to the C API and the
compiler folds it away — but it adds the parts C cannot express:

```cpp
#include <nack/nack.hpp>

int main()
{
    nack::config config = nack::default_config();
    config.title = "roguelike";
    nack::app app{config};                       // nack_shutdown on scope exit

    int purse = 120;
    nack::console panel{20, 10};                 // freed on scope exit
    panel.draw_box(0, 0, 20, 10, nack::grey, nack::black, "inventory");
    panel.print(2, 2, nack::white, nack::black, "a) rusty sword");
    panel.print(2, 3, nack::grey, nack::black, "{} gold", purse);

    while (!app.should_close()) {
        app.console().clear();
        panel.blit_to(app.console(), 30, 20);
        app.present();

        if (auto ev = app.wait()) {
            if (auto *key = std::get_if<nack::key_event>(&*ev))
                if (key->key == nack::key::escape)
                    break;
        }
    }
}
```

Consoles, tilesets and the library itself free themselves. Events arrive as a
`std::variant`, so reading the wrong arm of the union is a compile error rather
than a convention in a comment. `poll()` returns `std::optional` instead of a
bool and an out-parameter, sizes come back as structured bindings, and
modifiers are a real bitmask type rather than a bare `uint32_t`.

`console_view` is the non-owning half, so one helper takes either kind:

```cpp
void draw_panel(nack::console_view console);   // the root or an owned console
```

`print` takes a {fmt} format string when you give it arguments, and plain text
when you do not — so a runtime string prints as it stands rather than being
mistaken for a format. {fmt}'s compile-time checking needs C++20; under C++17
a mismatched format string is a `fmt::format_error` at run time instead. Both
beat C varargs reading whatever was on the stack.

Two things the header has to do for you, which is half the reason it exists.
`<nack/nack.h>` carries no `extern "C"` guards, because the C library does not
pretend to be C++; the wrapper supplies them. And the `NACK_RGB` family are
compound literals — a GCC and Clang extension in C++, rejected outright by
MSVC — so the colours are `constexpr` there instead. Do not reach for the C
macros from C++.

Exceptions are used only where construction fails, and every constructor has a
`try_create` counterpart returning `std::optional`. Built with exceptions off,
the throwing paths abort with the message instead.

{fmt} is vendored under `third_party/fmt` and built header-only, so it is
compiled into whoever includes `<nack/nack.hpp>` and never into libnack. A C
program links no C++ runtime.

The C library is unchanged and stays the ABI other languages bind to.

## Consoles

Every drawing call takes a console. `nack_root()` is the one that gets
presented; offscreen consoles are for composing:

```c
struct nack_console *panel = nack_console_new(20, 10);
nack_draw_box(panel, 0, 0, 20, 10, NACK_GREY, NACK_BLACK, "inventory");
nack_print(panel, 2, 2, NACK_WHITE, NACK_BLACK, "a) rusty sword");
nack_blit(panel, 0, 0, 20, 10, nack_root(), 30, 20, 1.0f, 0.9f);
```

The last two arguments are foreground and background alpha, so a translucent
overlay is a blit with a low background alpha.

Passing `NULL` as a console is an error, not a shorthand for the root. That
matters because `nack_console_new` returns `NULL` when it fails: if `NULL`
meant the root, an unchecked allocation failure would quietly draw over the
screen instead of doing nothing and saying why.

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

Windows needs a real OpenGL 3.3 driver. A machine with no graphics driver
installed has only the GDI generic renderer, which is OpenGL 1.1 and has no
`WGL_ARB_create_context`; libnack says so rather than limping along on a
context it cannot use. Dropping Mesa's `opengl32.dll` and `libgallium_wgl.dll`
next to the executable gives a software 3.3 implementation, which is how CI
tests the Win32 renderer.

**X11 goes through XCB, not Xlib.** Xlib is linked for one reason: EGL on the
proprietary NVIDIA driver needs an Xlib `Display*`, and `EGL_EXT_platform_xcb`
is Mesa-only. Where that extension exists the connection stays pure XCB.
`-DNACK_XCB_XLIB_FALLBACK=OFF` drops the dependency, at the cost of EGL on
NVIDIA. GLX is not used.

**macOS renders with Metal, and falls back to OpenGL.** Apple deprecated
OpenGL in 10.14 and capped it at 4.1, so the console prefers Metal there: a
`CAMetalLayer` on the window's view, one pipeline built from Metal Shading
Language at startup, and the same quads the OpenGL backend gets. Both
renderers are compiled into the macOS build and the choice is made at run
time; if Metal cannot start, the OpenGL renderer takes the window over and the
library keeps working. `-DNACK_MACOS_OPENGL_ONLY=ON` leaves Metal out
altogether, and CI builds and tests both configurations. The renderer sits
behind an internal interface (`src/console/nack_gfx.h`), so the console layer
is unaware of which one it is talking to.

**Environment variables.** All of these are diagnostic escape hatches, meant
for reporting or working around a problem without a rebuild.

| Variable | Effect |
| --- | --- |
| `NACK_DEBUG=1` | Writes the backend and renderer chosen, and why anything was passed over, to stderr. |
| `NACK_BACKEND=wayland\|x11` | Forces the windowing backend on Unix. |
| `NACK_RENDERER=metal\|opengl` | Tries that renderer first; the others stay behind it as fallbacks. |
| `NACK_WAYLAND_FORCE_CSD=1` | Draws libnack's own decorations even where xdg-decoration exists. |

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
returned. It is run twice: once normally, and once with the preferred renderer
forced to fail, which is how the fallback gets exercised on a machine with
only one working renderer.

`tests/image_test.c` covers the seam between libnack and the vendored decoder.
PNGs are generated by `tools/mkpng.py` across every colour type, bit depth and
row filter, interlaced and not, and compared pixel for pixel against reference
data the decoder had no part in producing. JPEG is lossy and has nothing exact
to compare against, so `tools/mkjpeg.py` — a small baseline encoder written
for the tests, since there is no encoder in the tree — produces images that
are decoded back and checked for size, orientation, opacity and drift. The two
implementations agree to within a few levels on every channel, which is worth
more than either checked against itself. Corrupt and truncated input must not
crash, and must not report a size it did not produce.

`tests/cpp_smoke.cpp` covers the C++ header: that handles free themselves and
free exactly once, that a moved-from handle does not double free, that views
and owners are interchangeable where a helper takes one, and that the variant
carries the arm the event type says it does. CI runs the whole suite a second
time under AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer,
which is what a RAII wrapper has to be held to.

`tests/win32_abi_check` compares the hand-rolled Win32 declarations with the
SDK.

Run here against Xvfb, sway and Weston, and cross-compiled for Windows with
MinGW. CI runs the suite on Linux, on macOS in both the Metal and
OpenGL-only configurations, and on Windows against Mesa's software OpenGL.

## Licence

MIT. The built-in font derives from Daniel Hepper's font8x8, which is public
domain.

Vendored under `third_party/`: stb_image, which is public domain or MIT at
your choice. Either way linking libnack carries no obligation beyond keeping
the notice. `third_party/VENDORING.md` records the version and the
configuration it is built with.
