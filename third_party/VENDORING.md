# Vendored code

Image decoding is parsing untrusted input from disk. That is the last place to
prefer something written here over code that has been attacked for years, so
libnack does not decode PNG or JPEG itself. Both decoders are vendored in
source form rather than looked for on the system, so building libnack still
needs nothing installed.

Nothing in here is libnack's code. It is compiled with warnings off, since
its warnings are not ours to fix and would bury the ones that are, and with
hidden visibility, so a program linking libnack does not end up with a second
copy of these symbols in its global namespace.

## LodePNG

| | |
| --- | --- |
| Version | 20260119 |
| Source | `https://github.com/lvandeve/lodepng` |
| Files | `lodepng.h`, `lodepng.c`, `LICENSE` |
| Licence | zlib |

Upstream ships the implementation as `lodepng.cpp`. It is also valid C — the
C++ API is behind `LODEPNG_COMPILE_CPP` — so it is renamed to `lodepng.c`
here and compiled as C99, because this project has no C++ in it. That rename
is the only modification.

The build defines `LODEPNG_NO_COMPILE_ENCODER`, `LODEPNG_NO_COMPILE_DISK` and
`LODEPNG_NO_COMPILE_CPP`: only decoding is wanted, and the rest is code
nothing could reach. Error text stays, because it is what a bad tileset
reports back to the caller.

LodePNG carries its own inflate, so there is no zlib here.

### Updating

Replace `lodepng.h`, drop the new `lodepng.cpp` in as `lodepng.c`, and run the
test suite. Nothing else should need changing.

## libjpeg-turbo

| | |
| --- | --- |
| Version | 3.1.2 |
| Source | `https://github.com/libjpeg-turbo/libjpeg-turbo` |
| Files | `src/`, `LICENSE.md`, `README.ijg` |
| Licence | IJG, plus BSD-3-Clause and zlib for parts (see `LICENSE.md`) |
| API used | TurboJPEG (`turbojpeg.h`) |

The sources are unmodified. What is not here: the SIMD extensions, the
command-line tools (`cjpeg`, `djpeg`, `jpegtran`, `tjbench`), the tests, the
Java bindings, the fuzzers, the documentation and the test images.

`CMakeLists.txt` in that directory is libnack's, not upstream's. Upstream's
build refuses to be used from another project:

> The libjpeg-turbo build system cannot be integrated into another build
> system using add_subdirectory(). Use ExternalProject_Add() instead.

`ExternalProject_Add` fetches and builds at build time, which is the opposite
of vendoring, so the source list and the configure-time checks are transcribed
from upstream's `CMakeLists.txt` instead. Three things about that are worth
knowing before an update:

- **SIMD is off.** On x86 it would need NASM, for a speedup nothing here
  wants: a tileset is decoded once at startup. Everything SIMD in 3.x sits
  behind `#ifdef WITH_SIMD`, so leaving it undefined is the whole of it.
- **Some `.c` files are `#include`d, not compiled.** The `src/wrapper/*.c`
  stubs each include one implementation file at a given sample precision,
  which is how 3.x builds 8-, 12- and 16-bit support from one source tree.
  Deleting a `.c` file because it is not in the source list will break the
  build.
- **`BMP_SUPPORTED` and `PPM_SUPPORTED` are required.** `tj3LoadImage` and
  `tj3SaveImage` are part of the TurboJPEG API and call into the BMP and PPM
  readers and writers, so `turbojpeg.c` does not link without them. Nothing in
  libnack reads a BMP or a PPM.

### Updating

Take a release tarball, copy `src/*.c`, `src/*.h`, `src/*.h.in` and
`src/wrapper/*.c`, then delete the files with a `main()` in them and the
readers and writers for formats the TurboJPEG API does not need
(`rdgif.c`, `rdtarga.c`, `wrgif.c`, `wrtarga.c`, and so on — `rdbmp.c`,
`wrbmp.c`, `rdppm.c` and `wrppm.c` must stay). Then diff upstream's
`JPEG_SOURCES` and `TURBOJPEG_SOURCES` against the lists in the local
`CMakeLists.txt`, and update `VERSION`, `COPYRIGHT_YEAR` and
`LIBJPEG_TURBO_VERSION_NUMBER` there.
