/*
 * The one translation unit that compiles stb_image's implementation.
 *
 * stb_image is a header that contains its own implementation, emitted only
 * where STB_IMAGE_IMPLEMENTATION is defined. This file is that place; every
 * other file just includes the header for the declarations.
 *
 * The configuration is deliberately narrow. stb_image can also read BMP, TGA,
 * PSD, GIF, HDR, PIC and PNM; none of those are formats a tileset should be,
 * and every one left enabled is more parser reachable from a file the program
 * did not write. STBI_ONLY_PNG and STBI_ONLY_JPEG cut them out at compile
 * time, so a BMP is refused rather than quietly working on one machine and
 * not another.
 */
#define STB_IMAGE_IMPLEMENTATION

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG

/* Everything is decoded from memory; the file reading is dead weight. */
#define STBI_NO_STDIO

/*
 * No floating point or HDR output either: the console wants 8-bit RGBA and
 * nothing else. Leaving these in would also drag in pow() from libm for code
 * nothing calls.
 */
#define STBI_NO_LINEAR
#define STBI_NO_HDR

/* Refuse absurd dimensions outright rather than trying to allocate them. */
#define STBI_MAX_DIMENSIONS 32768

#include "stb_image.h"
