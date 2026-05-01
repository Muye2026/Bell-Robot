#!/usr/bin/env python3
"""Import confirmed Openverse preview candidates into the local PGM dataset."""

from __future__ import annotations

import argparse
import csv
import urllib.request
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageEnhance, ImageOps


FRAME_SIZE = (320, 240)
USER_AGENT = "Bell-Robot confirmed Openverse importer"


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=45) as response:
        return response.read()


def fit_frame(image: Image.Image) -> Image.Image:
    image = image.convert("L")
    return ImageOps.fit(image, FRAME_SIZE, method=Image.Resampling.LANCZOS, centering=(0.5, 0.5))


def variants(image: Image.Image) -> list[Image.Image]:
    base = fit_frame(image)
    return [
        base,
        ImageOps.mirror(base),
        ImageEnhance.Contrast(base).enhance(1.22),
        ImageEnhance.Brightness(base).enhance(0.82),
    ]


def write_pgm(path: Path, image: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)


def import_csv(csv_path: Path, out_dir: Path, prefix: str) -> int:
    count = 0
    with csv_path.open(encoding="utf-8", newline="") as handle:
        for index, row in enumerate(csv.DictReader(handle), start=1):
            url = row.get("url") or row.get("thumbnail")
            if not url:
                continue
            try:
                image = Image.open(BytesIO(fetch(url))).convert("RGB")
            except Exception as exc:
                print(f"skip {index}: {exc}")
                continue

            for variant_index, variant in enumerate(variants(image), start=1):
                name = f"{prefix}_{index:02d}_{variant_index:02d}.pgm"
                write_pgm(out_dir / name, variant)
                count += 1
    return count


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seated-csv", type=Path, required=True)
    parser.add_argument("--empty-csv", type=Path, required=True)
    parser.add_argument("--dataset", type=Path, required=True)
    args = parser.parse_args()

    seated_count = import_csv(args.seated_csv, args.dataset / "seated", "openverse_confirmed_seated")
    empty_count = import_csv(args.empty_csv, args.dataset / "absent", "openverse_confirmed_empty")
    print(f"seated={seated_count} absent={empty_count}")


if __name__ == "__main__":
    main()
