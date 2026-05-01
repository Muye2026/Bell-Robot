#!/usr/bin/env python3
"""Import labeled Bell Robot browser screenshots into the PGM dataset."""

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

    top, bottom = max(row_groups, key=lambda item: item[1] - item[0])
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
    return left + trim_left, top + trim_top, left + trim_right, top + trim_bottom


def extract_preview(path: Path) -> Image.Image:
    gray = Image.open(path).convert("L")
    rect = find_preview_rect(gray)
    return gray.crop(rect).resize(FRAME_SIZE, Image.Resampling.BILINEAR)


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


def variants(frame: Image.Image) -> list[Image.Image]:
    shifted = [
        frame,
        apply_shift(frame, 6, -4),
        apply_shift(frame, -6, 4),
        apply_shift(frame, 10, 0),
        apply_shift(frame, -10, 0),
    ]
    output: list[Image.Image] = []
    for image in shifted:
        output.extend(
            [
                image,
                ImageEnhance.Brightness(image).enhance(0.86),
                ImageEnhance.Brightness(image).enhance(1.10),
                ImageEnhance.Contrast(image).enhance(1.18),
                ImageEnhance.Contrast(image).enhance(0.88),
                image.filter(ImageFilter.GaussianBlur(0.7)),
            ]
        )
    output.append(ImageOps.mirror(frame))
    output.append(ImageEnhance.Contrast(ImageOps.mirror(frame)).enhance(1.12))
    return output


def write_pgm(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PPM")


def clear_prefix(folder: Path, prefix: str) -> None:
    if not folder.exists():
        return
    for path in folder.glob(f"{prefix}_*.pgm"):
        path.unlink()


def import_one(dataset: Path, label: str, source: Path, prefix: str) -> int:
    folder_name = "absent" if label in {"absent", "empty"} else "seated"
    folder = dataset / folder_name
    clear_prefix(folder, prefix)
    frame = extract_preview(source)
    count = 0
    for index, image in enumerate(variants(frame), start=1):
        write_pgm(image, folder / f"{prefix}_{index:02d}.pgm")
        count += 1
    return count


def import_many(dataset: Path, label: str, sources: list[Path], prefix: str) -> int:
    total = 0
    for index, source in enumerate(sources, start=1):
        source_prefix = f"{prefix}_{label}"
        if len(sources) > 1:
            source_prefix = f"{source_prefix}_{index:02d}"
        total += import_one(dataset, label, source, source_prefix)
    return total


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--absent", type=Path, action="append", default=[])
    parser.add_argument("--seated", type=Path, action="append", default=[])
    parser.add_argument("--prefix", default="real_office_20260501")
    args = parser.parse_args()

    if not args.absent and not args.seated:
        parser.error("provide at least one --absent or --seated screenshot")

    absent_count = import_many(args.dataset, "absent", args.absent, args.prefix)
    seated_count = import_many(args.dataset, "seated", args.seated, args.prefix)
    print(f"imported absent={absent_count} seated={seated_count}")


if __name__ == "__main__":
    main()
