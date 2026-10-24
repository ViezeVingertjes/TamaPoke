#!/usr/bin/env python3
"""Compara las capturas del renderer con las imagenes de referencia.

    compare.py <capturas> <referencia>            falla si alguna difiere
    compare.py --update [--prune] <capturas> <referencia>
                                                  las sobrescribe con las nuevas;
                                                  --prune borra las de pantallas que ya no existen

Si una captura difiere, guarda <nombre>.diff.png (actual | referencia | diferencia)
junto a las capturas, para verla en el artefacto de CI. Una pantalla nueva o de otro
tamano deja <nombre>.actual.png."""
import os
import sys

from PIL import Image, ImageChops


def load(path):
    return Image.open(path).convert("RGB")


def main():
    args = sys.argv[1:]
    update = "--update" in args
    prune = "--prune" in args
    args = [a for a in args if a not in ("--update", "--prune")]
    if len(args) != 2:
        sys.exit(__doc__)
    shots, golden = args
    names = sorted(f[:-4] for f in os.listdir(shots) if f.endswith(".ppm"))
    if not names:
        sys.exit("no hay capturas en " + shots)
    if update:
        os.makedirs(golden, exist_ok=True)
    elif not os.path.isdir(golden):
        sys.exit("no existe la carpeta de referencias " + golden)

    bad = []
    if not update:  # una referencia sin captura: la pantalla dejo de renderizarse
        for f in sorted(os.listdir(golden)):
            if f.endswith(".png") and f[:-4] not in names:
                bad.append((f[:-4], "hay referencia pero no captura"))
    for n in names:
        cur = load(os.path.join(shots, n + ".ppm"))
        ref_path = os.path.join(golden, n + ".png")
        if update:
            # no se reescribe una referencia que no cambia: los bytes del PNG
            # dependen de la version de Pillow/zlib y ensuciarian el git diff
            if not os.path.exists(ref_path) or ImageChops.difference(cur, load(ref_path)).getbbox():
                cur.save(ref_path, optimize=True)
            continue
        if not os.path.exists(ref_path):
            bad.append((n, "sin referencia"))
            cur.save(os.path.join(shots, n + ".actual.png"))
            continue
        ref = load(ref_path)
        if cur.size != ref.size:
            bad.append((n, "tamano %s, referencia %s" % (cur.size, ref.size)))
            cur.save(os.path.join(shots, n + ".actual.png"))
            continue
        diff = ImageChops.difference(cur, ref)
        box = diff.getbbox()
        if box:
            r, g, b = diff.split()  # cuenta el pixel si cualquier canal cambia
            any_ch = ImageChops.lighter(ImageChops.lighter(r, g), b).point(lambda v: 255 if v else 0)
            changed = any_ch.histogram()[255]
            bad.append((n, "%d px distintos, zona %s" % (changed, box)))
            sheet = Image.new("RGB", (cur.width * 3, cur.height))
            sheet.paste(cur, (0, 0))
            sheet.paste(ref, (cur.width, 0))
            sheet.paste(diff.point(lambda v: 255 if v else 0), (cur.width * 2, 0))
            sheet.save(os.path.join(shots, n + ".diff.png"))

    if update:
        for f in os.listdir(golden) if prune else []:  # pantallas que ya no existen
            if f.endswith(".png") and f[:-4] not in names:
                os.remove(os.path.join(golden, f))
        print("referencias actualizadas: %d" % len(names))
        return
    if bad:
        for n, why in bad:
            print("DIFIERE %-16s %s" % (n, why))
        sys.exit(1)
    print("%d pantallas identicas a la referencia" % len(names))


if __name__ == "__main__":
    main()
