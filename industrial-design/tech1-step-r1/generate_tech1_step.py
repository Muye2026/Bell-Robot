import math
import os

import FreeCAD as App
import Part
import Import


OUT_DIR = os.path.dirname(os.path.abspath(globals().get("__file__", os.getcwd())))
PROJECT_ROOT = os.path.dirname(os.path.dirname(OUT_DIR))
EXPORT_DIR = os.path.join(PROJECT_ROOT, "_cad-output")
DOC_NAME = "tech1_angular_a_structure"
EXPORT_EXCLUDE_TOKENS = ("keepout", "reference", "clearance", "parameter_summary")
FCSTD_PREFIX = "tech1-angular-a-structure-r2.FCStd"


P = {
    "body_length": 130.0,
    "body_height": 34.0,
    "body_depth": 18.0,
    "end_chamfer": 7.0,
    "front_vertical_height": 24.6,
    "body_edge_chamfer": 0.9,
    "panel_edge_chamfer": 0.28,
    "inner_length": 124.0,
    "inner_height": 31.0,
    "rear_extension": 36.0,
    "monitor_min": 8.0,
    "monitor_max": 20.0,
    "panel_chamfer": 1.45,
    "support_width": 28.0,
    "support_x": 51.0,
    "front_pad_radius": 2.4,
    "front_pad_length": 26.0,
}


def z_from_top(top_y, height):
    return P["body_height"] - top_y - height


def add_obj(doc, name, shape, color=None, transparency=0):
    obj = doc.addObject("Part::Feature", name)
    obj.Shape = shape
    if color and obj.ViewObject:
        obj.ViewObject.ShapeColor = color
    if obj.ViewObject:
        obj.ViewObject.Transparency = transparency
    return obj


def chamfer_shape(shape, amount, edge_filter=None):
    edges = shape.Edges
    if edge_filter:
        edges = [edge for edge in edges if edge_filter(edge)]
    if not edges:
        return shape
    try:
        return shape.makeChamfer(amount, edges)
    except Exception:
        return shape


def edge_center(edge):
    return edge.BoundBox.Center


def box(name, x, y, z, dx, dy, dz, color=None, transparency=0):
    shape = Part.makeBox(dx, dy, dz, App.Vector(x, y, z))
    return add_obj(App.ActiveDocument, name, shape, color, transparency)


def front_chamfered_panel(name, x, z, width, height, thickness, chamfer=None, color=None):
    c = min(chamfer if chamfer is not None else P["panel_chamfer"], width / 4, height / 4)
    pts = [
        App.Vector(x + c, 0, z),
        App.Vector(x + width - c, 0, z),
        App.Vector(x + width, 0, z + c),
        App.Vector(x + width, 0, z + height - c),
        App.Vector(x + width - c, 0, z + height),
        App.Vector(x + c, 0, z + height),
        App.Vector(x, 0, z + height - c),
        App.Vector(x, 0, z + c),
        App.Vector(x + c, 0, z),
    ]
    face = Part.Face(Part.makePolygon(pts))
    shape = face.extrude(App.Vector(0, thickness, 0))
    shape.translate(App.Vector(0, -thickness, 0))
    shape = chamfer_shape(shape, P["panel_edge_chamfer"])
    return add_obj(App.ActiveDocument, name, shape, color)


def front_rounded_panel(name, x, z, width, height, thickness, radius, color=None):
    shape = Part.makeBox(width, thickness, height, App.Vector(x, -thickness, z))
    shape = chamfer_shape(shape, min(P["panel_edge_chamfer"], 0.22))

    def vertical_front_corner(edge):
        b = edge.BoundBox
        center = b.Center
        return (
            abs(center.y + thickness) < 0.05
            and b.ZLength > height * 0.55
            and (abs(center.x - x) < radius * 1.8 or abs(center.x - (x + width)) < radius * 1.8)
        )

    try:
        edges = [edge for edge in shape.Edges if vertical_front_corner(edge)]
        if edges:
            shape = shape.makeFillet(radius, edges)
    except Exception:
        pass
    return add_obj(App.ActiveDocument, name, shape, color)


def make_face(points):
    return Part.Face(Part.makePolygon(points + [points[0]]))


def yz_profile_extrude(name, x, width, yz_points, color=None):
    pts = [App.Vector(x, y, z) for y, z in yz_points]
    face = make_face(pts)
    shape = face.extrude(App.Vector(width, 0, 0))
    return add_obj(App.ActiveDocument, name, shape, color)


def faceted_body():
    c = P["end_chamfer"]
    l = P["body_length"]
    h = P["body_height"]
    d = P["body_depth"]
    front_low = (h - P["front_vertical_height"]) / 2.0
    front_high = h - front_low
    # The front face is intentionally shorter than the rear face. Connecting
    # these two outlines creates the real top/bottom machined facets seen in
    # the A render instead of decorative groove lines.
    front = [
        App.Vector(0, 0, front_low + c),
        App.Vector(c, 0, front_low),
        App.Vector(l - c, 0, front_low),
        App.Vector(l, 0, front_low + c),
        App.Vector(l, 0, front_high - c),
        App.Vector(l - c, 0, front_high),
        App.Vector(c, 0, front_high),
        App.Vector(0, 0, front_high - c),
    ]
    rear = [
        App.Vector(0, d, c),
        App.Vector(c, d, 0),
        App.Vector(l - c, d, 0),
        App.Vector(l, d, c),
        App.Vector(l, d, h - c),
        App.Vector(l - c, d, h),
        App.Vector(c, d, h),
        App.Vector(0, d, h - c),
    ]
    faces = [make_face(front), make_face(list(reversed(rear)))]
    for i in range(len(front)):
        j = (i + 1) % len(front)
        faces.append(make_face([front[i], front[j], rear[j], rear[i]]))
    shell = Part.makeShell(faces)
    body = Part.makeSolid(shell)
    # Chamfer the front-face perimeter where the front panel transitions into
    # the top, bottom, and end side facets. Keep rear extreme edges untouched
    # so the overall 130 x 34 x 18 mm reference envelope remains intact.
    return chamfer_shape(
        body,
        P["body_edge_chamfer"],
        lambda edge: abs(edge_center(edge).y) < 0.05,
    )


def cylinder_x(name, x, y, z, radius, length, color=None):
    cyl = Part.makeCylinder(radius, length, App.Vector(x, y, z), App.Vector(1, 0, 0))
    return add_obj(App.ActiveDocument, name, cyl, color)


def cylinder_y(name, x, y, z, radius, length, color=None):
    cyl = Part.makeCylinder(radius, length, App.Vector(x, y, z), App.Vector(0, 1, 0))
    return add_obj(App.ActiveDocument, name, cyl, color)


def rear_support_arm_shape():
    x = P["support_x"]
    width = P["support_width"]

    def extrude_profile(x0, w, yz_points):
        face = Part.Face(Part.makePolygon([App.Vector(x0, y, z) for y, z in yz_points] + [App.Vector(x0, yz_points[0][0], yz_points[0][1])]))
        return face.extrude(App.Vector(w, 0, 0))

    top_counterweight = extrude_profile(
        x,
        width,
        [
            (29.8, 22.0),
            (42.8, 22.0),
            (44.4, 24.4),
            (44.4, 27.2),
            (31.0, 27.2),
            (29.8, 24.9),
        ],
    )
    top_beam = extrude_profile(
        x + 1.0,
        width - 2.0,
        [
            (21.2, 19.4),
            (46.8, 19.4),
            (49.0, 20.0),
            (49.0, 21.4),
            (21.2, 22.0),
        ],
    )
    front_bridge = extrude_profile(
        x + 5.2,
        width - 10.4,
        [
            (18.8, 18.6),
            (22.6, 18.6),
            (23.6, 19.4),
            (18.8, 19.4),
        ],
    )
    center_leg = extrude_profile(
        x + 10.1,
        7.8,
        [
            (33.0, 19.4),
            (36.2, 19.4),
            (36.6, 16.2),
            (36.4, 9.0),
            (33.2, 9.0),
        ],
    )
    rear_hook = extrude_profile(
        x + 9.9,
        8.2,
        [
            (46.8, 18.6),
            (49.2, 18.6),
            (52.2, 17.4),
            (52.2, 9.2),
            (50.0, 9.2),
            (50.0, 12.8),
            (49.0, 12.8),
            (49.0, 16.4),
            (46.8, 16.4),
        ],
    )
    shape = top_counterweight.fuse(top_beam).fuse(front_bridge).fuse(center_leg).fuse(rear_hook)
    return chamfer_shape(
        shape,
        0.9,
        lambda edge: (
            edge.BoundBox.XLength < width * 0.42
            and (
                edge.BoundBox.ZLength > 1.3
                or edge.BoundBox.YLength > 1.3
            )
        ),
    )


def front_support_pad_shape():
    pad = Part.makeCylinder(
        P["front_pad_radius"],
        P["front_pad_length"],
        App.Vector(52.0, -0.45, 1.25),
        App.Vector(1, 0, 0),
    )
    cap = Part.makeBox(P["front_pad_length"], 0.95, 0.8, App.Vector(52.0, 0.10, 1.05))
    return pad.fuse(cap)


def make_doc():
    doc = App.newDocument(DOC_NAME)
    App.setActiveDocument(DOC_NAME)

    aluminum = (0.72, 0.72, 0.70)
    black = (0.02, 0.02, 0.02)
    dark = (0.08, 0.08, 0.08)
    rubber = (0.01, 0.01, 0.01)
    pcb = (0.02, 0.10, 0.06)
    component = (0.12, 0.12, 0.12)
    amber = (0.90, 0.55, 0.18)
    glass = (0.05, 0.07, 0.08)

    add_obj(doc, "front_body_shell_faceted_130x34x18", faceted_body(), aluminum)

    # Front visual/service features. These are separate shallow solids so the
    # STEP remains easy to edit in downstream CAD. Their chamfered outlines
    # follow the A-render front: separate black panels, not a continuous strip.
    front_rounded_panel("left_oled_glass_window_29x16_rounded", 9.2, z_from_top(9.2, 14.6), 28.6, 14.6, 0.24, 1.35, black)
    box("left_oled_module_keepout_28x28", 8.8, 1.2, z_from_top(2.0, 28.0), 28.0, 1.5, 28.0, glass, 55)

    front_rounded_panel("center_camera_black_island_flush_38x16_rounded", 46.2, z_from_top(8.3, 16.4), 38.2, 16.4, 0.24, 1.35, black)
    box("camera_module_14x14_flush_keepout", 53.5, 1.0, z_from_top(9.1, 14.0), 14.0, 3.0, 14.0, component)
    cylinder_y("camera_lens_outer_bezel_flush_front", 65.0, -1.15, 17.0, 5.2, 1.0, dark)
    cylinder_y("camera_lens_mid_ring_flush_front", 65.0, -1.75, 17.0, 4.0, 0.72, black)
    cylinder_y("camera_lens_inner_glass_flush_front", 65.0, -2.2, 17.0, 2.7, 0.72, black)
    cylinder_y("camera_status_dot_flush", 77.2, -0.82, 17.0, 0.72, 0.34, dark)

    front_rounded_panel("right_service_black_panel_balancer_rounded", 103.4, z_from_top(9.3, 14.8), 22.8, 14.8, 0.24, 1.15, black)
    front_rounded_panel("right_tact_button_cap_flat", 111.5, z_from_top(12.8, 5.4), 7.3, 5.4, 0.48, 1.0, dark)
    cylinder_y("right_button_center_dot", 115.2, -0.9, z_from_top(15.1, 0.8), 0.42, 0.35, black)
    cylinder_y("right_service_status_dot", 106.4, -0.85, z_from_top(15.3, 0.8), 0.36, 0.32, black)
    for ix in range(4):
        for iz in range(5):
            cylinder_y(
                f"right_front_micro_sound_hole_{ix + 1}_{iz + 1}",
                119.8 + ix * 1.45,
                -0.72,
                11.5 + iz * 1.52,
                0.40,
                0.34,
                black,
            )

    # Right end service features, placed on the end cap side.
    box("right_usb_c_service_opening_upper", 128.8, 6.6, 20.0, 1.2, 7.0, 4.0, black)
    box("right_usb_c_service_opening_lower", 128.8, 6.6, 10.8, 1.2, 7.0, 4.0, black)
    for ix in range(4):
        for iz in range(5):
            cylinder_x(
                f"right_end_micro_sound_hole_{ix + 1}_{iz + 1}",
                130.2,
                11.4 + ix * 2.1,
                8.8 + iz * 2.15,
                0.48,
                0.8,
                black,
            )

    # Internal layout blocks from tech1-layout.json.
    box("dev_board_keepout_57x28", 60.0, 5.0, z_from_top(1.5, 28.0), 57.0, 1.6, 28.0, pcb, 20)
    cylinder_y("speaker_or_buzzer_keepout_dia12_lower", 108.0, 10.0, 8.0, 6.0, 4.0, component)
    box("usb_cable_clearance_lower_right", 116.0, 7.0, 3.0, 10.0, 8.0, 6.0, amber, 35)

    # Monitor-lamp style support. No spring clamp: front pad + rear arm + rear pad.
    add_obj(doc, "rear_support_arm_hook_monitor_lamp", rear_support_arm_shape(), dark)
    box("rear_rubber_pad_soft_contact", 61.0, 51.2, 12.4, 6.0, 0.7, 4.6, rubber)
    add_obj(doc, "front_rubber_pad_small_support", front_support_pad_shape(), rubber)

    # Thin monitor-thickness reference gauges, separated from product solids.
    box("monitor_thickness_reference_8mm", 88.0, P["body_depth"] + 8.0, -1.5, 2.0, 0.5, 31.0, (0.2, 0.2, 0.2), 70)
    box("monitor_thickness_reference_20mm", 92.0, P["body_depth"] + 20.0, -1.5, 2.0, 0.5, 31.0, (0.2, 0.2, 0.2), 70)

    # Add a small hidden parameter note object for CAD users.
    note = doc.addObject("App::Annotation", "parameter_summary")
    note.LabelText = (
        "Tech1 structure draft: body 130x34x18 mm, rear support 36 mm, "
        "monitor target 8-20 mm, camera flush with front face."
    )
    note.Position = App.Vector(0, -12, 42)

    doc.recompute()
    return doc


def export(doc):
    os.makedirs(EXPORT_DIR, exist_ok=True)
    fcstd = os.path.join(EXPORT_DIR, FCSTD_PREFIX)
    step = os.path.join(EXPORT_DIR, "tech1-angular-a-structure-r2.step")
    for path in [fcstd, step]:
        if os.path.exists(path):
            try:
                os.remove(path)
            except OSError:
                pass
    try:
        doc.saveAs(fcstd)
    except OSError:
        staged = [
            os.path.join(EXPORT_DIR, name)
            for name in os.listdir(EXPORT_DIR)
            if name.startswith(FCSTD_PREFIX + ".")
        ]
        if not staged:
            raise
        staged.sort(key=os.path.getmtime, reverse=True)
        fcstd = staged[0]
    solids = []
    for obj in doc.Objects:
        if not hasattr(obj, "Shape") or obj.Shape.isNull():
            continue
        lowered = obj.Name.lower()
        if any(token in lowered for token in EXPORT_EXCLUDE_TOKENS):
            continue
        solids.append(obj)
    Import.export(solids, step)
    return fcstd, step


if __name__ == "__main__":
    doc = make_doc()
    fcstd, step = export(doc)
    print(f"Wrote {fcstd}")
    print(f"Wrote {step}")
