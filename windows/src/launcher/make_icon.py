"""make_icon.py <out.ico>: the launcher's icon, drawn here (no image files in the source tree, no game art): a yellow rounded square with a
green leaf, at 256, 64, 48, 32, 24 and 16 pixels, each a PNG inside the .ico. Standard library only."""
import math, struct, sys, zlib

YELLOW, YELLOW_DARK, GREEN, GREEN_DARK, RIB = (255, 210, 63), (230, 176, 30), (46, 139, 87), (31, 104, 63), (127, 211, 160)


def inside_rounded(x, y, r=0.22):
    cx, cy = min(max(x, r), 1 - r), min(max(y, r), 1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


AX = math.radians(-40)                   # the leaf points up and to the right
UX, UY = math.cos(AX), math.sin(AX)      # along the leaf
VX, VY = -UY, UX                         # across it
CX, CY, R, D = 0.5, 0.53, 0.40, 0.255    # the leaf is where two circles of radius R, D either side of its middle line, overlap


def leaf(x, y):
    px, py = x - CX, y - CY
    u, v = px * UX + py * UY, px * VX + py * VY
    lens = (v - D) ** 2 + u * u <= R * R and (v + D) ** 2 + u * u <= R * R
    half = math.sqrt(R * R - D * D)
    stem = -half - 0.10 <= u <= -half + 0.02 and abs(v) <= 0.022
    rib = lens and abs(v) <= 0.014 and u < half - 0.06
    return lens, stem, rib


def colour(x, y):
    if not inside_rounded(x, y): return None
    lens, stem, rib = leaf(x, y)
    if rib: return RIB
    if lens: return GREEN if (x - CX) * VX + (y - CY) * VY < 0 else GREEN_DARK   # two tones across the middle line
    if stem: return GREEN_DARK
    t = max(0.0, (y - 0.55) / 0.45)                                              # a little shade toward the bottom
    return tuple(round(a + (b - a) * t) for a, b in zip(YELLOW, YELLOW_DARK))


def draw(size, ss=4):
    rows = []
    for j in range(size):
        row = bytearray(b'\0')                                                 # PNG filter: none
        for i in range(size):
            acc = [0, 0, 0, 0]
            for sj in range(ss):
                for si in range(ss):
                    c = colour((i + (si + 0.5) / ss) / size, (j + (sj + 0.5) / ss) / size)
                    if c: acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; acc[3] += 255
            k = ss * ss; a = acc[3] // k
            row += bytes((acc[0] * 255 // acc[3], acc[1] * 255 // acc[3], acc[2] * 255 // acc[3], a) if acc[3] else (0, 0, 0, 0))
        rows.append(bytes(row))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b''))


def main(out):
    sizes = (256, 64, 48, 32, 24, 16)
    pngs = [draw(s, 4 if s >= 64 else 8) for s in sizes]
    head = struct.pack('<HHH', 0, 1, len(sizes)); off = 6 + 16 * len(sizes); dirs = b''
    for s, p in zip(sizes, pngs):
        dirs += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(p), off); off += len(p)
    with open(out, 'wb') as f: f.write(head + dirs + b''.join(pngs))


if __name__ == '__main__':
    main(sys.argv[1])
