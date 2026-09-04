# libnack

A cell console for roguelikes, in C++20. You get a grid of cells to draw glyphs
and tiles into, in the spirit of libtcod and BearLibTerminal.

libnack owns the window, the OpenGL context and the renderer. None of that is
exposed, because none of it is what a roguelike wants to think about. The whole
API is the console.

```cpp
#include <nack/nack.hpp>

int main()
{
    nack::config config = nack::default_config();
    config.title = "my roguelike";
    config.columns = 80;
    config.rows = 50;
    auto app = nack::app::try_create(config);
    if (!app)
        return 1;

    while (!app->should_close()) {
        nack::clear();
        nack::put(x, y, '@', nack::white, nack::black);
        nack::print(0, 49, nack::grey, nack::black, "you are in a maze");
        app->present();

        while (auto event = app->poll()) {
            if (auto *key = std::get_if<nack::key_event>(&*event))
                if (key->key == nack::key::h)
                    x--;
        }
    }
}
```

| Platform | Windowing | Renderer |
| --- | --- | --- |
| Linux, BSD | Wayland (xdg-shell) or XCB, chosen at run time | OpenGL 3.3 via EGL |
| Windows | Win32 | OpenGL 3.3 via WGL |
| macOS | Cocoa | Metal |

The library is C++20, and `<nack/nack.hpp>` is the only public header — there
is no separate C API to bind to. Consoles, tilesets and the app itself free
themselves; events arrive as a `std::variant`, so reading the wrong arm of the
union is a compile error rather than a convention in a comment; `poll()`
returns `std::optional` instead of a bool and an out-parameter; sizes come back
as structured bindings; and modifiers are a real bitmask type rather than a
bare `uint32_t`.

Drawing calls that act on the window's own console — `nack::clear()`,
`nack::print(...)`, `nack::put(...)` and so on — are free functions, since
there is always exactly one of those and naming it every time buys nothing.
Everything else — an offscreen `nack::console`, a `nack::tileset`, the
`nack::app` itself — is a method on the object it acts on.

## Why it looks like this

- **A console, not a terminal.** Cells are addressed directly and redrawn as
  you like. There is no scrollback, no reflow and no PTY; it is a grid you
  paint, which is what a roguelike wants.
- **Turn-based games can sleep.** `app.wait()` blocks until the player does
  something, so a game waiting on input costs no CPU at all. Real-time games
  call `app.poll()` in a loop and present every frame instead. Both work;
  neither is privileged.
- **The mouse arrives in cells.** A `mouse_move_event`/`mouse_button_event`'s
  `x`/`y` is a column, not a pixel, already through the letterbox and scale
  transform. Pixel positions are there too if you want them.
- **Keys are physical.** `nack::key` values are USB HID usage codes describing
  the position of a key, so a movement binding does not move when the player
  changes layout. The numeric keypad is distinct from the number row, because
  eight-way movement needs it. Text, for entering a character's name, arrives
  separately as a `text_event` after the platform's dead-key and IME handling;
  Ctrl chords produce key events and no text.
- **Whole-pixel scaling by default.** The console is scaled to the window by a
  whole number and letterboxed, so tiles stay crisp. `nack::scaling::fit` and
  `nack::scaling::stretch` are there if you would rather fill the window.

## Tilesets

The built-in 8x8 CP437 font means a game runs before it ships any assets,
including box drawing and block characters. Beyond that:

```cpp
// A CP437 sheet: the classic roguelike tileset layout.
nack::tileset font{"terminal16x16.png", 16, 16, nack::layout::cp437};
app->set_font(font);

// Graphical tiles, drawn by index rather than by codepoint.
nack::tileset tiles{"creatures.png", 32, 32, nack::layout::row_major};
nack::put_tile(x, y, tiles, ORC_TILE, nack::white, nack::black);

// Unicode past CP437's 256 slots.
font.map_range(0x4E00, 0x4E7F, 256);
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

## Consoles

Every drawing method lives on a console. The window's own console is reached
through the root free functions (`nack::clear()`, `nack::print(...)`, and so
on) or through `app.console()`; offscreen consoles are for composing:

```cpp
nack::console panel{20, 10};                 // freed on scope exit
panel.draw_box(0, 0, 20, 10, nack::grey, nack::black, "inventory");
panel.print(2, 2, nack::white, nack::black, "a) rusty sword");
panel.print(2, 3, nack::grey, nack::black, "{} gold", 120);
panel.blit_to(app->console(), 30, 20, 1.0f, 0.9f);
```

The last two arguments to `blit_to` are foreground and background alpha, so a
translucent overlay is a blit with a low background alpha.

`console_view` is the non-owning half — a reference to either the root or an
owned console — so one helper takes either kind:

```cpp
void draw_panel(nack::console_view console);   // the root or an owned console
```

`print` takes a {fmt} format string when you give it arguments, and plain text
when you do not — so a runtime string prints as it stands rather than being
mistaken for a format. A format string that does not match its arguments is
rejected where it is written — {fmt}'s compile-time checking, which is one of
the reasons the header asks for C++20. `tests/cpp_bad_format.cpp` is a file
that must fail to compile, and the test suite checks that it does.

Colours are `constexpr`: `nack::white`, `nack::rgb(...)`, and so on — typed,
scoped, and usable in constant expressions.

Exceptions are used only where construction fails, and every constructor that
can fail has a `try_create` counterpart returning `std::optional`. Compiled
with `-fno-exceptions` (or the MSVC equivalent), the throwing paths abort with
the message instead.

{fmt} is vendored under `third_party/fmt` — see [Building](#building) for
where that copy comes from and how to use a different one.

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

{fmt} is vendored under `third_party/fmt` (the real upstream project, not a
stripped-down copy), so building libnack needs nothing installed. A project
that embeds libnack via `add_subdirectory` and already has its own
`fmt::fmt` target — its own `add_subdirectory` or `FetchContent`, added
before it adds libnack — can point libnack at that one instead with
`-DNACK_FMT_EXTERNAL=ON`, the same idea as spdlog's `SPDLOG_FMT_EXTERNAL`:
one copy of {fmt} in the final binary rather than two. libnack skips its own
`install()` rules when this is on, since a target the including project
defined itself cannot be expressed in an installed package's exported
targets file.

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

`tests/console_smoke.cpp` drives the console through `<nack/nack.hpp>` and
verifies frames by reading pixels back out of the framebuffer, not merely by
checking that `present()` returned. It is run twice: once normally, and once
with the preferred renderer forced to fail, which is how the fallback gets
exercised on a machine with only one working renderer.

`tests/image_test.cpp` covers the seam between libnack and the vendored decoder.
PNGs are generated by `tools/mkpng.py` across every colour type, bit depth and
row filter, interlaced and not, and compared pixel for pixel against reference
data the decoder had no part in producing. JPEG is lossy and has nothing exact
to compare against, so `tools/mkjpeg.py` — a small baseline encoder written
for the tests, since there is no encoder in the tree — produces images that
are decoded back and checked for size, orientation, opacity and drift. The two
implementations agree to within a few levels on every channel, which is worth
more than either checked against itself. Corrupt and truncated input must not
crash, and must not report a size it did not produce.

`tests/cpp_smoke.cpp` is a second, smaller pass focused on the public header
itself: that handles free themselves and free exactly once, that a moved-from
handle does not double free, that views and owners are interchangeable where a
helper takes one, and that the variant carries the arm the event type says it
does. CI runs the whole suite a second time under AddressSanitizer,
UndefinedBehaviorSanitizer and LeakSanitizer, which is what a RAII wrapper has
to be held to.

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
