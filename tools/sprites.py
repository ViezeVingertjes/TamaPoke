#!/usr/bin/env python3
"""TamaPoke sprite workshop.

The 32x32 sprites are BUILT from primitives (ellipses with automatic shading
on the lower-right edge, automatic silhouette outline)
and pixel-level details (eyes, mouth, spots). The script validates, renders a
contact-sheet PNG for visual review, and emits the C arrays (UI icons, egg, heart, poop):

  python3 tools/sprites.py        # validates + renders tools/sheet.png
  python3 tools/sprites.py emit   # regenerates species.h
"""
from PIL import Image, ImageDraw

PALETTE = {
    'k': '#1b1b25',  # outline / eyes
    'w': '#ffffff',  # eye highlight
    'y': '#f8d990',  # cream (belly, shell plate)
    'Y': '#e0b860',  # cream shade
    'o': '#f5863d',  # orange
    'O': '#d65f28',  # orange shade
    'r': '#e8503a',  # red (flame, charmeleon)
    'R': '#b53224',  # red shade
    'f': '#ffd95e',  # flame yellow
    't': '#8fd6b4',  # mint (bulbasaur)
    'T': '#5fae8c',  # mint shade
    'g': '#58b868',  # bulb green
    'G': '#3c8a4c',  # bulb green shade
    'd': '#3f7e62',  # bulbasaur spots
    'p': '#f08aa4',  # flower pink
    'P': '#c75f80',  # pink shade
    'b': '#7cc4ea',  # blue (squirtle)
    'B': '#4f93c4',  # blue shade
    'N': '#3a6fa0',  # blastoise blue
    'M': '#2a5278',  # blastoise blue shade
    'c': '#b07a45',  # shell brown
    'C': '#7e5530',  # brown shade
    'l': '#9aa9e0',  # lavender (wartortle)
    'L': '#6f7cb8',  # lavender shade
    's': '#aab0bc',  # cannon gray
    'S': '#787e8c',  # gray shade
}

W = H = 32


class G:
    def __init__(self, size=W):
        self.size = size
        self.g = [['.'] * size for _ in range(size)]

    def disk(self, cx, cy, rx, ry, ch, shade=None):
        for y in range(self.size):
            for x in range(self.size):
                dx, dy = (x - cx) / rx, (y - cy) / ry
                n = dx * dx + dy * dy
                if n <= 1.0:
                    c = ch
                    if shade and n >= 0.45 and (dx + dy) > 0.42:
                        c = shade
                    self.g[y][x] = c

    def rect(self, x0, y0, x1, y1, ch):
        for y in range(max(0, y0), min(self.size, y1 + 1)):
            for x in range(max(0, x0), min(self.size, x1 + 1)):
                self.g[y][x] = ch

    def px(self, x, y, ch):
        if 0 <= x < self.size and 0 <= y < self.size:
            self.g[y][x] = ch

    def erase(self, cx, cy, rx, ry):
        for y in range(self.size):
            for x in range(self.size):
                dx, dy = (x - cx) / rx, (y - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.g[y][x] = '.'

    def outline(self):
        out = [row[:] for row in self.g]
        for y in range(self.size):
            for x in range(self.size):
                if self.g[y][x] == '.':
                    continue
                for nx, ny in ((x-1, y), (x+1, y), (x, y-1), (x, y+1)):
                    if not (0 <= nx < self.size and 0 <= ny < self.size) or self.g[ny][nx] == '.':
                        out[y][x] = 'k'
                        break
        self.g = out

    def eye(self, x, y):
        # 3x4 eye with highlight
        self.rect(x, y, x + 2, y + 3, 'k')
        self.px(x, y + 1, 'w')
        self.px(x + 1, y, 'w')

    def smile(self, cx, row):
        self.px(cx - 3, row, 'k')
        self.rect(cx - 2, row + 1, cx + 2, row + 1, 'k')
        self.px(cx + 3, row, 'k')

    def toes(self, x, y):
        self.px(x, y, 'k')

    def rows(self):
        return [''.join(r) for r in self.g]


# ------------------------------------------------------------------ generic

def egg():
    g = G()
    g.disk(16, 17, 8.6, 11.0, 'y', 'Y')
    g.outline()
    g.rect(12, 11, 13, 12, 'g'); g.px(12, 13, 'g')
    g.rect(19, 16, 20, 17, 'g'); g.px(20, 18, 'g')
    g.rect(13, 22, 14, 23, 'g')
    return g.rows()


def poop():
    g = G()
    g.disk(16, 24, 7.0, 4.4, 'c', 'C')
    g.disk(16, 18, 5.0, 3.4, 'c', 'C')
    g.disk(16, 13, 3.0, 2.4, 'c', 'C')
    g.px(16, 9, 'c'); g.px(17, 10, 'c')
    g.outline()
    return g.rows()


def heart():
    # Hand-drawn and edited directly in species.h (commit 7c2b0f9,
    # "animaciones epicas, corazon y biomas"), without coming back here: the generator
    # kept a different procedural version and species.h could no longer be
    # regenerated identically. The heart that is actually shipped is pinned here.
    return [
        "................................",
        "................................",
        "................................",
        "................................",
        "................................",
        "................................",
        "........krrrrk....krrrrk........",
        ".......krrrrrrk..krrrrrrk.......",
        "......krrrrrrrrrrrrrrrrrrk......",
        ".....krrrrrrrrrrrrrrrrrrrrk.....",
        ".....krrwwrrrrrrrrrrrrrrRrk.....",
        ".....krwwrrrrrrrrrrrrrrRRrk.....",
        "......krrrrrrrrrrrrrrrrrrk......",
        ".......krrrrrrrrrrrrrrrrk.......",
        "........krrrrrrrrrrrrrrk........",
        ".........krrrrrrrrrrrrk.........",
        "..........krrrrrrrrrrk..........",
        "...........krrrrrrrrk...........",
        "............krrrrrrk............",
        ".............krrrrk.............",
        "..............krrk..............",
        "...............kk...............",
        "................................",
        "................................",
        "................................",
        "................................",
        "................................",
        "................................",
        "................................",
        "................................",
        "................................",
        "................................",
    ]


# ------------------------------------------------------------ button icons

def icon_food():
    g = G(16)
    g.disk(8, 9.5, 5.2, 4.8, 'r', 'R')
    g.px(8, 3, 'C'); g.px(8, 4, 'C')
    g.px(10, 3, 'g'); g.px(11, 3, 'g'); g.px(11, 2, 'g')
    g.outline()
    g.px(6, 7, 'w'); g.px(5, 8, 'w')
    return g.rows()


def icon_play():
    g = G(16)
    g.disk(8, 8, 6.2, 6.2, 'r', 'R')
    g.disk(8, 11.4, 5.4, 3.2, 'w', 'Y')
    g.outline()
    g.rect(3, 8, 13, 8, 'k')
    g.px(2, 8, 'k'); g.px(14, 8, 'k')
    g.rect(7, 7, 9, 9, 'k')
    g.px(8, 8, 'w')
    return g.rows()


def icon_light():
    g = G(16)
    g.disk(8, 8, 5.8, 5.8, 'f')
    g.erase(11, 5.5, 4.6, 4.6)
    g.outline()
    g.px(12, 3, 'f'); g.px(13, 5, 'f')
    return g.rows()


def icon_berry_blue():
    g = G(16)
    g.disk(8, 9.5, 5.2, 4.8, 'b', 'B')
    g.px(8, 3, 'C'); g.px(8, 4, 'C')
    g.px(10, 3, 'g'); g.px(11, 2, 'g')
    g.outline()
    g.px(6, 7, 'w'); g.px(5, 8, 'w')
    return g.rows()


def icon_berry_green():
    g = G(16)
    g.disk(8, 9.5, 5.2, 4.8, 'g', 'G')
    g.px(8, 3, 'C'); g.px(8, 4, 'C')
    g.px(10, 3, 't'); g.px(11, 2, 't')
    g.outline()
    g.px(6, 7, 'w'); g.px(5, 8, 'w')
    return g.rows()


def icon_candy():
    g = G(16)
    g.disk(8, 8, 4.4, 3.6, 'p', 'P')
    # wrapper: points at the sides
    g.px(2, 6, 'p'); g.px(2, 8, 'p'); g.px(2, 10, 'p'); g.rect(3, 7, 3, 9, 'p')
    g.px(13, 6, 'p'); g.px(13, 8, 'p'); g.px(13, 10, 'p'); g.rect(12, 7, 12, 9, 'p')
    g.outline()
    g.px(7, 6, 'w'); g.px(6, 7, 'w')
    g.px(8, 8, 'P'); g.px(9, 9, 'P')
    return g.rows()


def icon_clean():
    g = G(16)
    g.disk(7, 10, 4.6, 4.4, 'b', 'B')
    g.disk(12, 4.5, 2.2, 2.2, 'b', 'B')
    g.disk(3.5, 4, 1.4, 1.4, 'b')
    g.outline()
    g.px(5, 8, 'w'); g.px(6, 9, 'w'); g.px(11, 4, 'w')
    return g.rows()


SPRITES = {
    "ICON_FOOD": icon_food(),
    "ICON_PLAY": icon_play(),
    "ICON_LIGHT": icon_light(),
    "ICON_CLEAN": icon_clean(),
    "ICON_BERRY_B": icon_berry_blue(),
    "ICON_BERRY_G": icon_berry_green(),
    "ICON_CANDY": icon_candy(),
    "EGG": egg(),
    "POOP": poop(),
    "HEART": heart(),
}

def validate():
    ok = True
    for name, rows in SPRITES.items():
        n = len(rows)
        for i, row in enumerate(rows):
            if len(row) != n:
                print(f"{name} fila {i}: {len(row)} chars (esperaba {n})")
                ok = False
            bad = set(row) - set(PALETTE) - {'.'}
            if bad:
                print(f"{name} fila {i}: chars desconocidos {bad}")
                ok = False
    return ok


def render(path="tools/sheet.png", scale=10, percol=3):
    pad = 16
    names = list(SPRITES)
    ncols = percol
    nrows = (len(names) + ncols - 1) // ncols
    bgs = ['#f2efe1', '#141828']
    cw, chh = W * scale + pad, H * scale + pad + 18
    img = Image.new('RGB', (len(bgs) * (ncols * cw + pad) + pad, nrows * chh + pad), '#888888')
    d = ImageDraw.Draw(img)
    for bi, bg in enumerate(bgs):
        gx = pad + bi * (ncols * cw + pad)
        for i, name in enumerate(names):
            ox = gx + (i % ncols) * cw
            oy = pad + (i // ncols) * chh
            d.rectangle([ox, oy, ox + W * scale, oy + H * scale], fill=bg)
            sc = scale * W // len(SPRITES[name])
            for r, row in enumerate(SPRITES[name]):
                for c, ch in enumerate(row):
                    if ch in PALETTE:
                        d.rectangle([ox + c * sc, oy + r * sc,
                                     ox + (c + 1) * sc - 1, oy + (r + 1) * sc - 1],
                                    fill=PALETTE[ch])
            d.text((ox, oy + H * scale + 3), name, fill='#000')
    img.save(path)
    print(f"guardado {path}")


def rgb565(hexcol):
    r, g, b = int(hexcol[1:3], 16), int(hexcol[3:5], 16), int(hexcol[5:7], 16)
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


UI_COLORS = {
    'UI_BG_DAY': '#f2efe1', 'UI_BG_NIGHT': '#141828',
    'UI_INK': '#2a2a36', 'UI_INK_NIGHT': '#d8dcf0',
    'UI_TRACK': '#d8d2bd',
    'UI_BAR_OK': '#58b868', 'UI_BAR_WARN': '#e8a23c', 'UI_BAR_BAD': '#e8503a',
    'UI_WHITE': '#ffffff',
}


def emit_c(path="species.h"):
    out = []
    out.append("#pragma once\n#include <stdint.h>\n\n")
    out.append("// GENERADO por tools/sprites.py - edita alli y ejecuta:\n")
    out.append("//   python3 tools/sprites.py emit\n\n")
    out.append(f"#define SPRITE_W {W}\n#define SPRITE_H {H}\n\n")
    for name, hexcol in UI_COLORS.items():
        out.append(f"#define {name} 0x{rgb565(hexcol):04X}  // {hexcol}\n")
    out.append("\n")
    out.append("// sprite character -> RGB565\n")
    out.append("static inline uint16_t spriteColor(char ch) {\n  switch (ch) {\n")
    for ch, hexcol in PALETTE.items():
        out.append(f"    case '{ch}': return 0x{rgb565(hexcol):04X};  // {hexcol}\n")
    out.append("    default: return 0;\n  }\n}\n\n")
    for name, rows in SPRITES.items():
        out.append(f"static const char* const SPR_{name}[{len(rows)}] = {{  // {len(rows)}x{len(rows)}\n")
        for row in rows:
            out.append(f'  "{row}",\n')
        out.append("};\n\n")
    open(path, 'w').write(''.join(out))
    print(f"guardado {path}")


if __name__ == '__main__':
    import sys
    if not validate():
        print("-- errores de validacion --")
        sys.exit(1)
    render()
    if 'emit' in sys.argv:
        emit_c()
