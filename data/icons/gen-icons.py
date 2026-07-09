#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 VibeCodeBlogger
#
# Regenerate the hicolor icon set and the .ico from the 1024px master.
#
# This script is DEV-TIME ONLY and is NOT part of the meson build (it needs
# Pillow, which is not a build dependency). It exists purely for provenance and
# reproducibility: run it after editing the master to rebuild every raster size
# with identical resampling (Lanczos) and small-size sharpening (UnsharpMask on
# sizes <= 48px), so the committed PNGs are deterministic.
#
# Usage (from anywhere; paths are resolved relative to this file):
#   pip install Pillow
#   python3 data/icons/gen-icons.py
from __future__ import annotations
import os
from PIL import Image, ImageFilter

APPID = "io.github.vibecodeblogger.KeysAndClicksVisualizer"
SIZES = [16, 24, 32, 48, 64, 128, 256, 512]

# Paths relative to this script: SRC = master in the repo, OUT = data/icons.
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "master", f"{APPID}.png")
OUT = HERE

img = Image.open(SRC).convert("RGBA")
print(f"source: {img.size[0]}x{img.size[1]} mode after convert=RGBA")


def scaled(size: int) -> Image.Image:
    im = img.resize((size, size), Image.LANCZOS)
    # recover edge definition lost when a detailed icon is shrunk small
    if size <= 48:
        radius = 0.6 if size <= 24 else 0.8
        percent = 140 if size <= 24 else 110
        im = im.filter(ImageFilter.UnsharpMask(radius=radius, percent=percent, threshold=0))
    return im


made = []
for s in SIZES:
    d = os.path.join(OUT, "hicolor", f"{s}x{s}", "apps")
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, f"{APPID}.png")
    im = scaled(s)
    im.save(p, "PNG", optimize=True)
    made.append((s, p, os.path.getsize(p)))

# multi-size .ico for web/favicon/cross-platform (bonus, not installed)
ico_p = os.path.join(OUT, f"{APPID}.ico")
img.save(ico_p, sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

print("\n=== hicolor PNGs ===")
for s, p, sz in made:
    print(f"{s:>4}x{s:<4}  {sz / 1024:6.1f} KB")
print(f"\nico: {ico_p} ({os.path.getsize(ico_p) / 1024:.1f} KB)")
