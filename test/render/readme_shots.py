#!/usr/bin/env python3
"""Arma las imagenes del README (docs/screens/) con las capturas de referencia de
golden/: la redonda de 466 px se recorta en circulo y la cuadrada de 480 px lleva
las esquinas redondeadas del panel.

    readme_shots.py        (desde test/render; despues de `make golden`)"""
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
