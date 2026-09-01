"""Generate PNGs with known pixel content, across colour types, bit depths and
row filters, plus the expected RGBA output for each.

Usage: mkpng.py <output directory>. The build runs this into the build tree so
tests/png_test.c has fixtures to compare against; nothing is checked in,
because the point is that they are reproducible.
"""
import zlib, struct, os, random, sys

if len(sys.argv) != 2:
    sys.exit("usage: mkpng.py <output directory>")
OUT = sys.argv[1]

def out(name):
    return os.path.join(OUT, name)

def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
    return a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)

def filter_row(ftype, row, prev, bpp):
    out = bytearray()
    for i, v in enumerate(row):
        left = row[i-bpp] if i >= bpp else 0
        up = prev[i] if prev else 0
        ul = prev[i-bpp] if (prev and i >= bpp) else 0
        if ftype == 0: f = v
        elif ftype == 1: f = v - left
        elif ftype == 2: f = v - up
        elif ftype == 3: f = v - ((left + up) >> 1)
        else: f = v - paeth(left, up, ul)
        out.append(f & 0xFF)
    return out

# Adam7: (x start, y start, x step, y step) for each of the seven passes.
ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
         (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]

def interlace(w, h, pixels, bpp):
    """Splits 8-bit-per-sample pixel rows into the seven Adam7 sub-images,
    each filtered on its own, which is what the interlaced format is."""
    raw = bytearray()
    for xs, ys, xstep, ystep in ADAM7:
        prev = None
        for y in range(ys, h, ystep):
            row = bytearray()
            for x in range(xs, w, xstep):
                row += pixels[y][x * bpp:(x + 1) * bpp]
            if not row:
                continue
            raw.append(0)                      # filter type 0, none
            raw += filter_row(0, row, prev, bpp)
            prev = row
        # A pass with no rows at all contributes nothing, not even a filter byte.
    return raw

def write_png(path, w, h, colour, depth, rows_bytes, bpp, filters, palette=None,
              trns=None, interlaced=False):
    if interlaced:
        raw = interlace(w, h, rows_bytes, bpp)
    else:
        raw = bytearray()
        prev = None
        for y, row in enumerate(rows_bytes):
            ft = filters[y % len(filters)]
            raw.append(ft)
            raw += filter_row(ft, row, prev, bpp)
            prev = row
    body = b"\x89PNG\r\n\x1a\n"
    body += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, colour, 0, 0,
                                       1 if interlaced else 0))
    if palette is not None:
        body += chunk(b"PLTE", bytes(palette))
    if trns is not None:
        body += chunk(b"tRNS", bytes(trns))
    comp = zlib.compress(bytes(raw), 9)
    # Split across two IDATs to exercise multi-chunk handling.
    half = len(comp) // 2
    body += chunk(b"IDAT", comp[:half])
    body += chunk(b"IDAT", comp[half:])
    body += chunk(b"IEND", b"")
    open(path, "wb").write(body)

os.makedirs(OUT, exist_ok=True)
random.seed(7)
cases = []
W, H = 23, 17          # deliberately not a multiple of anything

pix = [[(random.randrange(256), random.randrange(256), random.randrange(256),
         random.randrange(256)) for _ in range(W)] for _ in range(H)]

allf = [0,1,2,3,4]

# colour 6: RGBA8
rows = [bytearray(b for p in row for b in p) for row in pix]
write_png(out("rgba8.png"), W, H, 6, 8, rows, 4, allf)
cases.append(("rgba8.png", W, H, [[p for p in row] for row in pix]))

# The same image again, Adam7 interlaced. The decoder libnack used to carry
# rejected these outright; LodePNG does not, so this is here to keep that true.
write_png(out("rgba8i.png"), W, H, 6, 8, rows, 4, allf, interlaced=True)
cases.append(("rgba8i.png", W, H, [[p for p in row] for row in pix]))

# colour 2: RGB8
rows = [bytearray(b for p in row for b in p[:3]) for row in pix]
write_png(out("rgb8.png"), W, H, 2, 8, rows, 3, allf)
cases.append(("rgb8.png", W, H, [[(p[0],p[1],p[2],255) for p in row] for row in pix]))

# colour 0: grey8
rows = [bytearray(p[0] for p in row) for row in pix]
write_png(out("grey8.png"), W, H, 0, 8, rows, 1, allf)
cases.append(("grey8.png", W, H, [[(p[0],p[0],p[0],255) for p in row] for row in pix]))

# colour 4: grey+alpha 8
rows = [bytearray(b for p in row for b in (p[0], p[3])) for row in pix]
write_png(out("greya8.png"), W, H, 4, 8, rows, 2, allf)
cases.append(("greya8.png", W, H, [[(p[0],p[0],p[0],p[3]) for p in row] for row in pix]))

# colour 3: palette 8-bit with tRNS
pal = [(i*7 % 256, i*13 % 256, i*29 % 256) for i in range(16)]
palb = bytes(b for c in pal for b in c)
alpha = bytes([255, 0] + [255]*14)
idx = [[random.randrange(16) for _ in range(W)] for _ in range(H)]
rows = [bytearray(row) for row in idx]
write_png(out("pal8.png"), W, H, 3, 8, rows, 1, allf, palette=palb, trns=alpha)
cases.append(("pal8.png", W, H,
              [[(pal[i][0], pal[i][1], pal[i][2], alpha[i]) for i in row] for row in idx]))

# colour 3: palette 4-bit (sub-byte packing)
idx4 = [[random.randrange(16) for _ in range(W)] for _ in range(H)]
rows = []
for row in idx4:
    b = bytearray()
    for i in range(0, W, 2):
        hi = row[i]
        lo = row[i+1] if i+1 < W else 0
        b.append((hi << 4) | lo)
    rows.append(b)
write_png(out("pal4.png"), W, H, 3, 4, rows, 1, [0], palette=palb, trns=alpha)
cases.append(("pal4.png", W, H,
              [[(pal[i][0], pal[i][1], pal[i][2], alpha[i]) for i in row] for row in idx4]))

# colour 3: palette 1-bit
idx1 = [[random.randrange(2) for _ in range(W)] for _ in range(H)]
rows = []
for row in idx1:
    b = bytearray()
    for i in range(0, W, 8):
        v = 0
        for k in range(8):
            if i+k < W and row[i+k]:
                v |= 1 << (7-k)
        b.append(v)
    rows.append(b)
write_png(out("pal1.png"), W, H, 3, 1, rows, 1, [0], palette=palb, trns=alpha)
cases.append(("pal1.png", W, H,
              [[(pal[i][0], pal[i][1], pal[i][2], alpha[i]) for i in row] for row in idx1]))

# A big one, to exercise long back-references in the inflate window.
BW, BH = 300, 200
big = [[((x*7+y*3) % 256, (x*x+y) % 256, (x^y) % 256, 255) for x in range(BW)] for y in range(BH)]
rows = [bytearray(b for p in row for b in p) for row in big]
write_png(out("big.png"), BW, BH, 6, 8, rows, 4, allf)
cases.append(("big.png", BW, BH, big))

with open(out("expected.bin"), "wb") as f:
    f.write(struct.pack("<I", len(cases)))
    for name, w, h, px in cases:
        nb = name.encode()
        f.write(struct.pack("<I", len(nb)) + nb + struct.pack("<II", w, h))
        for row in px:
            for p in row:
                f.write(bytes(p))
print("generated %d PNG cases" % len(cases))
