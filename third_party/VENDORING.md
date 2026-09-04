# Vendored code

Image decoding is parsing untrusted input from disk. That is the last place to
prefer something written here over code that has been attacked for years, so
libnack does not decode PNG or JPEG itself. The decoder is vendored in source
form rather than looked for on the system, so building libnack still needs
nothing installed.

Nothing in here is libnack's code, apart from the two files named below. It is
compiled with warnings off, since its warnings are not ours to fix and would
bury the ones that are, and with hidden visibility, so a program linking
libnack does not end up with a second copy of these symbols in its global
namespace.

## stb_image

| | |
| --- | --- |
| Version | 2.30 |
| Source | `https://github.com/nothings/stb` |
| Files | `stb_image.h`, `LICENSE` |
| Licence | public domain, or MIT — the file offers both |

`stb_image.h` is unmodified. `stb_image.c` and `CMakeLists.txt` in that
directory are libnack's: stb_image is a header carrying its own
implementation, emitted only where `STB_IMAGE_IMPLEMENTATION` is defined, and
`stb_image.c` is the one place that does it. It also pins the configuration,
which is deliberately narrow:

- `STBI_ONLY_PNG` and `STBI_ONLY_JPEG`. stb_image can also read BMP, TGA, PSD,
  GIF, HDR, PIC and PNM. None of those is a format a tileset should be, and
  every one left enabled is another parser reachable from a file the program
  did not write. Cutting them out means a BMP is refused, rather than quietly
  working on one machine and not another.
- `STBI_NO_STDIO`. Everything is decoded from memory.
- `STBI_NO_LINEAR` and `STBI_NO_HDR`. The console wants 8-bit RGBA and nothing
  else; these also drag in `pow()` from libm for code nothing calls.
- `STBI_MAX_DIMENSIONS 32768`, so an absurd size in a header is refused rather
  than attempted.

### What it covers

PNG at every colour type and bit depth, interlaced or not, and JPEG both
baseline and progressive. The libnack-written decoder this replaced handled
neither interlacing nor JPEG at all.

### One behaviour worth knowing

**stb_image is lenient about truncated JPEGs.** Once it has read the header it
returns an image of the declared size containing whatever scan data it managed
to read, rather than reporting an error — the same thing most image viewers
do. A truncated tileset therefore loads with its tail blank instead of failing
outright. Truncation before the header is still refused, and nothing here
reads out of bounds; `tests/image_test.c` checks both. If a future version of
libnack needs strictness instead, that is a check to add above stb_image, not
a patch to make to it.

### Updating

Replace `stb_image.h` and run the test suite. `stb_image.c` should not need
changing unless upstream renames a configuration macro.

## {fmt}

| | |
| --- | --- |
| Version | 12.2.0 |
| Source | `https://github.com/fmtlib/fmt` |
| Files | the full upstream release tree |
| Licence | MIT, with an exception for embedding the source |

Unmodified, and vendored whole (the real project's own `CMakeLists.txt`,
tests, docs and all) rather than a stripped-down copy of the headers a
header-only build needs. `add_subdirectory(third_party/fmt)` in the root
`CMakeLists.txt` builds it as {fmt} builds itself; libnack sets `FMT_INSTALL`
on so it exports itself alongside `nack` and links its own targets against
the `fmt::fmt` target that produces, same as any other consumer would.

Only `<nack/nack.hpp>` uses it. `print` takes a {fmt} format string there
rather than C varargs.

**{fmt}'s compile-time format checking needs C++20**, which is what libnack
builds as. Its `consteval` constructor rejects a mismatched format string
where the call is written, rather than at run time — so a bad `print` is a
build error, which varargs could never manage. `tests/cpp_bad_format.cpp`
holds one such call and is built by the `cpp_format_rejected` test, which
expects the build to fail; it is an OBJECT library rather than a program so
that only the compile can decide the outcome.

On MSVC that check needs two flags: `/utf-8`, which {fmt}'s own CMakeLists
already applies to everyone who links `fmt::fmt`, and `/Zc:__cplusplus`,
which it does not — that one is set directly on every target in the root
`CMakeLists.txt` that needs {fmt}'s compile-time checking to actually run.
Without it, MSVC reports `__cplusplus` as 199711L and {fmt} quietly falls back
to run-time
checking. Without the second, `cpp_format_rejected` would pass while proving
nothing.

## Test fixtures are not vendored

`tools/mkpng.py` and `tools/mkjpeg.py` generate the images
`tests/image_test.c` decodes, into the build tree. Nothing is checked in,
which is the point: pixels a decoder is compared against have to come from
somewhere other than that decoder. `mkjpeg.py` is a small baseline JPEG
encoder written for this — there is no encoder in the tree, and a decoder
checked against its own encoder proves very little either way.

That the two agree to within a few levels on every channel is a real result:
they are independent implementations of the same standard, and both being
wrong in the same direction is far less likely than either being wrong alone.
