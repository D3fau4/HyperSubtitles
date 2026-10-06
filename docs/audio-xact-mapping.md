# NeptuniaReBirth1 — CriWare Path → XACT Index Reverse Engineering Report

## Binary
- File: `NeptuniaReBirth1_patched.exe`
- MD5: `13838428093b2b868c42ea576f491899`
- Arch: x86-32, imagebase `0xAF0000`

---

## Overview

The game replaces the CriWare CriAtomEx audio runtime with a custom XACT 3 backend.
When CriAtomEx API is called (e.g., `criAtomExPlayer_SetFile`, `criAtomExPlayer_SetCueId`),
the game intercepts and translates it into an XACT cue index, then plays via
`IXACT3SoundBank::Play`.

Two translation paths exist depending on the call site.

---

## Path A — HCA virtual file path → XACT cue name

**Triggered by:** `criAtomExPlayer_SetFile(player, binder, "/VOICE/EVENT/0001234.hca")`

**Key functions:**

| Address    | Name |
|------------|------|
| `0xD53100` | `XactCore_SetPlayerFileCueFromPath` |
| `0xD52D40` | `XactCore_CueExistsForVirtualPath` |
| `0xD4EBF0` | `AudioResource_ResolveVirtualSoundPath` (CriFS hook) |

**Algorithm:**
```
1. basename = virtual_path.rsplit('/', 1)[-1]       // strrchr(path, '/') + 1
2. cue_name = basename[:basename.rfind('.')]         // strip extension
3. if "VOICE_EN" in virtual_path: cue_name += 'e'   // English voice suffix
4. cue_index = IXACT3SoundBank::GetCueIndex(cue_name)
```

**Examples:**

| Virtual path | XACT cue name |
|---|---|
| `/VOICE/EVENT/0001234.hca` | `0001234` |
| `/VOICE_EN/EVENT/0001234.hca` | `0001234e` |
| `/BGM/001.hca` | `001` |
| `/JINGLE/00001.hca` | `00001` |
| `/VOICE/BATTLE/00001234.hca` | `00001234` |

---

## Path B — CriAtomEx ACB handle + cue ID → XACT cue name

**Triggered by:** `criAtomExPlayer_SetCueId(player, acb_handle, cue_id)`

**Key functions:**

| Address    | Name |
|------------|------|
| `0xD4F290` | `criAtomExAcb_LoadAcbData_MapHashToCueBase` |
| `0xD4F5C0` | `criAtomExPlayer_SetCueId_Compat` |
| `0xD50700` | `AudioSystem_LoadCueHashTables` |
| `0xD53210` | `XactCore_SetPlayerCueByName` |

**Data files** (loaded from `data/SYSTEM/`):

| File | Entry size | Count | Description |
|------|-----------|-------|-------------|
| `SeHashIndex.dat` | 2 bytes (uint16) | filesize/2 | Lookup table: `[md5[0]*256+md5[1]]` → SeHashList start index |
| `SeHashList.dat`  | 18 bytes | filesize/18 | 16-byte MD5 + uint16 cue_base |
| `WavNameList.dat` | 64 bytes | filesize/64 | Fixed-width null-padded ASCII cue name strings |

**ACB load phase** (`criAtomExAcb_LoadAcbData_MapHashToCueBase` @ `0xD4F290`):
```
1. md5 = MD5(acb_file_bytes)               // via mbedtls_md5
2. key = (md5[0] << 8) | md5[1]           // first two bytes as big-endian uint16
3. entry_idx = SeHashIndex[key]            // uint16 at file offset key*2
4. loop while entry_idx < entry_count:
     if SeHashList[entry_idx][0:16] == md5:
         cue_base = SeHashList[entry_idx][16:18]  // uint16 LE
         break
     entry_idx += 1
5. acb_record = { cue_base, md5 }
```

**SetCueId phase** (`criAtomExPlayer_SetCueId_Compat` @ `0xD4F5C0`):
```
1. cue_name = WavNameList[acb_record.cue_base + cue_id]  // 64-byte slot
2. cue_index = IXACT3SoundBank::GetCueIndex(cue_name)    // -> uint16
3. player.cue_index = cue_index                           // at player+24
```

**SeHashList entry layout (18 = 0x12 bytes):**
```
offset  size  field
  0      16   MD5 of ACB file bytes (full 16-byte match required)
 16       2   cue_base  (uint16 LE) — base index into WavNameList
```

**SeHashIndex lookup (disasm @ `0xD4F308`–`0xD4F31D`):**
```asm
movzx   ecx, [md5+0]                        ; md5[0]
movzx   eax, [md5+1]                        ; md5[1]
shl     ecx, 8                               ; md5[0] * 256
add     ecx, eax                             ; key = (md5[0]<<8) | md5[1]
movzx   edi, word [g_se_hash_index + ecx*2] ; SeHashIndex[key] -> entry_idx
```

**MD5 comparison (disasm @ `0xD4F345`–`0xD4F35F`):**
```asm
mov     esi, 0Ch          ; 4 iterations * 4 bytes = 16 bytes total
loop:
  cmp [edx], [eax]        ; compare 4 bytes of SeHashList vs MD5
  jnz mismatch
  add edx, 4 / add eax, 4
  sub esi, 4
  jnb loop                ; loop while esi >= 0 (4 iters: 12,8,4,0 -> -4 exits)
; fall through = full 16-byte match
movzx eax, word [g_se_hash_list + edi*18 + 16]  ; read cue_base uint16
```

---

## XACT Sound Bank Files

| File | Purpose |
|------|---------|
| `data/SOUND.xgs` | Global XACT settings (`IXACT3Engine::Initialize`) |
| `data/SOUND_S.xwb` | In-memory wave bank (file-mapped) |
| `data/SOUND.xwb` | Streaming wave bank |
| `data/SOUND.xsb` | Sound bank with all cue definitions |

`SOUND.xsb` cue names exactly match the strings produced by both paths above.
`IXACT3SoundBank::GetCueIndex` returns `0xFFFF` if cue not found.

---

## Initialization call chain

```
AudioSystem_InitXactRuntime (0xD504F0)
  └─ XactCore_InitSoundBanks (0xD52990)
       ├─ XACT3CreateEngine()
       ├─ IXACT3Engine::Initialize(SOUND.xgs)
       ├─ IXACT3Engine::CreateInMemoryWaveBank(SOUND_S.xwb)
       └─ XactCore_LoadStreamingWaveAndSoundBank(SOUND.xwb, SOUND.xsb)
  └─ AudioSystem_LoadCueHashTables (0xD50700)
       ├─ SeHashIndex.dat -> g_se_hash_index  (count = filesize>>1)
       ├─ SeHashList.dat  -> g_se_hash_list   (count = filesize/18)
       └─ WavNameList.dat -> g_wav_name_list  (count = filesize>>6)
```

---

## Python Implementation

See `tools/extract/criware_path_to_xact.py`.

```python
from criware_path_to_xact import XactCueLookup, hca_path_to_xact_cue_name

# Path A (no data files needed)
cue_name = hca_path_to_xact_cue_name('/VOICE/EVENT/0001234.hca')  # -> '0001234'
cue_name = hca_path_to_xact_cue_name('/VOICE_EN/EVENT/0001234.hca')  # -> '0001234e'

# Path B (needs data/SYSTEM/*.dat files extracted from CPK)
lookup = XactCueLookup(r'path\to\data\SYSTEM')
with open('SystemSE.acb', 'rb') as f:
    acb_bytes = f.read()
cue_name = lookup.from_acb_and_cue_id(acb_bytes, cue_id=5)
```

---

## Key Addresses Summary

| Address    | Name | Notes |
|------------|------|-------|
| `0xD4EBF0` | `AudioResource_ResolveVirtualSoundPath` | CriFS file-open hook; redirects data/SOUND to XACT |
| `0xD4F290` | `criAtomExAcb_LoadAcbData_MapHashToCueBase` | Hashes ACB bytes; looks up cue_base via SeHash* |
| `0xD4F5C0` | `criAtomExPlayer_SetCueId_Compat` | WavNameList[cue_base+cue_id] -> XACT GetCueIndex |
| `0xD504F0` | `AudioSystem_InitXactRuntime` | Top-level audio init |
| `0xD50700` | `AudioSystem_LoadCueHashTables` | Loads SeHashIndex/SeHashList/WavNameList |
| `0xD52990` | `XactCore_InitSoundBanks` | XACT3Engine + wave/sound bank setup |
| `0xD52D40` | `XactCore_CueExistsForVirtualPath` | Path A: existence check via GetCueIndex |
| `0xD53100` | `XactCore_SetPlayerFileCueFromPath` | Path A: stores cue_index into player struct |
| `0xD53210` | `XactCore_SetPlayerCueByName` | Calls GetCueIndex(name) -> player+24 |
