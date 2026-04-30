#!/usr/bin/env python3
"""Bootstrap a small seated/absent dataset from phone screenshots.

This is a seed-data helper for the Bell Robot desk-front seated detector.
It extracts the real camera preview from browser screenshots, saves several
augmented seated frames, and synthesizes absent frames by removing the person
region with simple background interpolation. The result is only a first-pass
dataset; later rounds should still use /label direct captures from the device.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageChops, ImageEnhance, ImageFilter, ImageOps


FRAME_SIZE = (320, 240)
PREVIEW_THRESHOLD = 24
ROW_MIN_RATIO = 0.33
COL_MIN_RATIO = 0.38


def group_ranges(indices: list[int]) -> list[tuple[int, int]]:
    if not indices:
        return []
    groups: list[tuple[int, int]] = []
    start = prev = indices[0]
    for value in indices[1:]:
        if value == prev + 1:
            prev = value
            continue
        groups.append((start, prev + 1))
        start = prev = value
    groups.append((start, prev + 1))
    return groups


def find_preview_rect(gray: Image.Image) -> tuple[int, int, int, int]:
    width, height = gray.size
    pixels = gray.load()

    y_start = max(0, height // 8)
    y_end = min(height, height * 3 // 4)
    row_hits: list[int] = []
    for y in range(y_start, y_end):
        bright = 0
        for x in range(width):
            if pixels[x, y] > PREVIEW_THRESHOLD:
                bright += 1
        if bright >= int(width * ROW_MIN_RATIO):
            row_hits.append(y)
    row_groups = group_ranges(row_hits)
    if not row_groups:
        raise ValueError("failed to locate preview rows")

    row_group = max(row_groups, key=lambda item: item[1] - item[0])
    top, bottom = row_group
    region_height = bottom - top

    col_hits: list[int] = []
    for x in range(width):
        bright = 0
        for y in range(top, bottom):
            if pixels[x, y] > PREVIEW_THRESHOLD:
                bright += 1
        if bright >= int(region_height * COL_MIN_RATIO):
            col_hits.append(x)
    col_groups = group_ranges(col_hits)
    if not col_groups:
        raise ValueError("failed to locate preview cols")

    left, right = max(col_groups, key=lambda item: item[1] - item[0])
    crop = gray.crop((left, top, right, bottom))
    mask = crop.point(lambda value: 255 if value > PREVIEW_THRESHOLD else 0)
    bbox = mask.getbbox()
    if bbox is None:
        raise ValueError("preview crop bbox is empty")
    trim_left, trim_top, trim_right, trim_bottom = bbox
    return (
        left + trim_left,
        top + trim_top,
        left + trim_right,
        top + trim_bottom,
    )


def extract_preview(path: Path) -> Image.Image:
    gray = Image.open(path).convert("L")
    left, top, right, bottom = find_preview_rect(gray)
    crop = gray.crop((left, top, right, bottom))
    return crop.resize(FRAME_SIZE, Image.Resampling.BILINEAR)


def apply_shift(image: Image.Image, dx: int, dy: int) -> Image.Image:
    shifted = ImageChops.offset(image, dx, dy)
    if dx > 0:
        shifted.paste(image.crop((0, 0, dx, image.height)), (0, 0))
    elif dx < 0:
        shifted.paste(image.crop((image.width + dx, 0, image.width, image.height)), (image.width + dx, 0))
    if dy > 0:
        shifted.paste(image.crop((0, 0, image.width, dy)), (0, 0))
    elif dy < 0:
        shifted.paste(image.crop((0, image.height + dy, image.width, image.height)), (0, image.height + dy))
    return shifted


def remove_person_horizontal(image: Image.Image, box: tuple[float, float, float, float]) -> Image.Image:
    frame = image.copy()
    pixels = frame.load()
    width, height = frame.size
    left = max(1, int(width * box[0]))
    top = max(1, int(height * box[1]))
    right = min(width - 2, int(width * box[2]))
    bottom = min(height - 2, int(height * box[3]))

    for y in range(top, bottom):
        left_value = pixels[left - 1, y]
        right_value = pixels[right + 1, y]
        span = max(1, right - left)
        for x in range(left, right):
            alpha = (x - left) / span
            pixels[x, y] = int(left_value * (1.0 - alpha) + right_value * alpha)
    return frame.filter(ImageFilter.BoxBlur(2))


def remove_person_vertical(image: Image.Image, box: tuple[float, float, float, float]) -> Image.Image:
    frame = image.copy()
    pixels = frame.load()
    width, height = frame.size
    left = max(1, int(width * box[0]))
    top = max(1, int(height * box[1]))
    right = min(width - 2, int(width * box[2]))
    bottom = min(height - 2, int(height * box[3]))

    for x in range(left, right):
        top_value = pixels[x, top - 1]
        bottom_value = pixels[x, bottom + 1]
        span = max(1, bottom - top)
        for y in range(top, bottom):
            alpha = (y - top) / span
            pixels[x, y] = int(top_value * (1.0 - alpha) + bottom_value * alpha)
    return frame.filter(ImageFilter.BoxBlur(2))


def seated_variants(frame: Image.Image) -> list[Image.Image]:
    variants = [frame]

    shifted_a = apply_shift(frame, 8, -4)
    shifted_b = apply_shift(frame, -10, 6)
    bright = ImageEnhance.Brightness(frame).enhance(1.08)
    bright = ImageEnhance.Contrast(bright).enhance(0.92)
    dark = ImageEnhance.Brightness(frame).enhance(0.92)
    dark = ImageEnhance.Contrast(dark).enhance(1.10)
    dark = dark.filter(ImageFilter.GaussianBlur(0.8))

    variants.extend([shifted_a, shifted_b, bright, dark, ImageOps.mirror(frame)])
    return variants


def absent_variants(frame: Image.Image) -> list[Image.Image]:
    boxes = [
        (0.10, 0.04, 0.86, 0.96),
        (0.18, 0.06, 0.78, 0.92),
        (0.06, 0.08, 0.70, 0.96),
    ]
    variants = [
        remove_person_horizontal(frame, boxes[0]),
        remove_person_horizontal(frame, boxes[1]),
        remove_person_vertical(frame, boxes[2]),
    ]

    bright = ImageEnhance.Brightness(variants[0]).enhance(1.05)
    dark = ImageEnhance.Brightness(variants[1]).enhance(0.94)
    dark = ImageEnhance.Contrast(dark).enhance(0.95)
    variants.extend([bright, dark])
    return variants


def write_pgm(image: Image.Image, path: Path) -> None:
    image.save(path, format="PPM")


def clear_generated(folder: Path) -> None:
    if not folder.exists():
        return
    for path in folder.glob("seed_*.pgm"):
        path.unlink()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("screenshots", nargs="+", type=Path)
    args = parser.parse_args()

    absent_dir = args.out / "absent"
    seated_dir = args.out / "seated"
    absent_dir.mkdir(parents=True, exist_ok=True)
    seated_dir.mkdir(parents=True, exist_ok=True)
    clear_generated(absent_dir)
    clear_generated(seated_dir)

    seated_count = 0
    absent_count = 0
    for source_index, path in enumerate(args.screenshots, start=1):
        frame = extract_preview(path)
        for variant_index, variant in enumerate(seated_variants(frame), start=1):
            write_pgm(variant, seated_dir / f"seed_{source_index:02d}_{variant_index:02d}.pgm")
            seated_count += 1
        for variant_index, variant in enumerate(absent_variants(frame), start=1):
            write_pgm(variant, absent_dir / f"seed_{source_index:02d}_{variant_index:02d}.pgm")
            absent_count += 1

    print(f"generated seated={seated_count} absent={absent_count} into {args.out}")


if __name__ == "__main__":
    main()
