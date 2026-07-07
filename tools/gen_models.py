#!/usr/bin/env python3
"""Generates assets/models/*.obj — VAULT's prop + character meshes (week 11).

Procedural, deterministic, no external assets (same provenance rule as
gen_torus.py). Regenerate with:  python tools/gen_models.py

Everything is composed from two primitives — boxes (hard normals) and
cylinders (smooth sides, flat caps) — which is exactly the machined-robot
look the game wants. Faces are CCW-outward (ADR-012); positions + normals
+ UVs are explicit so the importer never has to guess.

Authoring conventions:
- Y up, character/prop "forward" = -Z (matches the player's yaw-0 facing).
- UNIT-BOUNDS models (crate, plate, door, emitter, receiver) fill roughly
  [-0.5, 0.5]^3 and are shaped by the spawn scale, exactly like the
  built-in cube they replace — no Lua call site changes shape.
- REAL-SIZE models (drone_body, drone_eye, glove) are authored in meters
  and spawned with scale 1 — their proportions must survive rotation.
"""

import math
import os


def rot_euler(deg):
    """3x3 rotation matrix, Ry @ Rx @ Rz (engine trs() order), degrees."""
    rx, ry, rz = (math.radians(d) for d in deg)
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)
    # Rz, then Rx, then Ry — composed as full matrices.
    m_z = ((cz, -sz, 0), (sz, cz, 0), (0, 0, 1))
    m_x = ((1, 0, 0), (0, cx, -sx), (0, sx, cx))
    m_y = ((cy, 0, sy), (0, 1, 0), (-sy, 0, cy))

    def mul(a, b):
        return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3))
                     for i in range(3))

    return mul(m_y, mul(m_x, m_z))


def apply(m, v):
    return tuple(m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] for i in range(3))


class Mesh:
    def __init__(self, name):
        self.name = name
        self.verts = []  # (pos, normal, uv)
        self.tris = []   # (a, b, c) 0-based

    def quad(self, p0, p1, p2, p3, n, uvs):
        base = len(self.verts)
        for p, uv in zip((p0, p1, p2, p3), uvs):
            self.verts.append((p, n, uv))
        self.tris.append((base, base + 1, base + 2))
        self.tris.append((base + 2, base + 3, base))

    def box(self, center, size, rot=None, pivot=None):
        """Axis-aligned box, optionally rotated (euler degrees) about pivot
        (defaults to its own center). Hard normals, 0..1 UVs per face."""
        m = rot_euler(rot) if rot else None
        pivot = pivot or center
        hx, hy, hz = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
        axes = {"x": (1, 0, 0), "y": (0, 1, 0), "z": (0, 0, 1)}

        def scaled(v):
            return (v[0] * hx, v[1] * hy, v[2] * hz)

        def neg(v):
            return (-v[0], -v[1], -v[2])

        x, y, z = axes["x"], axes["y"], axes["z"]
        # (normal, u, v) per face — u cross v == n keeps windings CCW-outward
        # (the same table sandbox/src/main.cpp's buildCube uses).
        faces = [(z, x, y), (neg(z), neg(x), y), (x, neg(z), y), (neg(x), z, y),
                 (y, x, neg(z)), (neg(y), x, z)]
        for n, u, v in faces:
            corners = []
            for cu, cv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                local = scaled((n[0] + cu * u[0] + cv * v[0], n[1] + cu * u[1] + cv * v[1],
                                n[2] + cu * u[2] + cv * v[2]))
                p = (center[0] + local[0], center[1] + local[1], center[2] + local[2])
                if m:
                    rel = (p[0] - pivot[0], p[1] - pivot[1], p[2] - pivot[2])
                    r = apply(m, rel)
                    p = (pivot[0] + r[0], pivot[1] + r[1], pivot[2] + r[2])
                corners.append(p)
            nrm = apply(m, n) if m else n
            uvs = ((0, 1), (1, 1), (1, 0), (0, 0))
            self.quad(corners[0], corners[1], corners[2], corners[3], nrm, uvs)

    def cylinder(self, center, radius, height, segments=12, r_top=None, axis="y", rot=None,
                 pivot=None):
        """Cylinder (or truncated cone via r_top) along `axis`. Smooth side
        normals, flat caps, CCW-outward."""
        r0, r1 = radius, radius if r_top is None else r_top
        axis_rot = {"y": None, "x": (0, 0, -90), "z": (90, 0, 0)}[axis]
        m_axis = rot_euler(axis_rot) if axis_rot else None
        m_extra = rot_euler(rot) if rot else None
        pivot = pivot or center
        h2 = height * 0.5

        def place(local, is_normal=False):
            p = apply(m_axis, local) if m_axis else local
            if not is_normal:
                p = (center[0] + p[0], center[1] + p[1], center[2] + p[2])
            if m_extra:
                if is_normal:
                    p = apply(m_extra, p)
                else:
                    rel = (p[0] - pivot[0], p[1] - pivot[1], p[2] - pivot[2])
                    r = apply(m_extra, rel)
                    p = (pivot[0] + r[0], pivot[1] + r[1], pivot[2] + r[2])
            return p

        ring = []
        for i in range(segments):
            th = 2.0 * math.pi * i / segments
            ring.append((math.cos(th), math.sin(th)))
        # Side quads: (bottom_i, top_i, top_i+1, bottom_i+1) is CCW-outward.
        slope = r0 - r1
        for i in range(segments):
            j = (i + 1) % segments
            u0, u1 = i / segments, (i + 1) / segments
            quad_pts = []
            quad_ns = []
            for (c, s), yy, rr in (((ring[i]), -h2, r0), ((ring[i]), h2, r1),
                                   ((ring[j]), h2, r1), ((ring[j]), -h2, r0)):
                quad_pts.append(place((rr * c, yy, rr * s)))
                ln = (c * height, slope, s * height)
                mag = math.sqrt(ln[0] ** 2 + ln[1] ** 2 + ln[2] ** 2) or 1.0
                quad_ns.append(place((ln[0] / mag, ln[1] / mag, ln[2] / mag), is_normal=True))
            base = len(self.verts)
            uvs = ((u0, 1), (u0, 0), (u1, 0), (u1, 1))
            for p, n, uv in zip(quad_pts, quad_ns, uvs):
                self.verts.append((p, n, uv))
            self.tris.append((base, base + 1, base + 2))
            self.tris.append((base + 2, base + 3, base))
        # Caps: top fan CCW seen from +axis, bottom fan the reverse.
        for yy, rr, up in ((h2, r1, True), (-h2, r0, False)):
            n = place((0, 1 if up else -1, 0), is_normal=True)
            c_pt = place((0, yy, 0))
            for i in range(segments):
                j = (i + 1) % segments
                a = place((rr * ring[i][0], yy, rr * ring[i][1]))
                b = place((rr * ring[j][0], yy, rr * ring[j][1]))
                base = len(self.verts)
                uv_c = (0.5, 0.5)
                uv_a = (0.5 + ring[i][0] * 0.5, 0.5 + ring[i][1] * 0.5)
                uv_b = (0.5 + ring[j][0] * 0.5, 0.5 + ring[j][1] * 0.5)
                if up:
                    for p, uv in ((c_pt, uv_c), (b, uv_b), (a, uv_a)):
                        self.verts.append((p, n, uv))
                else:
                    for p, uv in ((c_pt, uv_c), (a, uv_a), (b, uv_b)):
                        self.verts.append((p, n, uv))
                self.tris.append((base, base + 1, base + 2))

    def write(self, out_dir):
        lines = [f"# Generated by tools/gen_models.py - do not hand-edit.", f"o {self.name}"]
        for p, n, uv in self.verts:
            lines.append(f"v {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}")
            lines.append(f"vn {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}")
            lines.append(f"vt {uv[0]:.6f} {uv[1]:.6f}")
        for a, b, c in self.tris:
            lines.append(f"f {a + 1}/{a + 1}/{a + 1} {b + 1}/{b + 1}/{b + 1} "
                         f"{c + 1}/{c + 1}/{c + 1}")
        path = os.path.join(out_dir, f"{self.name}.obj")
        with open(path, "w", encoding="ascii", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        print(f"wrote {os.path.normpath(path)}: {len(self.verts)} vertices, "
              f"{len(self.tris)} triangles")


# ---- UNIT-BOUNDS props (shaped by spawn scale, like the cube they replace) --

def crate():
    m = Mesh("crate")
    m.box((0, 0, 0), (0.94, 0.94, 0.94))
    rail = 0.1
    for a, b in ((0.45, 0.45), (0.45, -0.45), (-0.45, 0.45), (-0.45, -0.45)):
        m.box((0, a, b), (1.0, rail, rail))   # 4 rails along X
        m.box((a, 0, b), (rail, 1.0, rail))   # 4 rails along Y
        m.box((a, b, 0), (rail, rail, 1.0))   # 4 rails along Z
    return m


def plate():
    # Three stepped tiers: reads as a button even squashed to 20 cm tall.
    m = Mesh("plate")
    m.box((0, -0.25, 0), (1.0, 0.5, 1.0))
    m.box((0, 0.0, 0), (0.84, 0.3, 0.84))
    m.box((0, 0.25, 0), (0.68, 0.5, 0.68))
    return m


def door():
    # Recessed slab in a full-thickness frame + mid-rail + hub: a vault door.
    m = Mesh("door")
    m.box((0, 0, 0), (1.0, 1.0, 0.62))
    m.box((0, 0.44, 0), (1.0, 0.12, 1.0))
    m.box((0, -0.44, 0), (1.0, 0.12, 1.0))
    m.box((-0.44, 0, 0), (0.12, 1.0, 1.0))
    m.box((0.44, 0, 0), (0.12, 1.0, 1.0))
    m.box((0, 0, 0), (1.0, 0.1, 0.9))
    m.cylinder((0, 0, 0), 0.09, 1.02, segments=10, axis="z")
    return m


def emitter():
    # Housing + barrel + muzzle ring, firing +X (beams travel east in-game).
    m = Mesh("emitter")
    m.box((-0.12, 0, 0), (0.75, 0.9, 0.9))
    m.cylinder((0.35, 0, 0), 0.2, 0.3, segments=12, axis="x")
    m.cylinder((0.46, 0, 0), 0.28, 0.08, segments=12, axis="x")
    return m


def receiver():
    # Housing + collector dish + lens, facing -X (toward the incoming beam).
    m = Mesh("receiver")
    m.box((0.12, 0, 0), (0.75, 0.9, 0.9))
    m.cylinder((-0.3, 0, 0), 0.34, 0.12, segments=12, axis="x")
    m.cylinder((-0.34, 0, 0), 0.18, 0.14, segments=12, axis="x")
    return m


# ---- REAL-SIZE characters (spawned at scale 1; forward = -Z) ---------------

def drone_body():
    m = Mesh("drone_body")
    m.cylinder((0, 0, 0), 0.21, 0.13, segments=10)            # hull
    m.cylinder((0, 0.095, 0), 0.14, 0.06, segments=10, r_top=0.10)  # dome
    m.cylinder((0, -0.09, 0), 0.09, 0.05, segments=10, r_top=0.13)  # skirt
    m.cylinder((0.2, -0.02, 0), 0.05, 0.12, segments=8)       # thruster pods
    m.cylinder((-0.2, -0.02, 0), 0.05, 0.12, segments=8)
    m.box((0, 0.01, -0.195), (0.14, 0.09, 0.05))              # eye mount
    m.box((0.06, 0.2, 0.1), (0.015, 0.12, 0.015), rot=(0, 0, 8))  # antenna
    return m


def drone_eye():
    # Separate entity: Lua scale-pulses it for emotion (aperture language).
    m = Mesh("drone_eye")
    m.cylinder((0, 0, 0), 0.055, 0.05, segments=12, axis="z")     # aperture ring
    m.cylinder((0, 0, -0.005), 0.032, 0.07, segments=12, axis="z")  # lens
    return m


def glove():
    # UNIT-7's gravity glove, right hand, wrist at origin, claws down -Z.
    m = Mesh("glove")
    m.cylinder((0, 0, 0.04), 0.07, 0.06, segments=10, axis="z")   # forearm stub
    m.cylinder((0, 0, -0.05), 0.085, 0.12, segments=10, axis="z")  # cuff
    m.box((0, 0, -0.18), (0.15, 0.1, 0.14))                       # palm
    m.box((0, 0.055, -0.19), (0.16, 0.03, 0.06))                  # knuckle plate
    m.cylinder((0, -0.01, -0.255), 0.035, 0.05, segments=10, axis="z")  # core lens
    for dx, yaw in ((-0.055, 14), (0.0, 0), (0.055, -14)):        # 3 claws
        m.box((dx, 0.02, -0.295), (0.034, 0.034, 0.1), rot=(-12, yaw, 0))
        m.box((dx * 1.15, -0.005, -0.37), (0.028, 0.028, 0.085), rot=(-35, yaw, 0))
    m.box((-0.085, -0.02, -0.22), (0.03, 0.03, 0.09), rot=(-20, 30, 0))  # thumb
    return m


def main() -> None:
    out_dir = os.path.join(os.path.dirname(__file__), "..", "assets", "models")
    os.makedirs(out_dir, exist_ok=True)
    for build in (crate, plate, door, emitter, receiver, drone_body, drone_eye, glove):
        build().write(out_dir)


if __name__ == "__main__":
    main()
