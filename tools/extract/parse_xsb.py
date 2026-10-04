"""
SOUND.xsb parser — NeptuniaReBirth1.
Produces correct wave_bank, wave_entry -> cue_name -> HCA virtual path mapping.

Sound entry format (absolute file offset):
  byte[7]    = entryLength (19, 23, or 47)
  19/23-byte: bank=byte[11], wave_entry=uint16_LE(bytes 9-10)
  47-byte:    bank=byte[41], wave_entry=uint16_LE(bytes 39-40)
  bank 0 = "SOUND"   (SOUND.xwb,   16623 entries, streaming)
  bank 1 = "SOUND_S" (SOUND_S.xwb,  2739 entries, in-memory)

Simple cues (19330): flags(1) + sound_ref_u32(4) = 5 bytes each.
Complex cues (30):   flags(1) + sound_ref_u32(4) + 10 bytes = 15 bytes each.
  Complex cues are BGM tracks 001-042 (32 titles, no 002-004/016/018/025/029-031).
"""

import struct
import sys
import os
import csv
import argparse

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')



def u8(data, o):  return data[o]
def u16(data, o): return struct.unpack_from('<H', data, o)[0]
def u32(data, o): return struct.unpack_from('<I', data, o)[0]


# ---------------------------------------------------------------------------
# Cue name -> HCA virtual path
# ---------------------------------------------------------------------------

def cue_to_virtual_path(cue_name: str) -> str:
    """Return HCA virtual path for Path-A cues, or '' for SFX/unknown."""
    en   = cue_name.endswith('e') and cue_name[:-1].isdigit()
    base = cue_name[:-1] if en else cue_name
    if not base.isdigit():
        return ''          # SFX / ACB-based, no HCA path
    n = len(base)
    if n == 3: return f'/BGM/{base}.hca'
    if n == 5: return f'/JINGLE/{base}.hca'
    if n in (7, 8):
        sub = 'EVENT' if base[0] == '8' else 'BATTLE'
        return f'/VOICE{"_EN" if en else ""}/{sub}/{base}.hca'
    return ''


# ---------------------------------------------------------------------------
# Parse
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description='Build the SOUND.xwb wave entry -> cue name -> HCA path manifest.')
    parser.add_argument('xsb', help='Path to SOUND.xsb')
    parser.add_argument('-o', '--output', default='sound_xwb_manifest.csv', help='Output CSV')
    args = parser.parse_args()
    XSB_PATH = args.xsb
    OUT_CSV  = args.output

    print(f'Reading {XSB_PATH} ...')
    with open(XSB_PATH, 'rb') as f:
        data = f.read()
    print(f'  Size: {len(data)} bytes')

    SIMPLE_OFF    = u32(data, 0x22)
    COMPLEX_OFF   = u32(data, 0x26)
    NUM_SIMPLE    = u16(data, 0x13)
    NUM_COMPLEX   = u16(data, 0x15)
    CUE_NAMES_OFF = u32(data, 0x2A)

    print(f'  numSimpleCues  = {NUM_SIMPLE}')
    print(f'  numComplexCues = {NUM_COMPLEX}')
    print(f'  simpleCuesOff  = {SIMPLE_OFF}')
    print(f'  complexCuesOff = {COMPLEX_OFF}')
    print(f'  cueNamesOff    = {CUE_NAMES_OFF}')

    # Parse simple cue names
    names = []
    off = CUE_NAMES_OFF
    for _ in range(NUM_SIMPLE):
        end = data.index(b'\x00', off)
        names.append(data[off:end].decode('ascii', errors='replace'))
        off = end + 1

    # Parse complex cue names (immediately follow simple names)
    complex_names = []
    while off < len(data):
        try:
            end = data.index(b'\x00', off)
            complex_names.append(data[off:end].decode('ascii', errors='replace'))
            off = end + 1
        except ValueError:
            break

    def decode_sound_ref(sref):
        elen = u8(data, sref + 7)
        if elen in (19, 23):
            return u8(data, sref + 11), u16(data, sref + 9), elen
        elif elen == 47:
            return u8(data, sref + 41), u16(data, sref + 39), elen
        return None, None, elen

    rows_s  = []   # bank 0 = SOUND.xwb
    rows_ss = []   # bank 1 = SOUND_S.xwb
    unknown = []
    entry_sizes = {}

    # Simple cues: 5 bytes each
    for i in range(NUM_SIMPLE):
        sound_ref = u32(data, SIMPLE_OFF + i * 5 + 1)
        cue_name  = names[i]
        bank, wave_entry, elen = decode_sound_ref(sound_ref)
        entry_sizes[elen] = entry_sizes.get(elen, 0) + 1
        if bank is None:
            unknown.append((i, cue_name, sound_ref, elen))
            continue
        vpath = cue_to_virtual_path(cue_name)
        row = (wave_entry, cue_name, vpath)
        (rows_s if bank == 0 else rows_ss).append(row)

    # Complex cues: 15 bytes each
    for i in range(NUM_COMPLEX):
        sound_ref = u32(data, COMPLEX_OFF + i * 15 + 1)
        cue_name  = complex_names[i] if i < len(complex_names) else f'complex_{i}'
        bank, wave_entry, elen = decode_sound_ref(sound_ref)
        entry_sizes[elen] = entry_sizes.get(elen, 0) + 1
        if bank is None:
            unknown.append((-i-1, cue_name, sound_ref, elen))
            continue
        vpath = cue_to_virtual_path(cue_name)
        row = (wave_entry, cue_name, vpath)
        (rows_s if bank == 0 else rows_ss).append(row)

    # Deduplicate: for each wave_entry prefer row with non-empty hca_virtual_path
    from collections import defaultdict
    best: dict[int, tuple] = {}
    for wave_entry, cue_name, vpath in rows_s:
        if wave_entry not in best or (not best[wave_entry][2] and vpath):
            best[wave_entry] = (wave_entry, cue_name, vpath)
    deduped = sorted(best.values())

    # Stats
    print(f'\nEntry length distribution: {entry_sizes}')
    if unknown:
        print(f'Unknown entry lengths: {len(unknown)} (first 5: {unknown[:5]})')
    print(f'Bank 0 (SOUND.xwb):   {len(rows_s)} cues, {len(deduped)} unique wave entries')
    print(f'Bank 1 (SOUND_S.xwb): {len(rows_ss)} cues')

    # Write deduped SOUND.xwb CSV
    header = ['wave_entry', 'cue_name', 'hca_virtual_path']
    with open(OUT_CSV, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(deduped)
    print(f'\nWrote {OUT_CSV} ({len(deduped)} unique wave entries)')

    # Write full (all cues) for reference
    rows_s.sort()
    all_csv = OUT_CSV.replace('.csv', '_all_cues.csv')
    with open(all_csv, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows_s)
    print(f'Wrote {all_csv} ({len(rows_s)} total cues)')

    # Preview by type
    from collections import defaultdict as dd2
    by_type = dd2(list)
    for r in deduped:
        p = r[2]
        t = ('SFX' if not p else
             'BGM' if '/BGM/' in p else
             'JINGLE' if '/JINGLE/' in p else
             'VOICE_EN' if 'VOICE_EN' in p else
             'VOICE' if 'VOICE' in p else 'OTHER')
        by_type[t].append(r)

    print('\nDeduped summary by type:')
    for t, rs in sorted(by_type.items()):
        wmin = min(r[0] for r in rs)
        wmax = max(r[0] for r in rs)
        print(f'  {t:12s}: {len(rs):5d} entries, wave [{wmin:5d}, {wmax:5d}]')

    print('\nSample rows:')
    for t in ['BGM', 'JINGLE', 'VOICE', 'VOICE_EN', 'SFX']:
        rs = by_type.get(t, [])
        for r in rs[:2]:
            print(f'  wave={r[0]:5d}  cue={r[1]:20s}  {r[2] or "(SFX)"}')

    max_s = max(r[0] for r in deduped)
    print(f'\nMax wave_entry: {max_s} < 16623 → valid: {max_s < 16623}')


if __name__ == '__main__':
    main()
