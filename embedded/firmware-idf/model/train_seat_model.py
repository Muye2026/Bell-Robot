#!/usr/bin/env python3
"""Train the Bell Robot local seated-person classifier.

The firmware consumes a compact int8 logistic model over 8x8 grayscale ROI
features. The intended camera placement is on a monitor or desktop, so the
positive class is a frontal or front-oblique seated person at a desk.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import Iterable


ROI_X_PERCENT = 20
ROI_Y_PERCENT = 25
ROI_W_PERCENT = 60
ROI_H_PERCENT = 55
FEATURE_W = 8
FEATURE_H = 8
SCALE = 4096


def read_pgm(path: Path) -> tuple[int, int, bytes]:
    raw = path.read_bytes()
    if not raw.startswith(b"P5"):
        raise ValueError(f"{path} is not a binary PGM (P5) file")

    tokens: list[bytes] = []
    index = 2
    while len(tokens) < 3:
        while index < len(raw) and raw[index] in b" \t\r\n":
            index += 1
        if index < len(raw) and raw[index] == ord("#"):
            while index < len(raw) and raw[index] not in b"\r\n":
                index += 1
            continue
        start = index
        while index < len(raw) and raw[index] not in b" \t\r\n":
            index += 1
        tokens.append(raw[start:index])

    while index < len(raw) and raw[index] in b" \t\r\n":
        index += 1

    width = int(tokens[0])
    height = int(tokens[1])
    max_value = int(tokens[2])
    if max_value != 255:
      raise ValueError(f"{path} has unsupported max value {max_value}")

    pixels = raw[index:]
    expected = width * height
    if len(pixels) < expected:
        raise ValueError(f"{path} is truncated: {len(pixels)} < {expected}")
    return width, height, pixels[:expected]


def extract_features(path: Path) -> list[int]:
    width, height, pixels = read_pgm(path)
    if width == FEATURE_W and height == FEATURE_H:
        return [value - 128 for value in pixels]

    x0 = width * ROI_X_PERCENT // 100
    y0 = height * ROI_Y_PERCENT // 100
    roi_w = width * ROI_W_PERCENT // 100
    roi_h = height * ROI_H_PERCENT // 100

    cells: list[int] = []
    for gy in range(FEATURE_H):
        for gx in range(FEATURE_W):
            sx0 = x0 + gx * roi_w // FEATURE_W
            sy0 = y0 + gy * roi_h // FEATURE_H
            sx1 = x0 + (gx + 1) * roi_w // FEATURE_W
            sy1 = y0 + (gy + 1) * roi_h // FEATURE_H
            total = 0
            count = 0
            for y in range(sy0, sy1, 2):
                row = y * width
                for x in range(sx0, sx1, 2):
                    total += pixels[row + x]
                    count += 1
            mean = 0 if count == 0 else total // count
            cells.append(mean)

    global_mean = sum(cells) // len(cells)
    features = []
    for mean in cells:
        centered = (mean - global_mean) * 2
        centered = max(-128, min(127, centered))
        features.append(centered)
    return features


def list_samples(dataset: Path) -> list[tuple[list[int], int, Path]]:
    samples: list[tuple[list[int], int, Path]] = []
    for label_name, label in (("absent", 0), ("empty", 0), ("seated", 1), ("occupied", 1)):
        folder = dataset / label_name
        if folder.exists():
            for path in sorted(folder.glob("*.pgm")):
                samples.append((extract_features(path), label, path))
    if not samples:
        raise ValueError(f"no .pgm samples found under {dataset}")
    labels = {label for _, label, _ in samples}
    if labels != {0, 1}:
        raise ValueError("dataset must contain both absent/seated samples")
    return samples


def sigmoid(value: float) -> float:
    if value >= 0:
        z = math.exp(-value)
        return 1.0 / (1.0 + z)
    z = math.exp(value)
    return z / (1.0 + z)


def train(samples: list[tuple[list[int], int, Path]], epochs: int, lr: float, l2: float) -> tuple[list[float], float]:
    weights = [0.0] * (FEATURE_W * FEATURE_H)
    bias = 0.0
    n = float(len(samples))

    for _ in range(epochs):
        grad_w = [0.0] * len(weights)
        grad_b = 0.0
        for features, label, _ in samples:
            logit = bias + sum(w * x for w, x in zip(weights, features)) / 128.0
            err = sigmoid(logit) - label
            for i, x in enumerate(features):
                grad_w[i] += err * x / 128.0
            grad_b += err

        for i in range(len(weights)):
            grad_w[i] = grad_w[i] / n + l2 * weights[i]
            weights[i] -= lr * grad_w[i]
        bias -= lr * grad_b / n

    return weights, bias


def evaluate(samples: list[tuple[list[int], int, Path]], weights: list[float], bias: float, threshold: float) -> str:
    tp = tn = fp = fn = 0
    for features, label, _ in samples:
        prob = sigmoid(bias + sum(w * x for w, x in zip(weights, features)) / 128.0)
        pred = 1 if prob >= threshold else 0
        if pred == 1 and label == 1:
            tp += 1
        elif pred == 0 and label == 0:
            tn += 1
        elif pred == 1:
            fp += 1
        else:
            fn += 1
    return f"tp={tp} tn={tn} fp={fp} fn={fn}"


def quantize(weights: list[float], bias: float) -> tuple[list[int], int]:
    q_weights = [max(-127, min(127, round(w * SCALE / 128.0))) for w in weights]
    q_bias = round(bias * SCALE)
    return q_weights, q_bias


def format_array(values: Iterable[int]) -> str:
    items = list(values)
    lines = []
    for i in range(0, len(items), 8):
        lines.append("    " + ", ".join(f"{value:4d}" for value in items[i : i + 8]) + ",")
    return "\n".join(lines)


def write_header(path: Path, weights: list[int], bias: int) -> None:
    content = f"""#pragma once

#include <stdint.h>

// Generated by model/train_seat_model.py.
static constexpr bool kSeatModelTrained = true;
static constexpr uint32_t kSeatModelVersion = 1;
static constexpr uint32_t kSeatModelFeatureCount = {len(weights)};
static constexpr int32_t kSeatModelBias = {bias};
static constexpr int8_t kSeatModelWeights[kSeatModelFeatureCount] = {{
{format_array(weights)}
}};
"""
    path.write_text(content, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=1200)
    parser.add_argument("--lr", type=float, default=0.08)
    parser.add_argument("--l2", type=float, default=0.0005)
    parser.add_argument("--threshold", type=float, default=0.65)
    args = parser.parse_args()

    samples = list_samples(args.dataset)
    weights, bias = train(samples, args.epochs, args.lr, args.l2)
    q_weights, q_bias = quantize(weights, bias)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_header(args.out, q_weights, q_bias)
    print(f"samples={len(samples)} {evaluate(samples, weights, bias, args.threshold)}")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
