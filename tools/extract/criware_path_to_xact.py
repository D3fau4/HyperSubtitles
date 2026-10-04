"""
NeptuniaReBirth1 - CriWare path to XACT cue index converter.

Reverse-engineered from NeptuniaReBirth1_patched.exe:

Two code paths exist depending on how the sound is triggered:

PATH A: criAtomExPlayer_SetFile (HCA virtual path)
  XactCore_SetPlayerFileCueFromPath (0xD53100) / XactCore_CueExistsForVirtualPath (0xD52D40)
  - Extract basename from virtual path (after last '/')
  - Strip file extension (before last '.')
  - If "VOICE_EN" anywhere in path, append 'e' to cue name
  - Call IXACT3SoundBank::GetCueIndex(cue_name) -> XACT cue index

PATH B: criAtomExPlayer_SetCueId (ACB handle + cue ID)
  criAtomExAcb_LoadAcbData_MapHashToCueBase (0xD4F290) + criAtomExPlayer_SetCueId_Compat (0xD4F5C0)
  - Compute MD5 of raw ACB file bytes
  - key = (md5[0] << 8) | md5[1]
  - entry_idx = SeHashIndex[key]  (uint16 array, 65536 entries)
  - Scan SeHashList from entry_idx; find entry whose first 16 bytes == MD5
  - cue_base = SeHashList[entry_idx].cue_base  (uint16 at byte offset 16)
  - cue_name = WavNameList[cue_base + cue_id]  (64-byte fixed-width string)
  - Call IXACT3SoundBank::GetCueIndex(cue_name) -> XACT cue index

Data files (inside data/SYSTEM CPK or loose):
  SeHashIndex.dat  - uint16[65536], indexed by (md5[0]<<8)|md5[1]
  SeHashList.dat   - 18-byte records: uint8[16] md5 + uint16 cue_base
  WavNameList.dat  - 64-byte fixed-width null-padded ASCII cue name strings
"""

import hashlib
import struct
import os
import sys


# ---------------------------------------------------------------------------
# Data file loaders
# ---------------------------------------------------------------------------

def load_se_hash_index(path: str) -> tuple:
    """Load SeHashIndex.dat -> tuple of uint16, length 65536."""
    with open(path, 'rb') as f:
        data = f.read()
    count = len(data) // 2
    return struct.unpack(f'<{count}H', data[:count * 2])


def load_se_hash_list(path: str) -> list:
    """Load SeHashList.dat -> list of (bytes md5_16, int cue_base)."""
    with open(path, 'rb') as f:
        data = f.read()
    entries = []
    for i in range(0, len(data) - 17, 18):
        md5_bytes = data[i:i + 16]
        cue_base = struct.unpack_from('<H', data, i + 16)[0]
        entries.append((md5_bytes, cue_base))
    return entries


def load_wav_name_list(path: str) -> list:
    """Load WavNameList.dat -> list of cue name strings (64-byte entries)."""
    with open(path, 'rb') as f:
        data = f.read()
    names = []
    for i in range(0, len(data), 64):
        entry = data[i:i + 64]
        name = entry.split(b'\x00', 1)[0].decode('ascii', errors='replace')
        names.append(name)
    return names


# ---------------------------------------------------------------------------
# PATH A: HCA virtual path -> XACT cue name
# ---------------------------------------------------------------------------

def hca_path_to_xact_cue_name(virtual_path: str) -> str:
    """
    Convert a CriWare virtual HCA path to the XACT sound bank cue name.

    Examples:
        '/VOICE/EVENT/0001234.hca'    -> '0001234'
        '/VOICE_EN/EVENT/0001234.hca' -> '0001234e'
        '/BGM/001.hca'                -> '001'
        '/JINGLE/00001.hca'           -> '00001'

    Matches XactCore_SetPlayerFileCueFromPath / XactCore_CueExistsForVirtualPath.
    """
    # strrchr(virtual_path, '/') + 1
    basename = virtual_path.rsplit('/', 1)[-1]
    # *strrchr(cue_name, '.') = 0  -> strip extension
    dot = basename.rfind('.')
    cue_name = basename[:dot] if dot != -1 else basename
    # if strstr(virtual_path, "VOICE_EN"): strcat_s(cue_name, "e")
    if 'VOICE_EN' in virtual_path:
        cue_name += 'e'
    return cue_name


# ---------------------------------------------------------------------------
# PATH B: ACB file + cue ID -> XACT cue name  (hash table lookup)
# ---------------------------------------------------------------------------

def acb_data_to_cue_base(
    acb_bytes: bytes,
    se_hash_index: tuple,
    se_hash_list: list,
) -> int:
    """
    Compute MD5 of ACB bytes, look up in SeHashIndex/SeHashList,
    return the cue_base index (uint16).  Returns -1 if not found.

    Matches criAtomExAcb_LoadAcbData_MapHashToCueBase (0xD4F290).
    """
    md5 = hashlib.md5(acb_bytes).digest()

    # SeHashIndex lookup: key = (md5[0] << 8) | md5[1]
    key = (md5[0] << 8) | md5[1]
    if key >= len(se_hash_index):
        return -1

    entry_idx = se_hash_index[key]

    # Linear scan SeHashList from entry_idx; compare full 16-byte MD5
    while entry_idx < len(se_hash_list):
        entry_md5, cue_base = se_hash_list[entry_idx]
        if entry_md5 == md5:
            return cue_base
        entry_idx += 1

    return -1


def cue_id_to_xact_cue_name(
    acb_bytes: bytes,
    cue_id: int,
    se_hash_index: tuple,
    se_hash_list: list,
    wav_name_list: list,
) -> str | None:
    """
    Convert raw ACB file bytes + CriAtomEx cue ID to XACT cue name string.

    Matches criAtomExPlayer_SetCueId_Compat (0xD4F5C0):
        cue_name = WavNameList[cue_base + cue_id]

    Returns None if ACB not found in hash tables.
    """
    cue_base = acb_data_to_cue_base(acb_bytes, se_hash_index, se_hash_list)
    if cue_base < 0:
        return None
    global_idx = cue_base + cue_id
    if global_idx >= len(wav_name_list):
        return None
    return wav_name_list[global_idx]


# ---------------------------------------------------------------------------
# Convenience: load all tables from a directory
# ---------------------------------------------------------------------------

class XactCueLookup:
    """Loads SeHashIndex/SeHashList/WavNameList and exposes both path->cue helpers."""

    def __init__(self, data_system_dir: str):
        self.se_hash_index = load_se_hash_index(
            os.path.join(data_system_dir, 'SeHashIndex.dat'))
        self.se_hash_list = load_se_hash_list(
            os.path.join(data_system_dir, 'SeHashList.dat'))
        self.wav_name_list = load_wav_name_list(
            os.path.join(data_system_dir, 'WavNameList.dat'))

    def from_hca_path(self, virtual_path: str) -> str:
        """PATH A: virtual .hca path -> XACT cue name (no table lookup needed)."""
        return hca_path_to_xact_cue_name(virtual_path)

    def from_acb_and_cue_id(self, acb_bytes: bytes, cue_id: int) -> str | None:
        """PATH B: raw ACB bytes + cue ID -> XACT cue name."""
        return cue_id_to_xact_cue_name(
            acb_bytes, cue_id,
            self.se_hash_index, self.se_hash_list, self.wav_name_list)


# ---------------------------------------------------------------------------
# Demo / self-test
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    print('=== PATH A: HCA virtual path -> XACT cue name ===')
    test_cases = [
        ('/VOICE/EVENT/0001234.hca',    '0001234'),
        ('/VOICE_EN/EVENT/0001234.hca', '0001234e'),
        ('/BGM/001.hca',                '001'),
        ('/JINGLE/00001.hca',           '00001'),
        ('/VOICE/BATTLE/00001234.hca',  '00001234'),
        ('/VOICE_EN/BATTLE/00001234.hca','00001234e'),
    ]
    all_ok = True
    for path, expected in test_cases:
        result = hca_path_to_xact_cue_name(path)
        status = 'OK' if result == expected else 'FAIL'
        if status == 'FAIL':
            all_ok = False
        print(f'  [{status}] {path!r:45s} -> {result!r}  (expected {expected!r})')
    print(f'All PATH A tests: {"PASS" if all_ok else "FAIL"}')

    print()
    print('=== PATH B: requires data/SYSTEM/*.dat files ===')
    data_dir = sys.argv[1] if len(sys.argv) > 1 else ''
    if data_dir and os.path.isdir(data_dir):
        lookup = XactCueLookup(data_dir)
        print(f'  SeHashIndex entries : {len(lookup.se_hash_index)}')
        print(f'  SeHashList  entries : {len(lookup.se_hash_list)}')
        print(f'  WavNameList entries : {len(lookup.wav_name_list)}')
        print('  Tables loaded OK.')
    else:
        print(f'  Directory not found: {data_dir!r}')
        print('  Skipping PATH B test.')
