#!/usr/bin/env python3
"""Generates assets/models/srf_terr_*.glb - rock skins for the Surface (lane O2).

Procedural, deterministic, stdlib only, no external assets (same provenance
rule as gen_models.py). Regenerate with:  python tools/gen_terrain.py

WHY: the Surface's terrain is axis-aligned boxes (the only collider the
engine had), so the valley reads as slabs. These meshes are faceted rock
SKINS that sit just in front of the box faces and lean back over the stepped
cliff tiers. They never sink into a box and never change where you can walk:
- below ~2.7 m above any walkable floor the skin stays within 0.21 m of the
  box face (the box remains the collider; the camera cannot get behind it);
- above that it bulges up to ~1.7 m toward the valley (overhead only);
- fixtures on a face (the cliff bell, both stair runs) get a RECESS: the
  skin dips behind the box face there, so the original face shows.

Three models, all authored in WORLD coordinates around the origin
(400, 0, 0) - spawn each at vec3(400, 0, 0), scale 1, no rotation:
  srf_terr_cliffs    the cliff ring (3 tiers, 4 sides) incl. its drop into
                     the ravine on the north and south walls
  srf_terr_ravine    both ravine walls, x = 366 and x = 378
  srf_terr_outcrops  the mesa, the ledge (B), the ore shelf, terrace C
Triangles are CCW toward the viewer (ADR-012), so the same files work as
static mesh colliders (ADR-032, collider = "mesh") once those land - but the
layout numbers below mirror assets/scripts/surface.lua: if a box moves there,
change it here and regenerate.
"""

import json
import math
import os
import struct

OX = 400.0  # model origin (world x); y and z origins are 0

# ---- palette (baseColorFactor; the importer ignores vertex colours, ADR-030)
MATS = [
    ("rock_light", (0.56, 0.50, 0.43)),
    ("rock_mid", (0.47, 0.42, 0.37)),
    ("rock_dark", (0.37, 0.34, 0.31)),
    ("rock_warm", (0.62, 0.54, 0.44)),
    ("rock_grass", (0.34, 0.46, 0.23)),
]
BANDS = [0, 1, 0, 3, 1, 2, 1, 0, 2, 3]  # strata order, bottom to top


# ---- deterministic noise ---------------------------------------------------
def _hash(ix, iy, seed):
    n = (ix * 374761393 + iy * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFFFF) / float(0x1000000)


def vnoise(x, y, seed):
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = _hash(ix, iy, seed), _hash(ix + 1, iy, seed)
    c, d = _hash(ix, iy + 1, seed), _hash(ix + 1, iy + 1, seed)
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy


def fbm(x, y, seed, octaves=3):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total += amp * vnoise(x, y, seed + o * 17)
        norm += amp
        amp *= 0.5
        x, y = x * 2.03, y * 2.03
    return total / norm


def clamp01(v):
    return 0.0 if v < 0 else (1.0 if v > 1 else v)


# ---- skin builder ----------------------------------------------------------
class Mesh:
    def __init__(self):
        self.tris = {i: [] for i in range(len(MATS))}  # material -> [(p0,p1,p2,n)]

    def add(self, mat, a, b, c, ref):
        """Triangle wound CCW toward `ref` (a vector on the viewer's side)."""
        ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
        vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        ln = math.sqrt(nx * nx + ny * ny + nz * nz)
        if ln < 1e-9:
            return
        nx, ny, nz = nx / ln, ny / ln, nz / ln
        if nx * ref[0] + ny * ref[1] + nz * ref[2] < 0:
            b, c = c, b
            nx, ny, nz = -nx, -ny, -nz
        self.tris[mat].append((a, b, c, (nx, ny, nz)))

    def count(self):
        return sum(len(t) for t in self.tris.values())


def path_samples(points, normals, tags, step, closed):
    """Walk a polyline in the XZ plane. Returns [(x, z, nx, nz, s, tag, corner)].
    `normals[i]` is segment i's unit normal TOWARD THE VIEWER; corner samples
    get the mitred sum so the offset skin stays continuous around the bend."""
    out, s = [], 0.0
    nseg = len(points) if closed else len(points) - 1
    for i in range(nseg):
        (x0, z0), (x1, z1) = points[i], points[(i + 1) % len(points)]
        length = math.hypot(x1 - x0, z1 - z0)
        n = max(1, int(math.ceil(length / step)))
        nx, nz = normals[i]
        for j in range(n):
            t = j / n
            corner = j == 0
            mx, mz = nx, nz
            if corner and (closed or i > 0):
                px, pz = normals[(i - 1) % nseg]
                mx, mz = nx + px, nz + pz  # right angles: |each axis| == 1
            if not corner:  # slide along the segment for irregular facets
                t += (_hash(i * 131 + j, 7, 99) - 0.5) * 0.5 / n
            out.append((x0 + (x1 - x0) * t, z0 + (z1 - z0) * t, mx, mz, s + length * t, tags[i], corner))
        s += length
    if not closed:
        x1, z1 = points[-1]
        out.append((x1, z1, normals[-1][0], normals[-1][1], s, tags[-1], True))
    return out, s


def profile_rows(poly, step):
    """Resample a (d, y) polyline (d = metres INTO the rock, y up). Returns
    [(d, y, nd, ny, breakpoint_index or -1)], normals pointing at the viewer."""
    rows = []
    for i in range(len(poly) - 1):
        (d0, y0), (d1, y1) = poly[i], poly[i + 1]
        n = max(1, int(math.ceil(math.hypot(d1 - d0, y1 - y0) / step)))
        for j in range(n):
            t = j / n
            rows.append([d0 + (d1 - d0) * t, y0 + (y1 - y0) * t, i if j == 0 else -1])
    rows.append([poly[-1][0], poly[-1][1], len(poly) - 1])
    out = []
    for k, (d, y, bp) in enumerate(rows):
        a, b = rows[max(0, k - 1)], rows[min(len(rows) - 1, k + 1)]
        dd, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dd, dy) or 1.0
        out.append((d, y, -dy / ln, dd / ln, bp))  # (nd, ny): toward viewer / up
    return out


def skin(mesh, samples, rows, closed, seed, amp, emit=None, rim=None, grass_above=None):
    """amp(x, z, y, tag) -> displacement amplitude in metres, or None for a
    RECESS (the vertex goes 0.06 m behind the box face). emit(sample, row_k)
    -> bool filters quads. rim(s) adds skyline height to rows above y 19."""
    grid = []
    for (x, z, nx, nz, s, tag, corner) in samples:
        col = []
        for k, (d, y, nd, ny, bp) in enumerate(rows):
            if bp < 0 and not corner:  # jitter rows that are not profile corners
                y += (_hash(int(s * 10), k, seed + 5) - 0.5) * 0.45
            a = amp(x - nx * d, z - nz * d, y, tag)
            if a is None:
                dd, yy = 0.06, y
            else:
                rib = fbm(s * 0.045, y * 0.02, seed, 2)
                det = fbm(s * 0.21, y * 0.24, seed + 3, 3)
                n = clamp01(0.55 * rib * rib * 1.9 + 0.45 * det)
                disp = a * n
                dd, yy = d + disp * nd, y + disp * ny
                if rim is not None and y > 19.0:
                    yy += rim(s) * clamp01((y - 19.0) / 3.0)
            col.append(((x - nx * dd, yy, z - nz * dd), (nx * -nd, ny, nz * -nd), tag))
        grid.append(col)
    ncol = len(grid) if closed else len(grid) - 1
    for i in range(ncol):
        c0, c1 = grid[i], grid[(i + 1) % len(grid)]
        for k in range(len(rows) - 1):
            if emit is not None and not emit(samples[i], k):
                continue
            a, b, c, d = c0[k], c1[k], c1[k + 1], c0[k + 1]
            ref = a[1]
            quads = ((a, b, c), (a, c, d)) if (i + k) % 2 == 0 else ((a, b, d), (b, c, d))
            for (p, q, r) in quads:
                cy = (p[0][1] + q[0][1] + r[0][1]) / 3.0
                cs = samples[i][4]
                # material: strata bands, wobbling along the wall
                # (bands drift up and down the face, and one facet in six
                # borrows its neighbour's colour, so they do not read as stripes)
                drift = 6.5 * fbm(cs * 0.035, cy * 0.05, seed + 9, 2)
                chip = 1 if _hash(i, k * 2 + (0 if p is quads[0][0] else 1), seed + 13) < 0.17 else 0
                band = int(math.floor((cy + 40.0 + drift) / 2.6)) + chip
                mat = BANDS[band % len(BANDS)]
                before = mesh.count()
                mesh.add(mat, p[0], q[0], r[0], ref)
                if grass_above is not None and mesh.count() > before:
                    tri = mesh.tris[mat][-1]
                    if tri[3][1] > 0.74 and cy > grass_above:
                        mesh.tris[mat].pop()
                        mesh.tris[4].append(tri)


# ---- the three models (numbers mirror assets/scripts/surface.lua) ----------
def build_cliffs():
    m = Mesh()
    pts = [(356, -44), (444, -44), (444, 44), (356, 44)]
    nrm = [(0, 1), (-1, 0), (0, -1), (1, 0)]  # toward the valley
    samples, _ = path_samples(pts, nrm, ["N", "E", "S", "W"], 1.7, True)
    # (d, y): clears the tier corners (0,8) (4,14) (8,21) and leans back
    poly = [(-0.12, -27.0), (-0.12, -0.5), (-0.12, 7.9), (0.3, 8.6), (3.6, 13.7), (4.3, 14.6),
            (7.6, 20.7), (8.3, 21.6), (10.8, 22.6), (13.5, 21.4)]
    rows = profile_rows(poly, 1.45)
    k_ground = next(k for k, r in enumerate(rows) if r[4] == 1)

    def floor_h(x, z, tag):  # walkable height right at the wall
        if tag == "E" and -28.6 < z < -15.4:
            return 4.5  # the ledge (B)
        if tag == "E" and 5.4 < z < 33.6:
            return 6.0  # the ore shelf
        return 0.0

    def amp(x, z, y, tag):
        if y < -0.45:  # the drop into the ravine (north and south walls only)
            return 1.3 * clamp01((x - 366.4) / 1.5) * clamp01((377.6 - x) / 1.5)
        if tag == "N" and 428.0 < x < 434.0 and 3.9 < y < 10.3:
            return None  # the bell, its plate and bracket stay in clear view
        h = y - floor_h(x, z, tag)
        if h < 2.7:
            a = 0.09
        elif h < 6.0:
            a = 0.09 + (h - 2.7) / 3.3 * 0.95
        else:
            a = 1.04 + clamp01((h - 6.0) / 8.0) * 0.65
        if tag == "N" and 424.5 < x < 437.5 and y < 12.5:
            a = min(a, 0.09)  # nothing overhangs the bell's throwing lane
        if tag == "W" and -35.0 < z < -25.5 and y < 8.0:
            a = min(a, 0.09)  # the mesa stands 0.5 m off this wall
        return a

    def emit(sample, k):
        if k >= k_ground:
            return True
        return sample[5] in ("N", "S") and 364.5 < sample[0] < 379.5 and not sample[6]

    def rim(s):
        return 3.2 * fbm(s * 0.05, 3.3, 41, 3)

    skin(m, samples, rows, True, 11, amp, emit, rim, grass_above=8.2)
    return m


def build_ravine():
    m = Mesh()
    poly = [(-0.10, -26.6), (-0.10, -1.7), (-0.02, -0.04)]
    rows = profile_rows(poly, 1.6)

    def amp(x, z, y, tag):
        a = 1.7 * clamp01((-1.7 - y) / 3.0) + 0.03
        if -23.8 < z < -20.2 and y > -1.4:
            a = 0.0  # the bridge slides through here
        return a

    for seed, x, n in ((23, 366.0, (1, 0)), (29, 378.0, (-1, 0))):
        samples, _ = path_samples([(x, -43.9), (x, 43.9)], [n], ["R"], 1.8, False)
        skin(m, samples, rows, False, seed, amp)
    return m


def build_outcrops():
    m = Mesh()

    def low_profile(top):
        return profile_rows([(-0.12, -0.3), (-0.12, top - 0.4), (-0.02, top - 0.03)], 1.15)

    def low_amp(top, recess=None):
        def amp(x, z, y, tag):
            if recess is not None and recess(x, z, tag):
                return None
            a = 0.09 if y < 2.7 else 0.09 + clamp01((y - 2.7) / 1.5) * 0.3
            return a * clamp01((top - 0.1 - y) / 0.5) + 0.0
        return amp

    # the mesa: SPARK's pad is on its top, which stays exactly as built
    pts = [(356.5, -33.5), (363.1, -33.5), (363.1, -26.9), (356.5, -26.9)]
    s, _ = path_samples(pts, [(0, -1), (1, 0), (0, 1), (-1, 0)], ["m"] * 4, 1.3, True)
    skin(m, s, low_profile(4.5), True, 51, low_amp(4.5))
    # the ledge (B): north, west (stairs at z -20..-17.6 recessed) and south faces
    pts = [(444, -28), (418, -28), (418, -16), (444, -16)]
    s, _ = path_samples(pts, [(0, -1), (-1, 0), (0, 1)], ["n", "w", "s"], 1.4, False)
    skin(m, s, low_profile(4.5), False, 57,
         low_amp(4.5, lambda x, z, tag: tag == "w" and -21.0 < z < -16.6))
    # the ore shelf: north, west (the arc yard side) and south faces
    pts = [(444, 6), (433, 6), (433, 33), (444, 33)]
    s, _ = path_samples(pts, [(0, -1), (-1, 0), (0, 1)], ["n", "w", "s"], 1.4, False)
    skin(m, s, low_profile(6.0), False, 61, low_amp(6.0))
    # terrace C: north, west (stairs at z 22..24.4 recessed) and south faces
    pts = [(424, 8), (414, 8), (414, 28), (424, 28)]
    s, _ = path_samples(pts, [(0, -1), (-1, 0), (0, 1)], ["n", "w", "s"], 1.3, False)
    skin(m, s, low_profile(2.5), False, 67,
         low_amp(2.5, lambda x, z, tag: tag == "w" and 21.0 < z < 25.4))
    return m


# ---- GLB writer ------------------------------------------------------------
def write_glb(path, mesh, name):
    bin_chunks, views, accessors, prims = [], [], [], []
    offset = 0

    def view(data, target):
        nonlocal offset
        pad = (-len(data)) % 4
        data += b"\x00" * pad
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data) - pad, "target": target})
        bin_chunks.append(data)
        offset += len(data)
        return len(views) - 1

    used = []
    for mat in range(len(MATS)):
        tris = mesh.tris[mat]
        if not tris:
            continue
        pos, nor, uv = [], [], []
        for (a, b, c, n) in tris:
            for p in (a, b, c):
                pos += [p[0] - OX, p[1], p[2]]
                nor += n
                uv += [(p[0] + p[2]) * 0.25, p[1] * 0.25]
        count = len(tris) * 3
        xs, ys, zs = pos[0::3], pos[1::3], pos[2::3]
        accessors.append({"bufferView": view(struct.pack("<%df" % len(pos), *pos), 34962),
                          "componentType": 5126, "count": count, "type": "VEC3",
                          "min": [min(xs), min(ys), min(zs)], "max": [max(xs), max(ys), max(zs)]})
        accessors.append({"bufferView": view(struct.pack("<%df" % len(nor), *nor), 34962),
                          "componentType": 5126, "count": count, "type": "VEC3"})
        accessors.append({"bufferView": view(struct.pack("<%df" % len(uv), *uv), 34962),
                          "componentType": 5126, "count": count, "type": "VEC2"})
        accessors.append({"bufferView": view(struct.pack("<%dI" % count, *range(count)), 34963),
                          "componentType": 5125, "count": count, "type": "SCALAR"})
        base = len(accessors) - 4
        prims.append({"mode": 4, "attributes": {"POSITION": base, "NORMAL": base + 1, "TEXCOORD_0": base + 2},
                      "indices": base + 3, "material": len(used)})
        used.append(mat)
    gltf = {
        "asset": {"version": "2.0", "generator": "forge tools/gen_terrain.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": prims}],
        "materials": [{"name": MATS[i][0], "doubleSided": False, "alphaMode": "OPAQUE",
                       "pbrMetallicRoughness": {"baseColorFactor": list(MATS[i][1]) + [1.0],
                                                "metallicFactor": 0.0, "roughnessFactor": 1.0}}
                      for i in used],
        "accessors": accessors, "bufferViews": views, "buffers": [{"byteLength": offset}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * ((-len(js)) % 4)
    blob = b"".join(bin_chunks)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(js), b"JSON") + js)
        f.write(struct.pack("<I4s", len(blob), b"BIN\x00") + blob)
    print("%s: %d triangles, %d materials, %d bytes" % (os.path.basename(path), mesh.count(), len(used), total))


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "models")
    out = os.environ.get("FORGE_TERRAIN_OUT", out)
    os.makedirs(out, exist_ok=True)
    for name, build in (("srf_terr_cliffs", build_cliffs), ("srf_terr_ravine", build_ravine),
                        ("srf_terr_outcrops", build_outcrops)):
        write_glb(os.path.join(out, name + ".glb"), build(), name)


if __name__ == "__main__":
    main()
