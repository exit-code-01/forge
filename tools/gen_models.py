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

    # ---- Assembly/Reactor zone primitives (appended; box/cylinder above are
    # untouched so the week-11/12 OBJs stay byte-identical) -----------------

    def poly(self, pts, away_from=None):
        """Flat convex polygon (fan), hard normal from the winding. If
        `away_from` is given the winding is flipped so the normal points
        away from that interior point (CCW-outward without hand-ordering)."""
        def nrm_of(q):
            ax, ay, az = (q[1][i] - q[0][i] for i in range(3))
            bx, by, bz = (q[2][i] - q[0][i] for i in range(3))
            n = (ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx)
            mag = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2)
            return (n[0] / mag, n[1] / mag, n[2] / mag)

        pts = list(pts)
        n = nrm_of(pts)
        if away_from is not None:
            c = [sum(p[i] for p in pts) / len(pts) for i in range(3)]
            if sum(n[i] * (c[i] - away_from[i]) for i in range(3)) < 0.0:
                pts.reverse()
                n = nrm_of(pts)
        uv_ring = ((0, 1), (1, 1), (1, 0), (0, 0))
        base = len(self.verts)
        for k, p in enumerate(pts):
            uv = (0.5, 0.0) if len(pts) == 3 and k == 2 else uv_ring[k % 4]
            self.verts.append((tuple(p), n, uv))
        for k in range(1, len(pts) - 1):
            self.tris.append((base, base + k, base + k + 1))

    def extrude_x(self, profile, x0, x1):
        """Convex (y, z) profile extruded along X from x0 to x1. Hard normals;
        every face is oriented away from the solid's centroid."""
        cy = sum(p[0] for p in profile) / len(profile)
        cz = sum(p[1] for p in profile) / len(profile)
        inside = ((x0 + x1) * 0.5, cy, cz)
        k = len(profile)
        for i in range(k):
            (ya, za), (yb, zb) = profile[i], profile[(i + 1) % k]
            self.poly(((x0, ya, za), (x1, ya, za), (x1, yb, zb), (x0, yb, zb)), away_from=inside)
        for x in (x0, x1):
            self.poly([(x, y, z) for y, z in profile], away_from=inside)

    def lathe(self, center, profile, segments=12, axis="y", smooth=False, closed=True):
        """Surface of revolution about `axis` through `center`. `profile` is a
        list of (radius, height) points wound COUNTER-CLOCKWISE in the
        (radius, height) plane (up the outside, down the inside), which makes
        every face CCW-outward. closed=True joins last->first (rings, tubes,
        tori); closed=False is for pole-to-pole profiles whose ends sit on
        the axis (spheres). Always smooth around the axis; `smooth` also
        blends normals along the profile (tori/spheres) instead of faceting
        it (machined rings, flanges)."""
        axis_rot = {"y": None, "x": (0, 0, -90), "z": (90, 0, 0)}[axis]
        m_axis = rot_euler(axis_rot) if axis_rot else None

        def place(local, is_normal=False):
            p = apply(m_axis, local) if m_axis else local
            if is_normal:
                return p
            return (center[0] + p[0], center[1] + p[1], center[2] + p[2])

        k = len(profile)
        seg_count = k if closed else k - 1
        seg_n = []
        for s in range(seg_count):
            (ra, ha), (rb, hb) = profile[s], profile[(s + 1) % k]
            dr, dh = rb - ra, hb - ha
            mag = math.hypot(dr, dh)
            seg_n.append((dh / mag, -dr / mag))  # outward for a CCW profile

        def vert_n(idx):  # blended profile normal at profile vertex idx
            prev_s, next_s = idx - 1, idx
            if closed:
                prev_s %= seg_count
                next_s %= seg_count
            parts = [seg_n[s] for s in (prev_s, next_s) if 0 <= s < seg_count]
            nr = sum(p[0] for p in parts)
            nh = sum(p[1] for p in parts)
            if profile[idx][0] < 1e-9:  # on the axis: a pole points along it
                nr = 0.0
            mag = math.hypot(nr, nh)
            return (nr / mag, nh / mag)

        lengths = [math.hypot(profile[(s + 1) % k][0] - profile[s][0],
                              profile[(s + 1) % k][1] - profile[s][1]) for s in range(seg_count)]
        total = sum(lengths)
        run = 0.0
        for s in range(seg_count):
            a, b = profile[s], profile[(s + 1) % k]
            na = vert_n(s) if smooth else seg_n[s]
            nb = vert_n((s + 1) % k) if smooth else seg_n[s]
            v0, v1 = 1.0 - run / total, 1.0 - (run + lengths[s]) / total
            run += lengths[s]
            for i in range(segments):
                th0 = 2.0 * math.pi * i / segments
                th1 = 2.0 * math.pi * ((i + 1) % segments) / segments
                u0, u1 = i / segments, (i + 1) / segments
                corners = []
                for (r, h), (nr, nh), th, u, v in ((a, na, th0, u0, v0), (b, nb, th0, u0, v1),
                                                   (b, nb, th1, u1, v1), (a, na, th1, u1, v0)):
                    c, sn = math.cos(th), math.sin(th)
                    corners.append((place((r * c, h, r * sn)),
                                    place((nr * c, nh, nr * sn), is_normal=True), (u, v)))
                # A profile point on the axis collapses one edge of the quad:
                # emit only the surviving triangle (no zero-area faces).
                if a[0] > 1e-9 and b[0] > 1e-9:
                    base = len(self.verts)
                    self.verts.extend(corners)
                    self.tris.append((base, base + 1, base + 2))
                    self.tris.append((base + 2, base + 3, base))
                elif b[0] > 1e-9:    # a is a pole
                    base = len(self.verts)
                    self.verts.extend(corners[:3])
                    self.tris.append((base, base + 1, base + 2))
                elif a[0] > 1e-9:    # b is a pole
                    base = len(self.verts)
                    self.verts.extend((corners[2], corners[3], corners[0]))
                    self.tris.append((base, base + 1, base + 2))

    def torus(self, center, major, minor, segments=12, sides=6, axis="y"):
        """Smooth torus: `major` ring radius, `minor` tube radius."""
        prof = [(major + minor * math.cos(2.0 * math.pi * j / sides),
                 minor * math.sin(2.0 * math.pi * j / sides)) for j in range(sides)]
        self.lathe(center, prof, segments=segments, axis=axis, smooth=True)

    def sphere(self, center, radius, segments=10, stacks=5, squash=1.0):
        """Smooth UV sphere (squash < 1 flattens it along Y)."""
        prof = []
        for j in range(stacks + 1):
            phi = -0.5 * math.pi + math.pi * j / stacks
            r = 0.0 if j in (0, stacks) else radius * math.cos(phi)
            prof.append((r, radius * squash * math.sin(phi)))
        self.lathe(center, prof, segments=segments, smooth=True, closed=False)

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


def bell():
    # Resonance bell (week 12): a gong dish facing +Z (toward the player's
    # firing lane) on a mount block. An instrument, not an enemy — stepped
    # concentric rings read as "strike me" at range.
    m = Mesh("bell")
    m.box((0, 0, -0.32), (0.26, 0.26, 0.36))                            # mount
    m.cylinder((0, 0, -0.06), 0.48, 0.12, segments=14, axis="z")        # outer ring
    m.cylinder((0, 0, 0.05), 0.32, 0.12, segments=14, axis="z", r_top=0.24)  # cone step
    m.cylinder((0, 0, 0.15), 0.12, 0.1, segments=10, axis="z")          # boss
    return m


def socket():
    # Snap-socket (week 12): a shallow landing basin — base pad, flared
    # funnel ring, four guide nubs. Forgiveness geometry: the script eases
    # anything that lands close into the seat.
    m = Mesh("socket")
    m.cylinder((0, -0.42, 0), 0.5, 0.16, segments=14)                   # base pad
    m.cylinder((0, -0.28, 0), 0.34, 0.14, segments=14, r_top=0.48)      # flare ring
    for a, b in ((0.4, 0), (-0.4, 0), (0, 0.4), (0, -0.4)):             # guide nubs
        m.box((a, -0.22, b), (0.12, 0.26, 0.12))
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


# ---- Assembly/Reactor zone props (all UNIT BOUNDS, appended after the
# week-11/12 set; each fills [-0.5, 0.5] on its dominant axes) --------------

def battery():
    # Upright power cell: fat body between two wider banded end caps, a carry
    # collar + centre terminal on top. Rotationally symmetric, so it reads
    # as "battery" from every side.
    m = Mesh("battery")
    m.cylinder((0, -0.43, 0), 0.5, 0.14, segments=14)                   # bottom cap band
    m.cylinder((0, -0.08, 0), 0.42, 0.56, segments=14)                  # body
    m.cylinder((0, -0.08, 0), 0.45, 0.07, segments=14)                  # mid band
    m.cylinder((0, 0.26, 0), 0.5, 0.12, segments=14)                    # top cap band
    m.lathe((0, 0, 0), [(0.27, 0.32), (0.38, 0.32), (0.38, 0.5), (0.27, 0.5)],
            segments=14)                                                # handle/carry ring
    m.cylinder((0, 0.39, 0), 0.12, 0.14, segments=10)                   # terminal
    return m


def prism():
    # Prism cube: a 12-strut cage around a 45-degree mirror slab. The slab is
    # a vertical plane running from the -X/-Z corner to the +X/+Z corner
    # (mirror normal = (1, 0, -1)/sqrt2), so a ray travelling +X leaves
    # travelling +Z (and +Z -> +X, -X -> -Z, -Z -> -X).
    m = Mesh("prism")
    s, c = 0.09, 0.455
    for a, b in ((c, c), (c, -c), (-c, c), (-c, -c)):
        m.box((0, a, b), (1.0, s, s))   # 4 struts along X
        m.box((a, 0, b), (s, 1.0, s))   # 4 struts along Y
        m.box((a, b, 0), (s, s, 1.0))   # 4 struts along Z
    m.box((0, 0, 0), (1.26, 0.86, 0.05), rot=(0, -45, 0))               # mirror slab
    return m


def core():
    # Collectible data core: an elongated octahedron (hard facets) with a
    # thin free-floating equatorial ring.
    m = Mesh("core")
    r = 0.3
    eq = ((r, 0, 0), (0, 0, r), (-r, 0, 0), (0, 0, -r))
    for apex in ((0, 0.5, 0), (0, -0.5, 0)):
        for i in range(4):
            m.poly((apex, eq[i], eq[(i + 1) % 4]), away_from=(0, 0, 0))
    m.lathe((0, 0, 0), [(0.41, -0.03), (0.5, -0.03), (0.5, 0.03), (0.41, 0.03)], segments=16)
    return m


def tesla():
    # Tesla coil: stepped base, column, a stack of four coil rings, and a
    # slightly squashed sphere terminal on top.
    m = Mesh("tesla")
    m.box((0, -0.45, 0), (1.0, 0.1, 1.0))                               # base slab
    m.cylinder((0, -0.36, 0), 0.4, 0.08, segments=12)                   # base step
    m.cylinder((0, -0.29, 0), 0.26, 0.06, segments=10)                  # collar
    m.cylinder((0, 0.0, 0), 0.1, 0.56, segments=8)                      # column
    for y in (-0.19, -0.07, 0.05, 0.17):                                # coil rings
        m.torus((0, y, 0), 0.2, 0.05, segments=10, sides=4)
    m.sphere((0, 0.34, 0), 0.2, segments=10, stacks=4, squash=0.8)      # terminal
    return m


def lamp():
    # Industrial ceiling lamp, hanging: mounting stem on top (+Y), a shoulder
    # cone, a short cylindrical housing, and a flat disc lens underneath (-Y).
    m = Mesh("lamp")
    m.cylinder((0, 0.34, 0), 0.07, 0.26, segments=8)                    # stem
    m.cylinder((0, 0.47, 0), 0.16, 0.06, segments=10)                   # ceiling rose
    m.cylinder((0, 0.15, 0), 0.5, 0.16, segments=14, r_top=0.16)        # shoulder cone
    m.cylinder((0, -0.18, 0), 0.5, 0.5, segments=14)                    # housing
    m.cylinder((0, -0.465, 0), 0.4, 0.07, segments=14)                  # lens disc
    return m


def barrel():
    # Oil drum: body + three rim bands (top, middle, bottom) + a bung.
    m = Mesh("barrel")
    m.cylinder((0, -0.01, 0), 0.45, 0.98, segments=14)                  # body
    for y in (-0.42, 0.0, 0.4):
        m.cylinder((0, y, 0), 0.5, 0.07, segments=14)                   # rim bands
    m.cylinder((0.24, 0.48, 0), 0.08, 0.04, segments=8)                 # bung
    return m


def pipe():
    # Straight pipe section along X, a flange at each end, hollow bore (one
    # lathe: flange - barrel - flange up the outside, bore back down).
    m = Mesh("pipe")
    m.lathe((0, 0, 0), [(0.27, -0.5), (0.5, -0.5), (0.5, -0.41), (0.37, -0.41), (0.37, 0.41),
                        (0.5, 0.41), (0.5, 0.5), (0.27, 0.5)], segments=14, axis="x")
    return m


def console():
    # Wall control panel. Back is flat at +Z (mount it against a wall there),
    # the slanted control face looks up and toward -Z (prop forward).
    m = Mesh("console")
    m.extrude_x([(-0.5, 0.5), (0.5, 0.5), (0.5, 0.1), (-0.1, -0.5), (-0.5, -0.5)], -0.5, 0.5)
    k = math.sqrt(0.5)

    def on_face(x, s, lift):  # s: 0 = top of the slant, 1 = bottom
        return (x, 0.5 - 0.6 * s + lift * k, 0.1 - 0.6 * s - lift * k)

    m.box(on_face(-0.12, 0.27, 0.01), (0.56, 0.04, 0.3), rot=(-45, 0, 0))   # screen bezel
    for x in (-0.32, -0.14, 0.04):
        m.box(on_face(x, 0.74, 0.02), (0.12, 0.07, 0.12), rot=(-45, 0, 0))  # buttons
    m.cylinder(on_face(0.31, 0.3, 0.03), 0.08, 0.1, segments=10, rot=(-45, 0, 0))  # knob
    m.cylinder(on_face(0.31, 0.72, 0.02), 0.1, 0.06, segments=10, rot=(-45, 0, 0))  # dial
    return m


def roller():
    # Conveyor end roller: drum along X with a rim at each end + axle stubs.
    m = Mesh("roller")
    m.cylinder((0, 0, 0), 0.46, 0.84, segments=16, axis="x")            # drum
    for x in (-0.37, 0.37):
        m.cylinder((x, 0, 0), 0.5, 0.08, segments=16, axis="x")         # end rims
    m.cylinder((0, 0, 0), 0.14, 1.0, segments=10, axis="x")             # axle
    return m


def strip():
    # Light-strip housing: shallow bar along X, raised bevelled lens on +Y
    # (rotate the entity to aim the lens; one mesh = one material).
    m = Mesh("strip")
    m.box((0, -0.25, 0), (1.0, 0.5, 1.0))                               # housing
    m.extrude_x([(0.0, -0.36), (0.0, 0.36), (0.5, 0.22), (0.5, -0.22)], -0.47, 0.47)  # lens
    return m


def main() -> None:
    out_dir = os.path.join(os.path.dirname(__file__), "..", "assets", "models")
    os.makedirs(out_dir, exist_ok=True)
    for build in (crate, plate, door, emitter, receiver, bell, socket, drone_body, drone_eye,
                  glove):
        build().write(out_dir)
    # Assembly/Reactor zone props - appended so the list above never reorders.
    for build in (battery, prism, core, tesla, lamp, barrel, pipe, console, roller, strip):
        build().write(out_dir)


if __name__ == "__main__":
    main()
