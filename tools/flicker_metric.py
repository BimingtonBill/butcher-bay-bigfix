"""Flicker metric for a CaptureBurst series (consecutive frames).

A pixel "flickers" at frame i when its luma jumps away at i and comes back at
i+1 (|a-b| > T, |b-c| > T, |a-c| < T/2). Camera motion moves pixels in one
direction and is not counted. Prints the per-triple fraction and the mean, and
optionally writes a heatmap PNG of where flicker happened.

Usage: python -I tools/flicker_metric.py frame1.png frame2.png ... [--heatmap out.png] [--threshold 40]
"""
import argparse
import numpy as np
from PIL import Image


def luma(path):
    rgb = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    return rgb @ np.array([0.299, 0.587, 0.114], dtype=np.float32)


def flicker_masks(frames, threshold):
    for a, b, c in zip(frames, frames[1:], frames[2:]):
        yield (np.abs(a - b) > threshold) & (np.abs(b - c) > threshold) & (np.abs(a - c) < threshold / 2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("frames", nargs="+")
    parser.add_argument("--threshold", type=float, default=40.0)
    parser.add_argument("--heatmap")
    args = parser.parse_args()
    frames = [luma(p) for p in args.frames]
    masks = list(flicker_masks(frames, args.threshold))
    fractions = [m.mean() for m in masks]
    for i, f in enumerate(fractions):
        print(f"triple {i}: {f * 100:.3f}% pixels flicker")
    print(f"mean: {np.mean(fractions) * 100:.3f}%  max: {np.max(fractions) * 100:.3f}%")
    if args.heatmap:
        heat = np.sum(masks, axis=0).astype(np.float32)
        heat = (255 * heat / max(1.0, heat.max())).astype(np.uint8)
        Image.fromarray(heat).save(args.heatmap)


if __name__ == "__main__":
    main()
