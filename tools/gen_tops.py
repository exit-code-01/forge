#!/usr/bin/env python3
"""Generates assets/models/srf_terr_tops.glb - the flat, exactly-level top
surfaces of the five raised rock platforms of the Surface (ledge, terrace,
ore shelf, mesa, lookout). Drawn AND used as a static triangle collider.
World coordinates (model x = world x - 400, applied by gt.write_glb).
Regenerate: python tools/gen_tops.py     Verify only: python tools/gen_tops.py --check
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_terrain as gt  # noqa: E402

gt.MATS = [("rock_a", (0.60, 0.54, 0.47)), ("rock_b", (0.54, 0.49, 0.43)), ("rock_c", (0.66, 0.58, 0.48)),
           ("dirt", (0.57, 0.47, 0.35)), ("grass", (0.42, 0.48, 0.26))]
ROCK_A, ROCK_B, ROCK_C, DIRT, GRASS = range(5)

# name, x0, x1, z0, z1, y, grass?, seed
RECTS = [
    ("ledge", 418.0, 444.0, -28.0, -16.0, 4.5, True, 11),
    ("terrace", 414.0, 424.0, 8.0, 28.0, 2.5, False, 23),
    ("ore_shelf", 433.0, 444.0, 6.0, 33.0, 6.0, True, 37),
    ("mesa", 356.5, 363.1, -33.5, -26.9, 4.5, False, 41),
    ("lookout", 384.0, 388.0, -30.0, -26.0, 6.0, False, 53),
]
CELL = 1.2
JIT = 0.35
SKIRT_DROP = 0.15
SKIRT_PUSH = 0.01
DIRT_T = 0.62
GRASS_T = 0.55
RIM = 1.5


def pick_mat(r, cx, cz):
    name, x0, x1, z0, z1, y, grass, seed = r
    rim = min(cx - x0, x1 - cx, cz - z0, z1 - cz)
    if grass and rim < RIM and gt.fbm(cx * 0.45, cz * 0.45, seed + 200) > GRASS_T:
        return GRASS
    if gt.fbm(cx * 0.35, cz * 0.35, seed + 100) > DIRT_T:
        return DIRT
    n = gt.fbm(cx * 0.18, cz * 0.18, seed)
    return ROCK_B if n < 0.42 else (ROCK_A if n < 0.58 else ROCK_C)


def build_rect(m, r):
    name, x0, x1, z0, z1, y, grass, seed = r
    nx = max(1, int(round((x1 - x0) / CELL)))
    nz = max(1, int(round((z1 - z0) / CELL)))
    cx_, cz_ = (x1 - x0) / nx, (z1 - z0) / nz
    P = [[None] * (nz + 1) for _ in range(nx + 1)]
    for i in range(nx + 1):
        for j in range(nz + 1):
            x, z = x0 + cx_ * i, z0 + cz_ * j
            if 0 < i < nx and 0 < j < nz:
                x += (gt.fbm(i * 1.3 + 0.37, j * 1.3 + 0.11, seed + 300, 1) * 2 - 1) * JIT * cx_
                z += (gt.fbm(i * 1.3 + 5.71, j * 1.3 + 9.13, seed + 400, 1) * 2 - 1) * JIT * cz_
            else:
                x = x0 if i == 0 else (x1 if i == nx else x)
                z = z0 if j == 0 else (z1 if j == nz else z)
            P[i][j] = (x, y, z)
    up = (0, 1, 0)
    for i in range(nx):
        for j in range(nz):
            a, b, c, d = P[i][j], P[i + 1][j], P[i + 1][j + 1], P[i][j + 1]
            tris = ((a, b, c), (a, c, d)) if (i + j) % 2 == 0 else ((a, b, d), (b, c, d))
            for t in tris:
                mat = pick_mat(r, sum(p[0] for p in t) / 3, sum(p[2] for p in t) / 3)
                m.add(mat, t[0], t[1], t[2], up)
    # rim skirt
    def quad(p, q, ref):
        p2, q2 = (p[0], y - SKIRT_DROP, p[2]), (q[0], y - SKIRT_DROP, q[2])
        m.add(ROCK_B, p, q, q2, ref)
        m.add(ROCK_B, p, q2, p2, ref)
    for i in range(nx):
        for j, s in ((0, -1), (nz, 1)):
            p, q = P[i][j], P[i + 1][j]
            quad((p[0], y, p[2] + s * SKIRT_PUSH), (q[0], y, q[2] + s * SKIRT_PUSH), (0, 0, s))
    for j in range(nz):
        for i, s in ((0, -1), (nx, 1)):
            p, q = P[i][j], P[i][j + 1]
            quad((p[0] + s * SKIRT_PUSH, y, p[2]), (q[0] + s * SKIRT_PUSH, y, q[2]), (s, 0, 0))


def build():
    m = gt.Mesh()
    for r in RECTS:
        build_rect(m, r)
    return m


def read_glb(path):
    d = open(path, "rb").read()
    jl = struct.unpack_from("<I", d, 12)[0]
    js = json.loads(d[20:20 + jl])
    bins = d[20 + jl + 8:]
    tris = []
    for prim in js["meshes"][0]["primitives"]:
        acc = js["accessors"][prim["attributes"]["POSITION"]]
        bv = js["bufferViews"][acc["bufferView"]]
        v = struct.unpack_from("<%df" % (acc["count"] * 3), bins, bv["byteOffset"])
        pts = [(v[k] + gt.OX, v[k + 1], v[k + 2]) for k in range(0, len(v), 3)]
        mat = js["materials"][prim["material"]]["name"]
        for k in range(0, len(pts), 3):
            tris.append((mat, tuple(pts[k:k + 3])))
    return js, tris


def normal(t):
    a, b, c = t
    u = [b[k] - a[k] for k in range(3)]
    w = [c[k] - a[k] for k in range(3)]
    return (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])


def check(path):
    js, tris = read_glb(path)
    allp = [p for _, t in tris for p in t]
    mn = [min(p[k] for p in allp) - (gt.OX if k == 0 else 0) for k in range(3)]
    mx = [max(p[k] for p in allp) - (gt.OX if k == 0 else 0) for k in range(3)]
    print("triangles %d  bounds(model) min %s max %s" % (len(tris), [round(v, 3) for v in mn], [round(v, 3) for v in mx]))
    counts, area = {}, {}
    tops, skirts = [], []
    for mat, t in tris:
        n = normal(t)
        ln = math.sqrt(sum(c * c for c in n))
        counts[mat] = counts.get(mat, 0) + 1
        (tops if n[1] / ln > 0.5 else skirts).append((mat, t, ln / 2))
        if n[1] / ln > 0.5:
            area[mat] = area.get(mat, 0) + ln / 2
    print("per-material triangles:", counts)
    ta = sum(area.values())
    print("top area share:", {k: round(v / ta, 3) for k, v in area.items()})
    eps = 1e-4
    minarea = 1e9
    for r in RECTS:
        name, x0, x1, z0, z1, y, _, _ = r
        rt = [(mat, t, a) for mat, t, a in tops
              if all(x0 - eps <= p[0] <= x1 + eps and z0 - eps <= p[2] <= z1 + eps for p in t)
              and abs(t[0][1] - y) < 1e-6]
        assert rt, name
        for mat, t, a in rt:
            assert normal(t)[1] > 0, (name, "face down")
            minarea = min(minarea, a)
            for p in t:
                assert p[1] == y, (name, p)
                for v, lo, hi in ((p[0], x0, x1), (p[2], z0, z1)):
                    if min(abs(v - lo), abs(v - hi)) < 0.05:
                        assert min(abs(v - lo), abs(v - hi)) < eps, (name, "boundary off edge", p)
        # every top vertex on a boundary edge is exact; area must cover rect
        tot = sum(a for _, _, a in rt)
        assert abs(tot - (x1 - x0) * (z1 - z0)) < 1e-2 * (x1 - x0) * (z1 - z0) * 0.01 + 1e-2, (name, tot)
        cxr, czr = (x0 + x1) / 2, (z0 + z1) / 2
        sk = [(t, a) for mat, t, a in skirts
              if all(x0 - 0.02 <= p[0] <= x1 + 0.02 and z0 - 0.02 <= p[2] <= z1 + 0.02 for p in t)
              and all(y - SKIRT_DROP - 1e-4 <= p[1] <= y + 1e-4 for p in t)]
        assert sk, name
        for t, a in sk:
            n = normal(t)
            cx = sum(p[0] for p in t) / 3 - cxr
            cz = sum(p[2] for p in t) / 3 - czr
            assert n[0] * cx + n[2] * cz > 0, (name, "skirt faces inward")
        print("%s: %d top tris, %d skirt tris OK" % (name, len(rt), len(sk)))
    print("min top triangle area %.4f" % minarea)
    assert minarea > 0.02, "sliver triangle"
    assert len(tops) + len(skirts) == len(tris)
    print("CHECK OK")


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "models")
    path = os.path.join(out, "srf_terr_tops.glb")
    if "--check" not in sys.argv:
        os.makedirs(out, exist_ok=True)
        gt.write_glb(path, build(), "srf_terr_tops")
    check(path)


if __name__ == "__main__":
    main()
