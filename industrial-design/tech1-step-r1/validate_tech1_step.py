import os

import FreeCAD as App
import Import


OUT_DIR = os.path.dirname(os.path.abspath(globals().get("__file__", os.getcwd())))
PROJECT_ROOT = os.path.dirname(os.path.dirname(OUT_DIR))
EXPORT_DIR = os.path.join(PROJECT_ROOT, "_cad-output")
FCSTD_PREFIX = "tech1-angular-a-structure-r2.FCStd"
FCSTD_PATH = os.path.join(EXPORT_DIR, FCSTD_PREFIX)
STEP_PATH = os.path.join(PROJECT_ROOT, "_cad-output", "tech1-angular-a-structure-r2.step")


def bbox_of(objects):
    bb = None
    for obj in objects:
        if not hasattr(obj, "Shape") or obj.Shape.isNull():
            continue
        b = obj.Shape.BoundBox
        if max(b.XLength, b.YLength, b.ZLength) > 1000:
            continue
        if bb is None:
            bb = App.BoundBox(b)
        else:
            bb.add(b)
    return bb


def main():
    fcstd_path = FCSTD_PATH
    if not os.path.exists(fcstd_path):
        staged = [
            os.path.join(EXPORT_DIR, name)
            for name in os.listdir(EXPORT_DIR)
            if name.startswith(FCSTD_PREFIX + ".")
        ]
        if staged:
            staged.sort(key=os.path.getmtime, reverse=True)
            fcstd_path = staged[0]

    print("Checking files")
    for path in [fcstd_path, STEP_PATH]:
        if not os.path.exists(path) or os.path.getsize(path) == 0:
            raise RuntimeError(f"Missing or empty file: {path}")
        print(f"{os.path.basename(path)}: {os.path.getsize(path)} bytes")

    print("Opening FCStd")
    doc = App.openDocument(fcstd_path)
    body = doc.getObject("front_body_shell_faceted_130x34x18")
    rear = doc.getObject("rear_support_arm_hook_monitor_lamp")
    if body is None or rear is None:
        raise RuntimeError("Expected named solids were not found in FCStd")
    b = body.Shape.BoundBox
    r = rear.Shape.BoundBox
    rear_extension = r.YMax - b.YMax
    print(f"body_bbox_mm: X={b.XLength:.2f}, Y={b.YLength:.2f}, Z={b.ZLength:.2f}")
    print(f"rear_support_extension_mm: {rear_extension:.2f}")
    if abs(b.XLength - 130.0) > 0.1 or abs(b.YLength - 18.0) > 0.1 or abs(b.ZLength - 34.0) > 0.1:
        raise RuntimeError("Body bounding box does not match 130 x 18 x 34 mm")
    if not (32.0 <= rear_extension <= 38.0):
        raise RuntimeError("Rear support extension is outside 32-38 mm")

    print("Importing STEP")
    step_doc = App.newDocument("step_import_check")
    Import.insert(STEP_PATH, step_doc.Name)
    step_doc.recompute()
    solids = [obj for obj in step_doc.Objects if hasattr(obj, "Shape") and not obj.Shape.isNull()]
    if not solids:
        raise RuntimeError("STEP imported no solids")
    sane_solids = []
    for obj in solids:
        b = obj.Shape.BoundBox
        if max(b.XLength, b.YLength, b.ZLength) <= 1000:
            sane_solids.append(obj)
    sb = bbox_of(sane_solids)
    if sb is None:
        raise RuntimeError("STEP imported solids, but no sane bounding boxes were found")
    print(f"step_bbox_mm: X={sb.XLength:.2f}, Y={sb.YLength:.2f}, Z={sb.ZLength:.2f}")
    print(f"step_solids: {len(solids)} imported, {len(sane_solids)} with sane bounds")
    print("Validation passed")


if __name__ == "__main__":
    main()
