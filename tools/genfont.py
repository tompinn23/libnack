"""Build the built-in CP437 font: ASCII from font8x8, the rest synthesised."""
import re

src = open('examples/font8x8_basic.h').read()
rows = re.findall(r'\{\s*((?:0x[0-9A-Fa-f]{2}\s*,\s*){7}0x[0-9A-Fa-f]{2})\s*\}', src)
assert len(rows) == 128

glyphs = [[int(v.strip(), 16) for v in r.split(',')] for r in rows]
glyphs += [[0]*8 for _ in range(128)]        # 128..255 filled in below

def setpix(g, x, y):
    g[y] |= 1 << x                            # font8x8 is LSB-leftmost

def hline(g, y, x0=0, x1=7):
    for x in range(x0, x1+1): setpix(g, x, y)

def vline(g, x, y0=0, y1=7):
    for y in range(y0, y1+1): setpix(g, x, y)

MID_X, MID_Y = 3, 3          # where the single-line box strokes cross

def box(up, down, left, right):
    g = [0]*8
    if up:    vline(g, MID_X, 0, MID_Y)
    if down:  vline(g, MID_X, MID_Y, 7)
    if left:  hline(g, MID_Y, 0, MID_X)
    if right: hline(g, MID_Y, MID_X, 7)
    return g

# CP437 single-line box drawing.
glyphs[179] = box(1,1,0,0)   # vertical
glyphs[196] = box(0,0,1,1)   # horizontal
glyphs[218] = box(0,1,0,1)   # top-left
glyphs[191] = box(0,1,1,0)   # top-right
glyphs[192] = box(1,0,0,1)   # bottom-left
glyphs[217] = box(1,0,1,0)   # bottom-right
glyphs[195] = box(1,1,0,1)   # tee right
glyphs[180] = box(1,1,1,0)   # tee left
glyphs[194] = box(0,1,1,1)   # tee down
glyphs[193] = box(1,0,1,1)   # tee up
glyphs[197] = box(1,1,1,1)   # cross

# Shade and block characters.
def shade(pattern):
    g = [0]*8
    for y in range(8):
        for x in range(8):
            if pattern(x, y): setpix(g, x, y)
    return g

glyphs[176] = shade(lambda x, y: (x + y) % 4 == 0)          # light shade
glyphs[177] = shade(lambda x, y: (x + y) % 2 == 0)          # medium shade
glyphs[178] = shade(lambda x, y: (x + y) % 4 != 0)          # dark shade
glyphs[219] = shade(lambda x, y: True)                      # full block
glyphs[220] = shade(lambda x, y: y >= 4)                    # lower half
glyphs[223] = shade(lambda x, y: y < 4)                     # upper half
glyphs[221] = shade(lambda x, y: x < 4)                     # left half
glyphs[222] = shade(lambda x, y: x >= 4)                    # right half
glyphs[254] = shade(lambda x, y: 2 <= x <= 5 and 2 <= y <= 5)  # centred square
glyphs[250] = shade(lambda x, y: x in (3, 4) and y in (3, 4))  # middle dot
glyphs[249] = glyphs[250]

# A few of the CP437 pictorials roguelikes actually use.
glyphs[1]  = shade(lambda x, y: 1 <= x <= 6 and 1 <= y <= 6 and
                                not (y in (3,) and x in (2, 5)))   # smiley
glyphs[2]  = glyphs[1]
glyphs[3]  = shade(lambda x, y: abs(x-3.5) + abs(y-3.5) < 3)       # heart-ish
glyphs[4]  = shade(lambda x, y: abs(x-3.5) + abs(y-3.5) < 3)       # diamond
glyphs[7]  = shade(lambda x, y: (x-3.5)**2 + (y-3.5)**2 < 3.0)     # bullet
glyphs[15] = shade(lambda x, y: (x-3.5)**2 + (y-3.5)**2 < 9.0 and
                                (x-3.5)**2 + (y-3.5)**2 > 2.0)     # sun/ring
glyphs[24] = shade(lambda x, y: x in (3,4) or (y < 3 and abs(x-3.5) < 3.5-y))
glyphs[25] = shade(lambda x, y: x in (3,4) or (y > 4 and abs(x-3.5) < y-4.5))
glyphs[26] = shade(lambda x, y: y in (3,4) or (x > 4 and abs(y-3.5) < x-4.5))
glyphs[27] = shade(lambda x, y: y in (3,4) or (x < 3 and abs(y-3.5) < 3.5-x))

out = ['''/*
 * The built-in 8x8 CP437 font, so a game can run before it ships any assets.
 *
 * Codepoints 0x20 to 0x7E come from Daniel Hepper's font8x8 (public domain,
 * after Marcel Sondaar's public domain VGA fonts). The box drawing, block and
 * shade characters, and a handful of the CP437 pictorials roguelikes lean on,
 * are generated rather than copied - see tools/genfont.py.
 *
 * One byte per row, least significant bit leftmost.
 */
#ifndef NACK_FONT8X8_H_INCLUDED
#define NACK_FONT8X8_H_INCLUDED

#include <stdint.h>

#define NACK_BUILTIN_FONT_W 8
#define NACK_BUILTIN_FONT_H 8
#define NACK_BUILTIN_FONT_COUNT 256

static const uint8_t nack__builtin_font[NACK_BUILTIN_FONT_COUNT][8] = {
''']
for i, g in enumerate(glyphs):
    out.append("    { %s },  /* %d */\n" % (", ".join("0x%02X" % b for b in g), i))
out.append("};\n\n#endif /* NACK_FONT8X8_H_INCLUDED */\n")
open('src/console/nack_font8x8.h', 'w').write("".join(out))
print("wrote 256 glyphs")
