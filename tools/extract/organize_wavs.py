"""
Organize extracted WAVs into their original virtual folder structure.

Reads sound_xwb_manifest.csv, moves wav_out/<index>.wav to:
  wav_out/BGM/001.wav
  wav_out/JINGLE/00001.wav
  wav_out/VOICE/EVENT/80000201.wav
  wav_out/VOICE_EN/EVENT/80001001.wav
  wav_out/VOICE/BATTLE/00010101.wav
  wav_out/SFX/<cue_name>.wav   (entries with no HCA path)

Usage:
  python organize_wavs.py sound_xwb_manifest.csv wav_out           # move files
  python organize_wavs.py sound_xwb_manifest.csv wav_out --copy    # copy instead of move
  python organize_wavs.py sound_xwb_manifest.csv wav_out --dry-run # preview only
"""

import csv
import os
import shutil
import argparse



def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('csv',     help='Manifest CSV produced by parse_xsb.py')
    parser.add_argument('wav_dir', help='Folder with <index>.wav files from extract_xwb.py')
    parser.add_argument('--copy',    action='store_true', help='Copy instead of move')
    parser.add_argument('--dry-run', action='store_true', help='Preview without touching files')
    args = parser.parse_args()
    CSV_PATH = args.csv
    WAV_DIR  = args.wav_dir

    action = 'COPY' if args.copy else ('DRY-RUN' if args.dry_run else 'MOVE')
    print(f'Action: {action}')

    with open(CSV_PATH, newline='', encoding='utf-8') as f:
        rows = list(csv.DictReader(f))

    moved = skipped = missing = 0

    for row in rows:
        wave_entry = int(row['wave_entry'])
        cue_name   = row['cue_name']
        vpath      = row['hca_virtual_path']

        src = os.path.join(WAV_DIR, f'{wave_entry:05d}.wav')
        if not os.path.exists(src):
            missing += 1
            continue

        if vpath:
            # e.g. /VOICE_EN/EVENT/80001001.hca -> VOICE_EN/EVENT/80001001.wav
            rel = vpath.lstrip('/').replace('.hca', '.wav')
        else:
            # SFX: no HCA virtual path, accessed by cue name via Path B — leave in place
            skipped += 1
            continue

        dst = os.path.join(WAV_DIR, rel)

        if src == dst:
            skipped += 1
            continue

        if not args.dry_run:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if args.copy:
                shutil.copy2(src, dst)
            else:
                shutil.move(src, dst)

        print(f'  {"[DRY]" if args.dry_run else ""} {wave_entry:05d}.wav -> {rel}')
        moved += 1

    print(f'\nDone: {moved} {"copied" if args.copy else "moved"}, {skipped} skipped, {missing} source files missing')


if __name__ == '__main__':
    main()
