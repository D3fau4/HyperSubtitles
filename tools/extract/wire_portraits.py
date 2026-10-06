"""
Registers the portraits from data/characters.json in the DLL project.

Rewrites, from scratch each time:
  Dll1/resource.h           #define IDB_FACE_<id> 2000+<id>  (one per portrait file)
  Dll1/HyperSubtitles.rc    IDB_FACE_<id> PNG "faces\\<id>.png"  (UTF-16 LE, as Visual Studio saves it)
  Dll1/DialogueBox.cpp      k_portraits rows: { character, IDB_FACE_<id> }
  Dll1/Dll1.vcxproj(.filters)  <Image Include="faces\\<id>.png" />

Characters sharing a portrait use the file of the lowest character id, the same
naming extract_portraits.py uses.

Usage:
  python wire_portraits.py
"""

import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DLL = REPO / 'Dll1'


def resource_name(cid):
    return f'IDB_FACE_{cid:03d}'


def file_name(cid):
    return f'{cid:03d}.png'


def portrait_rows(characters):
    owner_of = {}
    rows = []
    for cid in sorted(int(k) for k, v in characters.items() if v.get('portrait')):
        portrait = characters[str(cid)]['portrait']
        owner_of.setdefault(portrait, cid)
        rows.append((cid, owner_of[portrait]))
    return rows, sorted(set(owner_of.values()))


def patch_resource_h(owners):
    path = DLL / 'resource.h'
    text = path.read_bytes().decode('cp1252')
    text = re.sub(r'#define IDB_(?:PNG|FACE_)\w+\s+\d+\r?\n', '', text)
    marker = re.search(r'// Usado por HyperSubtitles\.rc\r?\n//\r?\n', text)
    if not marker:
        raise RuntimeError('resource.h: header comment not found')
    eol = '\r\n' if '\r\n' in marker.group(0) else '\n'
    defines = ''.join(f'#define {resource_name(c):<32}{2000 + c}{eol}' for c in owners)
    text = text[:marker.end()] + defines + text[marker.end():]
    text = re.sub(r'(#define _APS_NEXT_RESOURCE_VALUE\s+)\d+', lambda m: f'{m.group(1)}{2000 + max(owners) + 1}', text)
    path.write_bytes(text.encode('cp1252'))


def patch_rc(owners):
    path = DLL / 'HyperSubtitles.rc'
    text = path.read_text(encoding='utf-16')
    eol = '\r\n' if '\r\n' in text else '\n'
    block = re.search(r'IDB_(?:PNG|FACE_)\w+\s+PNG.*?(?=' + re.escape(eol) + r'#endif)', text, re.S)
    if not block:
        raise RuntimeError('HyperSubtitles.rc: PNG block not found')
    entries = (eol * 2).join(f'{resource_name(c):<24}PNG                     "faces\\\\{file_name(c)}"' for c in owners) + eol
    text = text[:block.start()] + entries + text[block.end():]
    path.write_text(text, encoding='utf-16', newline='')


def patch_dialogue_box(rows):
    path = DLL / 'DialogueBox.cpp'
    text = path.read_text(encoding='utf-8')
    table = re.search(r'k_portraits\[\] = \{(\r?\n)(.*?)\};', text, re.S)
    if not table:
        raise RuntimeError('DialogueBox.cpp: k_portraits not found')
    eol = table.group(1)
    body = ''.join(f'    {{ {c}, {resource_name(o)} }},{eol}' for c, o in rows)
    text = text[:table.start(2)] + body + text[table.end(2):]
    path.write_text(text, encoding='utf-8', newline='')


def patch_project(path, pattern, make):
    raw = path.read_bytes()
    bom = raw.startswith(b'\xef\xbb\xbf')
    text = raw.decode('utf-8-sig')
    items = list(re.finditer(pattern, text))
    if not items:
        raise RuntimeError(f'{path.name}: no faces images found')
    first = items[0].group(0)
    eol = '\r\n' if '\r\n' in first else '\n'
    indent = re.match(r'[ \t]*', first).group(0)
    inner = re.search(r'\n([ \t]*)<Filter>', first)
    text = text[:items[0].start()] + make(indent, inner.group(1) if inner else '', eol) + text[items[-1].end():]
    path.write_bytes((b'\xef\xbb\xbf' if bom else b'') + text.encode('utf-8'))


def wire(characters_path=REPO / 'data' / 'characters.json'):
    characters = json.loads(Path(characters_path).read_text(encoding='utf-8'))
    rows, owners = portrait_rows(characters)
    missing = [file_name(c) for c in owners if not (DLL / 'faces' / file_name(c)).exists()]
    if missing:
        raise RuntimeError(f'missing PNG in Dll1/faces: {", ".join(missing)} (run extract_portraits.py)')

    patch_resource_h(owners)
    patch_rc(owners)
    patch_dialogue_box(rows)
    patch_project(
        DLL / 'Dll1.vcxproj',
        r'[ \t]*<Image Include="faces\\[^"]+" />\r?\n',
        lambda ind, inner, eol: ''.join(f'{ind}<Image Include="faces\\{file_name(c)}" />{eol}' for c in owners),
    )
    patch_project(
        DLL / 'Dll1.vcxproj.filters',
        r'[ \t]*<Image Include="faces\\[^"]+">\r?\n[ \t]*<Filter>faces</Filter>\r?\n[ \t]*</Image>\r?\n',
        lambda ind, inner, eol: ''.join(
            f'{ind}<Image Include="faces\\{file_name(c)}">{eol}{inner}<Filter>faces</Filter>{eol}{ind}</Image>{eol}'
            for c in owners
        ),
    )
    print(f'{len(rows)} characters, {len(owners)} portrait resources wired into Dll1')


if __name__ == '__main__':
    wire()
