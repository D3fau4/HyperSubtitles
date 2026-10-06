"""
Event script (STCM2L .cl3) voice table reader — NeptuniaReBirth1.

Event scripts carry a voice table in GLOBAL_DATA followed by a block of
NUL-terminated dialogue strings. Each record is 9 int32:

  face, speaker, unknown, voice_id, text_offset, next_voice_id, flag, -1, -1

The last record of a table has next_voice_id = 0 and flag = 0.
Records with voice_id = 0 keep the table together but are not exported:
their voice cannot be derived reliably from next_voice_id.
text_offset is relative to the start of the string block that follows the table.
voice_id is the number used in /VOICE(_EN)/EVENT/%07u.hca.
Strings are Shift-JIS (cp932): mostly ASCII plus some kaomoji.

Usage:
  python parse_event_scripts.py --game "<game folder>" -o event_script_text.json
  python parse_event_scripts.py <folder or .pac> [...] -o event_script_text.json

--game reads event/script/*.cl3 straight from data/GAME00000.pac and every
DLC .pac. Folders are scanned for *.cl3; .pac files are read with extract_pac.
"""

import argparse
import fnmatch
import json
import os
import struct
import sys
from pathlib import Path

from extract_pac import read_entries, read_file

RECORD = struct.Struct('<9i')
MIN_ID = 80000000
MAX_ID = 80999999


def is_record(d, o):
    if o < 0 or o + RECORD.size > len(d):
        return False
    face, speaker, _, vid, off, nxt, flag, a, b = RECORD.unpack_from(d, o)
    if not (MIN_ID <= vid <= MAX_ID or vid == 0 and MIN_ID <= nxt <= MAX_ID) or not 0 <= off < 0x400000:
        return False
    if nxt == 0:
        return flag == 0
    return MIN_ID <= nxt <= MAX_ID and nxt != vid and flag >= 0 and a == -1 and b == -1


def find_tables(d, max_gap=64):
    tables = []
    o = 0
    while o + RECORD.size <= len(d):
        if not is_record(d, o):
            o += 4
            continue
        start = o
        table = []
        while is_record(d, o):
            table.append(RECORD.unpack_from(d, o))
            o += RECORD.size
            if table[-1][5] == 0:
                break
        if tables and table and tables[-1][2][-1][5] != 0 and start - tables[-1][1] <= max_gap:
            tables[-1] = (tables[-1][0], o, tables[-1][2] + table)
        else:
            tables.append((start, o, table))
    return [(end, table) for _, end, table in tables]


def string_block_base(d, start, table):
    offsets = sorted({r[4] for r in table})
    last = offsets[-1]
    p = start
    while p + last < len(d):
        p = d.find(b'\x00', p)
        if p == -1:
            return None
        p += 1
        if d[p] == 0:
            continue
        if all(off == 0 or d[p + off - 1] == 0 for off in offsets):
            return p
    return None


def read_string(d, p):
    end = d.index(b'\x00', p)
    return d[p:end].decode('cp932', errors='replace')


def is_text(s):
    return '�' not in s and all(c >= ' ' or c in '\n\r\t' for c in s)


def parse_bytes(raw):
    start = raw.find(b'STCM2L')
    if start == -1:
        return [], 0
    d = raw[start:]
    lines = []
    failed = 0
    for end, table in find_tables(d):
        base = string_block_base(d, end, table)
        if base is None:
            failed += 1
            continue
        texts = [read_string(d, base + r[4]) for r in table]
        if not all(is_text(t) for t in texts):
            failed += 1
            continue
        for (face, speaker, _, vid, _, _, _, _, _), text in zip(table, texts):
            if vid and text.strip():
                lines.append({
                    'id': vid,
                    'text': text,
                    'speaker': speaker,
                    'face': face,
                })
    return lines, failed


def iter_scripts(source):
    source = Path(source)
    if source.is_file() and source.suffix.lower() == '.pac':
        with source.open('rb') as f:
            for entry in read_entries(f):
                if fnmatch.fnmatch(entry['path'].lower(), 'event/script/*.cl3'):
                    yield entry['path'].rsplit('/', 2)[-2], read_file(f, entry)
        return
    for root, _, names in os.walk(source):
        for name in sorted(names):
            if name.lower().endswith('.cl3'):
                path = Path(root) / name
                yield path.parent.relative_to(source).as_posix(), path.read_bytes()


def game_sources(game):
    game = Path(game)
    return [game / 'data' / 'GAME00000.pac'] + sorted((game / 'DLC').rglob('*.pac'))


def main():
    parser = argparse.ArgumentParser(description='Extract voice id -> dialogue text from event scripts.')
    parser.add_argument('sources', nargs='*', help='Folders with *.cl3 or .pac files')
    parser.add_argument('--game', help='Game folder: reads data/GAME00000.pac and DLC/**/*.pac')
    parser.add_argument('-o', '--output', default='event_script_text.json', help='Output JSON')
    args = parser.parse_args()

    sources = list(args.sources) + (game_sources(args.game) if args.game else [])
    if not sources:
        parser.error('give --game or at least one folder / .pac')

    result = {}
    files = 0
    failed_tables = 0
    for source in sources:
        for script, raw in iter_scripts(source):
            lines, failed = parse_bytes(raw)
            failed_tables += failed
            if not lines:
                continue
            files += 1
            for line in lines:
                key = f"{line['id']:07d}"
                if key not in result:
                    result[key] = {
                        'text': line['text'],
                        'speaker': line['speaker'],
                        'face': line['face'],
                        'script': script,
                    }

    with open(args.output, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(dict(sorted(result.items())), f, ensure_ascii=False, indent=2)
        f.write('\n')
    print(f'{len(result)} voice lines from {files} scripts -> {args.output}')
    if failed_tables:
        print(f'WARNING: {failed_tables} tables without a matching string block', file=sys.stderr)


if __name__ == '__main__':
    main()
