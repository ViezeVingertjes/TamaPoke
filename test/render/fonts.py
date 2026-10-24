#!/usr/bin/env python3
"""Extrae de u8g2_fonts.c (libreria U8g2) las dos fuentes CJK que usa el sketch,
sin compilar los 39 MB del fichero entero."""
import re
import sys

NAMES = ["u8g2_font_unifont_t_japanese3", "u8g2_font_unifont_t_korean2"]
src = open(sys.argv[1], errors="replace").read()
out = ["// generado por fonts.py desde u8g2_fonts.c", "#include <stdint.h>"]
for n in NAMES:
    m = re.search(r"const uint8_t " + n + r"\[\d+\] U8G2_FONT_SECTION\(\"" + n + r"\"\) = \n", src)
    if not m:
        sys.exit("fonts.py: no encuentro " + n)
    end = src.index('";\n', m.end()) + 3
    body = src[m.end():end]
    size = re.match(r"const uint8_t \w+\[(\d+)\]", m.group(0)).group(1)
    out.append("extern const uint8_t %s[%s];" % (n, size))
    out.append("const uint8_t %s[%s] =\n%s" % (n, size, body))
    # si el corte cayera pronto, C++ rellenaria con ceros sin avisar: se comprueba
    out.append("static_assert(sizeof(%s) == %s, \"fuente %s truncada\");" % (
        body.rstrip().rstrip(";"), int(size), n))
open(sys.argv[2], "w").write("\n".join(out))
