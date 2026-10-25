#!/usr/bin/env python3
"""Generates pmdzoom.h: the integer zoom of each PMD sprite on the main screen.

The zoom used to come from the idle canvas height, which is padding of the
source sheet, so Charmeleon ended up smaller than Charmander. Now it comes from
the drawn art, and every stage of an evolution line shares the zoom of its
largest stage (the art itself already grows with each stage), except a stage that
would come out tiny, which steps up.

  python3 tools/pmd_zoom.py          # reads tools/sdcard/mons, writes pmdzoom.h
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from dex_data import DEX

ROOT = os.path.join(os.path.dirname(__file__), '..')
MONS = os.path.join(os.path.dirname(__file__), 'sdcard', 'mons')
CAP = 112          # biggest drawn side on a 466 px screen, so nothing blows up
FLOOR = 64         # smallest drawn side: a tiny stage of a huge line (Magikarp) steps up
MIN_Z, MAX_Z = 2, 5
# an evolvable stage carries the EVOLVE! bubble over its head: the bubble bottom (y 194)
# is 110 px above the ground (y 304)
EVOLVE_H = 108


def idle_size(dex):
    """(size, tallest) of the Idle action's art.

    size is sqrt(w * h) of the art's bbox averaged over the frames: the geometric
    mean keeps a wide or tall sprite from counting as bigger than a stocky one, the
    average stops one frame of motion (a bob, a swung tail) from deciding the zoom.
    tallest is the highest bbox of any frame.
    """
    b = open(os.path.join(MONS, f'p{dex:03d}.bin'), 'rb').read()
    n, pc = struct.unpack_from('<BH', b, 4)
    o = 7 + pc * 2
    for _ in range(n):
        aid, w, h, nf = struct.unpack_from('<4B', b, o)
        o += 4 + nf * 2
        data = b[o:o + w * h * nf]
        o += w * h * nf
        if aid != 0:
            continue
        sizes, tallest = [], 0
        for f in range(nf):
            fr = data[f * w * h:(f + 1) * w * h]
            xs = [i % w for i, v in enumerate(fr) if v != 0xFF]
            ys = [i // w for i, v in enumerate(fr) if v != 0xFF]
            if xs:  # a fully transparent frame (blink) has no art to measure
                bh = max(ys) - min(ys) + 1
                sizes.append(((max(xs) - min(xs) + 1) * bh) ** 0.5)
                tallest = max(tallest, bh)
        return sum(sizes) / len(sizes), tallest
    raise RuntimeError(f'#{dex} without Idle')


def families():
    """dex -> set of the dex numbers in its evolution line (Eevee's branches included)."""
    fam = {d[0]: {d[0]} for d in DEX}
    fam[134] = fam[135] = fam[136] = fam[133]
    fam[133].update((134, 135, 136))
    for num, *_rest, evo, _lvl in DEX:
        if evo:
            merged = fam[num] | fam[evo]
            for m in merged:
                fam[m] = merged
    return fam


def main():
    meas = {d[0]: idle_size(d[0]) for d in DEX}
    dims = {n: m[0] for n, m in meas.items()}
    evolves = {d[0] for d in DEX if d[4]}
    fam = families()
    zoom = [0]  # 0 = unused, the firmware falls back to the frame height
    for n in range(1, 152):
        z = min(MAX_Z, int(CAP / max(dims[m] for m in fam[n])))
        for m in fam[n] & evolves:
            z = min(z, int(EVOLVE_H / meas[m][1]))
        z = max(MIN_Z, z)
        while z < MAX_Z and dims[n] * z < FLOOR:
            z += 1
        zoom.append(z)
    out = ["#pragma once\n#include <stdint.h>\n\n",
           "// GENERADO por tools/pmd_zoom.py desde tools/sdcard/mons - no editar\n\n",
           "// integer zoom of the PMD sprite by dex number (0 unused)\n",
           "static const uint8_t PMD_ZOOM[152] = {"]
    out += [("\n  " if i % 16 == 0 else " ") + f"{z}," for i, z in enumerate(zoom)]
    out.append("\n};\n")
    with open(os.path.join(ROOT, 'pmdzoom.h'), 'w') as f:
        f.write(''.join(out))
    return dims, fam, zoom


if __name__ == '__main__':
    main()
