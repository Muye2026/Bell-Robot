import os

from PIL import Image, ImageDraw, ImageOps


OUT_DIR = os.path.dirname(os.path.abspath(globals().get("__file__", os.getcwd())))
PROJECT_ROOT = os.path.dirname(os.path.dirname(OUT_DIR))
RENDER_DIR = os.path.join(PROJECT_ROOT, "_render-raster")
CONCEPT_DIR = os.path.join(PROJECT_ROOT, "industrial-design", "concept-r3")

REFERENCE_IMAGE = os.environ.get("TECH1_REFERENCE_IMAGE") or os.path.join(CONCEPT_DIR, "01-tech1-angular-a.png")
RENDER_PREFIX = "tech1-angular-a-structure-r2-render"
SHEET_COMPARE = os.path.join(RENDER_DIR, "tech1-angular-a-review-reference-vs-model.png")
SHEET_VIEWS = os.path.join(RENDER_DIR, "tech1-angular-a-review-views.png")

AXO_IMAGE = os.path.join(RENDER_DIR, f"{RENDER_PREFIX}-axo_front_right.png")
VIEW_ORDER = [
    "front",
    "rear",
    "left",
    "right",
    "top",
    "bottom",
    "axo_front_right",
    "axo_front_left",
    "axo_rear_right",
    "axo_rear_left",
]


def fit_cover(image, size):
    return ImageOps.fit(image, size, method=Image.Resampling.LANCZOS)


def trim_background(image, threshold=248):
    rgb = image.convert("RGB")
    bbox = None
    pixels = rgb.load()
    for y in range(rgb.height):
        for x in range(rgb.width):
            r, g, b = pixels[x, y]
            if min(r, g, b) < threshold:
                if bbox is None:
                    bbox = [x, y, x, y]
                else:
                    bbox[0] = min(bbox[0], x)
                    bbox[1] = min(bbox[1], y)
                    bbox[2] = max(bbox[2], x)
                    bbox[3] = max(bbox[3], y)
    if bbox is None:
        return image
    pad = 28
    left = max(0, bbox[0] - pad)
    top = max(0, bbox[1] - pad)
    right = min(rgb.width, bbox[2] + pad)
    bottom = min(rgb.height, bbox[3] + pad)
    return image.crop((left, top, right, bottom))


def fit_contain(image, size):
    canvas = Image.new("RGB", size, (245, 245, 245))
    thumb = ImageOps.contain(image, size, method=Image.Resampling.LANCZOS)
    x = (size[0] - thumb.width) // 2
    y = (size[1] - thumb.height) // 2
    canvas.paste(thumb, (x, y))
    return canvas


def add_label(image, text):
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((18, 18, 220, 62), radius=12, fill=(255, 255, 255))
    draw.text((32, 30), text, fill=(30, 30, 30))


def compose_compare_sheet():
    reference = Image.open(REFERENCE_IMAGE).convert("RGB")
    axo = trim_background(Image.open(AXO_IMAGE).convert("RGB"))

    left = fit_cover(reference, (1500, 980))
    add_label(left, "Reference")
    right = fit_contain(axo, (1500, 980))
    add_label(right, "Current Model")

    sheet = Image.new("RGB", (3060, 1040), (235, 235, 235))
    sheet.paste(left, (20, 30))
    sheet.paste(right, (1540, 30))
    sheet.save(SHEET_COMPARE, quality=95)


def compose_views_sheet():
    card_size = (920, 520)
    cols = 2
    rows = 5
    sheet = Image.new("RGB", (1900, 2740), (235, 235, 235))

    for idx, view_name in enumerate(VIEW_ORDER):
        path = os.path.join(RENDER_DIR, f"{RENDER_PREFIX}-{view_name}.png")
        image = trim_background(Image.open(path).convert("RGB"))
        card = fit_contain(image, card_size)
        add_label(card, view_name)
        x = 30 + (idx % cols) * 940
        y = 30 + (idx // cols) * 540
        sheet.paste(card, (x, y))

    sheet.save(SHEET_VIEWS, quality=95)


if __name__ == "__main__":
    os.makedirs(RENDER_DIR, exist_ok=True)
    compose_compare_sheet()
    compose_views_sheet()
    print(SHEET_COMPARE)
    print(SHEET_VIEWS)
