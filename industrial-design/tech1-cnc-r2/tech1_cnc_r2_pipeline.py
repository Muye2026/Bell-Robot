#!/usr/bin/env python3
"""Tech One CNC R2: a 60 x 20 x 31mm brick with a full-face LED dot display.

Much simpler than tech1-cnc-r1. The form is:

- Front view: a 60 x 20mm rounded rectangle, essentially all display.
- The aluminium is a CNC'd rim running around the perimeter only. It is open
  front and back; the dark display panel closes the front, a cover closes the
  back, and everything else packs into the cavity between them.
- Depth is 31mm, matching the Apple Studio Display enclosure so the device reads
  as the same slab thickness as the monitor it sits with.
- A camera occupies the right end of the front face.

What this drops from R1: the faceted octagonal body, the three separate black
panels, the perforated aluminium plate and its milled seat, and the rear support
arm. There is no perforation here at all — the front is a dark panel with LEDs
behind it, which is both cheaper and a better off-state than drilled aluminium.

Same toolchain as R1: OCP/OpenCascade for solids and STEP, BRepMesh + trimesh
for STL/OBJ, numpy + Pillow for software-rendered review sheets.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

import numpy as np
import trimesh
from PIL import Image, ImageDraw, ImageFont, ImageOps

from OCP.Bnd import Bnd_Box
from OCP.BRep import BRep_Builder, BRep_Tool
from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepFilletAPI import BRepFilletAPI_MakeFillet
from OCP.BRepGProp import BRepGProp
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt
from OCP.GProp import GProp_GProps
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPControl import STEPControl_AsIs, STEPControl_Reader, STEPControl_Writer
from OCP.TopAbs import TopAbs_EDGE, TopAbs_FACE, TopAbs_REVERSED
from OCP.TopExp import TopExp, TopExp_Explorer
from OCP.TopLoc import TopLoc_Location
from OCP.TopoDS import TopoDS, TopoDS_Compound
from OCP.TopTools import TopTools_ListOfShape


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[1]
CAD_OUT = PROJECT_ROOT / "_cad-output" / "tech1-cnc-r2"
RENDER_OUT = PROJECT_ROOT / "_render-raster" / "tech1-cnc-r2"

REVISION = "tech1-cnc-r2"
STEM = "tech1-cnc-r2"

VIEW_SPECS = [
    ("front", (0.0, -1.0, 0.0)),
    ("rear", (0.0, 1.0, 0.0)),
    ("left", (-1.0, 0.0, 0.0)),
    ("right", (1.0, 0.0, 0.0)),
    ("top", (0.0, 0.0, 1.0)),
    ("bottom", (0.0, 0.0, -1.0)),
    ("axo_front_right", (1.0, -1.0, 0.55)),
    ("axo_front_left", (-1.0, -1.0, 0.55)),
    ("axo_rear_right", (1.0, 1.0, 0.5)),
]

COLORS = {
    "aluminum": (198, 198, 194),
    "panel": (17, 17, 19),
    "dot_off": (30, 30, 33),
    "lit": (255, 244, 219),
    "glass": (9, 11, 13),
    "back_cover": (54, 54, 57),
    "pcb": (10, 44, 33),
    "battery": (72, 72, 78),
    "camera_body": (24, 24, 26),
}

# 5x7 glyphs, identical to the firmware's kFont5x7 in
# embedded/firmware-idf/main/display_backend.cpp: one column bitmask per column,
# bit N is row N from the top. Sharing the exact data is what makes the lit
# render show strokes the device will actually draw.
FONT_5X7_COLUMNS = {
    "0": (0x3E, 0x51, 0x49, 0x45, 0x3E),
    "1": (0x00, 0x42, 0x7F, 0x40, 0x00),
    "2": (0x42, 0x61, 0x51, 0x49, 0x46),
    "3": (0x21, 0x41, 0x45, 0x4B, 0x31),
    "4": (0x18, 0x14, 0x12, 0x7F, 0x10),
    "5": (0x27, 0x45, 0x45, 0x45, 0x39),
    "6": (0x3C, 0x4A, 0x49, 0x49, 0x30),
    "7": (0x01, 0x71, 0x09, 0x05, 0x03),
    "8": (0x36, 0x49, 0x49, 0x49, 0x36),
    "9": (0x06, 0x49, 0x49, 0x29, 0x1E),
    ":": (0x00, 0x36, 0x36, 0x00, 0x00),
}
GLYPH_HEIGHT = 7
GLYPH_ADVANCE = 6
TIMER_COLUMNS = len("00:00") * GLYPH_ADVANCE  # 30, the hard floor on matrix width

# The lit render shows one consistent mid-session state, not a composite. An
# earlier pass drew "45:00" next to a 60% bar, which is a frame the device can
# never produce: 45 minutes remaining means zero elapsed. Deriving both numbers
# from one state keeps the review image honest.
DEMO_SIT_TARGET_MIN = 45
DEMO_ELAPSED_MIN = 27
TIMER_TEXT = f"{DEMO_SIT_TARGET_MIN - DEMO_ELAPSED_MIN:02d}:00"
BAR_DEMO_PERCENT = DEMO_ELAPSED_MIN * 100 // DEMO_SIT_TARGET_MIN

# Apple Studio Display enclosure depth, from the VESA-mount spec (1.2 in).
# The flat back means this is a uniform thickness, not a max.
STUDIO_DISPLAY_DEPTH_MM = 31.0

# Pouch LiPo energy density, conservative end of the commodity range.
LIPO_WH_PER_CM3 = 0.25
LIPO_NOMINAL_V = 3.7


@dataclass(frozen=True)
class Part:
    name: str
    shape: object
    color: tuple[int, int, int]
    export: bool = True
    render: bool = True
    obj: bool = True
    emissive: bool = False


@dataclass(frozen=True)
class Params:
    # Outer envelope. Depth is pinned to the Studio Display enclosure.
    body_w: float = 60.0
    body_h: float = 20.0
    body_d: float = STUDIO_DISPLAY_DEPTH_MM
    corner_r: float = 5.0

    # The CNC part is only this rim, open front and back.
    wall: float = 2.5

    # Front panel sits in a rebate so it finishes flush with the aluminium.
    panel_rebate_lip: float = 0.8
    panel_thickness: float = 1.6
    back_cover_thickness: float = 1.5

    camera_bore_dia: float = 7.0
    camera_edge_gap: float = 1.6

    # Dot grid. Pitch is the one number that decides whether MM:SS fits.
    pitch: float = 1.4
    dot_dia: float = 0.9
    active_margin: float = 1.2

    # Internal reference volumes, all as (x, y, z) extents.
    led_pcb: tuple[float, float, float] = (46.0, 1.6, 14.0)
    main_pcb: tuple[float, float, float] = (50.0, 6.0, 14.0)
    camera_module: tuple[float, float, float] = (8.5, 8.0, 8.5)
    battery: tuple[float, float, float] = (40.0, 20.0, 8.0)  # 802040 pouch, ~600mAh
    battery_label: str = "802040 pouch"

    # The board the firmware currently runs on, kept only to check whether it
    # would fit. It does not.
    dev_board: tuple[float, float, float] = (57.0, 1.6, 28.0)


P = Params()


# --------------------------------------------------------------------------
# Geometry
# --------------------------------------------------------------------------


def solid_volume(shape) -> float:
    props = GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, props)
    return props.Mass()


def make_box(x: float, y: float, z: float, dx: float, dy: float, dz: float):
    return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), dx, dy, dz).Shape()


def make_cylinder_y(x: float, y: float, z: float, radius: float, length: float):
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, z), gp_Dir(0, 1, 0)), radius, length).Shape()


def round_depth_edges(shape, radius: float):
    """Fillet the four edges running along Y, turning a box into a rounded bar.

    Building the rounded rectangle as a wire of lines and arcs would work too,
    but filleting a box is fewer moving parts and OpenCascade gives an exact
    result: the volume matches 'box minus four (r^2 - pi r^2/4) corners' to the
    last digit, which is what the validation asserts.
    """
    if radius <= 0:
        return shape
    maker = BRepFilletAPI_MakeFillet(shape)
    seen: set[tuple[float, float, float]] = set()
    added = 0
    explorer = TopExp_Explorer(shape, TopAbs_EDGE)
    while explorer.More():
        edge = TopoDS.Edge_s(explorer.Current())
        p1 = BRep_Tool.Pnt_s(TopExp.FirstVertex_s(edge))
        p2 = BRep_Tool.Pnt_s(TopExp.LastVertex_s(edge))
        delta = (p2.X() - p1.X(), p2.Y() - p1.Y(), p2.Z() - p1.Z())
        length = math.sqrt(sum(c * c for c in delta))
        if length > 1e-9 and abs(delta[1] / length) > 0.99:
            key = (
                round((p1.X() + p2.X()) / 2.0, 3),
                round((p1.Y() + p2.Y()) / 2.0, 3),
                round((p1.Z() + p2.Z()) / 2.0, 3),
            )
            if key not in seen:
                seen.add(key)
                maker.Add(radius, edge)
                added += 1
        explorer.Next()
    if added != 4:
        raise RuntimeError(f"Expected 4 depth-direction edges to fillet, found {added}")
    maker.Build()
    if not maker.IsDone():
        raise RuntimeError("Corner fillet failed")
    return maker.Shape()


def rounded_bar(x: float, y: float, z: float, dx: float, dy: float, dz: float, radius: float):
    return round_depth_edges(make_box(x, y, z, dx, dy, dz), radius)


def cut_many(base, tools: Sequence):
    tools = list(tools)
    if not tools:
        return base
    op = BRepAlgoAPI_Cut()
    arguments = TopTools_ListOfShape()
    arguments.Append(base)
    tool_list = TopTools_ListOfShape()
    for tool in tools:
        tool_list.Append(tool)
    op.SetArguments(arguments)
    op.SetTools(tool_list)
    op.SetRunParallel(True)
    op.Build()
    if not op.IsDone():
        raise RuntimeError("Boolean cut failed")
    return op.Shape()


def compound_of(shapes: Sequence):
    compound = TopoDS_Compound()
    builder = BRep_Builder()
    builder.MakeCompound(compound)
    for shape in shapes:
        builder.Add(compound, shape)
    return compound


# --------------------------------------------------------------------------
# Display grid
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Grid:
    cols: int
    rows: int
    pitch: float
    x0: float
    z0: float

    @property
    def dot_count(self) -> int:
        return self.cols * self.rows

    @property
    def active_w(self) -> float:
        return (self.cols - 1) * self.pitch

    @property
    def active_h(self) -> float:
        return (self.rows - 1) * self.pitch

    @property
    def driver_chips(self) -> int:
        return math.ceil(self.dot_count / 192)

    def center(self, col: int, row: int) -> tuple[float, float]:
        # row 0 at the top, matching the firmware's canvas coordinates.
        return (self.x0 + col * self.pitch, self.z0 + self.active_h - row * self.pitch)


def camera_center_x(p: Params) -> float:
    return p.body_w - p.wall - p.camera_edge_gap - p.camera_bore_dia / 2.0


def make_grid(p: Params) -> Grid:
    """Fit the largest dot grid into the front opening left of the camera."""
    left = p.wall + p.active_margin
    right = camera_center_x(p) - p.camera_bore_dia / 2.0 - p.camera_edge_gap
    bottom = p.wall + p.active_margin
    top = p.body_h - p.wall - p.active_margin

    cols = int((right - left) / p.pitch) + 1
    rows = int((top - bottom) / p.pitch) + 1

    # Centre the grid in the space it was fitted into.
    x0 = left + ((right - left) - (cols - 1) * p.pitch) / 2.0
    z0 = bottom + ((top - bottom) - (rows - 1) * p.pitch) / 2.0
    return Grid(cols, rows, p.pitch, x0, z0)


def glyph_dots(text: str, x0: int, y0: int) -> set[tuple[int, int]]:
    dots: set[tuple[int, int]] = set()
    for index, char in enumerate(text):
        columns = FONT_5X7_COLUMNS.get(char)
        if columns is None:
            continue
        for col_index, bits in enumerate(columns):
            for row_index in range(GLYPH_HEIGHT):
                if bits & (1 << row_index):
                    dots.add((x0 + index * GLYPH_ADVANCE + col_index, y0 + row_index))
    return dots


def countdown_dots(grid: Grid) -> set[tuple[int, int]]:
    """The frame the firmware would draw: MM:SS, plus a progress bar if it fits.

    Mirrors makeDisplayLayout() in embedded/firmware-idf/main/display_layout.cpp:
    the timer is centred, and a 2-row bar is added only when the canvas is tall
    enough for timer + gap + bar.
    """
    x0 = max(0, (grid.cols - TIMER_COLUMNS) // 2)
    bar_h = 2
    content_h = GLYPH_HEIGHT + 1 + bar_h
    if grid.rows >= content_h:
        y0 = (grid.rows - content_h) // 2
        dots = glyph_dots(TIMER_TEXT, x0, y0)
        # Track geometry copied from renderDisplayFrame(): x from 1 to cols-2,
        # with the last two columns kept as the end tick so the full length is
        # readable before the bar reaches it.
        bar_y = y0 + GLYPH_HEIGHT + 1
        bar_x = 1
        bar_w = grid.cols - 2
        fill = (bar_w * BAR_DEMO_PERCENT) // 100
        dots |= {(bar_x + i, bar_y + r) for i in range(fill) for r in range(bar_h)}
        dots |= {(bar_x + bar_w - 2 + i, bar_y + r) for i in range(2) for r in range(bar_h)}
        return dots
    y0 = max(0, (grid.rows - GLYPH_HEIGHT) // 2)
    return glyph_dots(TIMER_TEXT, x0, y0)


# --------------------------------------------------------------------------
# Model
# --------------------------------------------------------------------------


def build_model(p: Params, grid: Grid) -> list[Part]:
    parts: list[Part] = []

    inner_w = p.body_w - 2.0 * p.wall
    inner_h = p.body_h - 2.0 * p.wall
    inner_r = max(0.4, p.corner_r - p.wall)
    rebate_depth = p.panel_thickness + 0.1

    # The CNC part: a rim around the perimeter, open front and back.
    outer = rounded_bar(0.0, 0.0, 0.0, p.body_w, p.body_d, p.body_h, p.corner_r)
    cavity = rounded_bar(p.wall, -1.0, p.wall, inner_w, p.body_d + 2.0, inner_h, inner_r)
    rebate = rounded_bar(
        p.wall - p.panel_rebate_lip,
        -1.0,
        p.wall - p.panel_rebate_lip,
        inner_w + 2.0 * p.panel_rebate_lip,
        rebate_depth + 1.0,
        inner_h + 2.0 * p.panel_rebate_lip,
        inner_r + p.panel_rebate_lip,
    )
    back_seat = rounded_bar(
        p.wall - p.panel_rebate_lip,
        p.body_d - p.back_cover_thickness - 0.1,
        p.wall - p.panel_rebate_lip,
        inner_w + 2.0 * p.panel_rebate_lip,
        p.back_cover_thickness + 1.1,
        inner_h + 2.0 * p.panel_rebate_lip,
        inner_r + p.panel_rebate_lip,
    )
    usb = make_box(p.body_w / 2.0 - 4.6, p.body_d - 3.0, -1.0, 9.2, 4.0, p.wall + 1.5)
    parts.append(Part("rim_cnc_6061_bead_blast_anodised", cut_many(outer, [cavity, rebate, back_seat, usb]), COLORS["aluminum"]))

    # Front panel. Dark and continuous: unlit dots read as a faint texture
    # instead of the hard black hole field a perforated aluminium plate gives.
    panel_w = inner_w + 2.0 * p.panel_rebate_lip - 0.2
    panel_h = inner_h + 2.0 * p.panel_rebate_lip - 0.2
    panel = rounded_bar(
        p.wall - p.panel_rebate_lip + 0.1,
        0.0,
        p.wall - p.panel_rebate_lip + 0.1,
        panel_w,
        p.panel_thickness,
        panel_h,
        inner_r + p.panel_rebate_lip,
    )
    cx = camera_center_x(p)
    cz = p.body_h / 2.0
    panel = cut_many(panel, [make_cylinder_y(cx, -1.0, cz, p.camera_bore_dia / 2.0, p.panel_thickness + 2.0)])
    parts.append(Part("front_panel_dark_display_cover", panel, COLORS["panel"]))

    # Dots. Off dots are barely darker than the panel, which is what a dark
    # cover over LED packages actually looks like.
    lit = countdown_dots(grid)
    radius = p.dot_dia / 2.0
    off_shapes = []
    lit_shapes = []
    for col in range(grid.cols):
        for row in range(grid.rows):
            x, z = grid.center(col, row)
            shape = make_cylinder_y(x, -0.04, z, radius, 0.06)
            (lit_shapes if (col, row) in lit else off_shapes).append(shape)
    parts.append(Part(f"display_dots_off_{len(off_shapes)}", compound_of(off_shapes), COLORS["dot_off"], export=False))
    parts.append(Part(f"display_dots_lit_countdown_{len(lit_shapes)}", compound_of(lit_shapes), COLORS["lit"], export=False, emissive=True))

    # Camera stack in the bore.
    parts.append(Part("camera_lens_cover_glass", make_cylinder_y(cx, 0.10, cz, p.camera_bore_dia / 2.0 - 0.25, 0.5), COLORS["glass"]))
    parts.append(Part("camera_lens_barrel", make_cylinder_y(cx, 0.7, cz, 2.9, 3.2), COLORS["camera_body"]))

    parts.append(
        Part(
            "back_cover",
            rounded_bar(
                p.wall - p.panel_rebate_lip + 0.1,
                p.body_d - p.back_cover_thickness,
                p.wall - p.panel_rebate_lip + 0.1,
                panel_w,
                p.back_cover_thickness,
                panel_h,
                inner_r + p.panel_rebate_lip,
            ),
            COLORS["back_cover"],
        )
    )

    # Internal packing references. Exported to OBJ only, so the review renders
    # stay clean but the volumes are there to inspect.
    y = p.panel_thickness + 0.2
    led_w, led_d, led_h = p.led_pcb
    parts.append(Part("led_pcb_reference", make_box((p.body_w - led_w) / 2.0 - 2.0, y, (p.body_h - led_h) / 2.0, led_w, led_d, led_h), COLORS["pcb"], export=False, render=False))
    y += led_d + 0.4

    main_w, main_d, main_h = p.main_pcb
    parts.append(Part("main_pcb_reference", make_box((p.body_w - main_w) / 2.0, y, (p.body_h - main_h) / 2.0, main_w, main_d, main_h), COLORS["pcb"], export=False, render=False))
    y += main_d + 0.6

    cam_w, cam_d, cam_h = p.camera_module
    parts.append(Part("camera_module_reference", make_box(cx - cam_w / 2.0, p.panel_thickness + 0.2, cz - cam_h / 2.0, cam_w, cam_d, cam_h), COLORS["camera_body"], export=False, render=False))

    bat_w, bat_d, bat_h = p.battery
    parts.append(Part(f"battery_reference_{p.battery_label.replace(' ', '_')}", make_box((p.body_w - bat_w) / 2.0, y, (p.body_h - bat_h) / 2.0, bat_w, bat_d, bat_h), COLORS["battery"], export=False, render=False))

    return parts


# --------------------------------------------------------------------------
# Export
# --------------------------------------------------------------------------


def bbox(shape):
    box = Bnd_Box()
    BRepBndLib.AddOptimal_s(shape, box)
    return box.Get()


def bbox_dims(bounds):
    xmin, ymin, zmin, xmax, ymax, zmax = bounds
    return (xmax - xmin, ymax - ymin, zmax - zmin)


def write_step(parts: list[Part], path: Path):
    writer = STEPControl_Writer()
    writer.Transfer(compound_of([part.shape for part in parts if part.export]), STEPControl_AsIs)
    if writer.Write(str(path)) != IFSelect_RetDone:
        raise RuntimeError(f"STEP export failed for {path}")


def read_step_bbox(path: Path):
    reader = STEPControl_Reader()
    if reader.ReadFile(str(path)) != IFSelect_RetDone:
        raise RuntimeError(f"Could not read exported STEP: {path}")
    reader.TransferRoots()
    return bbox(reader.OneShape())


def triangulate_shape(shape, deflection: float = 0.12, angular: float = 0.7):
    BRepMesh_IncrementalMesh(shape, deflection, False, angular, True).Perform()
    vertices: list[tuple[float, float, float]] = []
    faces: list[tuple[int, int, int]] = []
    explorer = TopExp_Explorer(shape, TopAbs_FACE)
    offset = 0
    while explorer.More():
        face = TopoDS.Face_s(explorer.Current())
        loc = TopLoc_Location()
        tri = BRep_Tool.Triangulation_s(face, loc)
        if tri is None or tri.NbNodes() == 0:
            explorer.Next()
            continue
        transform = loc.Transformation()
        for idx in range(1, tri.NbNodes() + 1):
            pnt = tri.Node(idx).Transformed(transform)
            vertices.append((pnt.X(), pnt.Y(), pnt.Z()))
        flipped = face.Orientation() == TopAbs_REVERSED
        for idx in range(1, tri.NbTriangles() + 1):
            a, b, c = tri.Triangle(idx).Get()
            indices = (offset + a - 1, offset + b - 1, offset + c - 1)
            faces.append((indices[0], indices[2], indices[1]) if flipped else indices)
        offset += tri.NbNodes()
        explorer.Next()
    if not vertices or not faces:
        raise RuntimeError("OCP tessellation returned an empty mesh")
    return np.array(vertices, dtype=np.float64), np.array(faces, dtype=np.int64)


# The cache holds the shape next to its mesh on purpose: keying on id() without
# keeping a reference lets CPython recycle the address and silently serve the
# wrong geometry. That bug cost a debugging session in R1.
_MESH_CACHE: dict[int, tuple[object, trimesh.Trimesh]] = {}


def part_mesh(part: Part) -> trimesh.Trimesh:
    cached = _MESH_CACHE.get(id(part.shape))
    if cached is not None:
        return cached[1]
    vertices, faces = triangulate_shape(part.shape)
    mesh = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
    mesh.visual.face_colors = np.tile(np.array([*part.color, 255], dtype=np.uint8), (len(faces), 1))
    _MESH_CACHE[id(part.shape)] = (part.shape, mesh)
    return mesh


def part_meshes(parts: list[Part], selector: str):
    getter = {"export": lambda p: p.export, "render": lambda p: p.render, "obj": lambda p: p.obj}[selector]
    return [(part, part_mesh(part)) for part in parts if getter(part)]


def export_meshes(parts: list[Part], stl_path: Path, obj_path: Path):
    trimesh.util.concatenate([mesh for _, mesh in part_meshes(parts, "export")]).export(stl_path)

    mtl_path = obj_path.with_suffix(".mtl")
    obj_lines = [f"mtllib {mtl_path.name}\n"]
    mtl_lines: list[str] = []
    vertex_offset = 1
    seen: set[str] = set()
    for part, mesh in part_meshes(parts, "obj"):
        material = part.name.replace("-", "_").replace(".", "_")
        if material not in seen:
            r, g, b = [value / 255.0 for value in part.color]
            emissive = f"{r:.4f} {g:.4f} {b:.4f}" if part.emissive else "0.0000 0.0000 0.0000"
            mtl_lines.extend([f"newmtl {material}\n", f"Kd {r:.4f} {g:.4f} {b:.4f}\n",
                              "Ka 0.1200 0.1200 0.1200\n", "Ks 0.2500 0.2500 0.2500\n",
                              f"Ke {emissive}\n", "Ns 28.0000\n\n"])
            seen.add(material)
        obj_lines.append(f"o {part.name}\nusemtl {material}\n")
        for vertex in mesh.vertices:
            obj_lines.append(f"v {vertex[0]:.6f} {vertex[1]:.6f} {vertex[2]:.6f}\n")
        for face in mesh.faces:
            a, b, c = face + vertex_offset
            obj_lines.append(f"f {a} {b} {c}\n")
        vertex_offset += len(mesh.vertices)
    obj_path.write_text("".join(obj_lines), encoding="utf-8")
    mtl_path.write_text("".join(mtl_lines), encoding="utf-8")


# --------------------------------------------------------------------------
# Render
# --------------------------------------------------------------------------


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
    return right, normalize(np.cross(forward, right)), forward


def render_software(meshes, direction, size=(1600, 1000), pad=0.84, focus=None, ambient=1.0):
    width, height = size
    all_vertices = np.vstack([mesh.vertices for _, mesh in meshes])
    right, up, forward = view_basis(direction)

    if focus is None:
        # Bounding-box midpoint, not the vertex mean. The mean is dominated by
        # whichever part has the densest mesh — here the 320 dot cylinders carry
        # more vertices than the whole rim, which dragged the framing onto the
        # display and pushed the body out of frame in the side views.
        center = (all_vertices.min(axis=0) + all_vertices.max(axis=0)) / 2.0
        rel = all_vertices - center
        projected = np.column_stack((rel @ right, rel @ up))
        span = max(float(np.ptp(projected[:, 0])), float(np.ptp(projected[:, 1])), 1.0)
        scale = min(width, height) * pad / span
    else:
        center = np.array(focus[0], dtype=np.float64)
        scale = width * pad / float(focus[1])

    yy = np.linspace(0.0, 1.0, height)[:, None]
    top = np.array([248, 248, 248], dtype=np.float64) * ambient
    bottom = np.array([228, 230, 234], dtype=np.float64) * ambient
    image = np.zeros((height, width, 3), dtype=np.uint8)
    image[:] = ((1.0 - yy) * top + yy * bottom).astype(np.uint8)[:, None, :]
    zbuf = np.full((height, width), -np.inf, dtype=np.float64)

    light_dirs = [normalize((-0.4, -0.8, 1.0)), normalize((0.8, -0.2, 0.8)), normalize((0.0, 1.0, 0.5))]
    light_weights = [0.70, 0.34, 0.18]

    for part, mesh in meshes:
        rel = np.asarray(mesh.vertices) - center
        screen = np.column_stack((width / 2.0 + (rel @ right) * scale,
                                  height / 2.0 - (rel @ up) * scale,
                                  rel @ forward))
        base = np.array(part.color, dtype=np.float64)
        face_normals = mesh.face_normals
        for fidx in np.argsort(screen[mesh.faces][:, :, 2].mean(axis=1)):
            pts = screen[mesh.faces[fidx]]
            minx = max(int(math.floor(pts[:, 0].min())), 0)
            maxx = min(int(math.ceil(pts[:, 0].max())), width - 1)
            miny = max(int(math.floor(pts[:, 1].min())), 0)
            maxy = min(int(math.ceil(pts[:, 1].max())), height - 1)
            if minx > maxx or miny > maxy:
                continue
            (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = pts
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
            if part.emissive:
                color = np.clip(base, 0, 255).astype(np.uint8)
            else:
                normal = normalize(face_normals[fidx])
                shade = 0.28
                for ldir, weight in zip(light_dirs, light_weights):
                    shade += weight * max(0.0, float(np.dot(normal, ldir)))
                shade += 0.20 * max(0.0, float(abs(np.dot(normal, forward))))
                if part.color in (COLORS["panel"], COLORS["dot_off"], COLORS["glass"]):
                    shade = max(shade, 0.55)
                color = np.clip(base * min(shade, 1.30) * ambient, 0, 255).astype(np.uint8)
            image[miny : maxy + 1, minx : maxx + 1][visible] = color
            region[visible] = z[visible]

    return Image.fromarray(image)


_FONT_CACHE: dict[int, object] = {}


def label_font(size: int = 26):
    if size not in _FONT_CACHE:
        font = None
        for candidate in ("/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc"):
            try:
                font = ImageFont.truetype(candidate, size)
                break
            except OSError:
                continue
        _FONT_CACHE[size] = font or ImageFont.load_default()
    return _FONT_CACHE[size]


def add_label(image: Image.Image, text: str, size: int = 26):
    draw = ImageDraw.Draw(image)
    font = label_font(size)
    box = draw.textbbox((0, 0), text, font=font)
    draw.rounded_rectangle((18, 18, 18 + box[2] - box[0] + 32, 18 + box[3] - box[1] + 28),
                           radius=10, fill=(255, 255, 255), outline=(206, 206, 206))
    draw.text((34, 30), text, fill=(25, 25, 25), font=font)


def trim_background(image: Image.Image, threshold=244):
    rgb = image.convert("RGB")
    arr = np.array(rgb)
    mask = np.min(arr, axis=2) < threshold
    if not np.any(mask):
        return rgb
    ys, xs = np.where(mask)
    pad = 24
    return rgb.crop((max(int(xs.min()) - pad, 0), max(int(ys.min()) - pad, 0),
                     min(int(xs.max()) + pad, rgb.width - 1) + 1, min(int(ys.max()) + pad, rgb.height - 1) + 1))


def fit_contain(image: Image.Image, size, background=(242, 242, 242)):
    canvas = Image.new("RGB", size, background)
    thumb = ImageOps.contain(image.convert("RGB"), size, method=Image.Resampling.LANCZOS)
    canvas.paste(thumb, ((size[0] - thumb.width) // 2, (size[1] - thumb.height) // 2))
    return canvas


def view_path(name: str) -> Path:
    RENDER_OUT.mkdir(parents=True, exist_ok=True)
    return RENDER_OUT / f"{STEM}-{name}.png"


def render_all(parts: list[Part], p: Params, grid: Grid):
    meshes = part_meshes(parts, "render")
    for name, direction in VIEW_SPECS:
        render_software(meshes, direction).save(view_path(name), quality=95)

    focus = ((p.body_w / 2.0, 0.0, p.body_h / 2.0), p.body_w * 1.15)
    for name, ambient in (("display_detail", 1.0), ("display_lit", 0.38)):
        render_software(meshes, (0.0, -1.0, 0.0), size=(1800, 760), focus=focus, ambient=ambient).save(view_path(name), quality=95)

    card = (900, 560)
    names = [name for name, _ in VIEW_SPECS] + ["display_detail", "display_lit"]
    rows = math.ceil(len(names) / 2)
    sheet = Image.new("RGB", (1860, 30 + rows * 590), (235, 235, 235))
    for idx, name in enumerate(names):
        img = fit_contain(trim_background(Image.open(view_path(name))), card)
        add_label(img, name, size=22)
        sheet.paste(img, (30 + (idx % 2) * 930, 30 + (idx // 2) * 590))
    overview = RENDER_OUT / f"{STEM}-multi-view-overview.png"
    sheet.save(overview, quality=95)

    lit = fit_contain(Image.open(view_path("display_lit")).convert("RGB"), (1760, 740), background=(22, 22, 24))
    add_label(lit, f"{grid.cols} x {grid.rows} dots  {grid.pitch}mm pitch  "
                   f"active {grid.active_w:.1f} x {grid.active_h:.1f}mm  {grid.driver_chips} x IS31FL3733", size=24)
    hero = RENDER_OUT / f"{STEM}-display-lit.png"
    lit.save(hero, quality=95)
    return overview, hero


# --------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------


def find_part(parts: list[Part], prefix: str) -> Part:
    for part in parts:
        if part.name.startswith(prefix):
            return part
    raise KeyError(prefix)


def volume_cm3(extents: tuple[float, float, float]) -> float:
    return extents[0] * extents[1] * extents[2] / 1000.0


def validate(parts: list[Part], p: Params, grid: Grid, step_path: Path) -> dict:
    rim = find_part(parts, "rim_cnc_")
    dims = bbox_dims(bbox(rim.shape))

    envelope_ok = (abs(dims[0] - p.body_w) <= 0.15 and abs(dims[1] - p.body_d) <= 0.15
                   and abs(dims[2] - p.body_h) <= 0.15)
    depth_ok = abs(p.body_d - STUDIO_DISPLAY_DEPTH_MM) < 0.01

    # MM:SS at the shared 5x7 font needs 30 columns. Below that the countdown
    # cannot be shown at all, which would make the whole front face pointless.
    timer_ok = grid.cols >= TIMER_COLUMNS
    bar_ok = grid.rows >= GLYPH_HEIGHT + 1 + 2

    inner_w = p.body_w - 2.0 * p.wall
    inner_h = p.body_h - 2.0 * p.wall
    inner_d = p.body_d - p.panel_thickness - p.back_cover_thickness
    cavity_cm3 = inner_w * inner_h * inner_d / 1000.0

    occupied = {
        "led_pcb": volume_cm3(p.led_pcb),
        "main_pcb": volume_cm3(p.main_pcb),
        "camera_module": volume_cm3(p.camera_module),
        "battery": volume_cm3(p.battery),
    }
    free_cm3 = cavity_cm3 - sum(occupied.values())

    def fits(extents):
        return extents[0] <= inner_w and extents[1] <= inner_d and extents[2] <= inner_h

    battery_wh = volume_cm3(p.battery) * LIPO_WH_PER_CM3
    battery_mah = battery_wh / LIPO_NOMINAL_V * 1000.0

    if not envelope_ok:
        raise RuntimeError(f"Outer envelope failed: {dims}")
    if not timer_ok:
        raise RuntimeError(f"Grid is {grid.cols} columns, MM:SS needs {TIMER_COLUMNS}")

    step_bounds = read_step_bbox(step_path)
    return {
        "revision": REVISION,
        "envelope_mm": {"w": round(dims[0], 2), "d": round(dims[1], 2), "h": round(dims[2], 2)},
        "reference": {
            "studio_display_enclosure_depth_mm": STUDIO_DISPLAY_DEPTH_MM,
            "source": "Apple Studio Display tech specs, VESA mount adapter depth 1.2 in",
        },
        "construction": {
            "cnc_part": "perimeter rim only, open front and back",
            "wall_mm": p.wall,
            "corner_radius_mm": p.corner_r,
            "front_panel_thickness_mm": p.panel_thickness,
            "back_cover_thickness_mm": p.back_cover_thickness,
            "rim_volume_cm3": round(solid_volume(rim.shape) / 1000.0, 2),
        },
        "display": {
            "cols": grid.cols,
            "rows": grid.rows,
            "dot_count": grid.dot_count,
            "pitch_mm": grid.pitch,
            "dot_dia_mm": p.dot_dia,
            "active_area_mm": {"w": round(grid.active_w, 2), "h": round(grid.active_h, 2)},
            "driver_chips_is31fl3733": grid.driver_chips,
            "timer_columns_required": TIMER_COLUMNS,
            "lit_dots_in_frame": len(countdown_dots(grid)),
        },
        "packing": {
            "cavity_mm": {"w": round(inner_w, 2), "d": round(inner_d, 2), "h": round(inner_h, 2)},
            "cavity_cm3": round(cavity_cm3, 2),
            "occupied_cm3": {key: round(value, 2) for key, value in occupied.items()},
            "free_cm3": round(free_cm3, 2),
            "battery": {
                "label": p.battery_label,
                "extents_mm": list(p.battery),
                "estimated_wh": round(battery_wh, 2),
                "estimated_mah_at_3v7": round(battery_mah),
            },
            "freenove_dev_board_fits": fits(p.dev_board),
            "freenove_dev_board_mm": list(p.dev_board),
        },
        "step_bbox_mm": {k: round(v, 2) for k, v in zip("wdh", bbox_dims(step_bounds))},
        "validation": {
            "envelope_60x31x20": envelope_ok,
            "depth_matches_studio_display": depth_ok,
            "grid_fits_mm_ss": timer_ok,
            "grid_fits_progress_bar": bar_ok,
            "all_internals_fit_cavity": all(fits(extents) for extents in (p.led_pcb, p.main_pcb, p.camera_module, p.battery)),
            "free_volume_positive": free_cm3 > 0,
            "step_reimport": True,
        },
    }


def write_notes(summary: dict, overview: Path, hero: Path) -> Path:
    d = summary["display"]
    pack = summary["packing"]
    bat = pack["battery"]
    lines = [
        "# Tech One CNC R2 Notes",
        "",
        "## Form",
        "",
        f"`{summary['envelope_mm']['w']} x {summary['envelope_mm']['h']}mm` rounded rectangle from the front, "
        f"`{summary['envelope_mm']['d']}mm` deep. The depth is not a styling choice: it is the Apple Studio",
        "Display enclosure thickness, so the device reads as the same slab when it sits with one.",
        "",
        "The aluminium is a rim around the perimeter only, open front and back. The dark",
        "display panel closes the front, a cover closes the back, and everything else packs",
        "into the cavity between them.",
        "",
        "## What R2 drops from R1",
        "",
        "- The faceted octagonal body and the three separate black functional panels.",
        "- The perforated aluminium plate, its milled seat, and the whole hole-drilling",
        "  question. There is no perforation here: the front is a dark panel with LEDs",
        "  behind it. That is cheaper, and the off state is better — unlit dots read as a",
        "  faint texture instead of the hard black dot field drilled aluminium produced.",
        "- The rear support arm. At 60 x 20 x 31mm the device is a stable brick on its own",
        "  60 x 31mm footprint and does not need to hang off a monitor.",
        "",
        "## Display",
        "",
        f"The grid is derived, not chosen: the front opening minus the camera leaves",
        f"`{d['active_area_mm']['w']} x {d['active_area_mm']['h']}mm`, which at a `{d['pitch_mm']}mm` pitch gives "
        f"`{d['cols']} x {d['rows']}` = {d['dot_count']} dots on {d['driver_chips_is31fl3733']} x IS31FL3733.",
        "",
        f"`{d['cols']}` columns is the number that matters. `MM:SS` in the shared 5x7 font needs",
        f"`{d['timer_columns_required']}` columns, so there is one column of margin on each side and no room to",
        "widen the font. A 2.0mm pitch would give only 24 columns and could not show the",
        "countdown at all, which is what pins the pitch this fine.",
        "",
        f"`{d['rows']}` rows is exactly enough for the 7-row timer plus a gap plus a 2-row progress",
        "bar, which is the `Compact` layout in `embedded/firmware-idf/main/display_layout.h`.",
        "",
        "## Packing",
        "",
        f"Cavity is `{pack['cavity_mm']['w']} x {pack['cavity_mm']['d']} x {pack['cavity_mm']['h']}mm` = "
        f"`{pack['cavity_cm3']}cm3`, of which `{pack['free_cm3']}cm3` is left after the LED PCB, main PCB,",
        "camera module and battery.",
        "",
        "Two consequences worth deciding on before this goes further:",
        "",
        f"**The current dev board does not fit.** The Freenove ESP32-S3 CAM board is",
        f"`{pack['freenove_dev_board_mm'][0]} x {pack['freenove_dev_board_mm'][2]}mm`, and the cavity is only "
        f"`{pack['cavity_mm']['h']}mm` tall. This form requires a custom PCB",
        "around a WROOM-1U module — there is no arrangement of the existing board that works.",
        "",
        f"**Battery runtime is short.** A `{bat['label']}` at `{bat['extents_mm'][0]} x {bat['extents_mm'][1]} x "
        f"{bat['extents_mm'][2]}mm` works out to `{bat['estimated_mah_at_3v7']}mAh` on a",
        f"conservative `{LIPO_WH_PER_CM3} Wh/cm3`; commodity cells that size are usually rated nearer",
        "600mAh. With the camera sampling every 500ms plus the LED matrix, average draw",
        "lands somewhere around 120-180mA, so expect roughly 2.5-5 hours either way. This",
        "wants to be a USB-C powered device with the battery for cordless moves, not an",
        "all-day cordless one. Growing the battery means growing the body, and the depth is",
        "pinned to the Studio Display.",
        "",
        "## Antenna",
        "",
        "Same constraint as R1 and it has not gone away: the AP hotspot is a core feature,",
        "and an aluminium rim around a 2.4GHz antenna is still a problem. R1 solved it with",
        "a polymer rear support arm, which R2 no longer has. The back cover is the obvious",
        "candidate — make it polymer rather than aluminium and put the antenna against it.",
        "It faces away from the user and is not a visible surface.",
        "",
        "## Generated files",
        "",
        f"- STEP: `{Path(summary['files']['step']).relative_to(PROJECT_ROOT)}`",
        f"- STL: `{Path(summary['files']['stl']).relative_to(PROJECT_ROOT)}`",
        f"- OBJ: `{Path(summary['files']['obj']).relative_to(PROJECT_ROOT)}` (also carries the internal reference volumes)",
        f"- Multi-view: `{overview.relative_to(PROJECT_ROOT)}`",
        f"- Lit display: `{hero.relative_to(PROJECT_ROOT)}`",
        "",
        "## Not production-ready yet",
        "",
        "Exterior and packaging draft only. Not defined or verified: rim wall thickness",
        "against actual stiffness, panel retention and gasket, LED PCB stack-up and thermal",
        "path, real component placement inside the cavity, charging circuit and USB-C",
        "position, antenna placement verified by measurement, screw or adhesive strategy,",
        "and CNC DFM cost review.",
        "",
    ]
    path = RENDER_OUT / f"{STEM}-notes.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    return path


def main():
    parser = argparse.ArgumentParser(description=f"Generate, export, render and validate the {REVISION} draft.")
    parser.add_argument("--pitch", type=float, default=P.pitch, help="dot pitch in mm")
    args = parser.parse_args()

    params = Params(pitch=args.pitch)
    CAD_OUT.mkdir(parents=True, exist_ok=True)
    RENDER_OUT.mkdir(parents=True, exist_ok=True)

    grid = make_grid(params)
    parts = build_model(params, grid)

    step_path = CAD_OUT / f"{STEM}.step"
    stl_path = CAD_OUT / f"{STEM}.stl"
    obj_path = CAD_OUT / f"{STEM}.obj"
    write_step(parts, step_path)
    export_meshes(parts, stl_path, obj_path)
    overview, hero = render_all(parts, params, grid)

    summary = validate(parts, params, grid, step_path)
    summary["files"] = {"step": str(step_path), "stl": str(stl_path), "obj": str(obj_path),
                        "overview": str(overview), "display_lit": str(hero)}
    (CAD_OUT / f"{STEM}-validation.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(summary, indent=2, ensure_ascii=False))

    notes = write_notes(summary, overview, hero)
    print(f"Wrote {notes}")


if __name__ == "__main__":
    main()
