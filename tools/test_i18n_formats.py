#!/usr/bin/env python3
"""Checks that the tables in i18n.cpp have as many entries as their enums.

STRINGS is declared as STRINGS[STR_COUNT], so a table with too FEW strings
does not fail to compile: the remaining slots are silently zero-initialized
(null pointers), which crashes or reads garbage the first time that StrId is
used. Too many *is* a compile error, so this mainly guards the silent case,
but it checks the count outright either way. The medal tables are checked
against MED_COUNT the same way.

The tables are parsed straight out of the .cpp source (no build step needed).

  python3 tools/test_i18n_formats.py
"""
import os
import re
import sys

HERE = os.path.dirname(__file__)
I18N_CPP = os.path.join(HERE, '..', 'i18n.cpp')

STRING_RE = re.compile(r'"(?:[^"\\]|\\.)*"')


def extract_table(source, var_name):
    """Find the `var_name[...] = { ... };` table and return its string literals."""
    m = re.search(rf'\b{re.escape(var_name)}\s*\[[^;{{]*\]\s*=\s*\{{', source)
    if not m:
        raise ValueError(f'table {var_name} not found')
    end = source.index('};', m.end())
    return STRING_RE.findall(source[m.end():end])


def count_enumerators(header_path, enum_name, terminator):
    """Count comma-separated enumerators before `terminator`."""
    src = open(header_path, encoding='utf-8').read()
    m = re.search(rf'enum\s+{enum_name}\s*(?::\s*\w+\s*)?\{{(.*?)\}};', src, re.S)
    if not m:
        raise ValueError(f'enum {enum_name} not found in {header_path}')
    # strip line comments first: they sit between enumerators, not just
    # at line ends, so a plain comma-split would swallow them into names
    body = re.sub(r'//[^\n]*', '', m.group(1))
    names = [n.strip().split('=')[0].strip() for n in body.split(',') if n.strip()]
    if terminator not in names:
        raise ValueError(f'{terminator} not found in enum {enum_name}')
    return names.index(terminator)


def main():
    source = open(I18N_CPP, encoding='utf-8').read()
    str_count = count_enumerators(os.path.join(HERE, '..', 'i18n.h'), 'StrId', 'STR_COUNT')
    med_count = 8  # MED_COUNT, #define'd in pet.h

    errors = []
    for var, expected in (('STRINGS', str_count), ('MED_NAME', med_count), ('MED_DSC', med_count)):
        n = len(extract_table(source, var))
        if n != expected:
            errors.append(f'{var} has {n} entries, expected {expected}')

    if errors:
        print(f'{len(errors)} i18n consistency error(s):')
        for e in errors:
            print(f'  - {e}')
        return 1
    print(f'OK: STRINGS ({str_count} ids) and the 2 medal tables ({med_count} medals) have the right size.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
