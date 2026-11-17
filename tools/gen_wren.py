"""tools/gen_wren.py: builds assets/models/wren_bot.glb, the Surface quest-giver.

An ORIGINAL robot (no third-party geometry): a stocky relay-keeper unit with a
visor head, antenna, back-mounted dish and jointed limbs. Rigid skinning (each
part follows one bone), 15 joints, three clips: Idle (loop), Interact, Cheer.

Run headless from the repo root:
    blender -b --factory-startup -P tools/gen_wren.py
    blender -b --factory-startup -P tools/gen_wren.py -- --preview out_dir   # also PNG stills
Blender 4.4+ (written against 5.3). Authored facing -Y (glTF +Z), 1.55 m tall.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "models", "wren_bot.glb")
FPS = 30

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = FPS


def material(name, rgb, metallic=0.2, rough=0.6):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = rough
    m.diffuse_color = (*rgb, 1.0)
    return m


SHELL = material("wren_shell", (0.86, 0.60, 0.22))    # weathered relay orange
PLATE = material("wren_plate", (0.82, 0.80, 0.74))    # off-white panels
JOINT = material("wren_joint", (0.14, 0.15, 0.17))    # dark joints and visor
TEAL = material("wren_teal", (0.10, 0.55, 0.58))      # accents
GLOW = material("wren_glow", (0.45, 0.95, 1.00))      # eyes, chest light

parts = []  # (object, bone name)


def finish(obj, mat, bone):
    obj.data.materials.append(mat)
    parts.append((obj, bone))
    return obj


def box(bone, mat, centre, size, bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=centre)
    o = bpy.context.active_object
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel > 0:
        mod = o.modifiers.new("b", "BEVEL")
        mod.width, mod.segments = bevel, 1
        bpy.ops.object.modifier_apply(modifier="b")
    return finish(o, mat, bone)


def cyl(bone, mat, a, b, radius, verts=10, radius2=None):
    a, b = Vector(a), Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=radius, radius2=radius2 or radius,
                                    depth=d.length, location=(a + b) * 0.5)
    o = bpy.context.active_object
    o.rotation_euler = d.to_track_quat("Z", "Y").to_euler()
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    return finish(o, mat, bone)


def ball(bone, mat, centre, radius, scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=radius, location=centre)
    o = bpy.context.active_object
    o.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return finish(o, mat, bone)


# ---- joints (Blender Z up, facing -Y) -------------------------------------
J = {
    "root": (0, 0, 0), "hips": (0, 0, 0.70), "chest": (0, 0, 0.86), "head": (0, 0, 1.24),
    "antenna": (0.10, 0.02, 1.50),
    "upper_arm.L": (0.31, 0, 1.14), "forearm.L": (0.33, 0, 0.86),
    "upper_arm.R": (-0.31, 0, 1.14), "forearm.R": (-0.33, 0, 0.86),
    "thigh.L": (0.12, 0, 0.68), "shin.L": (0.12, 0, 0.38), "foot.L": (0.12, 0, 0.08),
    "thigh.R": (-0.12, 0, 0.68), "shin.R": (-0.12, 0, 0.38), "foot.R": (-0.12, 0, 0.08),
}
PARENT = {
    "hips": "root", "chest": "hips", "head": "chest", "antenna": "head",
    "upper_arm.L": "chest", "forearm.L": "upper_arm.L", "upper_arm.R": "chest", "forearm.R": "upper_arm.R",
    "thigh.L": "hips", "shin.L": "thigh.L", "foot.L": "shin.L",
    "thigh.R": "hips", "shin.R": "thigh.R", "foot.R": "shin.R",
}

# ---- body -----------------------------------------------------------------
box("hips", JOINT, (0, 0, 0.72), (0.34, 0.22, 0.14), 0.03)
cyl("hips", JOINT, (0, 0, 0.76), (0, 0, 0.88), 0.09)                      # waist
box("chest", SHELL, (0, 0, 1.03), (0.50, 0.32, 0.34), 0.05)               # torso
box("chest", PLATE, (0, -0.165, 1.06), (0.34, 0.03, 0.20), 0.01)          # chest plate
ball("chest", GLOW, (0.09, -0.185, 1.10), 0.028)                          # status light
box("chest", TEAL, (-0.05, -0.185, 1.00), (0.16, 0.012, 0.03))            # label strip
box("chest", JOINT, (0, 0.20, 1.04), (0.30, 0.10, 0.26), 0.02)            # back pack
cyl("chest", JOINT, (0.0, 0.25, 1.12), (0.0, 0.34, 1.22), 0.015, 6)       # dish arm
cyl("chest", PLATE, (0.0, 0.33, 1.21), (0.0, 0.37, 1.25), 0.13, 12, 0.03)  # relay dish
cyl("chest", JOINT, (0, 0, 1.20), (0, 0, 1.26), 0.06)                     # neck
# head: wide visor block, two eyes, ear caps, antenna
box("head", SHELL, (0, 0, 1.37), (0.36, 0.28, 0.22), 0.05)
box("head", JOINT, (0, -0.135, 1.37), (0.29, 0.03, 0.10), 0.01)           # visor
ball("head", GLOW, (0.07, -0.150, 1.375), 0.03, (1, 0.5, 1))
ball("head", GLOW, (-0.07, -0.150, 1.375), 0.03, (1, 0.5, 1))
cyl("head", TEAL, (0.18, 0, 1.37), (0.21, 0, 1.37), 0.06, 10)
cyl("head", TEAL, (-0.18, 0, 1.37), (-0.21, 0, 1.37), 0.06, 10)
cyl("antenna", JOINT, (0.10, 0.02, 1.47), (0.10, 0.02, 1.62), 0.008, 6)
ball("antenna", GLOW, (0.10, 0.02, 1.635), 0.022)
for s, side in ((1, "L"), (-1, "R")):
    ua, fa = "upper_arm." + side, "forearm." + side
    ball(ua, JOINT, (s * 0.31, 0, 1.14), 0.075)                            # shoulder
    box(ua, SHELL, (s * 0.335, 0, 1.17), (0.12, 0.17, 0.10), 0.02)         # shoulder pad
    cyl(ua, PLATE, (s * 0.315, 0, 1.09), (s * 0.33, 0, 0.90), 0.045, 8)
    ball(fa, JOINT, (s * 0.33, 0, 0.86), 0.055)                            # elbow
    cyl(fa, SHELL, (s * 0.33, 0, 0.83), (s * 0.335, 0, 0.62), 0.06, 8, 0.07)   # cuff forearm
    box(fa, JOINT, (s * 0.335, 0, 0.565), (0.09, 0.11, 0.10), 0.02)        # hand
    box(fa, JOINT, (s * 0.335, -0.06, 0.52), (0.07, 0.03, 0.06), 0.008)    # fingers
    th, sh, ft = "thigh." + side, "shin." + side, "foot." + side
    ball(th, JOINT, (s * 0.12, 0, 0.68), 0.065)                            # hip
    cyl(th, PLATE, (s * 0.12, 0, 0.64), (s * 0.12, 0, 0.42), 0.055, 8)
    ball(sh, JOINT, (s * 0.12, 0, 0.38), 0.06)                             # knee
    cyl(sh, SHELL, (s * 0.12, 0, 0.35), (s * 0.12, 0, 0.12), 0.06, 8, 0.075)
    box(ft, JOINT, (s * 0.12, -0.04, 0.04), (0.15, 0.28, 0.08), 0.02)      # foot

# ---- one mesh, rigid weights ------------------------------------------------
for obj, bone in parts:
    vg = obj.vertex_groups.new(name=bone)
    vg.add(range(len(obj.data.vertices)), 1.0, "REPLACE")
    for poly in obj.data.polygons:
        poly.use_smooth = False
bpy.ops.object.select_all(action="DESELECT")
for obj, _ in parts:
    obj.select_set(True)
bpy.context.view_layer.objects.active = parts[0][0]
bpy.ops.object.join()
body = bpy.context.active_object
body.name = "WrenBot"
body.data.name = "WrenBot"

# ---- armature: every bone points up, so pose axes are X = right, Y = up,
# Z = toward the viewer (-Y world); easy to reason about --------------------
arm_data = bpy.data.armatures.new("WrenRig")
rig = bpy.data.objects.new("WrenRig", arm_data)
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
for name, head in J.items():
    eb = arm_data.edit_bones.new(name)
    eb.head = head
    eb.tail = (head[0], head[1], head[2] + 0.1)
    eb.roll = 0.0
for name, parent in PARENT.items():
    arm_data.edit_bones[name].parent = arm_data.edit_bones[parent]
bpy.ops.object.mode_set(mode="OBJECT")
body.parent = rig
body.modifiers.new("Armature", "ARMATURE").object = rig

for pb in rig.pose.bones:
    pb.rotation_mode = "XYZ"
D = math.radians


def clip(name, frames, keys):
    """keys: {bone: [(frame, (rx, ry, rz) degrees, optional (lx, ly, lz))]}"""
    rig.animation_data_create()
    ad = rig.animation_data
    ad.action = None
    for pb in rig.pose.bones:
        pb.rotation_euler = (0, 0, 0)
        pb.location = (0, 0, 0)
    for pb in rig.pose.bones:  # every bone keyed at both ends: clips fully override
        ks = keys.get(pb.name, [(0, (0, 0, 0))])
        if ks[-1][0] != frames:
            ks = ks + [(frames, ks[0][1]) + tuple(ks[0][2:])]
        for k in ks:
            pb.rotation_euler = tuple(D(a) for a in k[1])
            pb.keyframe_insert("rotation_euler", frame=k[0] + 1)
            if pb.name == "hips":
                pb.location = k[2] if len(k) > 2 else (0, 0, 0)
                pb.keyframe_insert("location", frame=k[0] + 1)
    act = ad.action
    act.name = name
    track = ad.nla_tracks.new()
    track.name = name
    track.strips.new(name, 1, act)
    track.mute = True
    ad.action = None
    return act


# pose axes: +X pitch = lean/swing BACK, -X = forward; +Z = left arm out to the side
clip("Idle", 72, {
    "hips": [(0, (0, 0, 0), (0, 0, 0)), (36, (0, 0, 0), (0, -0.012, 0)), (72, (0, 0, 0), (0, 0, 0))],
    "chest": [(0, (0, 0, 0)), (36, (1.5, 0, 0)), (72, (0, 0, 0))],
    "head": [(0, (0, -6, 0)), (36, (-2, 6, 0)), (72, (0, -6, 0))],
    "antenna": [(0, (0, 0, -6)), (18, (0, 0, 6)), (36, (0, 0, -6)), (54, (0, 0, 6)), (72, (0, 0, -6))],
    "upper_arm.L": [(0, (0, 0, 4)), (36, (-3, 0, 6)), (72, (0, 0, 4))],
    "upper_arm.R": [(0, (0, 0, -4)), (36, (-3, 0, -6)), (72, (0, 0, -4))],
    "forearm.L": [(0, (-8, 0, 0)), (36, (-12, 0, 0)), (72, (-8, 0, 0))],
    "forearm.R": [(0, (-8, 0, 0)), (36, (-12, 0, 0)), (72, (-8, 0, 0))],
})
clip("Interact", 42, {  # a short "listen, here" gesture with the right hand
    "chest": [(0, (0, 0, 0)), (10, (-4, 8, 0)), (30, (-4, 8, 0)), (42, (0, 0, 0))],
    "head": [(0, (0, -6, 0)), (8, (-10, 0, 0)), (14, (4, 0, 0)), (20, (-8, 0, 0)), (30, (0, 0, 0)), (42, (0, -6, 0))],
    "upper_arm.R": [(0, (0, 0, -4)), (10, (-55, 0, -12)), (30, (-50, 0, -12)), (42, (0, 0, -4))],
    "forearm.R": [(0, (-8, 0, 0)), (10, (-70, 0, 0)), (16, (-50, 0, 0)), (22, (-70, 0, 0)), (30, (-55, 0, 0)), (42, (-8, 0, 0))],
    "upper_arm.L": [(0, (0, 0, 4)), (42, (0, 0, 4))],
    "forearm.L": [(0, (-8, 0, 0)), (42, (-8, 0, 0))],
    "antenna": [(0, (0, 0, -6)), (10, (0, 0, 10)), (20, (0, 0, -10)), (30, (0, 0, 8)), (42, (0, 0, -6))],
})
clip("Cheer", 54, {  # both arms up, two hops
    "hips": [(0, (0, 0, 0), (0, 0, 0)), (6, (0, 0, 0), (0, -0.06, 0)), (14, (0, 0, 0), (0, 0.14, 0)),
             (22, (0, 0, 0), (0, -0.05, 0)), (30, (0, 0, 0), (0, 0.12, 0)), (40, (0, 0, 0), (0, -0.03, 0)),
             (54, (0, 0, 0), (0, 0, 0))],
    "chest": [(0, (0, 0, 0)), (14, (6, 0, 0)), (40, (4, 0, 0)), (54, (0, 0, 0))],
    "head": [(0, (0, -6, 0)), (14, (12, 0, 0)), (40, (10, 0, 0)), (54, (0, -6, 0))],
    "upper_arm.L": [(0, (0, 0, 4)), (12, (-10, 0, 150)), (22, (-10, 0, 135)), (30, (-10, 0, 152)), (40, (-10, 0, 140)), (54, (0, 0, 4))],
    "upper_arm.R": [(0, (0, 0, -4)), (12, (-10, 0, -150)), (22, (-10, 0, -135)), (30, (-10, 0, -152)), (40, (-10, 0, -140)), (54, (0, 0, -4))],
    "forearm.L": [(0, (-8, 0, 0)), (12, (0, 0, 20)), (40, (0, 0, 20)), (54, (-8, 0, 0))],
    "forearm.R": [(0, (-8, 0, 0)), (12, (0, 0, -20)), (40, (0, 0, -20)), (54, (-8, 0, 0))],
    "thigh.L": [(0, (0, 0, 0)), (6, (-18, 0, 0)), (14, (0, 0, 0)), (22, (-14, 0, 0)), (30, (0, 0, 0)), (54, (0, 0, 0))],
    "thigh.R": [(0, (0, 0, 0)), (6, (-18, 0, 0)), (14, (0, 0, 0)), (22, (-14, 0, 0)), (30, (0, 0, 0)), (54, (0, 0, 0))],
    "shin.L": [(0, (0, 0, 0)), (6, (30, 0, 0)), (14, (0, 0, 0)), (22, (24, 0, 0)), (30, (0, 0, 0)), (54, (0, 0, 0))],
    "shin.R": [(0, (0, 0, 0)), (6, (30, 0, 0)), (14, (0, 0, 0)), (22, (24, 0, 0)), (30, (0, 0, 0)), (54, (0, 0, 0))],
    "antenna": [(0, (0, 0, -6)), (14, (0, 0, 14)), (22, (0, 0, -14)), (30, (0, 0, 14)), (40, (0, 0, -10)), (54, (0, 0, -6))],
})

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if "--preview" in args:  # stills for a human look; not needed for the export
    out_dir = args[args.index("--preview") + 1]
    cam_data = bpy.data.cameras.new("cam")
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    cam.location = (1.9, -3.0, 1.25)
    cam.rotation_euler = (Vector((0, 0, 0.82)) - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.render.resolution_x, scene.render.resolution_y = 420, 520
    acts = {a.name: a for a in bpy.data.actions}
    for name, frame in (("Idle", 1), ("Interact", 11), ("Cheer", 13), ("Cheer", 7)):
        rig.animation_data.action = acts[name]
        scene.frame_set(frame)
        scene.render.filepath = os.path.join(out_dir, "wren_%s_%d.png" % (name, frame))
        bpy.ops.render.render(write_still=True)
    rig.animation_data.action = None
    bpy.data.objects.remove(cam)

for pb in rig.pose.bones:
    pb.rotation_euler = (0, 0, 0)
    pb.location = (0, 0, 0)
scene.frame_set(1)
bpy.ops.export_scene.gltf(
    filepath=os.path.normpath(OUT), export_format="GLB", export_yup=True, export_apply=False,
    export_skins=True, export_animations=True, export_animation_mode="NLA_TRACKS",
    export_force_sampling=True, export_frame_range=False, export_cameras=False, export_lights=False,
    export_materials="EXPORT", export_normals=True, export_texcoords=True)
print("WREN_BOT written:", os.path.normpath(OUT), os.path.getsize(OUT), "bytes")
