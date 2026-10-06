"""
Portrait extractor — NeptuniaReBirth1.

Reads the 1024x512 character faces (global/face/NNN.tid for the normal form,
global/face/trans/NNN.tid for the HDD form) from data/SYSTEM00000.pac and the
DLC .pac files, and writes them as PNG following the "portrait" field of
data/characters.json ("face/001", "trans/003"). Characters sharing a portrait
get a single file, named by the lowest character id that uses it. When writing
to Dll1/faces it then runs wire_portraits.py to register them in the DLL project.

TID layout used here: 32-bit RGBA, width/height at 0x44/0x48, pixel data
offset at 0x5C, no compression.

Usage:
  python extract_portraits.py --game "<game folder>" [-o Dll1/faces] [--characters data/characters.json]
  python extract_portraits.py --face-dir "<extracted SYSTEM00000>/global/face"
"""

import argparse
import json
import struct
import zlib
from pathlib import Path

from extract_pac import read_entries, read_file
from wire_portraits import wire

REPO = Path(__file__).resolve().parents[2]


def parse_tid(d, name):
    if d[:3] != b'TID':
        raise ValueError(f'{name} is not a TID file')
    width, height, bpp = struct.unpack_from('<III', d, 0x44)
    if bpp != 32:
        raise ValueError(f'{name}: unsupported bpp {bpp}')
    offset = struct.unpack_from('<I', d, 0x5C)[0]
    return width, height, d[offset:offset + width * height * 4]


def write_png(path, width, height, rgba):
    try:
        from PIL import Image
    except ImportError:
        Image = None
    if Image is not None:
        Image.frombytes('RGBA', (width, height), rgba).save(path, optimize=True)
        return

    def chunk(tag, data):
        body = tag + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)

    stride = width * 4
    raw = b''.join(b'\x00' + rgba[y * stride:(y + 1) * stride] for y in range(height))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9))
    png += chunk(b'IEND', b'')
    path.write_bytes(png)


def stored_path(portrait):
    kind, number = portrait.split('/')
    return f'global/face/{number}.tid' if kind == 'face' else f'global/face/{kind}/{number}.tid'


def faces_from_game(game, wanted):
    game = Path(game)
    found = {}
    for pac in [game / 'data' / 'SYSTEM00000.pac'] + sorted((game / 'DLC').rglob('*.pac')):
        with pac.open('rb') as f:
            for entry in read_entries(f):
                path = entry['path'].lower()
                if path in wanted and path not in found:
                    found[path] = (pac.name, read_file(f, entry))
    return found


def faces_from_folder(face_dir, wanted):
    face_dir = Path(face_dir)
    found = {}
    for path in wanted:
        source = face_dir / path[len('global/face/'):]
        if source.exists():
            found[path] = (face_dir.name, source.read_bytes())
    return found


def main():
    parser = argparse.ArgumentParser(description='Extract character portraits as PNG.')
    parser.add_argument('--game', help='Game folder (reads data/SYSTEM00000.pac and DLC/**/*.pac)')
    parser.add_argument('--face-dir', help='Already extracted global/face folder')
    parser.add_argument('-o', '--out', type=Path, default=REPO / 'Dll1' / 'faces', help='Output folder')
    parser.add_argument('--characters', type=Path, default=REPO / 'data' / 'characters.json')
    parser.add_argument('--no-wire', action='store_true', help='Do not update the Dll1 project (resource.h, .rc, k_portraits)')
    args = parser.parse_args()
    if not args.game and not args.face_dir:
        parser.error('give --game or --face-dir')

    characters = json.loads(args.characters.read_text(encoding='utf-8'))
    portraits = {}
    for cid in sorted(characters, key=int):
        portrait = characters[cid].get('portrait')
        if portrait:
            portraits.setdefault(stored_path(portrait), []).append(cid)
    found = faces_from_game(args.game, set(portraits)) if args.game else faces_from_folder(args.face_dir, set(portraits))

    args.out.mkdir(parents=True, exist_ok=True)
    count = 0
    for path, ids in portraits.items():
        names = ', '.join(characters[cid]['name'] for cid in ids)
        if path not in found:
            print(f'MISSING {path} ({names})')
            continue
        source, data = found[path]
        width, height, rgba = parse_tid(data, path)
        target = args.out / f'{int(ids[0]):03d}.png'
        write_png(target, width, height, rgba)
        print(f'{source}:{path} -> {target.name} ({names})')
        count += 1
    print(f'{count} portraits -> {args.out}')
    if not args.no_wire and args.out.resolve() == (REPO / 'Dll1' / 'faces').resolve():
        wire(args.characters)


if __name__ == '__main__':
    main()
