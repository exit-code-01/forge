#!/usr/bin/env python3
"""Generates assets/models/spl_*.glb - the Spillway's own pieces (Location 3, ADR-034).

Procedural, deterministic, stdlib only, no external assets (same provenance
rule as gen_models.py / gen_terrain.py). Regenerate with:  python tools/gen_spillway.py

Models:
  spl_turbine    a vertical-shaft Kaplan-style runner (hub, 10 pitched blades, a
                 shroud ring, a shaft stub). Origin = centre of its base, about
                 5.4 m across and 2.7 m tall. The script spins it with
                 forge.scene.setRotation (visual only, no collider).
  spl_dam_face   the dam's downstream face dressing, authored in WORLD space
                 around (800, 0, 0): pilasters, string courses and a cornice
                 on the plane z = -28 above the sub-area walls (y 5.4 .. 9.4).
                 Spawn at vec3(800, 0, 0), scale 1. Visual only: the dam body
                 is a box in spillway.lua.
  spl_pier       a chamfered round pier with a capital, 6 m tall, 0.9 m wide
                 (hall columns, crown gate posts). Origin = centre of its base.
Flat-colour materials only (baseColorFactor); the importer ignores vertex
colours (ADR-030). Triangles are CCW toward the viewer (ADR-012).
"""

import json
import math
import os
import struct

MATS = [
    ("concrete_light", (0.62, 0.60, 0.56)),
    ("concrete_mid", (0.48, 0.47, 0.45)),
    ("concrete_dark", (0.34, 0.34, 0.34)),
    ("steel", (0.46, 0.48, 0.52)),
    ("steel_dark", (0.16, 0.17, 0.19)),
    ("rust", (0.55, 0.30, 0.16)),
]
C_LIGHT, C_MID, C_DARK, STEEL, STEEL_DK, RUST = range(6)


class Mesh:
    def __init__(self):
        self.tris = {i: [] for i in range(len(MATS))}

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

    def bounds(self):
        pts = [p for t in self.tris.values() for tri in t for p in tri[:3]]
        return ([min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)])


def quad(mesh, mat, p0, p1, p2, p3, ref):
    mesh.add(mat, p0, p1, p2, ref)
    mesh.add(mat, p0, p2, p3, ref)


def solid(mesh, mat, pts, centre):
    """Six faces of a hexahedron given 8 corner points (bottom ring 0-3, top ring 4-7).
    Each face is wound outward (away from `centre`)."""
    faces = ((0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))
    for f in faces:
        q = [pts[i] for i in f]
        mx = sum(p[0] for p in q) / 4 - centre[0]
        my = sum(p[1] for p in q) / 4 - centre[1]
        mz = sum(p[2] for p in q) / 4 - centre[2]
        quad(mesh, mat, q[0], q[1], q[2], q[3], (mx, my, mz))


def box(mesh, mat, x0, x1, y0, y1, z0, z1):
    pts = [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1),
           (x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)]
    solid(mesh, mat, pts, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))


def cylinder(mesh, mat, cx, cz, r0, r1, y0, y1, sides):
    """Frustum: radius r0 at y0, r1 at y1, `sides` flat facets, capped."""
    for i in range(sides):
        a0, a1 = 2 * math.pi * i / sides, 2 * math.pi * (i + 1) / sides
        b = [(cx + r0 * math.cos(a0), y0, cz + r0 * math.sin(a0)), (cx + r0 * math.cos(a1), y0, cz + r0 * math.sin(a1)),
             (cx + r1 * math.cos(a1), y1, cz + r1 * math.sin(a1)), (cx + r1 * math.cos(a0), y1, cz + r1 * math.sin(a0))]
        am = (a0 + a1) / 2
        quad(mesh, mat, b[0], b[1], b[2], b[3], (math.cos(am), 0.0, math.sin(am)))
        mesh.add(mat, (cx, y1, cz), b[3], b[2], (0, 1, 0))
        mesh.add(mat, (cx, y0, cz), b[0], b[1], (0, -1, 0))


def rot_y(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c)


# ---- spl_turbine -------------------------------------------------------------
def build_turbine():
    m = Mesh()
    cylinder(m, STEEL_DK, 0, 0, 0.60, 0.60, 0.0, 0.25, 16)       # base flange
    cylinder(m, STEEL, 0, 0, 0.55, 0.40, 0.25, 1.30, 16)           # hub, tapering up
    cylinder(m, STEEL_DK, 0, 0, 0.22, 0.22, 1.30, 2.70, 10)        # shaft stub
    cylinder(m, RUST, 0, 0, 0.34, 0.34, 2.05, 2.25, 10)            # a coupling collar
    blades = 10
    for i in range(blades):
        a = 2 * math.pi * i / blades
        # local blade: radial 0.45..2.45 along +x, thickness 0.10 in z, height 0.30..1.10,
        # pitched 24 degrees about its radial axis
        pitch = math.radians(24)
        pts = []
        for (x, y, z) in ((0.45, 0.30, -0.28), (2.45, 0.30, -0.22), (2.45, 0.30, 0.22), (0.45, 0.30, 0.28),
                          (0.45, 1.10, -0.28), (2.45, 1.10, -0.22), (2.45, 1.10, 0.22), (0.45, 1.10, 0.28)):
            yy = 0.70 + (y - 0.70) * math.cos(pitch) - z * math.sin(pitch) * 0.6
            zz = z * math.cos(pitch) + (y - 0.70) * math.sin(pitch) * 0.6
            pts.append(rot_y((x, yy, zz), a))
        solid(m, STEEL, pts, rot_y((1.45, 0.70, 0.0), a))
    # shroud: a ring of 24 facets around the blade tips
    ring = 24
    for i in range(ring):
        a0, a1 = 2 * math.pi * i / ring, 2 * math.pi * (i + 1) / ring
        ro, ri = 2.70, 2.52
        pts = [(ri * math.cos(a0), 0.15, ri * math.sin(a0)), (ri * math.cos(a1), 0.15, ri * math.sin(a1)),
               (ro * math.cos(a1), 0.15, ro * math.sin(a1)), (ro * math.cos(a0), 0.15, ro * math.sin(a0)),
               (ri * math.cos(a0), 1.25, ri * math.sin(a0)), (ri * math.cos(a1), 1.25, ri * math.sin(a1)),
               (ro * math.cos(a1), 1.25, ro * math.sin(a1)), (ro * math.cos(a0), 1.25, ro * math.sin(a0))]
        solid(m, STEEL_DK if i % 4 else RUST, pts, (0.0, 0.70, 0.0))
    return m


# ---- spl_pier ----------------------------------------------------------------
def build_pier():
    m = Mesh()
    cylinder(m, C_MID, 0, 0, 0.50, 0.50, 0.0, 0.35, 8)        # plinth
    cylinder(m, C_LIGHT, 0, 0, 0.42, 0.42, 0.35, 5.35, 8)     # shaft
    cylinder(m, C_MID, 0, 0, 0.42, 0.58, 5.35, 5.70, 8)       # capital flare
    cylinder(m, C_DARK, 0, 0, 0.58, 0.58, 5.70, 6.00, 8)      # abacus
    return m


# ---- spl_dam_face -------------------------------------------------------------
OX = 800.0


def build_dam_face():
    m = Mesh()
    zf = -28.0  # the plane of the dam front; everything stands proud toward +z
    for i in range(0, 11):
        x = 756.0 + 8.0 * i + 4.0
        box(m, C_MID, x - 0.75, x + 0.75, 5.4, 9.0, zf, zf + 0.45)        # pilaster
        box(m, C_LIGHT, x - 0.95, x + 0.95, 8.6, 9.0, zf, zf + 0.62)      # its capital
    for y in (6.6, 7.9):
        box(m, C_DARK, 752.0, 848.0, y, y + 0.14, zf, zf + 0.12)          # string courses
    box(m, C_LIGHT, 752.0, 848.0, 9.0, 9.4, zf - 0.1, zf + 0.75)           # cornice under the parapet
    # three weathered stains (darker blocks) where the spill gates would run
    for gx in (777.0, 800.0, 823.0):
        box(m, C_DARK, gx - 1.1, gx + 1.1, 5.4, 8.6, zf, zf + 0.08)
    return m


# ---- GLB writer (same layout as gen_terrain.write_glb) -------------------------
def write_glb(path, mesh, name, ox=0.0):
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
                pos += [p[0] - ox, p[1], p[2]]
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
        "asset": {"version": "2.0", "generator": "forge tools/gen_spillway.py"},
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
    lo, hi = mesh.bounds()
    print("%s: %d triangles, %d materials, %d bytes, bounds x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f" % (
        os.path.basename(path), mesh.count(), len(used), total,
        lo[0] - ox, hi[0] - ox, lo[1], hi[1], lo[2], hi[2]))


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "models")
    out = os.environ.get("FORGE_SPILLWAY_OUT", out)
    os.makedirs(out, exist_ok=True)
    write_glb(os.path.join(out, "spl_turbine.glb"), build_turbine(), "spl_turbine")
    write_glb(os.path.join(out, "spl_pier.glb"), build_pier(), "spl_pier")
    write_glb(os.path.join(out, "spl_dam_face.glb"), build_dam_face(), "spl_dam_face", OX)


if __name__ == "__main__":
    main()
