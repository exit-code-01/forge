#!/usr/bin/env python3
"""Generates assets/textures/{concrete,floor,metal}.png - VAULT's three
material families (week 4 art pass). Pure stdlib (manual PNG encode via
zlib), deterministic. Regenerate with:  python tools/gen_textures.py
"""

import os
import random
import struct
import zlib

SIZE = 256


def write_png(path, pixels):  # pixels: list of rows of (r,g,b)
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    raw = b"".join(b"\x00" + b"".join(struct.pack("BBB", *px) for px in row) for row in pixels)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
    print(f"wrote {os.path.normpath(path)}")


def clamp(v):
    return max(0, min(255, int(v)))


def main() -> None:
    out = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")
    os.makedirs(out, exist_ok=True)
    rng = random.Random(7)

    # concrete: light warm gray, speckle noise, faint horizontal pour seams.
    rows = []
    for y in range(SIZE):
        row = []
        seam = 12 if y % 64 in (0, 1) else 0
        for x in range(SIZE):
            base = 168 + rng.randint(-9, 9) - seam
            row.append((clamp(base), clamp(base - 2), clamp(base - 6)))
        rows.append(row)
    write_png(os.path.join(out, "concrete.png"), rows)

    # floor: darker concrete with a 64px tile grid (reads scale at a glance).
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            grid = 18 if (x % 64 in (0, 1) or y % 64 in (0, 1)) else 0
            base = 128 + rng.randint(-7, 7) - grid
            row.append((clamp(base), clamp(base), clamp(base + 3)))
        rows.append(row)
    write_png(os.path.join(out, "floor.png"), rows)

    # metal: dark blue-gray, horizontal brush streaks, panel lines + rivets.
    # Tinted variants carry the colour language (week 9): red = locked,
    # green = solved/powered, orange = interactable. metal.png is generated
    # FIRST so its bytes stay identical to the week-4 original.
    def metal(filename, tint):
        streak = [rng.randint(-10, 10) for _ in range(SIZE)]
        rows = []
        for y in range(SIZE):
            row = []
            panel = 16 if y % 128 in (0, 1, 2) else 0
            for x in range(SIZE):
                rivet = 0
                if (x % 128 in (14, 15, 16)) and (y % 128 in (14, 15, 16)):
                    rivet = -26
                base = 96 + streak[y] + rng.randint(-3, 3) - panel + rivet
                row.append((clamp(base - 6 + tint[0]), clamp(base + tint[1]),
                            clamp(base + 10 + tint[2])))
            rows.append(row)
        write_png(os.path.join(out, filename), rows)

    metal("metal.png", (0, 0, 0))
    metal("metal_red.png", (62, -28, -38))
    metal("metal_green.png", (-30, 56, -28))
    metal("metal_orange.png", (68, 20, -52))
    # blue = SPARK/drone (week 11 character pass). Appended LAST so the rng
    # stream keeps every earlier texture byte-identical.
    metal("metal_blue.png", (-26, 6, 66))


if __name__ == "__main__":
    main()
