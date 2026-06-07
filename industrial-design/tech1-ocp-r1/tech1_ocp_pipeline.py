#!/usr/bin/env python3
"""Mac runnable Tech One A CAD/export/render/review pipeline.

This pipeline intentionally avoids FreeCAD and VTK. It uses OCP/OpenCascade for
true solids and STEP I/O, then tessellates those solids for STL/OBJ export and
simple software-rendered review images.
"""

from __future__ import annotations

import argparse
import json
import math
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import numpy as np
import trimesh
from PIL import Image, ImageDraw, ImageOps

from OCP.Bnd import Bnd_Box
from OCP.BRep import BRep_Builder, BRep_Tool
from OCP.BRepAlgoAPI import BRepAlgoAPI_Fuse
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakePolygon
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder, BRepPrimAPI_MakePrism
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt, gp_Vec
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPControl import STEPControl_AsIs, STEPControl_Reader, STEPControl_Writer
from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED
from OCP.TopExp import TopExp_Explorer
from OCP.TopLoc import TopLoc_Location
from OCP.TopoDS import TopoDS, TopoDS_Compound, TopoDS_Shell


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[1]
CONCEPT_DIR = PROJECT_ROOT / "industrial-design" / "concept-r3"
REFERENCE_IMAGE = CONCEPT_DIR / "01-tech1-angular-a.png"
CAD_OUT = PROJECT_ROOT / "_cad-output" / "tech1-ocp-r1"
RENDER_OUT = PROJECT_ROOT / "_render-raster" / "tech1-ocp-r1"

VIEW_SPECS = [
    ("front", (0.0, -1.0, 0.0), False),
    ("rear", (0.0, 1.0, 0.0), False),
    ("left", (-1.0, 0.0, 0.0), False),
    ("right", (1.0, 0.0, 0.0), False),
    ("top", (0.0, 0.0, 1.0), False),
    ("bottom", (0.0, 0.0, -1.0), False),
    ("axo_front_right", (1.0, -1.0, 0.78), True),
    ("axo_front_left", (-1.0, -1.0, 0.78), True),
    ("axo_rear_right", (1.0, 1.0, 0.72), True),
    ("axo_rear_left", (-1.0, 1.0, 0.72), True),
]


@dataclass(frozen=True)
class Part:
    name: str
    shape: object
    color: tuple[int, int, int]
    export: bool = True


@dataclass(frozen=True)
class ModelParams:
    name: str
    body_length: float = 130.0
    body_height: float = 34.0
    body_depth: float = 18.0
    end_chamfer: float = 5.0
    front_side_bottom_inset: float = 1.8
    panel_thickness: float = 0.46
    oled_x: float = 10.8
    oled_w: float = 28.8
    oled_h: float = 15.0
    camera_x: float = 46.0
    camera_w: float = 38.0
    camera_h: float = 16.4
    service_x: float = 104.0
    service_w: float = 22.8
    service_h: float = 14.8
    support_width: float = 32.0
    support_x: float = 49.0
    support_rear_y: float = 54.0
    support_top_z: float = 28.6
    lower_pad_w: float = 33.0
    lower_pad_x: float = 48.5
    front_status_dot_x: float = 77.1
    notes: str = ""


INITIAL = ModelParams(
    name="initial",
    end_chamfer=4.0,
    front_side_bottom_inset=1.1,
    oled_x=12.2,
    oled_w=26.5,
    camera_x=47.8,
    camera_w=34.2,
    service_x=106.3,
    service_w=19.6,
    support_width=27.0,
    support_x=51.5,
    support_rear_y=51.0,
    lower_pad_w=24.0,
    lower_pad_x=53.0,
    notes="Initial pass kept the structure close to the old FreeCAD draft; panels and rear support read too small against the A render.",
)

FINAL = ModelParams(
    name="final",
    end_chamfer=5.4,
    front_side_bottom_inset=2.2,
    oled_x=9.4,
    oled_w=29.2,
    camera_x=45.2,
    camera_w=39.6,
    service_x=102.8,
    service_w=24.4,
    support_width=36.0,
    support_x=47.0,
    support_rear_y=54.3,
    lower_pad_w=34.0,
    lower_pad_x=48.0,
    notes="Refined pass widens the separated black functional panels, increases the symmetric end bevel, and makes the monitor-lamp rear support more visible.",
)

COLORS = {
    "aluminum": (188, 188, 184),
    "black": (9, 9, 10),
    "dark": (33, 33, 35),
    "rubber": (5, 5, 6),
    "glass": (7, 9, 10),
    "pcb": (8, 46, 34),
    "copper": (150, 83, 35),
}


def make_polygon_face(points: Iterable[tuple[float, float, float]]):
    poly = BRepBuilderAPI_MakePolygon()
    pts = list(points)
    for x, y, z in pts:
        poly.Add(gp_Pnt(x, y, z))
    poly.Close()
    return BRepBuilderAPI_MakeFace(poly.Wire()).Face()


def make_shell_solid(faces):
    shell = TopoDS_Shell()
    builder = BRep_Builder()
    builder.MakeShell(shell)
    for face in faces:
        builder.Add(shell, face)
    # A closed shell exported as a solid is easier for STEP consumers than a
    # loose face set. OCP accepts this because all faces share matching edges.
    from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeSolid

    solid_maker = BRepBuilderAPI_MakeSolid()
    solid_maker.Add(shell)
    solid_maker.Build()
    return solid_maker.Solid()


def make_faceted_body(p: ModelParams):
    c = p.end_chamfer
    side_inset = p.front_side_bottom_inset
    length = p.body_length
    height = p.body_height
    depth = p.body_depth

    # The A render reads as a constant-section bar: the camera/front side and
    # the rear side are the same size. Earlier drafts tapered from a smaller
    # front outline to a larger rear outline, which made the top view look like
    # a trapezoid. Keep one mirrored octagonal outline and extrude it in depth.
    outline = [
        (side_inset, 0, c),
        (c, 0, 0),
        (length - c, 0, 0),
        (length - side_inset, 0, c),
        (length, 0, height - c),
        (length - c, 0, height),
        (c, 0, height),
        (0, 0, height - c),
    ]
    front = outline
    rear = [(x, depth, z) for x, _, z in outline]
    faces = [make_polygon_face(front), make_polygon_face(reversed(rear))]
    for idx in range(len(front)):
        nxt = (idx + 1) % len(front)
        faces.append(make_polygon_face([front[idx], front[nxt], rear[nxt], rear[idx]]))
    return make_shell_solid(faces)


def make_box(x: float, y: float, z: float, dx: float, dy: float, dz: float):
    return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), dx, dy, dz).Shape()


def make_cylinder(axis: str, x: float, y: float, z: float, radius: float, length: float):
    directions = {
        "x": gp_Dir(1, 0, 0),
        "y": gp_Dir(0, 1, 0),
        "z": gp_Dir(0, 0, 1),
    }
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, z), directions[axis]), radius, length).Shape()


def prism_from_polygon(points, vector):
    face = make_polygon_face(points)
    return BRepPrimAPI_MakePrism(face, gp_Vec(*vector)).Shape()


def oct_panel(x: float, z: float, width: float, height: float, thickness: float, bevel: float):
    c = min(bevel, width / 4.0, height / 4.0)
    pts = [
        (x + c, 0, z),
        (x + width - c, 0, z),
        (x + width, 0, z + c),
        (x + width, 0, z + height - c),
        (x + width - c, 0, z + height),
        (x + c, 0, z + height),
        (x, 0, z + height - c),
        (x, 0, z + c),
    ]
    return prism_from_polygon(pts, (0, -thickness, 0))


def yz_prism(x: float, width: float, yz_points):
    points = [(x, y, z) for y, z in yz_points]
    return prism_from_polygon(points, (width, 0, 0))


def fuse_all(shapes):
    iterator = iter(shapes)
    fused = next(iterator)
    for shape in iterator:
        op = BRepAlgoAPI_Fuse(fused, shape)
        op.Build()
        fused = op.Shape()
    return fused


def z_from_top(p: ModelParams, top: float, height: float) -> float:
    return p.body_height - top - height


def rear_support_shape(p: ModelParams):
    x = p.support_x
    w = p.support_width
    rear_y = p.support_rear_y
    top_z = p.support_top_z
    shapes = [
        yz_prism(
            x,
            w,
            [
                (26.8, top_z - 5.8),
                (rear_y - 7.0, top_z - 5.8),
                (rear_y - 4.5, top_z - 3.1),
                (rear_y - 4.5, top_z),
                (28.6, top_z),
                (26.8, top_z - 2.4),
            ],
        ),
        yz_prism(
            x + 1.0,
            w - 2.0,
            [
                (18.0, 19.1),
                (rear_y - 4.0, 19.1),
                (rear_y - 1.1, 20.0),
                (rear_y - 1.1, 21.8),
                (18.0, 22.4),
            ],
        ),
        yz_prism(
            x + w * 0.36,
            w * 0.28,
            [
                (34.2, 19.0),
                (37.2, 19.0),
                (37.8, 8.8),
                (34.6, 8.8),
            ],
        ),
        yz_prism(
            x + w * 0.34,
            w * 0.32,
            [
                (rear_y - 6.6, 18.2),
                (rear_y - 3.2, 18.2),
                (rear_y, 16.9),
                (rear_y, 9.2),
                (rear_y - 2.4, 9.2),
                (rear_y - 2.4, 13.0),
                (rear_y - 3.7, 13.0),
                (rear_y - 3.7, 16.3),
                (rear_y - 6.6, 16.3),
            ],
        ),
    ]
    return fuse_all(shapes)


def build_model(p: ModelParams) -> list[Part]:
    parts: list[Part] = []
    parts.append(Part("front_body_shell_faceted_130x34x18", make_faceted_body(p), COLORS["aluminum"]))

    panel_z = z_from_top(p, 9.2, p.oled_h)
    parts.append(Part("left_oled_black_glass_window_29x16", oct_panel(p.oled_x, panel_z, p.oled_w, p.oled_h, p.panel_thickness, 1.25), COLORS["black"]))
    parts.append(Part("left_oled_active_display_recess", make_box(p.oled_x + 5.6, -0.62, panel_z + 2.7, 16.8, 0.22, 9.4), COLORS["glass"]))
    for idx, xoff in enumerate([10.6, 13.4, 16.2], 1):
        parts.append(Part(f"left_oled_small_status_tick_{idx}", make_cylinder("y", p.oled_x + xoff, -0.96, panel_z + 2.2, 0.38, 0.22), COLORS["glass"]))

    camera_z = z_from_top(p, 8.6, p.camera_h)
    parts.append(Part("center_camera_black_island_flush_40x16", oct_panel(p.camera_x, camera_z, p.camera_w, p.camera_h, p.panel_thickness, 1.35), COLORS["black"]))
    cx = p.camera_x + p.camera_w / 2.0 - 0.6
    cz = camera_z + p.camera_h / 2.0
    parts.append(Part("camera_lens_outer_bezel", make_cylinder("y", cx, -1.10, cz, 5.5, 0.86), COLORS["dark"]))
    parts.append(Part("camera_lens_inner_ring", make_cylinder("y", cx, -1.54, cz, 4.05, 0.68), COLORS["black"]))
    parts.append(Part("camera_lens_glass", make_cylinder("y", cx, -1.92, cz, 2.55, 0.44), COLORS["glass"]))
    parts.append(Part("camera_status_dot", make_cylinder("y", p.front_status_dot_x, -0.92, cz, 0.76, 0.26), COLORS["dark"]))

    service_z = z_from_top(p, 9.3, p.service_h)
    parts.append(Part("right_service_black_panel_balancer", oct_panel(p.service_x, service_z, p.service_w, p.service_h, p.panel_thickness, 1.12), COLORS["black"]))
    parts.append(Part("right_tact_button_cap", oct_panel(p.service_x + 3.0, service_z + 4.8, 7.4, 5.2, 0.30, 0.95), COLORS["dark"]))
    parts.append(Part("right_service_status_dot", make_cylinder("y", p.service_x + 12.2, -0.94, service_z + 7.5, 0.45, 0.24), COLORS["dark"]))
    for col in range(4):
        for row in range(5):
            parts.append(
                Part(
                    f"right_front_micro_sound_hole_{col + 1}_{row + 1}",
                    make_cylinder("y", p.service_x + 15.8 + col * 1.55, -0.92, service_z + 4.0 + row * 1.52, 0.38, 0.24),
                    COLORS["dark"],
                )
            )

    # Right end face service details sit on the symmetric chamfered cap.
    parts.append(Part("right_end_usb_upper_dark_insert", make_box(129.15, 7.0, 20.2, 0.8, 6.3, 3.6), COLORS["dark"]))
    parts.append(Part("right_end_usb_lower_dark_insert", make_box(129.15, 7.0, 10.7, 0.8, 6.3, 3.6), COLORS["dark"]))
    for col in range(3):
        for row in range(5):
            parts.append(Part(f"right_end_micro_hole_{col + 1}_{row + 1}", make_cylinder("x", 130.05, 7.4 + col * 2.05, 8.8 + row * 2.0, 0.42, 0.46), COLORS["dark"]))

    # Transparent-ish internal references are exported to OBJ/render but left out
    # of STEP/STL because this is an exterior CAD draft.
    parts.append(Part("dev_board_keepout_reference_57x28", make_box(60.0, 5.2, z_from_top(p, 1.5, 28.0), 57.0, 1.1, 28.0), COLORS["pcb"], False))
    parts.append(Part("speaker_buzzer_keepout_reference_dia12", make_cylinder("y", 108.0, 10.4, 8.4, 6.0, 3.2), COLORS["dark"], False))

    parts.append(Part("rear_support_arm_monitor_lamp_style", rear_support_shape(p), COLORS["dark"]))
    parts.append(Part("front_rubber_pad_centered_soft_contact", make_box(p.lower_pad_x, -0.9, 0.2, p.lower_pad_w, 2.2, 3.0), COLORS["rubber"]))
    parts.append(Part("rear_rubber_pad_soft_contact", make_box(p.support_x + p.support_width / 2.0 - 4.0, p.support_rear_y - 0.3, 9.2, 8.0, 0.9, 4.8), COLORS["rubber"]))
    return parts


def make_compound(parts: Iterable[Part], include_reference: bool = False):
    compound = TopoDS_Compound()
    builder = BRep_Builder()
    builder.MakeCompound(compound)
    for part in parts:
        if part.export or include_reference:
            builder.Add(compound, part.shape)
    return compound


def bbox(shape) -> tuple[float, float, float, float, float, float]:
    box = Bnd_Box()
    BRepBndLib.Add_s(shape, box)
    return box.Get()


def bbox_dims(bounds) -> tuple[float, float, float]:
    xmin, ymin, zmin, xmax, ymax, zmax = bounds
    return (xmax - xmin, ymax - ymin, zmax - zmin)


def write_step(parts: list[Part], path: Path):
    writer = STEPControl_Writer()
    writer.Transfer(make_compound(parts), STEPControl_AsIs)
    status = writer.Write(str(path))
    if status != IFSelect_RetDone:
        raise RuntimeError(f"STEP export failed for {path}: status={status}")


def read_step_bbox(path: Path):
    reader = STEPControl_Reader()
    status = reader.ReadFile(str(path))
    if status != IFSelect_RetDone:
        raise RuntimeError(f"Could not read exported STEP: {path}")
    reader.TransferRoots()
    return bbox(reader.OneShape())


def triangulate_shape(shape, deflection: float = 0.45):
    mesher = BRepMesh_IncrementalMesh(shape, deflection)
    mesher.Perform()

    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, int, int]] = []
    face_exp = TopExp_Explorer(shape, TopAbs_FACE)
    offset = 0
    while face_exp.More():
        face = TopoDS.Face_s(face_exp.Current())
        loc = TopLoc_Location()
        tri = BRep_Tool.Triangulation_s(face, loc)
        if tri is None or tri.NbNodes() == 0:
            face_exp.Next()
            continue
        transform = loc.Transformation()
        for idx in range(1, tri.NbNodes() + 1):
            pnt = tri.Node(idx).Transformed(transform)
            vertices.append((pnt.X(), pnt.Y(), pnt.Z()))
        reversed_face = face.Orientation() == TopAbs_REVERSED
        for idx in range(1, tri.NbTriangles() + 1):
            a, b, c = tri.Triangle(idx).Get()
            face_indices = (offset + a - 1, offset + b - 1, offset + c - 1)
            if reversed_face:
                face_indices = (face_indices[0], face_indices[2], face_indices[1])
            faces.append(face_indices)
        offset += tri.NbNodes()
        face_exp.Next()
    if not vertices or not faces:
        raise RuntimeError("OCP tessellation returned an empty mesh")
    return np.array(vertices, dtype=np.float64), np.array(faces, dtype=np.int64)


def part_meshes(parts: list[Part], include_reference: bool = False):
    meshes = []
    for part in parts:
        if not part.export and not include_reference:
            continue
        vertices, faces = triangulate_shape(part.shape)
        mesh = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
        mesh.visual.face_colors = np.tile(np.array([*part.color, 255], dtype=np.uint8), (len(faces), 1))
        meshes.append((part, mesh))
    return meshes


def export_meshes(parts: list[Part], stl_path: Path, obj_path: Path):
    meshes = [mesh for _, mesh in part_meshes(parts)]
    combined = trimesh.util.concatenate(meshes)
    combined.export(stl_path)

    write_obj(parts, obj_path)


def write_obj(parts: list[Part], obj_path: Path):
    mtl_path = obj_path.with_suffix(".mtl")
    obj_lines = [f"mtllib {mtl_path.name}\n"]
    mtl_lines = []
    vertex_offset = 1
    seen_materials: set[str] = set()
    for part, mesh in part_meshes(parts, include_reference=True):
        mat_name = part.name.replace("-", "_")
        if mat_name not in seen_materials:
            r, g, b = [value / 255.0 for value in part.color]
            mtl_lines.extend(
                [
                    f"newmtl {mat_name}\n",
                    f"Kd {r:.4f} {g:.4f} {b:.4f}\n",
                    "Ka 0.1200 0.1200 0.1200\n",
                    "Ks 0.2500 0.2500 0.2500\n",
                    "Ns 28.0000\n\n",
                ]
            )
            seen_materials.add(mat_name)
        obj_lines.append(f"o {part.name}\n")
        obj_lines.append(f"usemtl {mat_name}\n")
        for vertex in mesh.vertices:
            obj_lines.append(f"v {vertex[0]:.6f} {vertex[1]:.6f} {vertex[2]:.6f}\n")
        for face in mesh.faces:
            a, b, c = face + vertex_offset
            obj_lines.append(f"f {a} {b} {c}\n")
        vertex_offset += len(mesh.vertices)
    obj_path.write_text("".join(obj_lines), encoding="utf-8")
    mtl_path.write_text("".join(mtl_lines), encoding="utf-8")


def normalize(v):
    arr = np.array(v, dtype=np.float64)
    length = np.linalg.norm(arr)
    if length < 1e-9:
        raise ValueError(f"Cannot normalize zero vector: {v}")
    return arr / length


def view_basis(direction):
    forward = normalize(direction)
    up = np.array([0.0, 0.0, 1.0])
    if abs(np.dot(forward, up)) > 0.92:
        up = np.array([0.0, -1.0 if forward[2] > 0 else 1.0, 0.0])
    right = normalize(np.cross(up, forward))
    up = normalize(np.cross(forward, right))
    return right, up, forward


def render_software(meshes, direction, perspective=False, size=(1600, 1050), pad=0.86):
    width, height = size
    all_vertices = np.vstack([mesh.vertices for _, mesh in meshes])
    center = all_vertices.mean(axis=0)
    right, up, forward = view_basis(direction)

    rel = all_vertices - center
    projected = np.column_stack((rel @ right, rel @ up))
    span = max(float(np.ptp(projected[:, 0])), float(np.ptp(projected[:, 1])), 1.0)
    scale = min(width, height) * pad / span

    yy = np.linspace(0.0, 1.0, height)[:, None]
    bg = np.zeros((height, width, 3), dtype=np.uint8)
    top = np.array([248, 248, 248], dtype=np.float64)
    bottom = np.array([228, 230, 234], dtype=np.float64)
    bg[:] = ((1.0 - yy) * top + yy * bottom).astype(np.uint8)[:, None, :]
    image = bg
    zbuf = np.full((height, width), -np.inf, dtype=np.float64)

    light_dirs = [normalize((-0.4, -0.8, 1.0)), normalize((0.8, -0.2, 0.8)), normalize((0.0, 1.0, 0.5))]
    light_weights = [0.70, 0.34, 0.18]

    for part, mesh in meshes:
        verts = np.asarray(mesh.vertices)
        rel = verts - center
        view_x = rel @ right
        view_y = rel @ up
        depth = rel @ forward
        sx = width / 2.0 + view_x * scale
        sy = height / 2.0 - view_y * scale
        screen = np.column_stack((sx, sy, depth))
        base = np.array(part.color, dtype=np.float64)
        face_normals = mesh.face_normals
        order = np.argsort(screen[mesh.faces][:, :, 2].mean(axis=1))
        for fidx in order:
            tri_idx = mesh.faces[fidx]
            pts = screen[tri_idx]
            minx = max(int(math.floor(pts[:, 0].min())), 0)
            maxx = min(int(math.ceil(pts[:, 0].max())), width - 1)
            miny = max(int(math.floor(pts[:, 1].min())), 0)
            maxy = min(int(math.ceil(pts[:, 1].max())), height - 1)
            if minx >= maxx or miny >= maxy:
                continue
            x0, y0, z0 = pts[0]
            x1, y1, z1 = pts[1]
            x2, y2, z2 = pts[2]
            denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if abs(denom) < 1e-9:
                continue
            px, py = np.meshgrid(np.arange(minx, maxx + 1), np.arange(miny, maxy + 1))
            w0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / denom
            w1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / denom
            w2 = 1.0 - w0 - w1
            mask = (w0 >= -0.002) & (w1 >= -0.002) & (w2 >= -0.002)
            if not np.any(mask):
                continue
            z = w0 * z0 + w1 * z1 + w2 * z2
            region = zbuf[miny : maxy + 1, minx : maxx + 1]
            visible = mask & (z > region)
            if not np.any(visible):
                continue
            normal = normalize(face_normals[fidx])
            shade = 0.28
            for ldir, weight in zip(light_dirs, light_weights):
                shade += weight * max(0.0, float(np.dot(normal, ldir)))
            shade += 0.20 * max(0.0, float(abs(np.dot(normal, forward))))
            if part.color == COLORS["black"] or part.color == COLORS["glass"]:
                shade = max(shade, 0.46)
            color = np.clip(base * min(shade, 1.30), 0, 255).astype(np.uint8)
            target = image[miny : maxy + 1, minx : maxx + 1]
            target[visible] = color
            region[visible] = z[visible]

    return Image.fromarray(image)


def add_label(image: Image.Image, text: str):
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((18, 18, 235, 62), radius=10, fill=(255, 255, 255), outline=(210, 210, 210))
    draw.text((32, 31), text, fill=(25, 25, 25))


def trim_background(image: Image.Image, threshold=244):
    rgb = image.convert("RGB")
    arr = np.array(rgb)
    mask = np.min(arr, axis=2) < threshold
    if not np.any(mask):
        return rgb
    ys, xs = np.where(mask)
    pad = 30
    left = max(int(xs.min()) - pad, 0)
    right = min(int(xs.max()) + pad, rgb.width - 1)
    top = max(int(ys.min()) - pad, 0)
    bottom = min(int(ys.max()) + pad, rgb.height - 1)
    return rgb.crop((left, top, right + 1, bottom + 1))


def fit_contain(image: Image.Image, size):
    canvas = Image.new("RGB", size, (242, 242, 242))
    thumb = ImageOps.contain(image.convert("RGB"), size, method=Image.Resampling.LANCZOS)
    canvas.paste(thumb, ((size[0] - thumb.width) // 2, (size[1] - thumb.height) // 2))
    return canvas


def fit_cover(image: Image.Image, size):
    return ImageOps.fit(image.convert("RGB"), size, method=Image.Resampling.LANCZOS)


def render_views(parts: list[Part], p: ModelParams) -> list[Path]:
    meshes = part_meshes(parts)
    out_dir = RENDER_OUT / p.name
    out_dir.mkdir(parents=True, exist_ok=True)
    outputs = []
    for view_name, direction, perspective in VIEW_SPECS:
        image = render_software(meshes, direction, perspective=perspective)
        path = out_dir / f"tech1-a-ocp-{p.name}-{view_name}.png"
        image.save(path, quality=95)
        outputs.append(path)
    return outputs


def compose_review_sheets(p: ModelParams):
    out_dir = RENDER_OUT / p.name
    axo = out_dir / f"tech1-a-ocp-{p.name}-axo_front_right.png"
    reference = Image.open(REFERENCE_IMAGE).convert("RGB")
    model = trim_background(Image.open(axo).convert("RGB"))
    left = fit_cover(reference, (1500, 980))
    right = fit_contain(model, (1500, 980))
    add_label(left, "Reference")
    add_label(right, f"Model {p.name}")
    sheet = Image.new("RGB", (3060, 1040), (235, 235, 235))
    sheet.paste(left, (20, 30))
    sheet.paste(right, (1540, 30))
    compare_path = out_dir / f"tech1-a-ocp-{p.name}-reference-vs-model.png"
    sheet.save(compare_path, quality=95)

    card = (900, 500)
    views = Image.new("RGB", (1860, 2710), (235, 235, 235))
    for idx, (view_name, _, _) in enumerate(VIEW_SPECS):
        path = out_dir / f"tech1-a-ocp-{p.name}-{view_name}.png"
        img = fit_contain(trim_background(Image.open(path)), card)
        add_label(img, view_name)
        x = 30 + (idx % 2) * 930
        y = 30 + (idx // 2) * 530
        views.paste(img, (x, y))
    overview_path = out_dir / f"tech1-a-ocp-{p.name}-multi-view-overview.png"
    views.save(overview_path, quality=95)
    return compare_path, overview_path


def validate_outputs(parts: list[Part], p: ModelParams, step_path: Path) -> dict:
    body = next(part for part in parts if part.name == "front_body_shell_faceted_130x34x18")
    support = next(part for part in parts if part.name == "rear_support_arm_monitor_lamp_style")
    body_bounds = bbox(body.shape)
    support_bounds = bbox(support.shape)
    step_bounds = read_step_bbox(step_path)
    body_dims = bbox_dims(body_bounds)
    support_extension = support_bounds[4] - body_bounds[4]
    body_ok = (
        abs(body_dims[0] - p.body_length) <= 0.15
        and abs(body_dims[1] - p.body_depth) <= 0.15
        and abs(body_dims[2] - p.body_height) <= 0.15
    )
    support_ok = 32.0 <= support_extension <= 38.0
    if not body_ok:
        raise RuntimeError(f"Body bbox failed validation: {body_dims}")
    if not support_ok:
        raise RuntimeError(f"Rear support extension failed validation: {support_extension:.2f}")
    return {
        "variant": p.name,
        "body_bbox_mm": {"x": round(body_dims[0], 2), "y": round(body_dims[1], 2), "z": round(body_dims[2], 2)},
        "rear_support_extension_mm": round(support_extension, 2),
        "step_bbox_mm": {
            "x": round(bbox_dims(step_bounds)[0], 2),
            "y": round(bbox_dims(step_bounds)[1], 2),
            "z": round(bbox_dims(step_bounds)[2], 2),
        },
        "validation": {
            "body_envelope_130x18x34": body_ok,
            "rear_support_extension_32_to_38": support_ok,
            "step_reimport": True,
        },
    }


def write_iteration_notes(initial_summary: dict, final_summary: dict, files: dict):
    md = f"""# Tech One A OCP CAD Iteration Notes

## Reference inputs

- `industrial-design/concept-r3/01-tech1-angular-a.png`
- `industrial-design/concept-r3/tech1-a-render-style-brief.md`
- `industrial-design/tech1-structure-r1/`
- `industrial-design/tech1-step-r1/` used only as legacy reference

## Closed-loop iteration

Initial render comparison showed the first OCP pass was still too close to the older structure draft:

- The left OLED, camera island, and right service panel read smaller than the Tech One A reference.
- The end chamfer did not show enough of the symmetric angular cap, and the side edges still read too vertical.
- The rear monitor-lamp support was present, but not visually strong enough from axonometric/rear views.
- The lower/front rubber pad was too short compared with the wide black contact seen under the center.
- The older body logic tapered from a smaller front outline to a larger rear outline, making the top view read as a trapezoid instead of a constant-section bar.

Refined parameters applied in the final pass:

- End chamfer increased from `4.0mm` to `5.4mm`.
- Front and rear outlines now use the same octagonal section, so the camera side and rear side stay equal size.
- Front-view side edges now have a mirrored inward slant using a `2.2mm` lower inset.
- OLED panel widened from `26.5mm` to `29.2mm`.
- Camera island widened from `34.2mm` to `39.6mm`.
- Right service panel widened from `19.6mm` to `24.4mm`.
- Rear support width increased from `27.0mm` to `36.0mm`, with rear reach moved to `{FINAL.support_rear_y:.1f}mm`.
- Center lower rubber pad widened from `24.0mm` to `34.0mm`.

## Validation summary

Initial body envelope: `{initial_summary["body_bbox_mm"]}`, rear support extension `{initial_summary["rear_support_extension_mm"]}mm`.

Final body envelope: `{final_summary["body_bbox_mm"]}`, rear support extension `{final_summary["rear_support_extension_mm"]}mm`.

Final STEP reimport bbox: `{final_summary["step_bbox_mm"]}`.

## Generated files

- Final STEP: `{files["final_step"]}`
- Final STL: `{files["final_stl"]}`
- Final OBJ: `{files["final_obj"]}`
- Final comparison sheet: `{files["final_compare"]}`
- Final multi-view overview: `{files["final_overview"]}`
- Initial comparison sheet: `{files["initial_compare"]}`

## Not production-ready yet

This is still an exterior/structure draft. It does not yet define production wall thickness, draft angles, tolerance stack-up, screw bosses, snap fits, cable routing, real PCB fastening, gasket strategy, adhesive details, injection or CNC process splits, assembly order, DFM checks, or measured dimensions for the actual OLED/camera/USB/speaker modules.
"""
    notes_path = RENDER_OUT / "tech1-a-ocp-iteration-notes.md"
    notes_path.write_text(md, encoding="utf-8")
    return notes_path


def run_variant(p: ModelParams) -> dict:
    CAD_OUT.mkdir(parents=True, exist_ok=True)
    RENDER_OUT.mkdir(parents=True, exist_ok=True)
    parts = build_model(p)
    step_path = CAD_OUT / f"tech1-a-ocp-{p.name}.step"
    stl_path = CAD_OUT / f"tech1-a-ocp-{p.name}.stl"
    obj_path = CAD_OUT / f"tech1-a-ocp-{p.name}.obj"
    write_step(parts, step_path)
    export_meshes(parts, stl_path, obj_path)
    render_views(parts, p)
    compare_path, overview_path = compose_review_sheets(p)
    summary = validate_outputs(parts, p, step_path)
    summary["files"] = {
        "step": str(step_path),
        "stl": str(stl_path),
        "obj": str(obj_path),
        "compare": str(compare_path),
        "overview": str(overview_path),
    }
    summary_path = CAD_OUT / f"tech1-a-ocp-{p.name}-validation.json"
    summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    return summary


def main():
    parser = argparse.ArgumentParser(description="Generate, export, render, compare, and validate Tech One A OCP CAD draft.")
    parser.add_argument("--variant", choices=["all", "initial", "final"], default="all")
    args = parser.parse_args()

    selected = {"initial": [INITIAL], "final": [FINAL], "all": [INITIAL, FINAL]}[args.variant]
    summaries = {}
    for params in selected:
        print(f"Generating {params.name} model")
        summaries[params.name] = run_variant(params)
        print(json.dumps(summaries[params.name], indent=2, ensure_ascii=False))

    if args.variant == "all":
        files = {
            "final_step": summaries["final"]["files"]["step"],
            "final_stl": summaries["final"]["files"]["stl"],
            "final_obj": summaries["final"]["files"]["obj"],
            "final_compare": summaries["final"]["files"]["compare"],
            "final_overview": summaries["final"]["files"]["overview"],
            "initial_compare": summaries["initial"]["files"]["compare"],
        }
        notes_path = write_iteration_notes(summaries["initial"], summaries["final"], files)
        print(f"Wrote {notes_path}")


if __name__ == "__main__":
    main()
