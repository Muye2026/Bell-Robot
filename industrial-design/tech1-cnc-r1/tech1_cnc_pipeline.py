#!/usr/bin/env python3
"""Tech One CNC R1: bead-blasted aluminium body with a perforated LED dot display.

This revision replaces the ``tech1-ocp-r1`` appearance (silver faceted shell with
three separate black glass panels) with the direction chosen for the Studio
Display style refresh:

- One continuous bead-blasted / clear-anodised aluminium front. No black panels.
- The OLED window becomes a perforated dot-matrix field: a separate 0.5mm
  aluminium plate with a real drilled/etched hole array, pressed into a milled
  seat in the CNC body. Holes are true boolean cuts, so the exported STEP carries
  the actual pattern.
- The camera keeps a dedicated bore; perforation cannot be shot through.
- The rear support arm becomes a polymer part so the 2.4GHz antenna has a way
  out of the metal enclosure.

Two display resolutions are modelled so the hardware choice can be made from
renders rather than from a spreadsheet:

- ``matrix-a``: 32 x 8 dots, 2.0mm pitch. Countdown only.
- ``matrix-b``: 32 x 16 dots, 1.6mm pitch. Countdown plus a progress bar row.

Same toolchain as ``tech1-ocp-r1``: OCP/OpenCascade for solids and STEP,
BRepMesh + trimesh for STL/OBJ, numpy + Pillow for software-rendered review
sheets. No FreeCAD, no VTK.
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
from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Fuse
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakePolygon
from OCP.BRepGProp import BRepGProp
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder, BRepPrimAPI_MakePrism
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt, gp_Vec
from OCP.GProp import GProp_GProps
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPControl import STEPControl_AsIs, STEPControl_Reader, STEPControl_Writer
from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED
from OCP.TopExp import TopExp_Explorer
from OCP.TopLoc import TopLoc_Location
from OCP.TopoDS import TopoDS, TopoDS_Compound
from OCP.TopTools import TopTools_ListOfShape


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[1]
REFERENCE_IMAGE = PROJECT_ROOT / "industrial-design" / "concept-r3" / "01-tech1-angular-a.png"
CAD_OUT = PROJECT_ROOT / "_cad-output" / "tech1-cnc-r1"
RENDER_OUT = PROJECT_ROOT / "_render-raster" / "tech1-cnc-r1"

REVISION = "tech1-cnc-r1"
STEM = "tech1-cnc"

VIEW_SPECS = [
    ("front", (0.0, -1.0, 0.0)),
    ("rear", (0.0, 1.0, 0.0)),
    ("left", (-1.0, 0.0, 0.0)),
    ("right", (1.0, 0.0, 0.0)),
    ("top", (0.0, 0.0, 1.0)),
    ("bottom", (0.0, 0.0, -1.0)),
    ("axo_front_right", (1.0, -1.0, 0.78)),
    ("axo_front_left", (-1.0, -1.0, 0.78)),
    ("axo_rear_right", (1.0, 1.0, 0.72)),
    ("axo_rear_left", (-1.0, 1.0, 0.72)),
]

COLORS = {
    # Bead-blasted then clear-anodised 6061. Blasting alone would oxidise and
    # hold fingerprints, so the render targets the anodised value, not raw metal.
    "aluminum": (198, 198, 194),
    "aluminum_recess": (168, 168, 165),
    "polymer": (40, 40, 43),
    "black": (12, 12, 13),
    "glass": (8, 10, 12),
    "lit": (255, 244, 219),
    "rubber": (6, 6, 7),
    "pcb": (8, 46, 34),
    "antenna": (176, 112, 56),
}

# IS31FL3733 drives a 12 x 16 matrix, so 192 dots per chip on one I2C bus.
DRIVER_DOTS_PER_CHIP = 192

# 5x7 glyphs, copied verbatim from the firmware's kFont5x7 in
# embedded/firmware-idf/main/display_backend.cpp: one column bitmask per column,
# bit N is row N counting down from the top. Monospaced on a 6px advance.
#
# Sharing the exact glyph data matters. An earlier pass used a hand-drawn font
# here, which lit 71 dots for "45:00" against the firmware's 77 — so the lit
# render was showing a display the device would never produce. The whole point
# of the lit sheets is judging legibility, and that only works if the strokes on
# screen are the strokes the device draws.
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
GLYPH_WIDTH = 5
GLYPH_HEIGHT = 7
GLYPH_ADVANCE = 6


@dataclass(frozen=True)
class Part:
    name: str
    shape: object
    color: tuple[int, int, int]
    export: bool = True   # STEP + STL
    render: bool = True   # raster views
    obj: bool = True      # OBJ dump, which also carries reference-only volumes
    emissive: bool = False


@dataclass(frozen=True)
class PerfSpec:
    """Perforated display window.

    ``pitch`` is the hole centre spacing, which is also the LED pitch on the
    driver PCB behind the plate. ``margin`` is the blank aluminium border
    between the outermost hole centres and the plate edge.
    """

    cols: int
    rows: int
    pitch: float
    hole_dia: float
    plate_thickness: float
    margin: float

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
    def plate_w(self) -> float:
        return self.active_w + 2.0 * self.margin

    @property
    def plate_h(self) -> float:
        return self.active_h + 2.0 * self.margin

    @property
    def driver_chips(self) -> int:
        return math.ceil(self.dot_count / DRIVER_DOTS_PER_CHIP)

    @property
    def open_area_percent(self) -> float:
        hole_area = self.dot_count * math.pi * (self.hole_dia / 2.0) ** 2
        return 100.0 * hole_area / (self.plate_w * self.plate_h)

    @property
    def drill_aspect_ratio(self) -> float:
        return self.plate_thickness / self.hole_dia


@dataclass(frozen=True)
class ModelParams:
    name: str
    title: str
    perf: PerfSpec
    lit_dots: frozenset

    body_length: float = 130.0
    body_height: float = 34.0
    body_depth: float = 18.0
    # Softer than tech1-ocp-r1 (5.4mm): a monolithic milled bar wants a calmer
    # end cap than the faceted three-panel front did.
    end_chamfer: float = 4.6
    front_side_bottom_inset: float = 1.0

    # The perforated plate is centred inside this span of the front face.
    display_zone_x0: float = 9.0
    display_zone_x1: float = 83.0
    seat_depth: float = 0.55
    cavity_depth: float = 4.2

    camera_center_x: float = 99.0
    camera_bore_dia: float = 9.2
    camera_bore_depth: float = 6.0

    button_x: tuple[float, float] = (114.0, 122.5)
    usb_x: float = 130.0

    support_width: float = 36.0
    support_x: float = 47.0
    support_rear_y: float = 54.3
    support_top_z: float = 28.6
    lower_pad_w: float = 34.0
    lower_pad_x: float = 48.0

    notes: str = ""


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


def bar_dots(x0: int, x1: int, y0: int, y1: int) -> set[tuple[int, int]]:
    return {(x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)}


# One consistent device state for both variants: 45 minute target, 27 elapsed,
# so 18:00 remaining and a 60% bar. Drawing "45:00" next to a 60% bar, as an
# earlier pass did, shows a frame the firmware can never produce.
DEMO_TIMER_TEXT = "18:00"
DEMO_BAR_PERCENT = 60

# matrix-a fills all eight rows with the countdown and has nothing left over.
MATRIX_A_DOTS = glyph_dots(DEMO_TIMER_TEXT, 1, 0)

# matrix-b keeps the same countdown glyphs and still has room for a 60% progress
# bar plus its end tick, which is the whole argument for the taller matrix.
# Offsets match makeDisplayLayout(32, 16) in the firmware: content height is
# 7 + 1 + 2 = 10, centred in 16 rows gives timerY = 3 and barY = 11.
MATRIX_B_DOTS = (
    glyph_dots(DEMO_TIMER_TEXT, 1, 3)
    | bar_dots(1, 30 * DEMO_BAR_PERCENT // 100, 11, 12)
    | bar_dots(29, 30, 11, 12)
)


MATRIX_A = ModelParams(
    name="matrix-a",
    title="32 x 8 @ 2.0mm",
    perf=PerfSpec(cols=32, rows=8, pitch=2.0, hole_dia=0.9, plate_thickness=0.5, margin=3.0),
    lit_dots=frozenset(MATRIX_A_DOTS),
    notes=(
        "Countdown only. The eight rows are exactly consumed by one 5x7 line, so "
        "state text and PROB move to the web UI and status has to be carried by "
        "brightness or animation."
    ),
)

MATRIX_B = ModelParams(
    name="matrix-b",
    title="32 x 16 @ 1.6mm",
    perf=PerfSpec(cols=32, rows=16, pitch=1.6, hole_dia=0.75, plate_thickness=0.5, margin=2.0),
    lit_dots=frozenset(MATRIX_B_DOTS),
    notes=(
        "Countdown plus a second element. Needs the finer 1.6mm pitch to fit the "
        "34mm body at all, and the remaining aluminium border above and below the "
        "window is the number to watch."
    ),
)

VARIANTS = {MATRIX_A.name: MATRIX_A, MATRIX_B.name: MATRIX_B}


# --------------------------------------------------------------------------
# Geometry helpers
# --------------------------------------------------------------------------


def make_polygon_face(points: Iterable[tuple[float, float, float]]):
    poly = BRepBuilderAPI_MakePolygon()
    for x, y, z in points:
        poly.Add(gp_Pnt(x, y, z))
    poly.Close()
    return BRepBuilderAPI_MakeFace(poly.Wire()).Face()


def make_box(x: float, y: float, z: float, dx: float, dy: float, dz: float):
    return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), dx, dy, dz).Shape()


def make_cylinder(axis: str, x: float, y: float, z: float, radius: float, length: float):
    directions = {"x": gp_Dir(1, 0, 0), "y": gp_Dir(0, 1, 0), "z": gp_Dir(0, 0, 1)}
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, z), directions[axis]), radius, length).Shape()


def solid_volume(shape) -> float:
    props = GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, props)
    return props.Mass()


def orient_solid(shape):
    """Flip a solid whose faces point inward.

    Extruding a polygon whose winding happens to face away from the extrusion
    direction yields a solid with negative volume. OpenCascade treats that as
    the complement of the intended shape, so every downstream boolean silently
    inverts. Normalising here keeps callers from having to care about winding.
    """
    return shape.Reversed() if solid_volume(shape) < 0.0 else shape


def prism_from_polygon(points, vector):
    return orient_solid(BRepPrimAPI_MakePrism(make_polygon_face(points), gp_Vec(*vector)).Shape())


def yz_prism(x: float, width: float, yz_points):
    return prism_from_polygon([(x, y, z) for y, z in yz_points], (width, 0, 0))


def fuse_all(shapes):
    iterator = iter(shapes)
    fused = next(iterator)
    for shape in iterator:
        op = BRepAlgoAPI_Fuse(fused, shape)
        op.Build()
        fused = op.Shape()
    return fused


def cut_many(base, tools: Sequence):
    """Single boolean against every tool at once.

    512 sequential cuts would take minutes; one multi-tool cut takes under a
    second and is what makes the real hole array practical here.
    """
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


def z_from_top(p: ModelParams, top: float, height: float) -> float:
    return p.body_height - top - height


# --------------------------------------------------------------------------
# Model
# --------------------------------------------------------------------------


def plate_origin(p: ModelParams) -> tuple[float, float]:
    """Bottom-left corner of the perforated plate on the front face."""
    perf = p.perf
    x = p.display_zone_x0 + (p.display_zone_x1 - p.display_zone_x0 - perf.plate_w) / 2.0
    z = (p.body_height - perf.plate_h) / 2.0
    return x, z


def dot_center(p: ModelParams, col: int, row: int) -> tuple[float, float]:
    """Hole centre for a grid coordinate, with row 0 at the top."""
    perf = p.perf
    plate_x, plate_z = plate_origin(p)
    x = plate_x + perf.margin + col * perf.pitch
    z = plate_z + perf.plate_h - perf.margin - row * perf.pitch
    return x, z


def make_faceted_body(p: ModelParams):
    """Mirrored octagonal section extruded through the depth.

    tech1-ocp-r1 hand-assembled this from loose faces into a shell. That shell
    had inconsistent face orientation, which never showed up while the pipeline
    only exported and rendered it, but makes it useless as a boolean argument:
    OpenCascade reads the inverted solid as infinite-minus-the-bar, so cutting
    it adds the tool volumes instead of removing them. A prism from a single
    planar face gives a correctly oriented solid.
    """
    c = p.end_chamfer
    side_inset = p.front_side_bottom_inset
    length = p.body_length
    height = p.body_height

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
    return orient_solid(prism_from_polygon(outline, (0, p.body_depth, 0)))


def body_cutters(p: ModelParams):
    """Everything milled out of the aluminium billet from the front and edges."""
    perf = p.perf
    plate_x, plate_z = plate_origin(p)
    cutters = []

    # Seat for the perforated plate, 0.15mm clearance per side for a press fit.
    cutters.append(
        make_box(
            plate_x - 0.15,
            -1.0,
            plate_z - 0.15,
            perf.plate_w + 0.30,
            p.seat_depth + 1.0,
            perf.plate_h + 0.30,
        )
    )

    # Through cavity behind the active area. The seat lip left around it is what
    # actually supports the 0.5mm plate.
    active_x = plate_x + perf.margin - perf.pitch * 0.5
    active_z = plate_z + perf.margin - perf.pitch * 0.5
    cutters.append(
        make_box(
            active_x,
            -1.0,
            active_z,
            perf.active_w + perf.pitch,
            p.cavity_depth + 1.0,
            perf.active_h + perf.pitch,
        )
    )

    # Camera bore. Perforation cannot be shot through, so the lens gets its own
    # opening with a glass insert.
    cutters.append(
        make_cylinder(
            "y",
            p.camera_center_x,
            -1.0,
            p.body_height / 2.0,
            p.camera_bore_dia / 2.0,
            p.camera_bore_depth + 1.0,
        )
    )

    # Two top-face buttons: GPIO1 dismiss/recalibrate, GPIO2 sample capture.
    # Keeping them off the front preserves the uninterrupted aluminium face.
    for button_x in p.button_x:
        cutters.append(make_cylinder("z", button_x, 9.0, p.body_height - 3.0, 2.35, 4.0))

    # USB-C on the right end face.
    cutters.append(make_box(p.usb_x - 3.2, 5.4, p.body_height / 2.0 - 1.75, 4.2, 9.2, 3.5))

    # Seat for the lower soft-contact pad. tech1-ocp-r1 let this pad stand 0.9mm
    # proud of the front face, which read as a black slab across the bottom of
    # an otherwise uninterrupted aluminium surface. Recessing it leaves a thin
    # dark line and still gives the 0.35mm of rubber the monitor bezel needs.
    cutters.append(make_box(p.lower_pad_x - 0.2, -1.0, 0.4, p.lower_pad_w + 0.4, 3.3, 2.6))

    # Buzzer exit perforation on the underside, matching the bottom_or_rear
    # sound exit already recorded in tech1-structure-r1/tech1-layout.json.
    for col in range(4):
        for row in range(3):
            cutters.append(
                make_cylinder("z", 110.0 + col * 2.2, 7.2 + row * 2.2, -1.0, 0.45, 4.0)
            )

    return cutters


def make_perf_plate(p: ModelParams):
    """0.5mm aluminium plate with the real hole array cut through it.

    Modelled as a separate part on purpose: drilling several hundred 0.75-0.9mm
    holes directly into the CNC body would be one plunge per hole, and a 2mm
    wall would give the light a tunnel with a very narrow viewing angle. A thin
    etched or laser-cut plate pressed into a milled seat solves both.
    """
    perf = p.perf
    plate_x, plate_z = plate_origin(p)
    plate = make_box(plate_x, 0.0, plate_z, perf.plate_w, perf.plate_thickness, perf.plate_h)
    tools = []
    for col in range(perf.cols):
        for row in range(perf.rows):
            x, z = dot_center(p, col, row)
            tools.append(make_cylinder("y", x, -0.5, z, perf.hole_dia / 2.0, perf.plate_thickness + 1.0))
    return cut_many(plate, tools)


def make_lit_dots(p: ModelParams):
    """Emissive plugs sitting inside the holes that the countdown lights up."""
    perf = p.perf
    radius = perf.hole_dia / 2.0 * 0.94
    shapes = []
    for col, row in sorted(p.lit_dots):
        if not (0 <= col < perf.cols and 0 <= row < perf.rows):
            continue
        x, z = dot_center(p, col, row)
        shapes.append(make_cylinder("y", x, -0.02, z, radius, perf.plate_thickness))
    return compound_of(shapes)


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
    perf = p.perf
    plate_x, plate_z = plate_origin(p)
    parts: list[Part] = []

    body = cut_many(make_faceted_body(p), body_cutters(p))
    parts.append(Part(f"body_cnc_6061_bead_blast_anodised_{int(p.body_length)}x{int(p.body_height)}x{int(p.body_depth)}", body, COLORS["aluminum"]))

    # Dark backing so unlit holes read as dots instead of open air. In hardware
    # this is the black solder-mask LED PCB plus a light-isolation mask.
    parts.append(
        Part(
            "display_black_backing_and_led_pcb_reference",
            make_box(
                plate_x + 0.2,
                perf.plate_thickness + 0.02,
                plate_z + 0.2,
                perf.plate_w - 0.4,
                p.cavity_depth - perf.plate_thickness - 0.4,
                perf.plate_h - 0.4,
            ),
            COLORS["black"],
        )
    )

    parts.append(
        Part(
            f"display_perf_plate_al_{perf.plate_thickness}mm_{perf.cols}x{perf.rows}_p{perf.pitch}",
            make_perf_plate(p),
            COLORS["aluminum"],
        )
    )

    parts.append(
        Part(
            f"display_lit_dots_countdown_{len(p.lit_dots)}",
            make_lit_dots(p),
            COLORS["lit"],
            export=False,
            emissive=True,
        )
    )

    # Camera stack inside the bore.
    cz = p.body_height / 2.0
    parts.append(Part("camera_bore_black_liner", make_cylinder("y", p.camera_center_x, 0.30, cz, p.camera_bore_dia / 2.0 - 0.05, p.camera_bore_depth - 0.3), COLORS["black"]))
    parts.append(Part("camera_lens_cover_glass", make_cylinder("y", p.camera_center_x, 0.12, cz, p.camera_bore_dia / 2.0 - 0.35, 0.55), COLORS["glass"]))
    parts.append(Part("camera_lens_barrel", make_cylinder("y", p.camera_center_x, 1.2, cz, 2.6, 3.4), COLORS["glass"]))
    parts.append(Part("camera_privacy_status_dot", make_cylinder("y", p.camera_center_x + 8.6, -0.18, cz, 0.7, 0.3), COLORS["polymer"]))

    # Button caps sit slightly proud of the top face.
    for index, button_x in enumerate(p.button_x, 1):
        parts.append(
            Part(
                f"top_button_cap_{index}_{'dismiss_recalibrate' if index == 1 else 'sample_capture'}",
                make_cylinder("z", button_x, 9.0, p.body_height - 3.0, 2.2, 3.35),
                COLORS["polymer"],
            )
        )

    # Rear support arm in polymer. Aluminium wraps the 2.4GHz antenna in a
    # Faraday cage and the perforation does not help: 0.75-0.9mm holes are three
    # orders of magnitude below the 125mm wavelength, so the plate is solid to
    # RF. The arm is the RF window.
    parts.append(Part("rear_support_arm_polymer_rf_window", rear_support_shape(p), COLORS["polymer"]))
    parts.append(
        Part(
            "antenna_keepout_reference_ufl_patch",
            make_box(p.support_x + p.support_width / 2.0 - 9.0, 30.0, 20.0, 18.0, 1.0, 6.0),
            COLORS["antenna"],
            export=False,
            render=False,
        )
    )

    parts.append(
        Part(
            "dev_board_keepout_reference_57x28",
            make_box(60.0, 6.0, z_from_top(p, 2.5, 26.0), 57.0, 1.1, 26.0),
            COLORS["pcb"],
            export=False,
            render=False,
        )
    )
    parts.append(
        Part(
            "buzzer_keepout_reference_dia12",
            make_cylinder("z", 113.3, 10.4, 5.0, 6.0, 3.2),
            COLORS["polymer"],
            export=False,
            render=False,
        )
    )

    parts.append(Part("front_rubber_pad_recessed_soft_contact", make_box(p.lower_pad_x, -0.35, 0.5, p.lower_pad_w, 2.6, 2.4), COLORS["rubber"]))
    parts.append(Part("rear_rubber_pad_soft_contact", make_box(p.support_x + p.support_width / 2.0 - 4.0, p.support_rear_y - 0.3, 9.2, 8.0, 0.9, 4.8), COLORS["rubber"]))
    return parts


# --------------------------------------------------------------------------
# Export
# --------------------------------------------------------------------------


def make_compound(parts: Iterable[Part]):
    return compound_of([part.shape for part in parts if part.export])


def bbox(shape) -> tuple[float, float, float, float, float, float]:
    # AddOptimal_s rather than Add_s: the plain version inflates by the shape's
    # tolerance, and boolean results carry tolerances large enough (~0.23mm
    # here) to swamp the envelope checks.
    box = Bnd_Box()
    BRepBndLib.AddOptimal_s(shape, box)
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


def triangulate_shape(shape, deflection: float = 0.3, angular: float = 0.9):
    mesher = BRepMesh_IncrementalMesh(shape, deflection, False, angular, True)
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


# Meshing is the expensive half of a run and part_meshes() is called once each
# for STEP/STL, the raster views and the OBJ dump, so results are cached.
#
# The cached value keeps the shape alive alongside its mesh. Keying on
# id(shape) without holding a reference is a trap: once one variant's parts go
# out of scope CPython is free to hand the same address to the next variant's
# shapes, and the cache then silently serves the previous variant's geometry.
_MESH_CACHE: dict[int, tuple[object, trimesh.Trimesh]] = {}


def reset_mesh_cache():
    _MESH_CACHE.clear()


def part_mesh(part: Part) -> trimesh.Trimesh:
    key = id(part.shape)
    cached = _MESH_CACHE.get(key)
    if cached is not None:
        return cached[1]
    vertices, faces = triangulate_shape(part.shape)
    mesh = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
    mesh.visual.face_colors = np.tile(np.array([*part.color, 255], dtype=np.uint8), (len(faces), 1))
    _MESH_CACHE[key] = (part.shape, mesh)
    return mesh


def part_meshes(parts: list[Part], selector: str):
    getter = {"export": lambda part: part.export, "render": lambda part: part.render, "obj": lambda part: part.obj}[selector]
    return [(part, part_mesh(part)) for part in parts if getter(part)]


def export_meshes(parts: list[Part], stl_path: Path, obj_path: Path):
    meshes = [mesh for _, mesh in part_meshes(parts, "export")]
    trimesh.util.concatenate(meshes).export(stl_path)
    write_obj(parts, obj_path)


def write_obj(parts: list[Part], obj_path: Path):
    mtl_path = obj_path.with_suffix(".mtl")
    obj_lines = [f"mtllib {mtl_path.name}\n"]
    mtl_lines: list[str] = []
    vertex_offset = 1
    seen_materials: set[str] = set()
    for part, mesh in part_meshes(parts, "obj"):
        mat_name = part.name.replace("-", "_").replace(".", "_")
        if mat_name not in seen_materials:
            r, g, b = [value / 255.0 for value in part.color]
            mtl_lines.extend(
                [
                    f"newmtl {mat_name}\n",
                    f"Kd {r:.4f} {g:.4f} {b:.4f}\n",
                    "Ka 0.1200 0.1200 0.1200\n",
                    "Ks 0.2500 0.2500 0.2500\n",
                    f"Ke {r:.4f} {g:.4f} {b:.4f}\n" if part.emissive else "Ke 0.0000 0.0000 0.0000\n",
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


# --------------------------------------------------------------------------
# Software render
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
    up = normalize(np.cross(forward, right))
    return right, up, forward


def render_software(meshes, direction, size=(1600, 1050), pad=0.86, focus=None, ambient=1.0):
    """Painter-ordered z-buffer rasteriser.

    ``focus`` is ``(center_xyz, span_mm)`` and pins the framing to a known
    region of the model instead of auto-fitting, which is how the display detail
    sheets stay comparable between variants.

    ``ambient`` scales everything that is not emissive. At 1.0 the render shows
    the physical perforation on a lit anodised surface. Lower values stand in
    for the condition that actually matters for legibility: a desk where the
    LEDs are the bright thing and the aluminium is not. Cream dots on a silver
    plate are nearly invisible at full ambient even when the pattern is correct.
    """
    width, height = size
    all_vertices = np.vstack([mesh.vertices for _, mesh in meshes])
    right, up, forward = view_basis(direction)

    if focus is None:
        # Bounding-box midpoint, not the vertex mean. The mean is dominated by
        # whichever part has the densest mesh — the perforated plate carries far
        # more vertices than the body, which pulls the framing off centre.
        center = (all_vertices.min(axis=0) + all_vertices.max(axis=0)) / 2.0
        rel = all_vertices - center
        projected = np.column_stack((rel @ right, rel @ up))
        span = max(float(np.ptp(projected[:, 0])), float(np.ptp(projected[:, 1])), 1.0)
        scale = min(width, height) * pad / span
    else:
        # Framed on the horizontal span, not the smaller dimension: the display
        # band is wide and short, and fitting it by height would pull the whole
        # 130mm body back into frame.
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
        verts = np.asarray(mesh.vertices)
        rel = verts - center
        sx = width / 2.0 + (rel @ right) * scale
        sy = height / 2.0 - (rel @ up) * scale
        depth = rel @ forward
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
            if minx > maxx or miny > maxy:
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
            if part.emissive:
                color = np.clip(base, 0, 255).astype(np.uint8)
            else:
                normal = normalize(face_normals[fidx])
                shade = 0.28
                for ldir, weight in zip(light_dirs, light_weights):
                    shade += weight * max(0.0, float(np.dot(normal, ldir)))
                shade += 0.20 * max(0.0, float(abs(np.dot(normal, forward))))
                if part.color in (COLORS["black"], COLORS["glass"]):
                    shade = max(shade, 0.42)
                color = np.clip(base * min(shade, 1.30) * ambient, 0, 255).astype(np.uint8)
            target = image[miny : maxy + 1, minx : maxx + 1]
            target[visible] = color
            region[visible] = z[visible]

    return Image.fromarray(image)


_FONT_CACHE: dict[int, object] = {}


def label_font(size: int = 26):
    if size in _FONT_CACHE:
        return _FONT_CACHE[size]
    font = None
    for candidate in (
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFNS.ttf",
    ):
        try:
            font = ImageFont.truetype(candidate, size)
            break
        except OSError:
            continue
    if font is None:
        font = ImageFont.load_default()
    _FONT_CACHE[size] = font
    return font


def add_label(image: Image.Image, text: str, size: int = 26):
    draw = ImageDraw.Draw(image)
    font = label_font(size)
    box = draw.textbbox((0, 0), text, font=font)
    w, h = box[2] - box[0], box[3] - box[1]
    draw.rounded_rectangle((18, 18, 18 + w + 32, 18 + h + 28), radius=10, fill=(255, 255, 255), outline=(206, 206, 206))
    draw.text((34, 30), text, fill=(25, 25, 25), font=font)


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


def fit_contain(image: Image.Image, size, background=(242, 242, 242)):
    canvas = Image.new("RGB", size, background)
    thumb = ImageOps.contain(image.convert("RGB"), size, method=Image.Resampling.LANCZOS)
    canvas.paste(thumb, ((size[0] - thumb.width) // 2, (size[1] - thumb.height) // 2))
    return canvas


def fit_cover(image: Image.Image, size):
    return ImageOps.fit(image.convert("RGB"), size, method=Image.Resampling.LANCZOS)


def variant_dir(p: ModelParams) -> Path:
    out = RENDER_OUT / p.name
    out.mkdir(parents=True, exist_ok=True)
    return out


def view_path(p: ModelParams, view_name: str) -> Path:
    return variant_dir(p) / f"{STEM}-{p.name}-{view_name}.png"


def render_views(parts: list[Part], p: ModelParams) -> list[Path]:
    meshes = part_meshes(parts, "render")
    outputs = []
    for view_name, direction in VIEW_SPECS:
        image = render_software(meshes, direction)
        path = view_path(p, view_name)
        image.save(path, quality=95)
        outputs.append(path)

    # Display close-ups: front view pinned to the perforated window plus some
    # surrounding aluminium. Both variants use the same 84mm span so matrix-a
    # correctly reads as the physically wider window.
    plate_x, plate_z = plate_origin(p)
    focus = ((plate_x + p.perf.plate_w / 2.0, 0.0, plate_z + p.perf.plate_h / 2.0), 84.0)
    for view_name, ambient in (("display_detail", 1.0), ("display_lit", 0.40)):
        image = render_software(meshes, (0.0, -1.0, 0.0), size=(1800, 900), focus=focus, ambient=ambient)
        path = view_path(p, view_name)
        image.save(path, quality=95)
        outputs.append(path)
    return outputs


def compose_review_sheets(p: ModelParams):
    out_dir = variant_dir(p)
    reference = Image.open(REFERENCE_IMAGE).convert("RGB")
    model = trim_background(Image.open(view_path(p, "axo_front_right")).convert("RGB"))
    left = fit_cover(reference, (1500, 980))
    right = fit_contain(model, (1500, 980))
    add_label(left, "Reference: concept-r3 tech1-a")
    add_label(right, f"Model {REVISION} {p.name}")
    sheet = Image.new("RGB", (3060, 1040), (235, 235, 235))
    sheet.paste(left, (20, 30))
    sheet.paste(right, (1540, 30))
    compare_path = out_dir / f"{STEM}-{p.name}-reference-vs-model.png"
    sheet.save(compare_path, quality=95)

    card = (900, 500)
    names = [name for name, _ in VIEW_SPECS] + ["display_detail", "display_lit"]
    rows = math.ceil(len(names) / 2)
    views = Image.new("RGB", (1860, 30 + rows * 530), (235, 235, 235))
    for idx, view_name in enumerate(names):
        img = fit_contain(trim_background(Image.open(view_path(p, view_name))), card)
        add_label(img, view_name, size=22)
        views.paste(img, (30 + (idx % 2) * 930, 30 + (idx // 2) * 530))
    overview_path = out_dir / f"{STEM}-{p.name}-multi-view-overview.png"
    views.save(overview_path, quality=95)
    return compare_path, overview_path


def compose_variant_comparison(summaries: dict) -> Path:
    """Side-by-side of the two display resolutions, lit, at matched scale."""
    card = (1760, 880)
    sheet = Image.new("RGB", (1820, 60 + len(summaries) * 940), (22, 22, 24))
    for idx, name in enumerate(summaries):
        p = VARIANTS[name]
        perf = p.perf
        img = fit_contain(Image.open(view_path(p, "display_lit")).convert("RGB"), card, background=(22, 22, 24))
        add_label(
            img,
            f"{p.name}  {p.title}  {perf.dot_count} dots  "
            f"active {perf.active_w:.1f} x {perf.active_h:.1f}mm  "
            f"border {summaries[name]['display']['body_border_mm']:.1f}mm  "
            f"{perf.driver_chips} x IS31FL3733",
            size=24,
        )
        sheet.paste(img, (30, 30 + idx * 940))
    path = RENDER_OUT / f"{STEM}-display-resolution-comparison.png"
    sheet.save(path, quality=95)
    return path


# --------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------


def find_part(parts: list[Part], prefix: str) -> Part:
    for part in parts:
        if part.name.startswith(prefix):
            return part
    raise KeyError(f"No part starting with {prefix!r}")


def validate_outputs(parts: list[Part], p: ModelParams, step_path: Path) -> dict:
    perf = p.perf
    body = find_part(parts, "body_cnc_")
    plate = find_part(parts, "display_perf_plate_")
    support = find_part(parts, "rear_support_arm_polymer")

    body_bounds = bbox(body.shape)
    body_dims = bbox_dims(body_bounds)
    support_extension = bbox(support.shape)[4] - body_bounds[4]

    # The boolean is verified by volume rather than by eye: a plate short of
    # holes, or with holes that merged into each other, will not match.
    plate_volume = solid_volume(plate.shape)
    expected_plate_volume = (
        perf.plate_w * perf.plate_h * perf.plate_thickness
        - perf.dot_count * math.pi * (perf.hole_dia / 2.0) ** 2 * perf.plate_thickness
    )
    plate_volume_ok = abs(plate_volume - expected_plate_volume) < 0.05

    _, plate_z = plate_origin(p)
    border = plate_z  # symmetric, so the bottom border equals the top border

    body_ok = (
        abs(body_dims[0] - p.body_length) <= 0.15
        and abs(body_dims[1] - p.body_depth) <= 0.15
        and abs(body_dims[2] - p.body_height) <= 0.15
    )
    support_ok = 32.0 <= support_extension <= 38.0
    hole_ok = perf.hole_dia < perf.pitch * 0.75

    step_bounds = read_step_bbox(step_path)

    if not body_ok:
        raise RuntimeError(f"Body bbox failed validation: {body_dims}")
    if not support_ok:
        raise RuntimeError(f"Rear support extension failed validation: {support_extension:.2f}")
    if not plate_volume_ok:
        raise RuntimeError(f"Perf plate volume {plate_volume:.4f} != expected {expected_plate_volume:.4f}")
    if not hole_ok:
        raise RuntimeError(f"Hole diameter {perf.hole_dia} too close to pitch {perf.pitch}")

    return {
        "revision": REVISION,
        "variant": p.name,
        "title": p.title,
        "body_bbox_mm": {"x": round(body_dims[0], 2), "y": round(body_dims[1], 2), "z": round(body_dims[2], 2)},
        "rear_support_extension_mm": round(support_extension, 2),
        "display": {
            "cols": perf.cols,
            "rows": perf.rows,
            "dot_count": perf.dot_count,
            "pitch_mm": perf.pitch,
            "hole_dia_mm": perf.hole_dia,
            "plate_thickness_mm": perf.plate_thickness,
            "active_area_mm": {"w": round(perf.active_w, 2), "h": round(perf.active_h, 2)},
            "plate_size_mm": {"w": round(perf.plate_w, 2), "h": round(perf.plate_h, 2)},
            "body_border_mm": round(border, 2),
            "open_area_percent": round(perf.open_area_percent, 2),
            "drill_depth_to_dia_ratio": round(perf.drill_aspect_ratio, 2),
            "driver_chips_is31fl3733": perf.driver_chips,
            "lit_dots_in_demo_frame": len(p.lit_dots),
        },
        "step_bbox_mm": {
            "x": round(bbox_dims(step_bounds)[0], 2),
            "y": round(bbox_dims(step_bounds)[1], 2),
            "z": round(bbox_dims(step_bounds)[2], 2),
        },
        "validation": {
            "body_envelope_130x18x34": body_ok,
            "rear_support_extension_32_to_38": support_ok,
            "perf_plate_volume_matches_hole_count": plate_volume_ok,
            "hole_dia_below_75pct_of_pitch": hole_ok,
            "step_reimport": True,
        },
        "measured": {
            "perf_plate_volume_mm3": round(plate_volume, 4),
            "perf_plate_volume_expected_mm3": round(expected_plate_volume, 4),
        },
    }


def write_iteration_notes(summaries: dict, comparison_path: Path) -> Path:
    a = summaries.get("matrix-a")
    b = summaries.get("matrix-b")
    lines = [
        f"# Tech One CNC R1 Iteration Notes",
        "",
        "## What changed from tech1-ocp-r1",
        "",
        "- Front is now one continuous bead-blasted / clear-anodised aluminium surface.",
        "  The three separate black glass panels (left OLED window, centre camera island,",
        "  right service panel) are gone; they were the strongest visual tie to the old",
        "  concept render and the least compatible with a Studio Display reading.",
        "- The OLED window is replaced by a perforated dot-matrix field. Holes are real",
        "  boolean cuts in a separate 0.5mm aluminium plate, not a texture, so the",
        "  exported STEP carries the pattern.",
        f"- End chamfer softened from `5.4mm` to `{MATRIX_A.end_chamfer}mm`, and the front side",
        f"  inset reduced from `2.2mm` to `{MATRIX_A.front_side_bottom_inset}mm`, so the bar reads as a milled",
        "  billet instead of a faceted shell.",
        "- Buttons moved from the front panel to the top face, keeping the front clean.",
        "- Rear support arm is now a polymer part and carries the antenna.",
        "",
        "## Why the perforation is a separate plate",
        "",
        "Drilling several hundred sub-millimetre holes straight into the CNC body is one",
        "plunge per hole, and a ~2mm front wall gives each hole a depth-to-diameter ratio",
        "high enough to collimate the light into a narrow viewing cone. A 0.5mm plate,",
        "etched or laser cut, pressed into a milled seat, fixes both. The seat lip left",
        "around the through cavity is what supports it.",
        "",
        "Process order is CNC, deburr, bead blast, anodise. Bead blasting alone leaves raw",
        "aluminium that oxidises and holds fingerprints, so the anodise is not optional.",
        "The perforated plate is handled separately so blasting media does not pack the",
        "holes.",
        "",
        "## Why the rear arm is polymer",
        "",
        "An aluminium enclosure is a Faraday cage at 2.4GHz, and the perforation does not",
        "relieve it: the holes are three orders of magnitude smaller than the 125mm",
        "wavelength, so the plate is RF-solid. The AP hotspot is a core feature of this",
        "device (camera preview, /ota, /samples all live on it), so the antenna needs a",
        "non-metallic window. The rear support arm already exists, already faces away from",
        "the user, and is large enough for a U.FL patch. `antenna_keepout_reference_ufl_patch`",
        "marks the volume.",
        "",
        "## Display resolution comparison",
        "",
        "| | matrix-a | matrix-b |",
        "| --- | --- | --- |",
    ]
    if a and b:
        da, db = a["display"], b["display"]
        rows = [
            ("Grid", f"{da['cols']} x {da['rows']}", f"{db['cols']} x {db['rows']}"),
            ("Dots", str(da["dot_count"]), str(db["dot_count"])),
            ("Pitch", f"{da['pitch_mm']}mm", f"{db['pitch_mm']}mm"),
            ("Hole diameter", f"{da['hole_dia_mm']}mm", f"{db['hole_dia_mm']}mm"),
            ("Active area", f"{da['active_area_mm']['w']} x {da['active_area_mm']['h']}mm", f"{db['active_area_mm']['w']} x {db['active_area_mm']['h']}mm"),
            ("Plate size", f"{da['plate_size_mm']['w']} x {da['plate_size_mm']['h']}mm", f"{db['plate_size_mm']['w']} x {db['plate_size_mm']['h']}mm"),
            ("Aluminium border above/below", f"{da['body_border_mm']}mm", f"{db['body_border_mm']}mm"),
            ("Open area", f"{da['open_area_percent']}%", f"{db['open_area_percent']}%"),
            ("IS31FL3733 chips", str(da["driver_chips_is31fl3733"]), str(db["driver_chips_is31fl3733"])),
            ("Lit dots in the demo frame", str(da["lit_dots_in_demo_frame"]), str(db["lit_dots_in_demo_frame"])),
        ]
        for label, va, vb in rows:
            lines.append(f"| {label} | {va} | {vb} |")
        lines += [
            "",
            f"`matrix-a` uses all eight rows for one 5x7 line, so `PROB` and the state text",
            "move to the web UI permanently and on-device state has to be carried by",
            "brightness or animation.",
            "",
            f"`matrix-b` fits the countdown plus a progress bar, but only at a 1.6mm pitch,",
            f"and it leaves `{db['body_border_mm']}mm` of aluminium above and below the window against",
            f"`{da['body_border_mm']}mm` for matrix-a. That border is the real constraint: a pressed-in",
            "plate wants more material around the seat than that. Keeping 16 rows comfortably",
            "means either dropping to a ~1.4mm pitch or growing the body height from 34mm to",
            "roughly 40mm.",
            "",
            f"Side-by-side render: `{comparison_path.relative_to(PROJECT_ROOT)}`",
            "",
            "## Open area is the next lever, and it is not a resolution question",
            "",
            "The lit renders show something the dot count does not predict: at these hole",
            f"diameters the unlit field is a strong dark dot pattern ({da['open_area_percent']}% open area for",
            f"matrix-a, {db['open_area_percent']}% for matrix-b), and it competes with the lit dots hard",
            "enough that the countdown is harder to read than the raw resolution suggests.",
            "Both variants suffer from it, so it does not decide between them.",
            "",
            "Contrast here is set by hole diameter, not by pitch or dot count. Dropping",
            f"matrix-a from `{da['hole_dia_mm']}mm` to roughly `0.6mm` takes open area from `{da['open_area_percent']}%` to about",
            "`5%`, which turns the off state into a faint texture instead of a grid of black",
            "dots. The cost is light output per dot, so it trades against LED brightness and",
            "how well each emitter is centred under its hole. The alternative is a diffusing",
            "film behind the plate, which lifts the off state without giving up aperture.",
            "",
            "Worth resolving with a physical sample before either resolution is committed:",
            "one 0.5mm plate, a few hole diameters in one strip, a single LED behind it.",
            "",
            "## Inherited pipeline defect found while building this",
            "",
            "`tech1-ocp-r1` assembles the body from loose faces via `make_shell_solid`, and",
            "that shell has inconsistent face orientation: its computed volume is negative.",
            "It never mattered there because that pipeline only exports and renders the",
            "shape. It matters here because OpenCascade reads a negative-volume solid as the",
            "complement of the intended shape, so every boolean cut against it *adds* the",
            "tool volume instead of removing it. This revision builds the body as a prism",
            "from a single planar face and normalises winding in `orient_solid()`.",
            "",
            "If `tech1-ocp-r1` is ever extended with booleans, it needs the same fix.",
        ]
    lines += [
        "",
        "## Generated files",
        "",
    ]
    for name, summary in summaries.items():
        for key, path in summary["files"].items():
            lines.append(f"- `{name}` {key}: `{Path(path).relative_to(PROJECT_ROOT)}`")
    lines += [
        "",
        "## Not production-ready yet",
        "",
        "Still an exterior and layout draft. Not yet defined or verified: wall thickness",
        "and rib layout for the milled body, plate press-fit tolerance and retention,",
        "per-dot light isolation between adjacent holes, anodise thickness growth inside",
        "sub-millimetre holes, LED PCB stack-up and thermal path, measured camera module",
        "and dev board fit, antenna placement verified by actual RF measurement rather",
        "than by keepout volume, screw bosses, cable routing, and CNC DFM cost review.",
        "",
    ]
    notes_path = RENDER_OUT / f"{STEM}-iteration-notes.md"
    notes_path.write_text("\n".join(lines), encoding="utf-8")
    return notes_path


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------


def run_variant(p: ModelParams) -> dict:
    CAD_OUT.mkdir(parents=True, exist_ok=True)
    RENDER_OUT.mkdir(parents=True, exist_ok=True)
    reset_mesh_cache()
    parts = build_model(p)

    step_path = CAD_OUT / f"{STEM}-{p.name}.step"
    stl_path = CAD_OUT / f"{STEM}-{p.name}.stl"
    obj_path = CAD_OUT / f"{STEM}-{p.name}.obj"
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
        "display_detail": str(view_path(p, "display_detail")),
        "display_lit": str(view_path(p, "display_lit")),
    }
    summary_path = CAD_OUT / f"{STEM}-{p.name}-validation.json"
    summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    return summary


def main():
    parser = argparse.ArgumentParser(description=f"Generate, export, render, compare and validate the {REVISION} CAD draft.")
    parser.add_argument("--variant", choices=["all", *VARIANTS], default="all")
    args = parser.parse_args()

    selected = list(VARIANTS) if args.variant == "all" else [args.variant]
    summaries: dict[str, dict] = {}
    for name in selected:
        print(f"Generating {name}")
        summaries[name] = run_variant(VARIANTS[name])
        print(json.dumps(summaries[name], indent=2, ensure_ascii=False))

    if args.variant == "all":
        comparison_path = compose_variant_comparison(summaries)
        print(f"Wrote {comparison_path}")
        notes_path = write_iteration_notes(summaries, comparison_path)
        print(f"Wrote {notes_path}")


if __name__ == "__main__":
    main()
