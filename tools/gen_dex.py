#!/usr/bin/env python3
"""Generates dex.h (Pokedex table for the firmware) from dex_data.py.

  python3 tools/gen_dex.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from dex_data import DEX, TYPE_ACCENTS, CLASSIC, RARE, LEGENDARY
from dex_stats import BASE_STATS


def rgb565(hexcol):
    r, g, b = int(hexcol[1:3], 16), int(hexcol[3:5], 16), int(hexcol[5:7], 16)
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


# background biome per type (the lighting comes from the RTC's real time)
# 0 MEADOW, 1 BEACH, 2 FOREST, 3 VOLCANO, 4 MOUNTAIN, 5 SNOW
TYPE_BIOME = {
    'agua': 1, 'planta': 2, 'bicho': 2, 'fuego': 3,
    'roca': 4, 'tierra': 4, 'dragon': 1, 'hielo': 5,  # Gen 1 dragons (Dratini) live in the water
    'normal': 0, 'electrico': 0, 'lucha': 0, 'veneno': 0,
    'psiquico': 0, 'fantasma': 0,
}

# per-dex# exceptions (the type is not enough): rock/water sea fossils -> beach
BIOME_OVERRIDE = {138: 1, 139: 1, 140: 1, 141: 1}  # Omanyte, Omastar, Kabuto, Kabutops


def main():
    out = []
    out.append("#pragma once\n#include <stdint.h>\n\n")
    out.append("// GENERADO por tools/gen_dex.py desde tools/dex_data.py - no editar\n\n")
    out.append("#define DEX_COUNT 151\n")
    out.append("#define DEX_EEVEE 133  // random branch: 134/135/136\n\n")
    out.append(
        "// rarity: 0 = evolution only, 1 = common, 2 = rare, 3 = legendary\n"
        "enum : uint8_t { R_EVO = 0, R_COMUN, R_RARO, R_LEGENDARIO };\n\n"
        "struct DexEntry {\n"
        "  const char *name;\n"
        "  uint8_t evolvesTo;    // dex number, 0 = final form\n"
        "  uint8_t evolveLevel;\n"
        "  uint8_t rarity;       // can come from an egg if > 0\n"
        "  uint16_t accent;      // RGB565 type color for the UI\n"
        "  uint8_t bHp, bAtk, bDef, bSpe;  // real Gen 1 base stats\n"
        "  uint8_t biome;        // 0 meadow 1 beach 2 forest 3 volcano 4 mountain 5 snow\n"
        "};\n\n")
    # base forms = those that are nobody's evolution (Eevee's branches are)
    evolved = {evo for *_, evo, _lvl in [(d[4], d[5]) for d in DEX] for evo in [_[0] for _ in [(d[4],) for d in DEX]]}
    evolved = {d[4] for d in DEX if d[4]} | {135, 136}
    rarities = []
    out.append("static const DexEntry DEX_TBL[DEX_COUNT + 1] = {\n")
    out.append('  { "?", 0, 0, 0, 0x2946, 50, 50, 50, 50, 0 },  // 0: unused\n')
    for num, slug, display, typ, evo, lvl in DEX:
        acc = rgb565(TYPE_ACCENTS[typ])
        if num in evolved:
            rar = 'R_EVO'
        elif num in LEGENDARY:
            rar = 'R_LEGENDARIO'
        elif num in RARE:
            rar = 'R_RARO'
        else:
            rar = 'R_COMUN'
        rarities.append(rar)
        hp, atk, df, spe = BASE_STATS[num]
        bio = BIOME_OVERRIDE.get(num, TYPE_BIOME[typ])
        out.append(f'  {{ "{display}", {evo}, {lvl}, {rar}, 0x{acc:04X}, {hp}, {atk}, {df}, {spe}, {bio} }},  // {num} {typ}\n')
    out.append("};\n\n")

    out.append(
        "// Species name (DEX_TBL[0] when out of range).\n"
        "static inline const char *dexName(int16_t dex) {\n"
        "  return DEX_TBL[dex < 1 || dex > DEX_COUNT ? 0 : dex].name;\n"
        "}\n\n")

    out.append("// the first egg of a game: classic starters\n")
    out.append("static const int16_t CLASSIC_DEX[] = { %s };\n" % ", ".join(map(str, CLASSIC)))
    out.append(f"#define NUM_CLASSIC_DEX {len(CLASSIC)}\n")
    from collections import Counter
    c = Counter(rarities)
    print(f"bases: {c['R_COMUN']} comunes, {c['R_RARO']} raras, {c['R_LEGENDARIO']} legendarias, {c['R_EVO']} solo-evolucion")

    path = os.path.join(os.path.dirname(__file__), '..', 'dex.h')
    open(path, 'w').write(''.join(out))
    print(f"guardado {os.path.normpath(path)} ({len(DEX)} especies)")


if __name__ == '__main__':
    main()
