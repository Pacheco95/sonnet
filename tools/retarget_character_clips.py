#!/usr/bin/env python3
"""Adds the motion-captured locomotion clips to the character sample's humanoid (issue #505).

  python3 tools/retarget_character_clips.py <downloads>

<downloads> is a folder holding, anywhere below it, `Male1_bvh.zip`, the ACCAD Open Motion Project's BVH takes
of its "Male 1" subject (CC BY 3.0, see apps/samples/character/LICENSES.md). Blender 4.2 or later must be on the
PATH, or named by the BLENDER environment variable (`/snap/bin/blender` here); numpy is needed too.

The script unpacks the takes it uses into a temporary folder and runs itself inside Blender
(`blender -b --factory-startup --python <this> -- sample ...`), which imports each take with Blender's own BVH
importer and samples, at every captured frame, the world position and orientation of each joint. The rest runs
outside Blender, on those samples and on the committed `humanoid.glb`:

1. Retarget onto the humanoid's rig (`tools/generate_character_humanoid.py`, Unreal mannequin bone names).
   A standing frame of the subject (REFERENCE) is matched by the humanoid in a pose built from its `Stand` clip,
   each mapped bone turned the least amount that points it as the subject's bone points. At every frame, a
   mapped bone's world orientation is the subject's bone's orientation relative to the reference frame, applied
   to that pose. The hips follow the subject's, scaled by the ratio of the two legs' lengths, and each leg is
   solved with two-bone inverse kinematics so the ankle follows the subject's scaled ankle: the humanoid's
   thigh and shin are not in the subject's proportion, and without it the planted feet slide.
2. Ground: the capture floor's tilt along each take is taken out, and each take is lifted so the shoes, skinned
   from the model's own mesh and weights, stand on y = 0; a planted shoe is put on the ground and none goes into
   it (plant).
3. Foot contacts: a shoe is planted while its lowest vertex is within CONTACT_HEIGHT of the ground and
   its heel or toes move slower than CONTACT_SPEED; the frame it comes down is a footstep.
4. Clips (CLIPS): every clip is in place, its horizontal travel removed, since the character's capsule moves it
   (ADR-0038). A cycle (walk, run) runs from a left footstep to the next one, whose interval is the stride;
   key times are warped piecewise linearly so the right footstep falls at half the cycle, as ADR-0038's phase
   synchronisation needs, and what the last frame differs from the first is spread over the cycle so it loops.
   The idle loop is the stretch of the standing take whose ends match best, closed the same way. The jump's
   flight is taken out of the pelvis's height: the take-off ends when the feet leave the ground, the fall loops
   the descent forwards then back, and the landing starts a frame before the feet touch.
5. Writes the clips into `humanoid.glb` after `Stand` (removing any it added before, so a second run gives the
   same file) and their footstep and landing events into the sidecar's `animationEvents`, and prints what it
   measured: stride lengths, contact phases, foot heights and sliding.

Then open the project in the editor once (or run any capture), which rebuilds the sidecar's sub-asset list for
the new clips. Run from anywhere; set KEEP_WORK=<folder> to keep the samples and a per-clip report
(`report.json`). With Blender 5.2.2 two runs wrote the same bytes.
"""
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODEL = ROOT / "apps" / "samples" / "character" / "assets" / "character" / "humanoid.glb"
ARCHIVE = "Male1_bvh.zip"
FPS = 30.0  # every Male 1 take is captured at 30 Hz

# The standing take and frame the humanoid's reference pose is matched to.
REFERENCE = ("Male1_A1_Stand", 0)

# Humanoid bone <- ACCAD joint. The rest of the rig (fingers, Root) keeps the `Stand` clip's pose.
MAPPING = {
    "pelvis": "Hips", "spine_01": "ToSpine", "spine_02": "Spine", "spine_03": "Spine1",
    "neck_01": "Neck", "head": "Head",
    "clavicle_l": "LeftShoulder", "upperarm_l": "LeftArm", "lowerarm_l": "LeftForeArm", "hand_l": "LeftHand",
    "clavicle_r": "RightShoulder", "upperarm_r": "RightArm", "lowerarm_r": "RightForeArm", "hand_r": "RightHand",
    "thigh_l": "LeftUpLeg", "calf_l": "LeftLeg", "foot_l": "LeftFoot", "ball_l": "LeftToeBase",
    "thigh_r": "RightUpLeg", "calf_r": "RightLeg", "foot_r": "RightFoot", "ball_r": "RightToeBase",
}
# The bone each one is pointed at in the reference pose: (humanoid child, ACCAD child or "tail" for the
# joint's end site). Bones not listed keep their parent's turn (head, ball).
AIM = {
    "spine_01": ("spine_02", "Spine"), "spine_02": ("spine_03", "Spine1"), "neck_01": ("head", "Head"),
    "clavicle_l": ("upperarm_l", "LeftArm"), "upperarm_l": ("lowerarm_l", "LeftForeArm"),
    "lowerarm_l": ("hand_l", "LeftHand"), "hand_l": ("middle_01_l", "tail"),
    "clavicle_r": ("upperarm_r", "RightArm"), "upperarm_r": ("lowerarm_r", "RightForeArm"),
    "lowerarm_r": ("hand_r", "RightHand"), "hand_r": ("middle_01_r", "tail"),
    "thigh_l": ("calf_l", "LeftLeg"), "calf_l": ("foot_l", "LeftFoot"), "foot_l": ("ball_l", "LeftToeBase"),
    "thigh_r": ("calf_r", "RightLeg"), "calf_r": ("foot_r", "RightFoot"), "foot_r": ("ball_r", "RightToeBase"),
}
# Bones aimed by two directions (up to a joint, across to the pair of joints that give left and right). Up is
# measured from the bone's own joint, or from the across pair's midpoint where the joint is not comparable.
FRAMES = {
    # Up is the trunk's axis, the hip joints' midpoint to the chest: the subject's `ToSpine` sits behind its
    # hips (Hips to ToSpine is 92 degrees from vertical, pelvis to spine_01 20), so aiming at it tipped the
    # pelvis 72 degrees forward. Measured this way the two agree within 4 degrees.
    "pelvis": (("spine_03", "Spine1"), ("thigh_l", "thigh_r", "LeftUpLeg", "RightUpLeg"), "midpoint"),
    "spine_03": (("neck_01", "Neck"), ("clavicle_l", "clavicle_r", "LeftShoulder", "RightShoulder"), "joint"),
}

CONTACT_HEIGHT = 0.025  # metres above the ground: a shoe this low is down...
CONTACT_SPEED = 0.6     # ...if its lowest point also moves slower than this, in m/s
LAND_LEAD = 1           # frames of the landing clip before the feet touch, so its event is crossed

# Clip name -> the take it is cut from and how (see build_clip). Every take is Male 1's.
CLIPS = {
    "Idle": {"take": "Male1_A1_Stand", "kind": "idle", "min_length": 3.0},
    "Walk": {"take": "Male1_B3_Walk", "kind": "cycle"},
    "Run": {"take": "Male1_C03_Run", "kind": "cycle"},
    "JumpStart": {"take": "Male1_C20_Run2Jump2Walk", "kind": "takeoff", "lead": 0.5, "tail": 2},
    "Fall": {"take": "Male1_C20_Run2Jump2Walk", "kind": "fall"},
    "Land": {"take": "Male1_B17_Walk2Hop2Walk", "kind": "land"},
    "HardLand": {"take": "Male1_C20_Run2Jump2Walk", "kind": "land", "argument": "hard"},
}
RECOVERED = 0.9  # a landing ends when the pelvis is back this far from its lowest to its height walking


# ---------------------------------------------------------------------------------------------
# Inside Blender: sample the takes
# ---------------------------------------------------------------------------------------------


def blender_sample(out_dir: Path, files):
    import bpy
    import numpy as np

    to_gltf = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], float)  # Blender's Z up -> glTF's Y up
    for source in files:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_anim.bvh(filepath=str(source), global_scale=0.01, use_fps_scale=False, rotate_mode='NATIVE')
        rig = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
        first, last = (int(round(f)) for f in rig.animation_data.action.frame_range)
        heads, tails, rotations = [], [], []
        for frame in range(first, last + 1):
            bpy.context.scene.frame_set(frame)
            world = np.array(rig.matrix_world)
            matrices = [world @ np.array(b.matrix) for b in rig.pose.bones]
            heads.append([to_gltf @ m[:3, 3] for m in matrices])
            rotations.append([to_gltf @ m[:3, :3] @ to_gltf.T for m in matrices])
            tails.append([to_gltf @ (world @ np.append(np.array(b.tail), 1.0))[:3] for b in rig.pose.bones])
        np.savez(out_dir / (Path(source).stem + ".npz"), names=np.array([b.name for b in rig.pose.bones]),
                 heads=np.array(heads), tails=np.array(tails), rotations=np.array(rotations))


# ---------------------------------------------------------------------------------------------
# Rotations
# ---------------------------------------------------------------------------------------------


def np_():
    import numpy
    return numpy


def quat_to_matrix(q):
    np = np_()
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def matrix_to_quat(m):
    np = np_()
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def orthonormal(m):
    np = np_()
    u, _, vt = np.linalg.svd(m)
    r = u @ vt
    if np.linalg.det(r) < 0:
        u[:, -1] *= -1
        r = u @ vt
    return r


def axis_angle(axis, angle):
    np = np_()
    axis = np.asarray(axis, float)
    norm = np.linalg.norm(axis)
    if norm < 1e-12:
        return np.eye(3)
    x, y, z = axis / norm
    c, s, t = math.cos(angle), math.sin(angle), 1 - math.cos(angle)
    return np.array([[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
                     [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
                     [t * x * z - s * y, t * y * z + s * x, t * z * z + c]])


def rotation_between(a, b):
    """The least rotation taking direction a to direction b."""
    np = np_()
    a = a / np.linalg.norm(a)
    b = b / np.linalg.norm(b)
    axis = np.cross(a, b)
    angle = math.atan2(np.linalg.norm(axis), float(np.dot(a, b)))
    if np.linalg.norm(axis) < 1e-9:
        if np.dot(a, b) > 0:
            return np.eye(3)
        axis = np.cross(a, [1.0, 0.0, 0.0]) if abs(a[0]) < 0.9 else np.cross(a, [0.0, 1.0, 0.0])
    return axis_angle(axis, angle)


def frame_of(up, across):
    """An orthonormal frame whose Y is `up` and whose X is `across` made perpendicular to it."""
    np = np_()
    y = up / np.linalg.norm(up)
    x = across - np.dot(across, y) * y
    x /= np.linalg.norm(x)
    return np.column_stack([x, y, np.cross(x, y)])


def yaw(angle):
    return axis_angle([0.0, 1.0, 0.0], angle)


def slerp(q0, q1, t):
    np = np_()
    if np.dot(q0, q1) < 0:
        q1 = -q1
    d = min(1.0, float(np.dot(q0, q1)))
    if d > 0.9995:
        q = q0 + t * (q1 - q0)
        return q / np.linalg.norm(q)
    theta = math.acos(d)
    return (math.sin((1 - t) * theta) * q0 + math.sin(t * theta) * q1) / math.sin(theta)


# ---------------------------------------------------------------------------------------------
# The binary glTF
# ---------------------------------------------------------------------------------------------

COMPONENTS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_glb(path: Path):
    data = path.read_bytes()
    magic, version, _ = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF" and version == 2, f"{path} is not a binary glTF 2.0 file"
    json_length, = struct.unpack_from("<I", data, 12)
    document = json.loads(data[20:20 + json_length])
    bin_offset = 20 + json_length
    bin_length, = struct.unpack_from("<I", data, bin_offset)
    return document, bytearray(data[bin_offset + 8:bin_offset + 8 + bin_length])


def accessor(document, binary, index):
    np = np_()
    a = document["accessors"][index]
    view = document["bufferViews"][a["bufferView"]]
    width = WIDTHS[a["type"]]
    dtype = np.dtype("<" + COMPONENTS[a["componentType"]])
    stride = view.get("byteStride", dtype.itemsize * width)
    start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
    rows = [np.frombuffer(binary, dtype, width, start + i * stride) for i in range(a["count"])]
    values = np.array(rows, float)
    if a.get("normalized"):
        values /= float(np.iinfo(dtype).max)
    return values


def write_glb(path: Path, document, binary: bytes):
    text = json.dumps(document, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary = bytes(binary) + b"\0" * (-len(binary) % 4)
    body = struct.pack("<I4s", len(text), b"JSON") + text + struct.pack("<I4s", len(binary), b"BIN\0") + binary
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body)


def drop_animations(document, binary, keep):
    """Removes every animation not named in `keep`, and the accessors and buffer bytes only they used."""
    document["animations"] = [a for a in document.get("animations", []) if a["name"] in keep]
    used_accessors = set()
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            used_accessors.update(primitive["attributes"].values())
            if "indices" in primitive:
                used_accessors.add(primitive["indices"])
            for target in primitive.get("targets", []):
                used_accessors.update(target.values())
    for skin in document.get("skins", []):
        if "inverseBindMatrices" in skin:
            used_accessors.add(skin["inverseBindMatrices"])
    for animation in document["animations"]:
        for sampler in animation["samplers"]:
            used_accessors.update((sampler["input"], sampler["output"]))
    accessor_map = {old: new for new, old in enumerate(sorted(used_accessors))}
    accessors = [document["accessors"][old] for old in sorted(used_accessors)]
    used_views = {a["bufferView"] for a in accessors} | {i["bufferView"] for i in document.get("images", [])
                                                           if "bufferView" in i}
    view_map, views, packed = {}, [], bytearray()
    for old in sorted(used_views):
        view = dict(document["bufferViews"][old])
        start = view.get("byteOffset", 0)
        packed += b"\0" * (-len(packed) % 4)
        chunk = binary[start:start + view["byteLength"]]
        view["byteOffset"] = len(packed)
        packed += chunk
        view_map[old] = len(views)
        views.append(view)
    for a in accessors:
        a["bufferView"] = view_map[a["bufferView"]]
    for image in document.get("images", []):
        if "bufferView" in image:
            image["bufferView"] = view_map[image["bufferView"]]
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            primitive["attributes"] = {k: accessor_map[v] for k, v in primitive["attributes"].items()}
            if "indices" in primitive:
                primitive["indices"] = accessor_map[primitive["indices"]]
            primitive["targets"] = [{k: accessor_map[v] for k, v in t.items()} for t in primitive.get("targets", [])]
            if not primitive["targets"]:
                del primitive["targets"]
    for skin in document.get("skins", []):
        if "inverseBindMatrices" in skin:
            skin["inverseBindMatrices"] = accessor_map[skin["inverseBindMatrices"]]
    for animation in document["animations"]:
        for sampler in animation["samplers"]:
            sampler["input"] = accessor_map[sampler["input"]]
            sampler["output"] = accessor_map[sampler["output"]]
    document["accessors"] = accessors
    document["bufferViews"] = views
    document["buffers"][0]["byteLength"] = len(packed)
    return packed


def append_floats(document, binary, values, kind, minmax=False):
    np = np_()
    values = np.asarray(values, "<f4")
    binary += b"\0" * (-len(binary) % 4)
    document["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": values.nbytes})
    binary += values.tobytes()
    entry = {"bufferView": len(document["bufferViews"]) - 1, "componentType": 5126,
             "count": int(values.shape[0]), "type": kind}
    if minmax:
        entry["min"] = [float(values.min())]
        entry["max"] = [float(values.max())]
    document["accessors"].append(entry)
    document["buffers"][0]["byteLength"] = len(binary)
    return len(document["accessors"]) - 1


# ---------------------------------------------------------------------------------------------
# The humanoid
# ---------------------------------------------------------------------------------------------


class Rig:
    """The humanoid's nodes, its `Stand` pose, and the shoes' skinning, from humanoid.glb."""

    def __init__(self, document, binary):
        np = np_()
        nodes = document["nodes"]
        self.names = [n["name"] for n in nodes]
        self.index = {n: i for i, n in enumerate(self.names)}
        self.parent = [-1] * len(nodes)
        for i, n in enumerate(nodes):
            for c in n.get("children", []):
                self.parent[c] = i
        self.translation = [np.array(n.get("translation", [0.0, 0.0, 0.0])) for n in nodes]
        self.rotation = [quat_to_matrix(n.get("rotation", [0.0, 0.0, 0.0, 1.0])) for n in nodes]
        stand = next(a for a in document["animations"] if a["name"] == "Stand")
        for channel in stand["channels"]:
            sampler = stand["samplers"][channel["sampler"]]
            value = accessor(document, binary, sampler["output"])[0]
            node = channel["target"]["node"]
            if channel["target"]["path"] == "rotation":
                self.rotation[node] = quat_to_matrix(value)
            elif channel["target"]["path"] == "translation":
                self.translation[node] = value
        self.order = []  # parents before children
        pending = [r for r in document["scenes"][0]["nodes"]]
        while pending:
            node = pending.pop(0)
            self.order.append(node)
            pending.extend(nodes[node].get("children", []))
        skin = document["skins"][0]
        self.joints = skin["joints"]
        inverse = accessor(document, binary, skin["inverseBindMatrices"]).reshape(-1, 4, 4)
        self.inverse_bind = [m.T for m in inverse]  # glTF matrices are column-major
        mesh = document["meshes"][nodes[self.index["Body"]]["mesh"]]
        shoes = next(i for i, m in enumerate(document["materials"]) if m["name"] == "Shoes")
        primitive = next(p for p in mesh["primitives"] if p["material"] == shoes)
        self.shoe_positions = accessor(document, binary, primitive["attributes"]["POSITION"])
        self.shoe_joints = accessor(document, binary, primitive["attributes"]["JOINTS_0"]).astype(int)
        self.shoe_weights = accessor(document, binary, primitive["attributes"]["WEIGHTS_0"])
        left = {self.joints.index(self.index[n]) for n in ("foot_l", "ball_l", "calf_l")}
        heaviest = self.shoe_joints[np.arange(len(self.shoe_joints)), self.shoe_weights.argmax(axis=1)]
        self.shoe_left = np.array([j in left for j in heaviest])

    def world(self, rotations=None, translations=None):
        """World rotations and positions of every node, from local overrides {node: matrix / vector}."""
        np = np_()
        rotations = rotations or {}
        translations = translations or {}
        R, P = [None] * len(self.names), [None] * len(self.names)
        for node in self.order:
            r = rotations.get(node, self.rotation[node])
            t = translations.get(node, self.translation[node])
            p = self.parent[node]
            if p < 0:
                R[node], P[node] = r, t
            else:
                R[node], P[node] = R[p] @ r, P[p] + R[p] @ t
        return R, P

    def world_from_world_rotations(self, world_rotations, pelvis_position):
        """Positions of every node given world rotations of the mapped bones and the pelvis's position."""
        np = np_()
        R, P = [None] * len(self.names), [None] * len(self.names)
        pelvis = self.index["pelvis"]
        for node in self.order:
            p = self.parent[node]
            if node in world_rotations:
                R[node] = world_rotations[node]
            else:
                R[node] = self.rotation[node] if p < 0 else R[p] @ self.rotation[node]
            if node == pelvis:
                P[node] = pelvis_position
            else:
                P[node] = self.translation[node] if p < 0 else P[p] + R[p] @ self.translation[node]
        return R, P

    def shoes(self, R, P):
        """World positions of the shoe vertices, skinned."""
        np = np_()
        palette = []
        for j, node in enumerate(self.joints):
            m = np.eye(4)
            m[:3, :3], m[:3, 3] = R[node], P[node]
            palette.append(m @ self.inverse_bind[j])
        palette = np.array(palette)
        homogeneous = np.c_[self.shoe_positions, np.ones(len(self.shoe_positions))]
        out = np.zeros((len(homogeneous), 3))
        for k in range(4):
            m = palette[self.shoe_joints[:, k]]
            out += self.shoe_weights[:, k:k + 1] * np.einsum("nij,nj->ni", m, homogeneous)[:, :3]
        return out


# ---------------------------------------------------------------------------------------------
# Retargeting
# ---------------------------------------------------------------------------------------------


class Take:
    def __init__(self, path: Path):
        np = np_()
        data = np.load(path)
        self.name = path.stem
        self.joint = {n: i for i, n in enumerate(data["names"].tolist())}
        self.heads, self.tails, self.rotations = data["heads"], data["tails"], data["rotations"]
        self.frames = len(self.heads)
        self.slope = self.level()

    def level(self):
        """Takes the capture floor's tilt out of the heights, along the direction the take travels.

        The Male 1 takes' floor is not level: planted ankles rise about 1 cm a metre along the walk. A line is
        fitted to the lower envelope of both ankles' heights against the distance along the take's travel, and
        every height is measured from it. Across the travel the feet are too close together to measure a tilt,
        and a take that does not travel (standing) is left as it is."""
        np = np_()
        points = np.concatenate([self.heads[:, self.joint[n]] for n in ("LeftFoot", "RightFoot")])
        travel = self.heads[-1, self.joint["Hips"]] - self.heads[0, self.joint["Hips"]]
        travel[1] = 0.0
        if np.linalg.norm(travel) < 1.0:
            return 0.0
        travel /= np.linalg.norm(travel)
        along = points @ travel
        keep = np.ones(len(points), bool)
        for _ in range(8):
            slope, offset = np.polyfit(along[keep], points[keep, 1], 1)
            residual = points[:, 1] - (slope * along + offset)
            keep = residual < np.percentile(residual, 30)
        for array in (self.heads, self.tails):
            array[..., 1] -= slope * (array @ travel)
        return float(slope)

    def head(self, name, f):
        return self.heads[f, self.joint[name]]

    def tail(self, name, f):
        return self.tails[f, self.joint[name]]

    def rot(self, name, f):
        return self.rotations[f, self.joint[name]]

    def facing(self, f):
        """The subject's forward at frame f, horizontal: up crossed with the hips' left-to-right."""
        np = np_()
        across = self.head("RightUpLeg", f) - self.head("LeftUpLeg", f)
        forward = np.cross([0.0, 1.0, 0.0], across)
        forward[1] = 0.0
        return forward / np.linalg.norm(forward)


def turn_to_forward(direction):
    """The yaw that takes a horizontal direction to -Z, the engine's forward."""
    return yaw(math.pi - math.atan2(direction[0], direction[2]))


class Retargeter:
    def __init__(self, rig: Rig, reference: Take, reference_frame: int):
        np = np_()
        self.rig = rig
        self.ref_take, self.ref_frame = reference, reference_frame
        self.ref_turn = turn_to_forward(reference.facing(reference_frame))
        R_stand, P_stand = rig.world()
        self.stand_R, self.stand_P = R_stand, P_stand
        idx = rig.index
        g = self.ref_turn

        def s_head(name):
            return g @ reference.head(name, reference_frame)

        self.ref_world = {}  # humanoid bone -> world rotation in the reference pose
        self.ref_source = {}  # humanoid bone -> the subject's world rotation at the reference frame, turned
        for bone in rig.order:
            name = rig.names[bone]
            if name not in MAPPING:
                continue
            source = MAPPING[name]
            self.ref_source[bone] = g @ reference.rot(source, reference_frame)
            if name in FRAMES:
                (child, s_child), (left, right, s_left, s_right), up_from = FRAMES[name]
                if up_from == "midpoint":
                    up_t = P_stand[idx[child]] - (P_stand[idx[left]] + P_stand[idx[right]]) / 2.0
                    up_s = s_head(s_child) - (s_head(s_left) + s_head(s_right)) / 2.0
                else:
                    up_t = R_stand[bone] @ rig.translation[idx[child]]
                    up_s = s_head(s_child) - s_head(source)
                across_t = R_stand[bone] @ (rig.translation[idx[right]] - rig.translation[idx[left]])
                across_s = s_head(s_right) - s_head(s_left)
                turn = frame_of(up_s, across_s) @ frame_of(up_t, across_t).T
            elif name in AIM:
                child, s_child = AIM[name]
                d_t = R_stand[bone] @ rig.translation[idx[child]]
                d_s = (g @ reference.tail(source, reference_frame) if s_child == "tail" else s_head(s_child)) \
                    - s_head(source)
                turn = rotation_between(d_t, d_s)
            else:  # keeps its parent's turn
                parent = rig.parent[bone]
                turn = self.ref_world[parent] @ R_stand[parent].T
            self.ref_world[bone] = turn @ R_stand[bone]
        # Leg lengths, for the scale of the hips' travel.
        leg = np.linalg.norm(rig.translation[idx["calf_l"]]) + np.linalg.norm(rig.translation[idx["foot_l"]])
        hip, knee, ankle = (reference.head(n, reference_frame) for n in ("LeftUpLeg", "LeftLeg", "LeftFoot"))
        source_leg = np.linalg.norm(knee - hip) + np.linalg.norm(ankle - knee)
        self.scale = leg / source_leg
        self.hip_mid_local = (rig.translation[idx["thigh_l"]] + rig.translation[idx["thigh_r"]]) / 2.0

    def pose(self, take: Take, f: int, turn, origin, lift, feet=(0.0, 0.0)):
        """World rotations of the mapped bones and the pelvis's position for frame f of a take. `lift` raises
        the whole pose and `feet` each ankle (left, right) on top of it."""
        np = np_()
        rig, idx = self.rig, self.rig.index
        world = {}
        for bone, ref_source in self.ref_source.items():
            source = MAPPING[rig.names[bone]]
            world[bone] = orthonormal(turn @ take.rot(source, f) @ ref_source.T @ self.ref_world[bone])

        def target(name):  # a subject's joint, turned, moved and scaled into the humanoid's world
            p = turn @ (take.head(name, f) - origin)
            p = p * self.scale
            p[1] += lift
            return p

        hip_mid = (target("LeftUpLeg") + target("RightUpLeg")) / 2.0
        pelvis = idx["pelvis"]
        pelvis_position = hip_mid - world[pelvis] @ self.hip_mid_local
        # Two-bone IK: each ankle where the subject's is, the knee bending in the plane it bends in already.
        R, P = rig.world_from_world_rotations(world, pelvis_position)
        for side, s_side, raise_foot in (("l", "Left", feet[0]), ("r", "Right", feet[1])):
            thigh, calf, foot = idx[f"thigh_{side}"], idx[f"calf_{side}"], idx[f"foot_{side}"]
            hip, knee, ankle = P[thigh], P[calf], P[foot]
            goal = hip + (target(f"{s_side}Foot") - target(f"{s_side}UpLeg"))
            goal[1] += raise_foot
            a, b = np.linalg.norm(rig.translation[calf]), np.linalg.norm(rig.translation[foot])
            reach = goal - hip
            distance = min(np.linalg.norm(reach), (a + b) * 0.9999)
            axis = reach / np.linalg.norm(reach)
            bend = knee - hip
            bend = bend - np.dot(bend, axis) * axis
            if np.linalg.norm(bend) < 1e-6:
                bend = world[pelvis] @ np.array([0.0, 0.0, -1.0])
                bend = bend - np.dot(bend, axis) * axis
            bend /= np.linalg.norm(bend)
            along = (a * a - b * b + distance * distance) / (2 * distance)
            height = math.sqrt(max(a * a - along * along, 0.0))
            new_knee = hip + axis * along + bend * height
            new_ankle = hip + axis * distance
            world[thigh] = rotation_between(knee - hip, new_knee - hip) @ world[thigh]
            knee_to_ankle = world[thigh] @ (R[thigh].T @ (ankle - knee))  # the shin as the turned thigh carries it
            carried = world[thigh] @ R[thigh].T @ world[calf]
            world[calf] = rotation_between(knee_to_ankle, new_ankle - new_knee) @ carried
        return world, pelvis_position

    def locals(self, world, pelvis_position):
        """Local rotations of every joint and the pelvis's local translation."""
        np = np_()
        rig = self.rig
        R, P = rig.world_from_world_rotations(world, pelvis_position)
        local = {}
        for node in rig.joints:
            p = rig.parent[node]
            local[node] = R[node] if p < 0 else R[p].T @ R[node]
        pelvis = rig.index["pelvis"]
        parent = rig.parent[pelvis]
        translation = R[parent].T @ (pelvis_position - P[parent])
        return local, translation, R, P


# ---------------------------------------------------------------------------------------------
# Clips
# ---------------------------------------------------------------------------------------------


class Track:
    """A retargeted stretch of a take: per frame, local rotations (quaternions) and the pelvis translation."""

    def __init__(self, retargeter, take, frames, turn, origin, lift, feet=(0.0, 0.0)):
        np = np_()
        self.frames = list(frames)
        self.rotations, self.pelvis, self.soles_left, self.soles_right = [], [], [], []
        self.joints = {n: [] for n in ("pelvis", "foot_l", "ball_l", "foot_r", "ball_r")}
        rig = retargeter.rig
        if not hasattr(feet[0], "__len__"):
            feet = [feet] * len(self.frames)
        for k, f in enumerate(self.frames):
            world, pelvis_position = retargeter.pose(take, f, turn, origin, lift, feet[k])
            local, translation, R, P = retargeter.locals(world, pelvis_position)
            self.rotations.append({n: matrix_to_quat(m) for n, m in local.items()})
            self.pelvis.append(translation)
            shoes = rig.shoes(R, P)
            for n, values in self.joints.items():
                values.append(P[rig.index[n]])
            self.soles_left.append(shoes[rig.shoe_left])
            self.soles_right.append(shoes[~rig.shoe_left])
        self.pelvis = np.array(self.pelvis)
        self.joints = {n: np.array(v) for n, v in self.joints.items()}
        for f in range(1, len(self.rotations)):  # one hemisphere, so keys interpolate the short way
            for n, q in self.rotations[f].items():
                if np.dot(q, self.rotations[f - 1][n]) < 0:
                    self.rotations[f][n] = -q


def sole_heights(points):
    np = np_()
    return np.array([p[:, 1].min() for p in points])


def lowest_points(points):
    np = np_()
    return np.array([p[p[:, 1].argmin()] for p in points])


def contacts(track, side, fps=FPS):
    """Frames a shoe is planted, and the frames it comes down (footsteps): its lowest vertex within
    CONTACT_HEIGHT of the ground while its heel (the ankle) or its toes (the ball joint) move slower than
    CONTACT_SPEED."""
    np = np_()
    height = sole_heights(track.soles_left if side == "l" else track.soles_right)

    def speed(points):
        v = np.r_[0.0, np.linalg.norm(np.diff(points[:, [0, 2]], axis=0), axis=1) * fps]
        return np.minimum(v, np.r_[v[1:], v[-1]])  # the slower side of the frame

    still = np.minimum(speed(track.joints[f"foot_{side}"]), speed(track.joints[f"ball_{side}"]))
    planted = (height < CONTACT_HEIGHT) & (still < CONTACT_SPEED)
    strikes = [f for f in range(1, len(planted)) if planted[f] and not planted[f - 1]]
    return planted, strikes, height


def ground(retargeter, take, frames, turn, origin):
    """How far to lift a take, and each ankle on top of that, so both shoes stand on y = 0 while planted.

    A shoe's ground is the 10th percentile of its lowest vertex's height over the take; the two shoes do not
    come out at the same height (1 to 5 cm apart), so each foot is levelled on its own."""
    np = np_()
    track = Track(retargeter, take, frames, turn, origin, 0.0)
    left = float(np.percentile(sole_heights(track.soles_left), 10))
    right = float(np.percentile(sole_heights(track.soles_right), 10))
    middle = (left + right) / 2.0
    return -middle, (middle - left, middle - right)


# ---------------------------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------------------------


def find(downloads: Path, name: str) -> Path:
    hits = sorted(downloads.rglob(name))
    if not hits:
        sys.exit(f"{name} not found under {downloads}")
    return hits[0]


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    if args and args[0] == "sample":
        blender_sample(Path(args[1]), args[2:])
        return
    if len(args) != 1:
        sys.exit(__doc__)
    import numpy as np  # noqa: F401 (checked before Blender runs)

    downloads = Path(args[0]).resolve()
    blender = os.environ.get("BLENDER") or shutil.which("blender") or "/snap/bin/blender"
    takes = sorted({c["take"] for c in CLIPS.values()} | {REFERENCE[0]})
    keep = os.environ.get("KEEP_WORK")
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(keep) if keep else Path(temporary)
        work.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(find(downloads, ARCHIVE)) as archive:
            members = {Path(m).stem: m for m in archive.namelist() if m.lower().endswith(".bvh")}
            files = []
            for take in takes:
                target = work / f"{take}.bvh"
                target.write_bytes(archive.read(members[take]))
                files.append(str(target))
        command = [blender, "-b", "--factory-startup", "--python", __file__, "--", "sample", str(work), *files]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0 or any(not (work / f"{t}.npz").exists() for t in takes):
            sys.exit(result.stdout + result.stderr)
        report = build(work, takes)
        (work / "report.json").write_text(json.dumps(report, indent=1))
    print(json.dumps(report, indent=1))


def plant(track, feet):
    """Per-frame ankle raises that put a planted shoe on the ground and keep every shoe out of it.

    The retargeted shoes are within a couple of centimetres of the ground while planted, and the toes of a
    deep landing go into it, as the subject's own toe markers do. A shoe that is planted is moved onto the
    ground and one below it onto it, by raising or lowering its ankle's goal; the raise is smoothed over five
    frames so the foot does not pop, then clamped again so no shoe ends below the ground."""
    np = np_()
    out = []
    for side, base in zip("lr", feet):
        planted, _, height = contacts(track, side)
        raw = np.where(planted | (height < 0.0), -height, 0.0)
        padded = np.pad(raw, 2, mode="edge")
        smooth = np.convolve(padded, np.array([1.0, 2.0, 3.0, 2.0, 1.0]) / 9.0, mode="valid")
        out.append(base + np.maximum(smooth, -height))
    return list(zip(out[0], out[1]))


class Prepared:
    """A take turned to face -Z, levelled, grounded and retargeted at every frame."""

    def __init__(self, retargeter, take: Take):
        np = np_()
        self.take = take
        travel = take.head("Hips", take.frames - 1) - take.head("Hips", 0)
        travel[1] = 0.0
        self.turn = turn_to_forward(travel if np.linalg.norm(travel) > 1.0 else take.facing(0))
        self.origin = take.head("Hips", 0) * np.array([1.0, 0.0, 1.0])
        frames = range(take.frames)
        self.lift, self.feet = ground(retargeter, take, frames, self.turn, self.origin)
        track = Track(retargeter, take, frames, self.turn, self.origin, self.lift, self.feet)
        self.track = Track(retargeter, take, frames, self.turn, self.origin, self.lift, plant(track, self.feet))
        self.planted, self.strikes, self.height = {}, {}, {}
        for side in "lr":
            self.planted[side], self.strikes[side], self.height[side] = contacts(self.track, side)

    def flight(self):
        """The longest stretch with both shoes off the ground: (first airborne frame, last one)."""
        airborne = [not self.planted["l"][f] and not self.planted["r"][f] and self.height["l"][f] > CONTACT_HEIGHT
                    and self.height["r"][f] > CONTACT_HEIGHT for f in range(self.take.frames)]
        best, f = (0, -1), 0
        while f < len(airborne):
            if airborne[f]:
                g = f
                while g + 1 < len(airborne) and airborne[g + 1]:
                    g += 1
                if g - f > best[1] - best[0]:
                    best = (f, g)
                f = g + 1
            else:
                f += 1
        return best

    def ballistic(self, first, last):
        """The pelvis's height over a flight, fitted by a parabola, as a function of the frame."""
        np = np_()
        frames = np.arange(first, last + 1)
        coefficients = np.polyfit(frames, self.track.joints["pelvis"][first:last + 1, 1], 2)
        return lambda f: float(np.polyval(coefficients, f))

    def travel(self, window=15):
        """The pelvis's horizontal path, low-passed over `window` frames: what a one-shot clip takes out."""
        np = np_()
        path = self.track.joints["pelvis"][:, [0, 2]]
        padded = np.pad(path, ((window // 2, window // 2), (0, 0)), mode="edge")
        kernel = np.ones(window) / window
        return np.column_stack([np.convolve(padded[:, k], kernel, mode="valid") for k in range(2)])


def pose_distance(track, a, b, joints):
    np = np_()
    rotation = sum(2 * math.acos(min(1.0, abs(float(np.dot(track.rotations[a][j], track.rotations[b][j])))))
                   for j in joints)
    return rotation + 10.0 * abs(float(track.joints["pelvis"][a][1] - track.joints["pelvis"][b][1]))


def largest_turn(track, a, b, joints):
    """The largest angle, in degrees, between a joint's rotations at two frames: what closing a loop spreads."""
    np = np_()
    return max(math.degrees(2 * math.acos(min(1.0, abs(float(np.dot(track.rotations[a][j], track.rotations[b][j]))))))
               for j in joints)


def close_loop(rotations, pelvis):
    """Spreads what the last key differs from the first over the keys, so the clip loops without a jump."""
    np = np_()
    count = len(rotations) - 1
    out_rotations, out_pelvis = [], pelvis.copy()
    first, last = rotations[0], rotations[-1]
    for k, keys in enumerate(rotations):
        t = k / count
        frame = {}
        for joint, q in keys.items():
            # correction = first * inverse(last), applied in part: slerp(identity, correction, t) * q
            x1, y1, z1, w1 = first[joint]
            x2, y2, z2, w2 = -last[joint][0], -last[joint][1], -last[joint][2], last[joint][3]
            correction = np.array([w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
                                   w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2])
            c = slerp(np.array([0.0, 0.0, 0.0, 1.0]), correction, t)
            frame[joint] = quat_multiply(c, q)
        out_rotations.append(frame)
        out_pelvis[k] = pelvis[k] + t * (pelvis[0] - pelvis[-1])
    return out_rotations, out_pelvis


def quat_multiply(a, b):
    np = np_()
    x1, y1, z1, w1 = a
    x2, y2, z2, w2 = b
    return np.array([w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
                     w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2])


def planted_slide(prepared, first, last):
    """The farthest a planted heel or toe moves while planted between two frames, in the take's own travel
    (before the clip is put in place): how much the retargeted feet slide."""
    np = np_()
    worst = 0.0
    for side in "lr":
        planted = prepared.planted[side]
        f = first
        while f <= last:
            if planted[f]:
                g = f
                while g + 1 <= last and planted[g + 1]:
                    g += 1
                for joint in (f"foot_{side}", f"ball_{side}"):
                    points = prepared.track.joints[joint][f:g + 1][:, [0, 2]]
                    heights = prepared.track.joints[joint][f:g + 1, 1]
                    low = heights <= heights.min() + 0.01  # the part of the shoe that is down
                    if low.sum() > 1:
                        span = points[low].max(axis=0) - points[low].min(axis=0)
                        worst = max(worst, float(np.linalg.norm(span)))
                f = g + 1
            else:
                f += 1
    return worst


def build_clip(name, spec, prepared: Prepared, rig: Rig):
    """Cuts a clip from a prepared take: key times, per-joint rotations, pelvis world positions, events."""
    np = np_()
    track, kind = prepared.track, spec["kind"]
    pelvis_world = track.joints["pelvis"].copy()
    mapped = [rig.index[n] for n in MAPPING]
    events, info = [], {"take": prepared.take.name}
    if kind == "cycle":
        lefts, rights = prepared.strikes["l"], prepared.strikes["r"]
        candidates = [(a, b) for a, b in zip(lefts, lefts[1:]) if any(a < r < b for r in rights)]
        a, b = min(candidates, key=lambda c: pose_distance(track, c[0], c[1], mapped))
        r = next(r for r in rights if a < r < b)
        frames = list(range(a, b + 1))
        duration = (b - a) / FPS
        stride = float(np.linalg.norm(pelvis_world[b, [0, 2]] - pelvis_world[a, [0, 2]]))
        phase = (r - a) / (b - a)
        # Warp the key times so the right foot comes down at half the cycle.
        times = [((f - a) / (r - a)) * duration / 2 if f <= r else duration / 2 + ((f - r) / (b - r)) * duration / 2
                 for f in frames]
        rotations = [track.rotations[f] for f in frames]
        pelvis = pelvis_world[a:b + 1].copy()
        line = np.linspace(0.0, 1.0, len(frames))[:, None]
        pelvis[:, [0, 2]] -= pelvis[0, [0, 2]] + line * (pelvis[-1, [0, 2]] - pelvis[0, [0, 2]])
        rotations, pelvis = close_loop(rotations, pelvis)
        pelvis[:, [0, 2]] -= pelvis[:-1, [0, 2]].mean(axis=0)
        events = [{"time": 0.0, "name": "footstep", "argument": "left"},
                  {"time": round(duration / 2, 4), "name": "footstep", "argument": "right"}]
        # The same stride from the feet: where each foot is planted a cycle apart, three frames after it comes
        # down (the right foot's cycle before this one where the take ends too soon after it).
        k, cycle, last = 3, b - a, prepared.take.frames - 1
        info["foot_stride"] = []
        for joint, f in (("foot_l", a), ("foot_r", r if r + cycle + 3 <= last else r - cycle)):
            here, there = track.joints[joint][f + k, [0, 2]], track.joints[joint][f + cycle + k, [0, 2]]
            info["foot_stride"].append(round(float(np.linalg.norm(there - here)), 3))
        info.update(frames=[a, b], right_frame=r, captured_right_phase=round(phase, 3), stride=round(stride, 3),
                    speed=round(stride / duration, 3), closure_degrees=round(largest_turn(track, a, b, mapped), 1),
                    slide=round(planted_slide(prepared, a, b), 3))
    elif kind == "idle":
        span = int(spec["min_length"] * FPS)
        best = None
        for a in range(0, prepared.take.frames - span - 1, 2):
            for b in range(a + span, prepared.take.frames - 1, 2):
                d = pose_distance(track, a, b, mapped) + pose_distance(track, a + 1, b + 1, mapped)
                if best is None or d < best[0]:
                    best = (d, a, b)
        _, a, b = best
        frames = list(range(a, b + 1))
        times = [(f - a) / FPS for f in frames]
        rotations, pelvis = close_loop([track.rotations[f] for f in frames], pelvis_world[a:b + 1].copy())
        pelvis[:, [0, 2]] -= pelvis[:-1, [0, 2]].mean(axis=0)
        info.update(frames=[a, b], closure_degrees=round(largest_turn(track, a, b, mapped), 1),
                    slide=round(planted_slide(prepared, a, b), 3))
    else:
        first_air, last_air = prepared.flight()
        height = prepared.ballistic(first_air, last_air)
        travel = prepared.travel()
        if kind == "takeoff":
            a, b = first_air - int(round(spec["lead"] * FPS)), first_air + spec["tail"]
            frames = list(range(a, b + 1))
            pelvis = pelvis_world[a:b + 1].copy()
            for k, f in enumerate(frames):
                if f > first_air:  # the rise is the capsule's
                    pelvis[k, 1] -= height(f) - height(first_air)
        elif kind == "fall":
            apex = int(round(max(range(first_air, last_air + 1), key=height)))
            down = list(range(apex, last_air))
            frames = down + down[-2::-1]  # the descent forwards, then back: a loop
            pelvis = pelvis_world[frames].copy()
            for k, f in enumerate(frames):
                pelvis[k, 1] -= height(f) - height(apex)
        else:  # land
            touch = last_air + 1
            stand = float(np.median(pelvis_world[:, 1]))
            lowest = touch + int(np.argmin(pelvis_world[touch:touch + int(FPS), 1]))
            goal = pelvis_world[lowest, 1] + RECOVERED * (stand - pelvis_world[lowest, 1])
            end = next(f for f in range(lowest, prepared.take.frames) if pelvis_world[f, 1] >= goal)
            a, b = touch - LAND_LEAD, end
            frames = list(range(a, b + 1))
            pelvis = pelvis_world[a:b + 1].copy()
            event = {"time": round((touch - a) / FPS, 4), "name": "landing"}
            if "argument" in spec:
                event["argument"] = spec["argument"]
            events.append(event)
            for side, word in (("l", "left"), ("r", "right")):
                for f in prepared.strikes[side]:
                    if touch + 0.15 * FPS < f <= b:
                        events.append({"time": round((f - a) / FPS, 4), "name": "footstep", "argument": word})
            events.sort(key=lambda e: e["time"])
            info.update(touchdown=touch, compression=round(stand - float(pelvis_world[lowest, 1]), 3))
        times = [k / FPS for k in range(len(frames))]
        rotations = [track.rotations[f] for f in frames]
        pelvis[:, [0, 2]] -= travel[frames]
        if kind == "fall":
            rotations, pelvis = close_loop(rotations, pelvis)
        info.update(frames=[frames[0], frames[-1]] if kind != "fall" else [frames[0], max(frames)],
                    flight=[first_air, last_air])
    # The shoes move up and down with the pelvis where its height was changed.
    soles = np.minimum(prepared.height["l"][frames], prepared.height["r"][frames]) \
        + pelvis[:, 1] - pelvis_world[frames, 1]
    if kind == "fall":  # the feet hang just above the capsule's bottom
        pelvis[:, 1] += 0.02 - float(soles.min())
        soles += 0.02 - float(soles.min())
    info.update(duration=round(times[-1], 4), keys=len(times), lowest_sole=round(float(soles.min()), 3))
    return {"times": times, "rotations": rotations, "pelvis": pelvis, "events": events, "info": info}


def write_clips(clips, rig: Rig, document, binary):
    np = np_()
    root_R, root_P = rig.world()
    pelvis = rig.index["pelvis"]
    parent = rig.parent[pelvis]
    for name, clip in clips.items():
        times = append_floats(document, binary, np.array(clip["times"]), "SCALAR", minmax=True)
        samplers, channels = [], []
        for joint in rig.joints:
            keys = np.array([frame[joint] for frame in clip["rotations"]])
            samplers.append({"input": times, "output": append_floats(document, binary, keys, "VEC4"),
                             "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": joint, "path": "rotation"}})
        local = (root_R[parent].T @ (clip["pelvis"] - root_P[parent]).T).T
        samplers.append({"input": times, "output": append_floats(document, binary, local, "VEC3"),
                         "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": pelvis, "path": "translation"}})
        document["animations"].append({"name": name, "channels": channels, "samplers": samplers})


def build(work: Path, takes):
    document, binary = read_glb(MODEL)
    binary = drop_animations(document, binary, {"Stand"})
    rig = Rig(document, binary)
    loaded = {t: Take(work / f"{t}.npz") for t in takes}
    retargeter = Retargeter(rig, loaded[REFERENCE[0]], REFERENCE[1])
    prepared = {}
    clips = {}
    for name, spec in CLIPS.items():
        if spec["take"] not in prepared:
            prepared[spec["take"]] = Prepared(retargeter, loaded[spec["take"]])
        clips[name] = build_clip(name, spec, prepared[spec["take"]], rig)
    write_clips(clips, rig, document, binary)
    write_glb(MODEL, document, binary)
    sidecar = MODEL.with_name(MODEL.name + ".meta")
    meta = json.loads(sidecar.read_text())
    settings = meta.setdefault("settings", {})
    settings["animationEvents"] = {name: clip["events"] for name, clip in clips.items() if clip["events"]}
    sidecar.write_text(json.dumps(meta, indent=2, sort_keys=True) + "\n")
    report = {"scale": round(retargeter.scale, 4),
              "takes": {t: {"slope": round(p.take.slope, 4), "lift": round(p.lift, 3),
                            "feet": [round(x, 3) for x in p.feet]} for t, p in prepared.items()},
              "clips": {name: {**clip["info"], "events": clip["events"]} for name, clip in clips.items()}}
    return report


if __name__ == "__main__":
    main()
