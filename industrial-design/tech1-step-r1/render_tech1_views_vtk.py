import math
import os

import FreeCAD as App
import Import
import vtk


OUT_DIR = os.path.dirname(os.path.abspath(globals().get("__file__", os.getcwd())))
PROJECT_ROOT = os.path.dirname(os.path.dirname(OUT_DIR))
EXPORT_DIR = os.path.join(PROJECT_ROOT, "_cad-output")
FCSTD_PREFIX = "tech1-angular-a-structure-r2.FCStd"
FCSTD_PATH = os.path.join(EXPORT_DIR, FCSTD_PREFIX)
STEP_PATH = os.path.join(PROJECT_ROOT, "_cad-output", "tech1-angular-a-structure-r2.step")
MODEL_PREFIX = "tech1-angular-a-structure-r2.FCStd"
PNG_PREFIX = "tech1-angular-a-structure-r2-render"
RENDER_DIR = os.path.join(PROJECT_ROOT, "_render-raster")
WIDTH = 1800
HEIGHT = 1200
TESSELLATION = 0.45

EXCLUDE_TOKENS = (
    "keepout",
    "reference",
    "clearance",
    "parameter_summary",
    "annotation",
)

VIEW_SPECS = [
    ("front", (0.0, -1.0, 0.0), False),
    ("rear", (0.0, 1.0, 0.0), False),
    ("left", (-1.0, 0.0, 0.0), False),
    ("right", (1.0, 0.0, 0.0), False),
    ("top", (0.0, 0.0, 1.0), False),
    ("bottom", (0.0, 0.0, -1.0), False),
    ("axo_front_right", (1.0, -1.0, 0.8), True),
    ("axo_front_left", (-1.0, -1.0, 0.8), True),
    ("axo_rear_right", (1.0, 1.0, 0.8), True),
    ("axo_rear_left", (-1.0, 1.0, 0.8), True),
]


def resolve_model():
    if os.path.exists(FCSTD_PATH):
        return FCSTD_PATH
    staged = [
        os.path.join(EXPORT_DIR, name)
        for name in os.listdir(EXPORT_DIR)
        if name.startswith(FCSTD_PREFIX + ".")
    ]
    if staged:
        staged.sort(key=os.path.getmtime, reverse=True)
        return staged[0]
    if os.path.exists(STEP_PATH):
        return STEP_PATH
    direct = os.path.join(OUT_DIR, MODEL_PREFIX)
    if os.path.exists(direct):
        return direct
    staged = [
        os.path.join(OUT_DIR, name)
        for name in os.listdir(OUT_DIR)
        if name.startswith(MODEL_PREFIX + ".")
    ]
    if not staged:
        raise FileNotFoundError(f"No FCStd matching {MODEL_PREFIX} was found")
    staged.sort(key=os.path.getmtime, reverse=True)
    return staged[0]


def classify_color(name):
    lowered = name.lower()
    if "body_shell" in lowered or "bevel" in lowered:
        return (0.77, 0.77, 0.75)
    if "rear_support" in lowered:
        return (0.14, 0.14, 0.15)
    if "rubber" in lowered:
        return (0.04, 0.04, 0.05)
    if any(
        token in lowered
        for token in (
            "black",
            "window",
            "island",
            "panel",
            "button",
            "camera_lens",
            "status_dot",
            "usb",
            "sound_hole",
        )
    ):
        return (0.10, 0.10, 0.11)
    if "camera_module" in lowered:
        return (0.18, 0.18, 0.19)
    return (0.78, 0.78, 0.76)


def normalize(vec):
    length = math.sqrt(sum(v * v for v in vec))
    return tuple(v / length for v in vec)


def build_polydata(shape, deflection):
    points, facets = shape.tessellate(deflection)
    vtk_points = vtk.vtkPoints()
    vtk_points.SetNumberOfPoints(len(points))
    for idx, point in enumerate(points):
        vtk_points.SetPoint(idx, point.x, point.y, point.z)

    polys = vtk.vtkCellArray()
    for facet in facets:
        if len(facet) < 3:
            continue
        tri = vtk.vtkTriangle()
        tri.GetPointIds().SetId(0, facet[0])
        tri.GetPointIds().SetId(1, facet[1])
        tri.GetPointIds().SetId(2, facet[2])
        polys.InsertNextCell(tri)

    polydata = vtk.vtkPolyData()
    polydata.SetPoints(vtk_points)
    polydata.SetPolys(polys)

    normals = vtk.vtkPolyDataNormals()
    normals.SetInputData(polydata)
    normals.SplittingOff()
    normals.AutoOrientNormalsOn()
    normals.ConsistencyOn()
    normals.ComputePointNormalsOn()
    normals.Update()
    return normals.GetOutput()


def make_actor(polydata, color, tag_text):
    mapper = vtk.vtkPolyDataMapper()
    mapper.SetInputData(polydata)
    actor = vtk.vtkActor()
    actor.SetMapper(mapper)
    prop = actor.GetProperty()
    prop.SetColor(*color)
    tag = tag_text.lower()
    if "body_shell" in tag:
        prop.SetInterpolationToPhong()
        prop.SetAmbient(0.10)
        prop.SetDiffuse(0.74)
        prop.SetSpecular(0.24)
        prop.SetSpecularPower(28.0)
    elif "rear_support" in tag or "rubber" in tag:
        prop.SetInterpolationToPhong()
        prop.SetAmbient(0.14)
        prop.SetDiffuse(0.50)
        prop.SetSpecular(0.12)
        prop.SetSpecularPower(18.0)
    else:
        prop.SetInterpolationToPhong()
        prop.SetAmbient(0.12)
        prop.SetDiffuse(0.44)
        prop.SetSpecular(0.52 if "black" in tag or "window" in tag or "island" in tag else 0.22)
        prop.SetSpecularPower(38.0)
    return actor


def collect_renderables(doc):
    actors = []
    bounds = [float("inf"), float("-inf"), float("inf"), float("-inf"), float("inf"), float("-inf")]
    for obj in doc.Objects:
        if not hasattr(obj, "Shape") or obj.Shape.isNull():
            continue
        lowered = f"{obj.Name} {getattr(obj, 'Label', '')}".lower()
        if any(token in lowered for token in EXCLUDE_TOKENS):
            continue
        box = obj.Shape.BoundBox
        values = (box.XMin, box.XMax, box.YMin, box.YMax, box.ZMin, box.ZMax)
        if not all(math.isfinite(value) for value in values):
            continue
        if max(abs(value) for value in values) > 10000:
            continue
        polydata = build_polydata(obj.Shape, TESSELLATION)
        actor = make_actor(polydata, classify_color(lowered), lowered)
        actors.append(actor)
        bounds[0] = min(bounds[0], box.XMin)
        bounds[1] = max(bounds[1], box.XMax)
        bounds[2] = min(bounds[2], box.YMin)
        bounds[3] = max(bounds[3], box.YMax)
        bounds[4] = min(bounds[4], box.ZMin)
        bounds[5] = max(bounds[5], box.ZMax)
    if not actors:
        raise RuntimeError("No renderable solids were collected from model")
    return actors, tuple(bounds)


def add_lights(renderer, center, span):
    positions = [
        (center[0] - span * 1.6, center[1] - span * 2.2, center[2] + span * 2.0),
        (center[0] + span * 1.8, center[1] - span * 1.0, center[2] + span * 1.6),
        (center[0], center[1] + span * 2.4, center[2] + span * 1.2),
        (center[0], center[1], center[2] + span * 3.0),
    ]
    intensities = [0.95, 0.62, 0.34, 0.24]
    for pos, intensity in zip(positions, intensities):
        light = vtk.vtkLight()
        light.SetLightTypeToSceneLight()
        light.SetPosition(*pos)
        light.SetFocalPoint(*center)
        light.SetIntensity(intensity)
        renderer.AddLight(light)


def configure_camera(renderer, bounds, direction, perspective):
    x_min, x_max, y_min, y_max, z_min, z_max = bounds
    center = (
        (x_min + x_max) / 2.0,
        (y_min + y_max) / 2.0,
        (z_min + z_max) / 2.0,
    )
    size_x = x_max - x_min
    size_y = y_max - y_min
    size_z = z_max - z_min
    radius = max(size_x, size_y, size_z)
    direction = normalize(direction)

    camera = renderer.GetActiveCamera()
    camera.SetFocalPoint(*center)
    camera.SetPosition(
        center[0] + direction[0] * radius * 4.0,
        center[1] + direction[1] * radius * 4.0,
        center[2] + direction[2] * radius * 4.0,
    )

    view_up = (0.0, 0.0, 1.0)
    if abs(direction[2]) > 0.92:
        view_up = (0.0, -1.0 if direction[2] > 0 else 1.0, 0.0)
    camera.SetViewUp(*view_up)

    if perspective:
        camera.ParallelProjectionOff()
        camera.SetViewAngle(18.0)
    else:
        camera.ParallelProjectionOn()
        camera.SetParallelScale(radius * 0.28)
    renderer.ResetCameraClippingRange()
    return center, radius


def write_png(window, path):
    capture = vtk.vtkWindowToImageFilter()
    capture.SetInput(window)
    capture.SetScale(1)
    capture.SetInputBufferTypeToRGBA()
    capture.ReadFrontBufferOff()
    capture.Update()

    writer = vtk.vtkPNGWriter()
    writer.SetFileName(path)
    writer.SetInputConnection(capture.GetOutputPort())
    writer.Write()


def open_model_doc(doc_path):
    if doc_path.lower().endswith((".step", ".stp")):
        doc = App.newDocument("tech1_step_render")
        Import.insert(doc_path, doc.Name)
        doc.recompute()
        return doc
    return App.openDocument(doc_path)


def render_views(doc_path):
    doc = open_model_doc(doc_path)
    try:
        os.makedirs(RENDER_DIR, exist_ok=True)
        actors, bounds = collect_renderables(doc)

        renderer = vtk.vtkRenderer()
        renderer.SetUseFXAA(True)
        renderer.GradientBackgroundOn()
        renderer.SetBackground(0.98, 0.98, 0.98)
        renderer.SetBackground2(0.91, 0.91, 0.91)

        for actor in actors:
            renderer.AddActor(actor)

        x_min, x_max, y_min, y_max, z_min, z_max = bounds
        center = ((x_min + x_max) / 2.0, (y_min + y_max) / 2.0, (z_min + z_max) / 2.0)
        span = max(x_max - x_min, y_max - y_min, z_max - z_min)
        add_lights(renderer, center, span)

        window = vtk.vtkRenderWindow()
        window.SetOffScreenRendering(1)
        window.SetSize(WIDTH, HEIGHT)
        window.AddRenderer(renderer)

        outputs = []
        for name, direction, perspective in VIEW_SPECS:
            configure_camera(renderer, bounds, direction, perspective)
            window.Render()
            target = os.path.join(RENDER_DIR, f"{PNG_PREFIX}-{name}.png")
            write_png(window, target)
            outputs.append(target)
        return outputs
    finally:
        try:
            App.closeDocument(doc.Name)
        except Exception:
            pass


if __name__ == "__main__":
    model = resolve_model()
    for path in render_views(model):
        print(path)
