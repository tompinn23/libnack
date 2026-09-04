"""Generate baseline JPEGs with known source pixels, for tests/image_test.cpp.

There is no JPEG encoder in the tree - stb_image only decodes - so the
fixtures are written here rather than produced by anything libnack links.
That is the point: an encoder from somewhere else is what makes decoding it
back a real test rather than a round trip through one implementation's own
idea of the format.

This is a plain baseline encoder: 8-bit, no subsampling, the quantisation and
Huffman tables out of the JPEG standard's Annex K. It is not trying to be a
good encoder, only a correct one.

Usage: mkjpeg.py <output directory>
"""
import struct, sys, os, math

ZIGZAG = [
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
]

# Annex K.1: the example luminance and chrominance quantisation tables.
QUANT_LUMA = [
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99,
]
QUANT_CHROMA = [
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
]

# Annex K.3: the example Huffman tables, as (bits, values) where bits[i] is
# how many codes have length i + 1.
DC_LUMA_BITS = [0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0]
DC_LUMA_VALS = list(range(12))
DC_CHROMA_BITS = [0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0]
DC_CHROMA_VALS = list(range(12))

AC_LUMA_BITS = [0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d]
AC_LUMA_VALS = [
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
    0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
    0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
]
AC_CHROMA_BITS = [0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77]
AC_CHROMA_VALS = [
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
    0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4,
    0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
]


def huffman_codes(bits, vals):
    """Canonical codes in the order the standard lays them out: shortest
    lengths first, incrementing, doubling at each new length."""
    codes = {}
    code = 0
    k = 0
    for length in range(1, 17):
        for _ in range(bits[length - 1]):
            codes[vals[k]] = (code, length)
            code += 1
            k += 1
        code <<= 1
    return codes


def scale_quant(table, quality):
    quality = max(1, min(100, quality))
    scale = 5000 // quality if quality < 50 else 200 - quality * 2
    out = []
    for v in table:
        q = (v * scale + 50) // 100
        out.append(max(1, min(255, q)))
    return out


COS = [[math.cos((2 * x + 1) * u * math.pi / 16) for u in range(8)]
       for x in range(8)]


def fdct(block):
    """The reference 8x8 DCT-II, written out rather than factored. Fixtures
    are tiny and clarity is worth more here than speed."""
    out = [0.0] * 64
    for v in range(8):
        for u in range(8):
            total = 0.0
            for y in range(8):
                for x in range(8):
                    total += block[y * 8 + x] * COS[x][u] * COS[y][v]
            cu = (1 / math.sqrt(2)) if u == 0 else 1.0
            cv = (1 / math.sqrt(2)) if v == 0 else 1.0
            out[v * 8 + u] = 0.25 * cu * cv * total
    return out


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.nbits = 0

    def write(self, code, length):
        for i in range(length - 1, -1, -1):
            self.acc = (self.acc << 1) | ((code >> i) & 1)
            self.nbits += 1
            if self.nbits == 8:
                self.out.append(self.acc)
                # A 0xFF byte in the entropy stream is escaped, or a decoder
                # would read it as the start of a marker.
                if self.acc == 0xFF:
                    self.out.append(0x00)
                self.acc = 0
                self.nbits = 0

    def flush(self):
        while self.nbits:
            self.write(1, 1)


def category(value):
    """How many bits the magnitude needs, which is what the Huffman symbol
    encodes; the bits themselves follow it."""
    n = 0
    magnitude = abs(value)
    while magnitude:
        magnitude >>= 1
        n += 1
    return n


def encode_bits(value, size):
    return value if value >= 0 else value + (1 << size) - 1


def encode_block(bw, coeffs, quant, dc_codes, ac_codes, prev_dc):
    q = [int(round(coeffs[i] / quant[i])) for i in range(64)]
    zz = [q[ZIGZAG[i]] for i in range(64)]

    diff = zz[0] - prev_dc
    size = category(diff)
    code, length = dc_codes[size]
    bw.write(code, length)
    if size:
        bw.write(encode_bits(diff, size), size)

    run = 0
    for i in range(1, 64):
        if zz[i] == 0:
            run += 1
            continue
        while run > 15:
            code, length = ac_codes[0xF0]        # ZRL: sixteen zeroes
            bw.write(code, length)
            run -= 16
        size = category(zz[i])
        code, length = ac_codes[(run << 4) | size]
        bw.write(code, length)
        bw.write(encode_bits(zz[i], size), size)
        run = 0
    if run:
        code, length = ac_codes[0x00]            # EOB
        bw.write(code, length)
    return zz[0]


def marker(tag, payload):
    return bytes([0xFF, tag]) + struct.pack(">H", len(payload) + 2) + payload


def dht(class_, ident, bits, vals):
    return marker(0xC4, bytes([(class_ << 4) | ident]) + bytes(bits) +
                  bytes(vals))


def encode_jpeg(path, w, h, pixels, quality=90, grey=False):
    """pixels is a list of rows of (r, g, b). No subsampling: every component
    is sampled 1x1, which keeps the block layout trivial."""
    qy = scale_quant(QUANT_LUMA, quality)
    qc = scale_quant(QUANT_CHROMA, quality)

    dc_luma = huffman_codes(DC_LUMA_BITS, DC_LUMA_VALS)
    ac_luma = huffman_codes(AC_LUMA_BITS, AC_LUMA_VALS)
    dc_chroma = huffman_codes(DC_CHROMA_BITS, DC_CHROMA_VALS)
    ac_chroma = huffman_codes(AC_CHROMA_BITS, AC_CHROMA_VALS)

    ncomp = 1 if grey else 3
    planes = [[[0.0] * w for _ in range(h)] for _ in range(ncomp)]
    for y in range(h):
        for x in range(w):
            r, g, b = pixels[y][x]
            luma = 0.299 * r + 0.587 * g + 0.114 * b
            planes[0][y][x] = luma - 128.0
            if not grey:
                planes[1][y][x] = -0.168736 * r - 0.331264 * g + 0.5 * b
                planes[2][y][x] = 0.5 * r - 0.418688 * g - 0.081312 * b

    body = b"\xFF\xD8"
    body += marker(0xE0, b"JFIF\x00" + bytes([1, 1, 0]) +
                   struct.pack(">HH", 1, 1) + bytes([0, 0]))
    body += marker(0xDB, bytes([0]) + bytes(qy[ZIGZAG[i]] for i in range(64)))
    if not grey:
        body += marker(0xDB, bytes([1]) +
                       bytes(qc[ZIGZAG[i]] for i in range(64)))

    frame = bytes([8]) + struct.pack(">HH", h, w) + bytes([ncomp])
    for c in range(ncomp):
        frame += bytes([c + 1, 0x11, 0 if c == 0 else 1])
    body += marker(0xC0, frame)

    body += dht(0, 0, DC_LUMA_BITS, DC_LUMA_VALS)
    body += dht(1, 0, AC_LUMA_BITS, AC_LUMA_VALS)
    if not grey:
        body += dht(0, 1, DC_CHROMA_BITS, DC_CHROMA_VALS)
        body += dht(1, 1, AC_CHROMA_BITS, AC_CHROMA_VALS)

    scan = bytes([ncomp])
    for c in range(ncomp):
        scan += bytes([c + 1, 0x00 if c == 0 else 0x11])
    scan += bytes([0, 63, 0])
    body += marker(0xDA, scan)

    bw = BitWriter()
    prev = [0] * ncomp
    for by in range(0, h, 8):
        for bx in range(0, w, 8):
            for c in range(ncomp):
                block = []
                for y in range(8):
                    for x in range(8):
                        # Edge blocks repeat the last row and column, which is
                        # what the standard expects for a partial MCU.
                        sy = min(by + y, h - 1)
                        sx = min(bx + x, w - 1)
                        block.append(planes[c][sy][sx])
                quant = qy if c == 0 else qc
                dcc = dc_luma if c == 0 else dc_chroma
                acc = ac_luma if c == 0 else ac_chroma
                prev[c] = encode_block(bw, fdct(block), quant, dcc, acc,
                                       prev[c])
    bw.flush()
    body += bytes(bw.out)
    body += b"\xFF\xD9"
    open(path, "wb").write(body)


if len(sys.argv) != 2:
    sys.exit("usage: mkjpeg.py <output directory>")
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)

W, H = 61, 43           # deliberately not a multiple of 8, to exercise the
                        # partial blocks at the right and bottom edges

# Smooth gradients rather than noise: JPEG mangles sharp edges, and a test
# that allows for that would not be checking much.
image = [[((x * 4) % 256, (y * 5) % 256, 128) for x in range(W)]
         for y in range(H)]

cases = []
encode_jpeg(os.path.join(OUT, "colour.jpg"), W, H, image, quality=95)
cases.append(("colour.jpg", W, H, image))

grey_source = [[(v, v, v) for v in ((x * 3 + y * 2) % 256 for x in range(W))]
               for y in range(H)]
encode_jpeg(os.path.join(OUT, "grey.jpg"), W, H, grey_source, quality=95,
            grey=True)
cases.append(("grey.jpg", W, H, grey_source))

# The pixels the decoder should come back with, near enough. Same layout as
# mkpng.py's expected.bin, but RGB: JPEG has no alpha to compare.
with open(os.path.join(OUT, "expected_jpeg.bin"), "wb") as f:
    f.write(struct.pack("<I", len(cases)))
    for name, w, h, px in cases:
        nb = name.encode()
        f.write(struct.pack("<I", len(nb)) + nb + struct.pack("<II", w, h))
        for row in px:
            for p in row:
                f.write(bytes(p))
print("generated %d JPEG cases" % len(cases))
