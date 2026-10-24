#!/usr/bin/env python3
"""Checks that what the web installer serves matches the source tree.

  python3 tools/validate_web_build.py

- manifest.json version == FW_VERSION in TamaPoke.ino, and its firmware file exists
- that firmware contains the same version string (it is not an older build)
- sprites.pak is a well-formed TPAK, with exactly the files of tools/sdcard/mons
  (same names and sizes: it was repacked after the last sprite change)
- its thumbs.bin has one valid entry per Pokedex number (DEX_COUNT in dex.h)

The thumbnail check is stricter than the firmware loader on purpose: a pack made by
make_thumbs.py has exactly one back-to-back entry per Pokedex number.

Idea and thumbnail rules from ShadowEnemyx's fork (tools/validate_web_build.py).
Exits with the first problem found.
"""
import json
import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def path(*parts):
    return os.path.normpath(os.path.join(ROOT, *parts))


def fail(msg):
    sys.exit(f'FAIL: {msg}')


def read(p, mode='r'):
    with open(p, mode, **({} if 'b' in mode else {'encoding': 'utf-8'})) as fh:
        return fh.read()


def validate_thumbs(blob, expected_count):
    """TPTH: "TPTH", u16 count, u32 offsets[count], then the entries back to back."""
    if len(blob) < 6 or blob[:4] != b'TPTH':
        fail('thumbs.bin: bad TPTH header')
    count = struct.unpack_from('<H', blob, 4)[0]
    if count != expected_count:
        fail(f'thumbs.bin: {count} thumbnails, expected {expected_count}')
    table_end = 6 + count * 4
    if table_end > len(blob):
        fail('thumbs.bin: truncated offset table')
    offsets = struct.unpack_from(f'<{count}I', blob, 6)
    previous = table_end
    for i, off in enumerate(offsets, 1):
        if off < previous or off + 3 > len(blob):
            fail(f'thumbs.bin: bad offset for entry {i}')
        w, h, pal = struct.unpack_from('<BBB', blob, off)
        if not (w and h and pal):
            fail(f'thumbs.bin: empty size or palette in entry {i}')
        end = off + 3 + pal * 2 + w * h
        nxt = offsets[i] if i < count else len(blob)
        if end != nxt:
            fail(f'thumbs.bin: entry {i} does not end where the next one starts')
        if any(px != 0xFF and px >= pal for px in blob[off + 3 + pal * 2:end]):
            fail(f'thumbs.bin: palette index out of range in entry {i}')
        previous = off


def parse_pak(blob):
    """TPAK: "TPAK", u16 count, count x (u8 nameLen, name, u32 size), then the data."""
    if len(blob) < 6 or blob[:4] != b'TPAK':
        fail('sprites.pak: bad TPAK header')
    count = struct.unpack_from('<H', blob, 4)[0]
    pos, entries = 6, []
    for _ in range(count):
        if pos >= len(blob) or pos + 1 + blob[pos] + 4 > len(blob):
            fail('sprites.pak: truncated index')
        n = blob[pos]
        try:
            name = blob[pos + 1:pos + 1 + n].decode('utf-8')
        except UnicodeDecodeError:
            fail('sprites.pak: a file name in the index is not UTF-8')
        entries.append((name, struct.unpack_from('<I', blob, pos + 1 + n)[0]))
        pos += 1 + n + 4
    if pos + sum(size for _, size in entries) != len(blob):
        fail('sprites.pak: the indexed sizes do not add up to the file size')
    data = {}
    for name, size in entries:
        data[name] = blob[pos:pos + size]
        pos += size
    if len(data) != len(entries):
        fail('sprites.pak: duplicate names')
    return data


def define(file, pattern):
    m = re.search(pattern, read(path(file)), re.M)
    if not m:
        fail(f'{file}: no match for {pattern}')
    return m.group(1)


def main():
    version = define('TamaPoke.ino', r'#define FW_VERSION "([^"]+)"')
    manifest = json.loads(read(path('web', 'manifest.json')))
    if manifest.get('version') != version:
        fail(f'manifest.json says {manifest.get("version")}, TamaPoke.ino says {version}')
    parts = [p for b in manifest.get('builds', []) for p in b.get('parts', [])]
    if not parts:
        fail('manifest.json lists no firmware parts')
    for part in parts:
        if 'path' not in part:
            fail('manifest.json has a part without a path')
        fw = path('web', part['path'])
        if not os.path.isfile(fw):
            fail(f'manifest.json points at a missing file: {part["path"]}')
        # the version is passed to printf as its own string literal: look for exactly that
        if b'\0' + version.encode() + b'\0' not in read(fw, 'rb'):
            fail(f'{part["path"]} was not built from version {version}: rebuild it with tools/build_web.sh')

    dex_count = int(define('dex.h', r'^#define DEX_COUNT (\d+)\s*$'))
    pak = parse_pak(read(path('web', 'sprites.pak'), 'rb'))
    mons = path('tools', 'sdcard', 'mons')
    expected = {f'mons/{f}': os.path.getsize(os.path.join(mons, f))
                for f in os.listdir(mons) if f.endswith('.bin')}
    if set(pak) != set(expected):
        diff = sorted(set(pak) ^ set(expected))[:5]
        fail(f'sprites.pak and tools/sdcard/mons list different files (e.g. {diff}): run tools/pack_bundle.py')
    stale = [n for n in expected if len(pak[n]) != expected[n]]
    if stale:
        fail(f'sprites.pak is out of date for {stale[:5]}: run tools/pack_bundle.py')
    if 'mons/thumbs.bin' not in pak:
        fail('sprites.pak has no mons/thumbs.bin')
    validate_thumbs(pak['mons/thumbs.bin'], dex_count)
    print(f'OK: firmware {version}, sprites.pak with {len(pak)} files, {dex_count} thumbnails')


if __name__ == '__main__':
    main()
