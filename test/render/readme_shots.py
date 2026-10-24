#!/usr/bin/env python3
"""Builds the README images (docs/screens/) from the reference screenshots in
golden/: the 466 px round one is cropped to a circle and the 480 px square one gets
the panel's rounded corners.

    readme_shots.py        (from test/render; after `make golden`)"""
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "docs", "screens")
ROUND = ["main_day", "poops_hungry", "feed_menu", "cta_evolve", "card_profile", "card_medals"]
SQUARE = ["main_night", "card_stats", "clock"]
CELL, GAP = 320, 24


def masked(path, radius):
    im = Image.open(path).convert("RGBA")
    m = Image.new("L", im.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, im.width - 1, im.height - 1), radius, fill=255)
    im.putalpha(m)
    return im.resize((CELL, CELL), Image.LANCZOS)


def sheet(names, folder, radius, cols):
    rows = (len(names) + cols - 1) // cols
    out = Image.new("RGBA", (cols * CELL + (cols - 1) * GAP, rows * CELL + (rows - 1) * GAP), (0, 0, 0, 0))
    for i, n in enumerate(names):
        im = masked(os.path.join(HERE, "golden", folder, n + ".png"), radius)
        out.paste(im, ((i % cols) * (CELL + GAP), (i // cols) * (CELL + GAP)), im)
    return out


os.makedirs(OUT, exist_ok=True)
sheet(ROUND, "en", 233, 3).save(os.path.join(OUT, "round.png"), optimize=True)
sheet(SQUARE, "sq480", 44, 3).save(os.path.join(OUT, "square.png"), optimize=True)
