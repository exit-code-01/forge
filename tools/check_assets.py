#!/usr/bin/env python3
"""Sanity checks for the Assembly/Reactor asset set (stdlib only).

Run from anywhere:  python tools/check_assets.py     (exit code 0 = all pass)

Models   - triangles only, indices valid, unit normals, bounds inside the
           unit box AND reaching it, watertight, positive signed volume
           (divergence theorem) and geometric-normal . vertex-normal > 0.
Textures - same size/format as the metal family, seamless at the wrap.
Sounds   - same WAV format as chime.wav, no clipping, one-shots start/end
           at zero, loops wrap with no step in value or slope.

The generators stay the source of truth; this only reads their output.
"""

import math
import os
import struct
import sys
import wave
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")
FAILS = []


def check(ok, what):
    if not ok:
        FAILS.append(what)
    return ok


# ---------------------------------------------------------------- models ----

NEW_MODELS = ("battery", "prism", "core", "tesla", "lamp", "barrel", "pipe", "console", "roller",
              "strip")
# The week-11/12 unit-bounds props: checked for validity/winding only (they
# predate the "fill the box" rule - socket and bell deliberately do not, and
# door's hub pokes 1 cm proud on purpose).
OLD_UNIT_MODELS = ("crate", "plate", "door", "emitter", "receiver", "bell", "socket")


def load_obj(path):
    v, vn, vt, faces = [], [], [], []
    with open(path, encoding="ascii") as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "v":
                v.append(tuple(float(x) for x in parts[1:4]))
            elif parts[0] == "vn":
                vn.append(tuple(float(x) for x in parts[1:4]))
            elif parts[0] == "vt":
                vt.append(tuple(float(x) for x in parts[1:3]))
            elif parts[0] == "f":
                faces.append([tuple(int(x) for x in corner.split("/")) for corner in parts[1:]])
    return v, vn, vt, faces


def check_model(name, must_fill):
    path = os.path.join(ROOT, "models", name + ".obj")
    v, vn, vt, faces = load_obj(path)
    tag = f"model {name}"
    check(len(faces) > 0, f"{tag}: no faces")
    check(all(len(f) == 3 for f in faces), f"{tag}: non-triangle face")
    check(all(len(c) == 3 for f in faces for c in f), f"{tag}: face corner without v/vt/vn")
    ok_idx = all(1 <= c[0] <= len(v) and 1 <= c[1] <= len(vt) and 1 <= c[2] <= len(vn)
                 for f in faces for c in f)
    if not check(ok_idx, f"{tag}: index out of range"):
        return
    check(all(abs(math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) - 1.0) < 1e-4 for n in vn),
          f"{tag}: non-unit normal")
    check(all(-1e-6 <= c <= 1.0 + 1e-6 for uv in vt for c in uv), f"{tag}: UV outside 0..1")

    lo = [min(p[i] for p in v) for i in range(3)]
    hi = [max(p[i] for p in v) for i in range(3)]
    if must_fill:
        check(all(lo[i] >= -0.5 - 1e-5 and hi[i] <= 0.5 + 1e-5 for i in range(3)),
              f"{tag}: outside unit bounds {lo} {hi}")
        check(all(lo[i] <= -0.47 and hi[i] >= 0.47 for i in range(3)),
              f"{tag}: does not reach the unit box {lo} {hi}")

    # Weld by position, then: no degenerate faces, per-face normal agreement,
    # watertight (every directed edge has its opposite), per-shell volume.
    weld = {}
    def wid(p):
        return weld.setdefault(tuple(round(c, 5) for c in p), len(weld))
    parent = {}
    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    edges = {}
    tris = []
    worst_dot = 1.0
    degenerate = 0
    for f in faces:
        p = [v[c[0] - 1] for c in f]
        a = [p[1][i] - p[0][i] for i in range(3)]
        b = [p[2][i] - p[0][i] for i in range(3)]
        g = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
        mag = math.sqrt(g[0] ** 2 + g[1] ** 2 + g[2] ** 2)
        if mag < 1e-9:
            degenerate += 1
            continue
        for c in f:
            n = vn[c[2] - 1]
            worst_dot = min(worst_dot, (g[0] * n[0] + g[1] * n[1] + g[2] * n[2]) / mag)
        ids = [wid(q) for q in p]
        for k in range(3):
            e = (ids[k], ids[(k + 1) % 3])
            edges[e] = edges.get(e, 0) + 1
            parent[find(ids[k])] = find(ids[(k + 1) % 3])
        tris.append((ids[0], p))
    check(degenerate == 0, f"{tag}: {degenerate} zero-area faces")
    check(worst_dot > 0.0, f"{tag}: face winding disagrees with its normals (dot {worst_dot:.3f})")
    open_edges = sum(1 for (a, b), n in edges.items() if edges.get((b, a), 0) != n)
    check(open_edges == 0, f"{tag}: not watertight ({open_edges} unmatched directed edges)")
    vol = {}
    for first, p in tris:
        six = (p[0][0] * (p[1][1] * p[2][2] - p[1][2] * p[2][1])
               - p[0][1] * (p[1][0] * p[2][2] - p[1][2] * p[2][0])
               + p[0][2] * (p[1][0] * p[2][1] - p[1][1] * p[2][0]))
        vol[find(first)] = vol.get(find(first), 0.0) + six / 6.0
    check(all(x > 1e-7 for x in vol.values()),
          f"{tag}: inside-out shell (min signed volume {min(vol.values()):.6f})")
    print(f"  {name:9s} {len(faces):4d} tris  {len(vol):2d} shells  vol {sum(vol.values()):.3f}  "
          f"min n.g {worst_dot:.2f}  bounds x[{lo[0]:+.3f},{hi[0]:+.3f}] "
          f"y[{lo[1]:+.3f},{hi[1]:+.3f}] z[{lo[2]:+.3f},{hi[2]:+.3f}]")


# -------------------------------------------------------------- textures ----

def load_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, head = 8, b"", None
    while pos < len(data):
        (length,), kind = struct.unpack(">I", data[pos:pos + 4]), data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            head = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    w, h, depth, ctype, _, _, interlace = head
    assert depth == 8 and interlace == 0 and ctype in (2, 6), "unsupported PNG layout"
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        ft = raw[y * (stride + 1)]
        cur = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = cur[i - bpp] if i >= bpp else 0
            b, c = prev[i], (prev[i - bpp] if i >= bpp else 0)
            if ft == 1:
                cur[i] = (cur[i] + a) & 255
            elif ft == 2:
                cur[i] = (cur[i] + b) & 255
            elif ft == 3:
                cur[i] = (cur[i] + (a + b) // 2) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                cur[i] = (cur[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(cur[x * bpp:x * bpp + 3]) for x in range(w)])
        prev = cur
    return (w, h, depth, ctype), rows


# name -> (row period, column period) of the pattern in pixels. The seam test
# compares the wrap step (last row -> row 0) with the SAME structural step
# inside the tile (row P-1 -> row P): a seamless texture cannot tell them
# apart. None = pure per-pixel noise family (metal_*), where the wrap is
# compared with the worst interior neighbour step instead.
NEW_TEXTURES = {"belt": (64, 128), "hazard": (64, 64), "panel": (128, 128), "grate": (32, 32),
                "metal_yellow": (128, None), "metal_white": (128, None),
                "metal_violet": (128, None)}


def line_diff(a, b):
    return sum(abs(p[i] - q[i]) for p, q in zip(a, b) for i in range(3)) / (3.0 * len(a))


def check_texture(name, periods, ref_head):
    head, rows = load_png(os.path.join(ROOT, "textures", name + ".png"))
    tag = f"texture {name}"
    check(head == ref_head, f"{tag}: header {head} differs from metal.png {ref_head}")
    h, w = len(rows), len(rows[0])
    cols = [[rows[y][x] for y in range(h)] for x in range(w)]
    report = []
    for label, lines, period in (("rows", rows, periods[0]), ("cols", cols, periods[1])):
        wrap = line_diff(lines[-1], lines[0])
        if period:
            inner = line_diff(lines[period - 1], lines[period])
        else:
            inner = max(line_diff(lines[k], lines[k + 1]) for k in range(len(lines) - 1))
        check(wrap <= inner * 1.3 + 1.5,
              f"{tag}: visible seam across {label} (wrap step {wrap:.2f} vs interior {inner:.2f})")
        report.append(f"{label} wrap {wrap:5.2f} / interior {inner:5.2f}")
    mean = [sum(p[i] for r in rows for p in r) / (w * h) for i in range(3)]
    check(30 <= sum(mean) / 3 <= 200, f"{tag}: not a mid-value albedo (mean {mean})")
    print(f"  {name:13s} {w}x{h} RGB8  mean ({mean[0]:5.1f},{mean[1]:5.1f},{mean[2]:5.1f})  "
          + "  ".join(report))


# ---------------------------------------------------------------- sounds ----

# name -> (target seconds, is_loop)
NEW_SOUNDS = {"zap": (0.45, False), "tesla_charge": (0.7, False), "hum_belt": (2.0, True),
              "hum_platform": (2.0, True), "pickup": (0.5, False), "power_up": (0.6, False),
              "power_down": (0.5, False), "impact_soft": (0.15, False),
              "impact_hard": (0.25, False), "chirp_hint": (0.35, False),
              "chirp_sad": (0.35, False), "prism": (0.6, False), "door_hiss": (0.5, False),
              "fanfare": (2.5, False), "secret": (1.2, False)}


def load_wav(path):
    with wave.open(path, "rb") as w:
        fmt = (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getcomptype())
        raw = w.readframes(w.getnframes())
    return fmt, [s[0] for s in struct.iter_unpack("<h", raw)]


def check_sound(name, seconds, is_loop, ref_fmt):
    fmt, s = load_wav(os.path.join(ROOT, "sounds", name + ".wav"))
    tag = f"sound {name}"
    check(fmt == ref_fmt, f"{tag}: format {fmt} differs from chime.wav {ref_fmt}")
    dur = len(s) / fmt[2]
    check(abs(dur - seconds) < 0.011, f"{tag}: duration {dur:.3f}s, wanted ~{seconds}s")
    peak = max(abs(x) for x in s)
    check(peak < 32767 * 0.95, f"{tag}: peak {peak / 32767:.3f} too hot / clipping")
    check(peak > 32767 * 0.10, f"{tag}: peak {peak / 32767:.3f} nearly silent")
    check(max(s) < 32767 and min(s) > -32767, f"{tag}: samples pinned at full scale")
    rms = math.sqrt(sum(x * x for x in s) / len(s)) / 32767
    d1 = sorted(abs(s[k + 1] - s[k]) for k in range(len(s) - 1))
    note = ""
    if is_loop:
        # Value step and slope step across the wrap vs the same measures
        # inside the loop (wrapping indices: s[-1] precedes s[0]).
        n = len(s)
        wrap1 = abs(s[0] - s[-1])
        typical1 = d1[len(d1) // 2]
        d2 = sorted(abs(s[k + 1] - 2 * s[k] + s[k - 1]) for k in range(1, n - 1))
        wrap2 = max(abs(s[(k + 1) % n] - 2 * s[k % n] + s[k - 1]) for k in (n - 1, n))
        check(wrap1 <= d1[-1], f"{tag}: wrap step {wrap1} exceeds any interior step {d1[-1]}")
        check(wrap2 <= d2[-1], f"{tag}: wrap slope kink {wrap2} exceeds any interior {d2[-1]}")
        p99 = d1[int(len(d1) * 0.99)]
        note = (f"  wrap step {wrap1} (median {typical1}, p99 {p99}, max {d1[-1]})"
                f"  slope kink {wrap2} (max {d2[-1]})")
    else:
        edge = max(abs(s[0]), abs(s[-1]))
        check(edge <= 0.004 * 32767, f"{tag}: one-shot does not start/end at zero ({s[0]}, {s[-1]})")
        check(abs(s[1] - s[0]) <= 0.25 * peak and abs(s[-1] - s[-2]) <= 0.05 * peak,
              f"{tag}: abrupt edge")
        note = f"  first {s[0]} last {s[-1]}"
    print(f"  {name:13s} {dur:5.3f}s  peak {peak / 32767:.3f}  rms {rms:.3f}{note}")


def main() -> int:
    print("models (new, must fill the unit box):")
    for name in NEW_MODELS:
        check_model(name, True)
    print("models (existing unit-bounds props, validity only):")
    for name in OLD_UNIT_MODELS:
        check_model(name, False)

    print("textures:")
    ref_head, _ = load_png(os.path.join(ROOT, "textures", "metal.png"))
    for name, periods in NEW_TEXTURES.items():
        check_texture(name, periods, ref_head)

    print("sounds:")
    ref_fmt, _ = load_wav(os.path.join(ROOT, "sounds", "chime.wav"))
    for name, (seconds, is_loop) in NEW_SOUNDS.items():
        check_sound(name, seconds, is_loop, ref_fmt)

    if FAILS:
        print(f"\nFAILED ({len(FAILS)}):")
        for f in FAILS:
            print("  - " + f)
        return 1
    print("\nall asset checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
