"""
DW_PACK (.pac) extractor — NeptuniaReBirth1.

Layout (from the game's reader, sub_664E20 / sub_6653F0):
  0x00  char[8] "DW_PACK"
  0x08  u32 0
  0x0C  u32 file_count
  0x10  u32 0
  0x14  file_count entries of 0x120 bytes:
          +0x000 u32 unknown, +0x004 u32 index, +0x008 char[0x104] path (cp932)
          +0x110 u32 stored_size, +0x114 u32 size, +0x118 u32 compressed, +0x11C u32 offset
  data  0x14 + file_count * 0x120 + offset

Compressed files (CDivideHuffman, sub_6636F0 / sub_668B30 / sub_6686E0):
  u32 magic, u32 chunk_count, u32 chunk_size, u32 header_size,
  chunk_count x (u32 size, u32 stored_size, u32 offset), chunk data at header_size + offset.
  Each chunk is a canonical-free Huffman stream, bits read MSB first: the tree comes
  first in preorder (1 = node followed by left and right subtrees, 0 = leaf followed
  by 8 bits), then the symbols (bit 0 = left, 1 = right).

Usage:
  python extract_pac.py GAME00000.pac --list [pattern ...]
  python extract_pac.py GAME00000.pac -o out "event/script/*" "*/face/*.tid"

Patterns are fnmatch globs on the stored path with / separators, case-insensitive.
"""

import argparse
import fnmatch
import struct
import sys
from pathlib import Path

ENTRY = 0x120


def read_entries(f):
    header = f.read(0x14)
    if header[:7] != b'DW_PACK':
        raise ValueError('not a DW_PACK file')
    count = struct.unpack_from('<I', header, 0x0C)[0]
    table = f.read(count * ENTRY)
    base = 0x14 + count * ENTRY
    entries = []
    for i in range(count):
        e = table[i * ENTRY:(i + 1) * ENTRY]
        path = e[8:0x10C].split(b'\0', 1)[0].decode('cp932').replace('\\', '/')
        stored, size, compressed, offset = struct.unpack_from('<4I', e, 0x110)
        entries.append({'path': path, 'stored': stored, 'size': size,
                        'compressed': compressed, 'offset': base + offset})
    return entries


class BitReader:
    def __init__(self, data):
        self.value = int.from_bytes(data, 'big')
        self.left = len(data) * 8

    def bits(self, n):
        if n > self.left:
            pad = n - self.left
            result = ((self.value & ((1 << self.left) - 1)) << pad) if self.left else 0
            self.left = 0
            return result
        self.left -= n
        return (self.value >> self.left) & ((1 << n) - 1)


def read_tree(reader):
    if reader.bits(1):
        left = read_tree(reader)
        right = read_tree(reader)
        return (left, right)
    return reader.bits(8)


def code_table(tree):
    codes = {}

    def walk(node, code, length):
        if isinstance(node, int):
            codes[node] = (code, max(length, 1))
            return
        walk(node[0], code << 1, length + 1)
        walk(node[1], (code << 1) | 1, length + 1)

    walk(tree, 0, 0)
    return codes


def decode_chunk(data, size):
    reader = BitReader(data)
    tree = read_tree(reader)
    if isinstance(tree, int):
        return bytes([tree]) * size
    pos = len(data) * 8 - reader.left
    padded = data + b'\0' * 8
    out = bytearray(size)
    codes = code_table(tree)
    width = max(length for _, length in codes.values())

    if width > 24:
        for i in range(size):
            node = tree
            while not isinstance(node, int):
                node = node[(padded[pos >> 3] >> (7 - (pos & 7))) & 1]
                pos += 1
            out[i] = node
        return bytes(out)

    symbols = bytearray(1 << width)
    lengths = bytearray(1 << width)
    for symbol, (code, length) in codes.items():
        start = code << (width - length)
        end = start + (1 << (width - length))
        symbols[start:end] = bytes([symbol]) * (end - start)
        lengths[start:end] = bytes([length]) * (end - start)

    shift_base = 32 - width
    mask = (1 << width) - 1
    from_bytes = int.from_bytes
    for i in range(size):
        b = pos >> 3
        window = (from_bytes(padded[b:b + 4], 'big') >> (shift_base - (pos & 7))) & mask
        out[i] = symbols[window]
        pos += lengths[window]
    return bytes(out)


def decompress(blob):
    _, chunks, _, header_size = struct.unpack_from('<4I', blob, 0)
    out = bytearray()
    for i in range(chunks):
        size, stored, offset = struct.unpack_from('<3I', blob, 16 + 12 * i)
        start = header_size + offset
        out += decode_chunk(blob[start:start + stored], size)
    return bytes(out)


def read_file(f, entry):
    f.seek(entry['offset'])
    blob = f.read(entry['stored'])
    if entry['compressed'] == 1:
        data = decompress(blob)
        if len(data) != entry['size']:
            raise ValueError(f"{entry['path']}: got {len(data)} bytes, expected {entry['size']}")
        return data
    return blob


def matches(path, patterns):
    lowered = path.lower()
    return not patterns or any(fnmatch.fnmatch(lowered, p.lower()) for p in patterns)


def main():
    parser = argparse.ArgumentParser(description='List or extract files from a DW_PACK .pac')
    parser.add_argument('pac', type=Path)
    parser.add_argument('patterns', nargs='*', help='Globs on the stored path (default: everything)')
    parser.add_argument('-o', '--out', type=Path, default=Path('pac_out'))
    parser.add_argument('--list', action='store_true', help='Only list matching files')
    args = parser.parse_intermixed_args()

    with args.pac.open('rb') as f:
        entries = [e for e in read_entries(f) if matches(e['path'], args.patterns)]
        if args.list:
            for e in entries:
                print(f"{e['size']:>10} {'C' if e['compressed'] == 1 else ' '} {e['path']}")
            print(f'{len(entries)} files', file=sys.stderr)
            return
        for e in entries:
            target = args.out / e['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(read_file(f, e))
        print(f'{len(entries)} files -> {args.out}')


if __name__ == '__main__':
    main()
