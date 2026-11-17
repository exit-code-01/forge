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
    # dark = HEAVY (week 12 mass language: the launch-it-yourself cube).
    # Same append-last rule: everything above stays byte-identical.
    metal("metal_dark.png", (-38, -36, -30))

    # ---- Assembly/Reactor zone ----
    # Three more tints of the metal family, appended LAST on the shared rng
    # (same rule as blue/dark: everything above stays byte-identical).
    # yellow = POWER (batteries, power sockets); white = light painted metal
    # (lamps, lenses, data cores); violet = prism cubes.
    metal("metal_yellow.png", (96, 58, -70))
    metal("metal_white.png", (88, 80, 68))
    metal("metal_violet.png", (30, -24, 62))

    # The pattern textures below each own an independently seeded rng, so
    # adding/reordering them can never disturb another texture's bytes. All
    # are mid-value albedo (the renderer tints / adds emissive in code) and
    # tile seamlessly: every feature period divides SIZE and the wear noise
    # is sampled on a wrapping lattice.
    def wrap_noise(seed, cells):
        """Tileable value noise in 0..1: `cells` lattice cells across SIZE."""
        r = random.Random(seed)
        lat = [[r.random() for _ in range(cells)] for _ in range(cells)]
        step = SIZE / cells

        def at(x, y):
            fx, fy = x / step, y / step
            x0, y0 = int(fx) % cells, int(fy) % cells
            x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
            tx, ty = fx - int(fx), fy - int(fy)
            tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
            top = lat[y0][x0] + (lat[y0][x1] - lat[y0][x0]) * tx
            bot = lat[y1][x0] + (lat[y1][x1] - lat[y1][x0]) * tx
            return top + (bot - top) * ty
        return at

    # belt: dark ribbed rubber with pale chevron arrows, 2 across x 4 along.
    # DIRECTION: every chevron is a "^" whose TIP POINTS AT PNG ROW 0 (the
    # top of the image). In OBJ/GL texture space (v = 1 at the top row) that
    # is +V; in the engine's sampled space (image convention after the
    # loader's FlipUVs, v = 0 at the top row) it is -V. On the built-in cube
    # (sandbox buildCube) the +Y face maps image-top to world -Z, so an
    # unrotated belt slab shows arrows pointing toward -Z (game "forward").
    # Scrolling the UVs so the image moves toward row 0 moves the arrows in
    # the direction they point.
    r = random.Random(101)
    wear = wrap_noise(102, 8)
    rows = []
    for y in range(SIZE):
        row = []
        rib = -9 if y % 8 == 0 else (5 if y % 8 == 1 else 0)   # cross ribs
        for x in range(SIZE):
            dx = abs((x % 128) - 64)             # 0 on the arrow's centre line
            arm = (y - dx * 0.5) % 64            # centre leads -> tip is up
            w = wear(x, y)
            if dx < 50 and 22 <= arm < 36:
                base = 150 - 34 * max(0.0, w - 0.45) / 0.55 + r.randint(-5, 5)
                row.append((clamp(base), clamp(base), clamp(base - 6)))
            else:
                edge = -12 if dx >= 63 else 0    # lane seam between arrows
                base = 44 + 10 * w + rib + edge + r.randint(-4, 4)
                row.append((clamp(base), clamp(base), clamp(base + 3)))
        rows.append(row)
    write_png(os.path.join(out, "belt.png"), rows)

    # hazard: 45-degree yellow/black warning stripes (64 px period along
    # x + y, so 4 stripe pairs per tile), paint chipped and scuffed.
    r = random.Random(201)
    chips = wrap_noise(202, 16)
    grime = wrap_noise(203, 4)
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            d = (x + y) % 64
            n = r.randint(-6, 6)
            g = 1.0 - 0.16 * grime(x, y)
            chipped = chips(x, y) > 0.74
            if d < 32:
                col = (104, 100, 94) if chipped else (206, 164, 30)
            else:
                col = (70, 68, 66) if chipped else (38, 38, 42)
            row.append((clamp(col[0] * g + n), clamp(col[1] * g + n), clamp(col[2] * g + n)))
        rows.append(row)
    write_png(os.path.join(out, "hazard.png"), rows)

    # panel: brushed mid-grey wall plates, 2 x 2 per tile, recessed seams
    # with a lit lip, a rivet in each plate corner.
    r = random.Random(301)
    streak = [r.randint(-7, 7) for _ in range(SIZE)]
    plate_tone = [[r.randint(-7, 7) for _ in range(2)] for _ in range(2)]
    rows = []
    for y in range(SIZE):
        row = []
        py = y % 128
        for x in range(SIZE):
            px = x % 128
            base = 132 + plate_tone[y // 128][x // 128] + streak[y] + r.randint(-3, 3)
            if px < 2 or py < 2:
                base -= 40                       # seam groove
            elif px == 2 or py == 2:
                base += 14                       # lit lip
            elif px == 127 or py == 127:
                base -= 10                       # shadowed lip
            else:
                rx = min(abs(px - 14), abs(px - 115))
                ry = min(abs(py - 14), abs(py - 115))
                d2 = rx * rx + ry * ry
                if d2 <= 9:
                    base += 22 if d2 <= 2 else -4    # rivet head + glint
                elif d2 <= 20:
                    base -= 26                       # rivet shadow ring
            row.append((clamp(base - 3), clamp(base), clamp(base + 5)))
        rows.append(row)
    write_png(os.path.join(out, "panel.png"), rows)

    # grate: dark floor grating - diamond openings on a 32 px lattice (8 x 8
    # per tile) between flat diagonal bars, each bar with a lit/shadowed lip.
    r = random.Random(401)

    def hole(x, y):
        u = abs((x % 32) - 15.5) / 16.0
        v = abs((y % 32) - 15.5) / 16.0
        return u + v < 0.86

    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            if hole(x, y):
                base = 22 + r.randint(-3, 3)
            else:
                base = 78 + r.randint(-5, 5)
                if hole(x + 1, y + 1) or hole(x, y + 2):
                    base += 26                   # lip above/left of an opening
                elif hole(x - 1, y - 1) or hole(x, y - 2):
                    base -= 24                   # lip below/right of an opening
            row.append((clamp(base - 2), clamp(base), clamp(base + 4)))
        rows.append(row)
    write_png(os.path.join(out, "grate.png"), rows)


if __name__ == "__main__":
    main()
