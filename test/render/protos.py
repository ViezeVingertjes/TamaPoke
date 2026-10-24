#!/usr/bin/env python3
"""Genera una copia de TamaPoke.ino lista para compilar como C++: inserta los
prototipos que el IDE de Arduino anade solo, justo antes de la primera funcion."""
import re
import sys

src = open(sys.argv[1], encoding="utf-8").read().split("\n")
sig = re.compile(r"^(?!\s|//|#|struct |class |enum |typedef |static const |extern )"
                 r"([A-Za-z_][\w:<>\*&, ]*?[\s\*&])(\w+)\(([^;{)]*)\)\s*(const\s*)?\{")
protos, first = [], None
for i, line in enumerate(src):
    m = sig.match(line)
    if m and m.group(1).split()[0] not in ("if", "for", "while", "switch", "return", "else"):
        if first is None:
            first = i
        if m.group(2) not in ("setup", "loop"):
            protos.append(line[: line.index("{")].rstrip() + ";")
if first is None:
    sys.exit("protos.py: no encontre ninguna funcion en " + sys.argv[1])
src[first:first] = (["// --- prototipos generados (como el IDE de Arduino) ---"] + protos
                    + ['#line %d "%s"' % (first + 1, sys.argv[1])])
open(sys.argv[2], "w", encoding="utf-8").write("\n".join(src))
