#!/usr/bin/env python3
"""Generates assets/models/srf_terr_ramps.glb - smooth walkable ramps over the
two box staircases (the ledge B and terrace C) of the Surface.

Authored in WORLD coordinates around origin (400,0,0): spawn at vec3(400,0,0).
Used as a static mesh collider AND drawn. Regenerate: python tools/gen_ramps.py
Verify only: python tools/gen_ramps.py --check
Numbers mirror assets/scripts/surface.lua stairs; change both together.
"""
import json
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_terrain as gt  # noqa: E402

gt.MATS = [("dirt", (0.60, 0.50, 0.37)), ("dirt_dark", (0.54, 0.45, 0.33)), ("rock", (0.52, 0.47, 0.41))]
DIRT, DIRT_DARK, ROCK = 0, 1, 2

# name, x0 (foot), x1 (top), landing y, z0, z1, seed
RAMPS = [
    ("ledge", 409.0, 418.0, 4.5, -20.0, -17.6, 101),
    ("terrace", 408.0, 414.0, 2.5, 22.0, 24.4, 103),
]
CELL = 0.5
NZ = 4
SKIRT_Y = -0.1
SKIRT_PUSH = 0.02


def plane(r, x):
    return r[3] * (x - r[1]) / (r[2] - r[1])


def height(r, x, z):
    x0, x1 = r[1], r[2]
    blend = min(1.0, (x - x0) / 0.5, (x1 - x) / 0.5)
    blend = max(0.0, blend)
    n = gt.fbm(x * 1.7, z * 1.7, r[6], 2)
    return plane(r, x) + (0.03 + 0.04 * min(1.0, n)) * blend


def build_ramp(m, r):
    x0, x1, z0, z1 = r[1], r[2], r[4], r[5]
    nx = int(round((x1 - x0) / CELL))
    xs = [x0 + (x1 - x0) * i / nx for i in range(nx + 1)]
    zs = [z0 + (z1 - z0) * j / NZ for j in range(NZ + 1)]
    H = [[height(r, x, z) for z in zs] for x in xs]
    for j in range(NZ + 1):  # exact ends
        H[0][j] = 0.0
        H[nx][j] = r[3]
    up = (0, 1, 0)
    for i in range(nx):
        for j in range(NZ):
            a = (xs[i], H[i][j], zs[j])
            b = (xs[i + 1], H[i + 1][j], zs[j])
            c = (xs[i + 1], H[i + 1][j + 1], zs[j + 1])
            d = (xs[i], H[i][j + 1], zs[j + 1])
            if j in (0, NZ - 1):
                mat = ROCK
            else:
                mat = DIRT_DARK if gt.fbm(i * 0.9 + 3.1, j * 1.3, r[6] + 5, 2) > 0.55 else DIRT
            m.add(mat, a, b, c, up)
            m.add(mat, a, c, d, up)
        # skirts
    for (j, zpush, ref) in ((0, -SKIRT_PUSH, (0, 0, -1)), (NZ, SKIRT_PUSH, (0, 0, 1))):
        z = zs[j] + zpush
        for i in range(nx):
            t0 = (xs[i], H[i][j], z)
            t1 = (xs[i + 1], H[i + 1][j], z)
            b0 = (xs[i], SKIRT_Y, z)
            b1 = (xs[i + 1], SKIRT_Y, z)
            m.add(ROCK, t0, t1, b1, ref)
            m.add(ROCK, t0, b1, b0, ref)


def build():
    m = gt.Mesh()
    for r in RAMPS:
        build_ramp(m, r)
    return m


def read_glb(path):
    d = open(path, "rb").read()
    jl = struct.unpack_from("<I", d, 12)[0]
    js = json.loads(d[20:20 + jl])
    off = 20 + jl + 8
    bins = d[off:]
    tris = []
    for prim in js["meshes"][0]["primitives"]:
        acc = js["accessors"][prim["attributes"]["POSITION"]]
        bv = js["bufferViews"][acc["bufferView"]]
        vals = struct.unpack_from("<%df" % (acc["count"] * 3), bins, bv["byteOffset"])
        pts = [(vals[k] + gt.OX, vals[k + 1], vals[k + 2]) for k in range(0, len(vals), 3)]
        for k in range(0, len(pts), 3):
            tris.append(tuple(pts[k:k + 3]))
    return js, tris


def normal(t):
    a, b, c = t
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


def check(path):
    js, tris = read_glb(path)
    allp = [p for t in tris for p in t]
    mn = [min(p[k] for p in allp) - (gt.OX if k == 0 else 0) for k in range(3)]
    mx = [max(p[k] for p in allp) - (gt.OX if k == 0 else 0) for k in range(3)]
    print("triangles %d  bounds(model) min %s max %s" % (len(tris), [round(v, 3) for v in mn], [round(v, 3) for v in mx]))
    tops = [t for t in tris if abs(normal(t)[1]) > 0.5 * (sum(c * c for c in normal(t)) ** 0.5)]
    bad = [t for t in tops if normal(t)[1] <= 0]
    assert not bad, "%d top triangles face down" % len(bad)
    print("top tris %d, none face down" % len(tops))
    rnd = random.Random(7)
    for r in RAMPS:
        rt = [t for t in tops if r[1] - 1e-6 <= min(p[0] for p in t) and max(p[0] for p in t) <= r[2] + 1e-6
              and r[4] - 1e-6 <= min(p[2] for p in t) and max(p[2] for p in t) <= r[5] + 1e-6]
        foot = [p[1] for t in rt for p in t if abs(p[0] - r[1]) < 1e-6]
        top = [p[1] for t in rt for p in t if abs(p[0] - r[2]) < 1e-6]
        assert foot and all(abs(y) < 1e-6 for y in foot), "foot row"
        assert top and all(abs(y - r[3]) < 1e-6 for y in top), "top row"
        n = 0
        while n < 200:
            x, z = rnd.uniform(r[1], r[2]), rnd.uniform(r[4], r[5])
            for t in rt:
                (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
                den = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
                if abs(den) < 1e-12:
                    continue
                l1 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / den
                l2 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / den
                l3 = 1 - l1 - l2
                if min(l1, l2, l3) >= -1e-9:
                    y = l1 * ay + l2 * by + l3 * cy
                    p = plane(r, x)
                    assert p - 1e-6 <= y <= p + 0.08 + 1e-6, (r[0], x, z, y, p)
                    n += 1
                    break
        print("%s: 200 random points within plane+[0,0.08]; foot y=0, top y=%.1f OK" % (r[0], r[3]))


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "models")
    path = os.path.join(out, "srf_terr_ramps.glb")
    if "--check" not in sys.argv:
        os.makedirs(out, exist_ok=True)
        gt.write_glb(path, build(), "srf_terr_ramps")
    check(path)


if __name__ == "__main__":
    main()
