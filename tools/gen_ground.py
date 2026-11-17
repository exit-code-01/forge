"""Generates assets/models/srf_terr_ground.glb: valley floor (drawn + static collider).
Run: python tools/gen_ground.py [--check]"""
import json, math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_terrain as gt

gt.MATS = [("grass_a", (0.33, 0.46, 0.22)), ("grass_b", (0.36, 0.48, 0.235)),
           ("grass_dry", (0.42, 0.48, 0.26)), ("dirt", (0.55, 0.45, 0.33)),
           ("scree", (0.50, 0.46, 0.40))]
GA, GB, GD, DIRT, SCREE = range(5)
SEED, SEED2, SEEDM, SEEDJ = 71, 113, 29, 211
X0, X1, Z0, Z1 = 352.0, 448.0, -48.0, 48.0
HOLES = [(366.0, 378.0, -1e9, 1e9), (398.5, 401.5, 34.0, 39.0)]
SUPP = [(420, 442, -48, -38), (412, 448, -30, 36), (392, 408, 30, 48),
        (352, 366, -38, -16), (364, 380, -1e9, 1e9)]
PATHS = [[(400, 31), (400, 9), (400, -6)], [(400, 12), (393, 15)],
         [(398, -8), (388, -20), (379, -22)], [(402, -8), (409, -18.8)], [(401, 14), (408, 23.2)]]
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "models", "srf_terr_ground.glb")


def in_hole(x, z):
    return any(a < x < b and c < z < d for a, b, c, d in HOLES)


def depth(x, z):
    return min(x - 356.0, 444.0 - x, z + 44.0, 44.0 - z)


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def mask(x, z, r):
    dx = max(r[0] - x, 0.0, x - r[1]); dz = max(r[2] - z, 0.0, z - r[3])
    return smooth(math.hypot(dx, dz) / 2.0)


def h(x, z):
    d = depth(x, z)
    if d >= 4.0:
        return 0.0
    s = 1.0 - max(d, 0.0) / 4.0
    v = s * s * (3 - 2 * s) * (0.55 + 0.35 * gt.fbm(x * 0.35, z * 0.35, SEED))
    for r in SUPP:
        v *= mask(x, z, r)
    return v


def seg_dist(px, pz, a, b):
    dx, dz = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((px - a[0]) * dx + (pz - a[1]) * dz) / (dx * dx + dz * dz)))
    return math.hypot(px - a[0] - t * dx, pz - a[1] - t * dz)


def is_dirt(x, z):
    w = 1.2 + 0.5 * gt.fbm(x * 0.5, z * 0.5, SEED2)
    if math.hypot(x - 393, z - 15) < 3.0:
        return True
    return any(seg_dist(x, z, p[i], p[i + 1]) < w for p in PATHS for i in range(len(p) - 1))


def lines(a, b, extra):
    n = int(round((b - a) / 1.5))
    return sorted(set([round(a + 1.5 * i, 4) for i in range(n + 1)] + list(extra)))


def jitter_grid(xs, zs):
    """Final (x, z) per grid index; frozen near boundary, holes and special lines."""
    nx, nz = len(xs), len(zs)
    def in_closed_hole(x, z):
        return any(a <= x <= b and c <= z <= d for a, b, c, d in HOLES)
    P = {}
    for i in range(nx):
        for j in range(nz):
            x, z = xs[i], zs[j]
            frozen = (i in (0, nx - 1) or j in (0, nz - 1) or x in (356.0, 444.0) or z in (-44.0, 44.0))
            if not frozen:
                for di in (-1, 0, 1):
                    for dj in (-1, 0, 1):
                        if in_closed_hole(xs[i + di], zs[j + dj]):
                            frozen = True
            if frozen:
                P[(i, j)] = (x, z); continue
            lx = 0.4 * min(x - xs[i - 1], xs[i + 1] - x)
            lz = 0.4 * min(z - zs[j - 1], zs[j + 1] - z)
            # sliver gaps (<1 m) next to special lines: keep that axis fixed
            lx = lx if lx >= 0.4 else 0.0
            lz = lz if lz >= 0.4 else 0.0
            ox = (gt._hash(i, j, SEEDJ) * 2 - 1) * 0.5
            oz = (gt._hash(i, j, SEEDJ + 1) * 2 - 1) * 0.5
            P[(i, j)] = (x + max(-lx, min(lx, ox)), z + max(-lz, min(lz, oz)))
    return P


def pick_mat(p0, p1, p2):
    cx = (p0[0] + p1[0] + p2[0]) / 3; cz = (p0[2] + p1[2] + p2[2]) / 3
    if (p0[1] + p1[1] + p2[1]) / 3 > 0.12:
        return SCREE
    if is_dirt(cx, cz):
        return DIRT
    n = gt.fbm(cx / 7.0, cz / 7.0, SEEDM)
    return GB if n > 0.62 else (GD if n < 0.36 else GA)


def build():
    xs = lines(X0, X1, [366, 378, 398.5, 401.5, 356, 444])
    zs = lines(Z0, Z1, [34, 39, -44, 44])
    mesh = gt.Mesh()
    P = jitter_grid(xs, zs)
    def V(i, j):
        x, z = P[(i, j)]
        return (x, h(x, z), z)
    up = (0, 1, 0)
    for i in range(len(xs) - 1):
        for j in range(len(zs) - 1):
            cx, cz = (xs[i] + xs[i + 1]) / 2, (zs[j] + zs[j + 1]) / 2
            if in_hole(cx, cz):
                continue
            p00, p10, p01, p11 = V(i, j), V(i + 1, j), V(i, j + 1), V(i + 1, j + 1)
            if (i + j) % 2 == 0:
                T = [(p00, p10, p11), (p00, p11, p01)]
            else:
                T = [(p00, p10, p01), (p10, p11, p01)]
            for t in T:
                mesh.add(pick_mat(*t), *t, up)
    return mesh


def check(path):
    with open(path, "rb") as f:
        data = f.read()
    jl = struct.unpack("<I", data[12:16])[0]
    g = json.loads(data[20:20 + jl])
    bo = 20 + jl + 8
    tris = []
    for p in g["meshes"][0]["primitives"]:
        def acc(k):
            a = g["accessors"][k]; bv = g["bufferViews"][a["bufferView"]]
            return a, bo + bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        a, o = acc(p["attributes"]["POSITION"])
        pos = [struct.unpack_from("<3f", data, o + 12 * k) for k in range(a["count"])]
        if "indices" in p:
            ia, io = acc(p["indices"])
            fmt, sz = ("<I", 4) if ia["componentType"] == 5125 else ("<H", 2)
            idx = [struct.unpack_from(fmt, data, io + sz * k)[0] for k in range(ia["count"])]
        else:
            idx = list(range(len(pos)))
        tris += [(pos[idx[k]], pos[idx[k + 1]], pos[idx[k + 2]]) for k in range(0, len(idx), 3)]
    print("triangles", len(tris))
    allv = [v for t in tris for v in t]
    print("bounds min", [min(v[c] for v in allv) for c in range(3)], "max", [max(v[c] for v in allv) for c in range(3)])
    eps = 1e-4
    for a, b, c in tris:
        ux, uy, uz = b[0]-a[0], b[1]-a[1], b[2]-a[2]; vx, vy, vz = c[0]-a[0], c[1]-a[1], c[2]-a[2]
        assert uz * vx - ux * vz > 0, "normal y <= 0"
        cx = (a[0]+b[0]+c[0]) / 3 + gt.OX; cz = (a[2]+b[2]+c[2]) / 3
        assert not in_hole(cx, cz), "centroid in hole"
        assert 0.5 * (uz * vx - ux * vz) > 0.02, "triangle too small"
    hv = {}
    for v in allv:
        x, z = round(v[0] + gt.OX, 3), round(v[2], 3)
        d = depth(x, z)
        if d >= 4.0:
            assert v[1] == 0.0, "nonzero interior"
        if 360 < x < 440 and -40 < z < 40 and d >= 4.0:
            assert v[1] == 0.0
        assert v[1] <= 0.95, "too tall"
        hv.setdefault((x, z), set()).add(round(v[1], 5))
    assert all(len(s) == 1 for s in hv.values()), "vertex height not a function of (x,z)"
    for (x, z), s in hv.items():
        if x in (366.0, 378.0) or (398.5 <= x <= 401.5 and z in (34.0, 39.0)) or (z in (34.0, 39.0) and x in (398.5, 401.5)):
            assert s == {0.0}, "hole-edge vertex nonzero"
    for (x, z) in hv:
        assert not in_hole(x, z), "vertex inside hole"
        if x in (366.0, 378.0) or (398.5 <= x <= 401.5 and 34.0 <= z <= 39.0):
            assert x in (366.0, 378.0, 398.5, 401.5) or z in (34.0, 39.0), "hole vertex"
    print("max y", max(v[1] for v in allv), "unique verts", len(hv))
    print("per material", {g["materials"][k]["name"]: sum(1 for _ in [0]) for k in range(0)} or [m["name"] for m in g["materials"]])
    print("CHECKS OK")


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    mesh = build()
    print({gt.MATS[k][0]: len(v) for k, v in mesh.tris.items()})
    gt.write_glb(OUT, mesh, "srf_terr_ground")
    check(OUT)


if __name__ == "__main__":
    if "--check" in sys.argv:
        check(OUT)
    else:
        main()
