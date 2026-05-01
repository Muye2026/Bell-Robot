#!/usr/bin/env python3
"""Generate Openverse candidate preview sheets for Bell Robot model curation.

The script intentionally produces review artifacts only. It does not copy files
into model/dataset and does not retrain or regenerate firmware model headers.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


USER_AGENT = "Bell-Robot candidate curation"
API_URL = "https://api.openverse.engineering/v1/images/"

SEATED_QUERIES = [
    "office worker sitting at desk computer",
    "person seated at desk laptop office",
    "woman sitting at desk laptop office",
    "man sitting at desk laptop office",
    "student sitting at desk laptop front",
    "person using laptop at desk facing camera",
    "employee working at computer desk",
    "person at workstation seated",
    "home office person sitting laptop desk",
    "person sitting at desk monitor office",
]

EMPTY_QUERIES = [
    "empty office desk monitor chair",
    "empty workstation desk computer chair",
    "unoccupied desk laptop office chair",
    "empty desk computer monitor office",
    "empty office chair desk computer",
    "empty home office desk laptop",
    "empty computer desk chair front view",
    "desk with monitor empty chair office",
]

SEATED_BAD = [
    "headshot",
    "portrait",
    "selfie",
    "close-up",
    "closeup",
    "face",
    "keyboard",
    "hands",
    "hand",
    "typing",
    "overhead",
    "top view",
    "flat lay",
    "from above",
    "conference",
    "meeting",
    "classroom",
    "lecture",
    "presentation",
    "podium",
    "group",
    "crowd",
    "team",
    "standing",
    "illustration",
    "drawing",
    "painting",
    "cartoon",
    "logo",
    "icon",
    "desk setup",
    "workstation setup",
    "computer lab",
]

EMPTY_BAD = [
    "person",
    "people",
    "man",
    "woman",
    "child",
    "boy",
    "girl",
    "hand",
    "hands",
    "selfie",
    "portrait",
    "face",
    "standing",
    "sitting",
    "occupied",
    "meeting",
    "conference",
    "classroom",
    "keyboard close",
    "close-up",
    "closeup",
    "top view",
    "overhead",
    "flat lay",
    "from above",
    "product",
    "setup",
    "illustration",
    "drawing",
    "painting",
    "cartoon",
]

SEATED_GOOD = [
    "desk",
    "laptop",
    "computer",
    "office",
    "working",
    "sitting",
    "seated",
    "worker",
    "student",
    "monitor",
    "workstation",
    "home office",
]

EMPTY_GOOD = [
    "empty",
    "unoccupied",
    "desk",
    "office",
    "computer",
    "monitor",
    "chair",
    "workstation",
    "laptop",
    "home office",
]


@dataclass
class Candidate:
    score: int
    query: str
    row: dict[str, object]
    image: Image.Image


def fetch(url: str, timeout: int = 35) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def openverse_search(query: str, page: int) -> list[dict[str, object]]:
    params = {
        "q": query,
        "license_type": "commercial",
        "source": "flickr,wikimedia",
        "extension": "jpg",
        "page_size": "20",
        "page": str(page),
    }
    url = API_URL + "?" + urllib.parse.urlencode(params)
    payload = json.loads(fetch(url).decode("utf-8"))
    return list(payload.get("results", []))


def text_for(row: dict[str, object]) -> str:
    fields = [str(row.get(key, "") or "") for key in ("title", "creator", "tags")]
    return " ".join(fields).lower()


def score_row(row: dict[str, object], mode: str) -> int:
    text = text_for(row)
    bad_words = SEATED_BAD if mode == "seated" else EMPTY_BAD
    good_words = SEATED_GOOD if mode == "seated" else EMPTY_GOOD

    if any(word in text for word in bad_words):
        return -999

    score = sum(3 for word in good_words if word in text)
    license_name = str(row.get("license", "") or "").lower()
    if license_name in {"by", "by-sa", "cc0", "pdm"}:
        score += 2
    if mode == "seated" and any(word in text for word in ("person", "man", "woman", "employee")):
        score += 4
    if mode == "seated" and any(word in text for word in ("front", "facing")):
        score += 4
    return score


def collect_candidates(mode: str, pages: int, limit: int) -> list[Candidate]:
    queries = SEATED_QUERIES if mode == "seated" else EMPTY_QUERIES
    rows: list[tuple[int, str, dict[str, object]]] = []
    seen: set[str] = set()

    for query in queries:
        for page in range(1, pages + 1):
            try:
                for row in openverse_search(query, page):
                    row_id = str(row.get("id") or row.get("url") or "")
                    if not row_id or row_id in seen:
                        continue
                    seen.add(row_id)
                    score = score_row(row, mode)
                    if score > 5:
                        rows.append((score, query, row))
                time.sleep(0.5)
            except Exception as exc:
                print(f"query failed: {query} page={page}: {exc}")
                time.sleep(1.0)

    candidates: list[Candidate] = []
    for score, query, row in sorted(rows, key=lambda item: item[0], reverse=True):
        thumb = str(row.get("thumbnail") or row.get("url") or "")
        if not thumb:
            continue
        try:
            image = Image.open(BytesIO(fetch(thumb, timeout=25))).convert("RGB")
            if image.width >= 120 and image.height >= 100:
                candidates.append(Candidate(score, query, row, image))
            time.sleep(0.12)
            if len(candidates) >= limit:
                break
        except Exception:
            continue
    return candidates


def write_preview(candidates: list[Candidate], output: Path, mode: str) -> None:
    card_w, card_h, cols = 300, 270, 4
    rows = max(1, math.ceil(len(candidates) / cols))
    sheet = Image.new("RGB", (cols * card_w, rows * card_h), "white")
    draw = ImageDraw.Draw(sheet)

    try:
        font = ImageFont.truetype("arial.ttf", 13)
        bold = ImageFont.truetype("arialbd.ttf", 15)
    except Exception:
        font = ImageFont.load_default()
        bold = font

    title = "SEATED candidate" if mode == "seated" else "EMPTY candidate"
    for index, candidate in enumerate(candidates):
        x = (index % cols) * card_w
        y = (index // cols) * card_h
        scale = min(280 / candidate.image.width, 168 / candidate.image.height)
        width = max(1, int(candidate.image.width * scale))
        height = max(1, int(candidate.image.height * scale))
        resized = candidate.image.resize((width, height), Image.Resampling.LANCZOS)
        sheet.paste(resized, (x + 10, y + 8))

        row = candidate.row
        image_title = " ".join(str(row.get("title") or "").split())[:42]
        source = f"{row.get('source') or ''} / {row.get('license') or ''}"
        draw.text((x + 10, y + 184), title, fill=(0, 120, 60), font=bold)
        draw.text((x + 10, y + 205), image_title, fill=(0, 0, 0), font=font)
        draw.text((x + 10, y + 226), source, fill=(50, 80, 140), font=font)
        draw.text((x + 10, y + 245), candidate.query[:44], fill=(80, 80, 80), font=font)

    sheet.save(output, quality=90)


def write_csv(candidates: list[Candidate], output: Path) -> None:
    with output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "score",
                "query",
                "title",
                "source",
                "license",
                "license_url",
                "foreign_landing_url",
                "thumbnail",
                "url",
            ]
        )
        for candidate in candidates:
            row = candidate.row
            writer.writerow(
                [
                    candidate.score,
                    candidate.query,
                    row.get("title"),
                    row.get("source"),
                    row.get("license"),
                    row.get("license_url"),
                    row.get("foreign_landing_url"),
                    row.get("thumbnail"),
                    row.get("url"),
                ]
            )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("seated", "empty"), required=True)
    parser.add_argument("--out-dir", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--tag", default="next")
    parser.add_argument("--pages", type=int, default=3)
    parser.add_argument("--limit", type=int, default=32)
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    candidates = collect_candidates(args.mode, args.pages, args.limit)
    preview = args.out_dir / f"candidate_overview_openverse_{args.mode}_{args.tag}.jpg"
    metadata = args.out_dir / f"openverse_{args.mode}_candidates_{args.tag}.csv"
    write_preview(candidates, preview, args.mode)
    write_csv(candidates, metadata)
    print(f"preview={preview}")
    print(f"metadata={metadata}")
    print(f"count={len(candidates)}")


if __name__ == "__main__":
    main()
