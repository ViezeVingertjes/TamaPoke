#!/usr/bin/env python3
"""Compares the renderer screenshots with the reference images.

    compare.py <shots> <reference>               fails if any differs
    compare.py --update [--prune] <shots> <reference>
                                                  overwrites them with the new ones;
                                                  --prune deletes those of screens that no longer exist

If a screenshot differs, it saves <name>.diff.png (actual | reference | difference)
next to the screenshots, to view it in the CI artifact. A new screen or one of another
size leaves <name>.actual.png."""
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
    if not update:  # a reference without a screenshot: the screen stopped rendering
        for f in sorted(os.listdir(golden)):
            if f.endswith(".png") and f[:-4] not in names:
                bad.append((f[:-4], "hay referencia pero no captura"))
    for n in names:
        cur = load(os.path.join(shots, n + ".ppm"))
        ref_path = os.path.join(golden, n + ".png")
        if update:
            # a reference that hasn't changed is not rewritten: the PNG bytes
            # depend on the Pillow/zlib version and would clutter the git diff
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
            r, g, b = diff.split()  # count the pixel if any channel changes
            any_ch = ImageChops.lighter(ImageChops.lighter(r, g), b).point(lambda v: 255 if v else 0)
            changed = any_ch.histogram()[255]
            bad.append((n, "%d px distintos, zona %s" % (changed, box)))
            sheet = Image.new("RGB", (cur.width * 3, cur.height))
            sheet.paste(cur, (0, 0))
            sheet.paste(ref, (cur.width, 0))
            sheet.paste(diff.point(lambda v: 255 if v else 0), (cur.width * 2, 0))
            sheet.save(os.path.join(shots, n + ".diff.png"))

    if update:
        for f in os.listdir(golden) if prune else []:  # screens that no longer exist
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
