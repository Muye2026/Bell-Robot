#!/usr/bin/env python3
"""Import user-confirmed preview-sheet cards into the PGM dataset.

This is a fallback when the original Openverse/Flickr image URLs are rate
limited. It crops the image area from the already-reviewed contact sheets.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageEnhance, ImageOps


FRAME_SIZE = (320, 240)


def fit_frame(image: Image.Image) -> Image.Image:
    return ImageOps.fit(image.convert("L"), FRAME_SIZE, method=Image.Resampling.LANCZOS, centering=(0.5, 0.5))


def variants(image: Image.Image) -> list[Image.Image]:
    base = fit_frame(image)
    return [
        base,
        ImageOps.mirror(base),
        ImageEnhance.Contrast(base).enhance(1.22),
        ImageEnhance.Brightness(base).enhance(0.82),
    ]


def crop_cards(
    sheet_path: Path,
    out_dir: Path,
    prefix: str,
    card_width: int,
    card_height: int,
    image_width: int,
    image_height: int,
) -> int:
    sheet = Image.open(sheet_path).convert("RGB")
    cols = max(1, sheet.width // card_width)
    rows = max(1, sheet.height // card_height)
    count = 0
    out_dir.mkdir(parents=True, exist_ok=True)

    for row in range(rows):
        for col in range(cols):
            x = col * card_width + 10
            y = row * card_height + 8
            crop = sheet.crop((x, y, x + image_width, y + image_height))
            if crop.getbbox() is None:
                continue
            # Skip empty cells from the final row.
            gray = crop.convert("L")
            if sum(gray.histogram()[245:]) > int(crop.width * crop.height * 0.92):
                continue
            count += 1
            for variant_index, image in enumerate(variants(crop), start=1):
                image.save(out_dir / f"{prefix}_{count:02d}_{variant_index:02d}.pgm")
    return count


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--seated-sheet", type=Path, required=True)
    parser.add_argument("--empty-sheet", type=Path, required=True)
    args = parser.parse_args()

    seated = crop_cards(
        args.seated_sheet,
        args.dataset / "seated",
        "openverse_confirmed_sheet_seated",
        card_width=320,
        card_height=280,
        image_width=300,
        image_height=178,
    )
    absent = crop_cards(
        args.empty_sheet,
        args.dataset / "absent",
        "openverse_confirmed_sheet_empty",
        card_width=300,
        card_height=270,
        image_width=280,
        image_height=168,
    )
    print(f"seated_cards={seated} absent_cards={absent}")


if __name__ == "__main__":
    main()
