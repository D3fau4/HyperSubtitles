"""
SOUND.xwb extractor — NeptuniaReBirth1.
Extracts MS-ADPCM entries as WAV files, named by wave index.

Usage:
  python extract_xwb.py SOUND.xwb                                  # extract all
  python extract_xwb.py SOUND.xwb --index 30 37                    # extract specific indices only
  python extract_xwb.py SOUND.xwb --csv sound_xwb_manifest.csv     # extract indices listed in manifest CSV
  python extract_xwb.py SOUND.xwb --csv sound_xwb_manifest.csv --prefix VOICE_EN/

Output: <out>/<index>.wav  (default ./wav_out)
"""

import struct
import os
import sys
import csv
import argparse




# ---------------------------------------------------------------------------
# WAV header builder for MS-ADPCM
# ---------------------------------------------------------------------------

def adpcm_wav_header(n_channels, sample_rate, n_block_align):
    """Build WAV header for MS-ADPCM (wFormatTag=0x0002)."""
    bits_per_sample   = 4
    samples_per_block = ((n_block_align - 7 * n_channels) * 2 // n_channels) + 2
    avg_bytes_per_sec = (sample_rate * n_block_align) // samples_per_block

    fmt_data = struct.pack('<HHIIHHHh',
        0x0002,            # wFormatTag = WAVE_FORMAT_ADPCM
        n_channels,        # nChannels
        sample_rate,       # nSamplesPerSec
        avg_bytes_per_sec, # nAvgBytesPerSec
        n_block_align,     # nBlockAlign
        bits_per_sample,   # wBitsPerSample
        2,                 # cbSize = 2 (extra bytes)
        samples_per_block, # wSamplesPerBlock
    )
    return fmt_data, samples_per_block


def write_wav(out_path, n_channels, sample_rate, n_block_align, pcm_data):
    fmt_data, _ = adpcm_wav_header(n_channels, sample_rate, n_block_align)
    data_size   = len(pcm_data)
    riff_size   = 4 + (8 + len(fmt_data)) + (8 + data_size)

    with open(out_path, 'wb') as f:
        f.write(b'RIFF')
        f.write(struct.pack('<I', riff_size))
        f.write(b'WAVE')
        f.write(b'fmt ')
        f.write(struct.pack('<I', len(fmt_data)))
        f.write(fmt_data)
        f.write(b'data')
        f.write(struct.pack('<I', data_size))
        f.write(pcm_data)


# ---------------------------------------------------------------------------
# XWB parser
# ---------------------------------------------------------------------------

class XwbReader:
    META_ELEM_SIZE = 24

    def __init__(self, xwb_path):
        self.path = xwb_path
        self.f    = open(xwb_path, 'rb')
        hdr       = self.f.read(12 + 5 * 8)
        segs = []
        for i in range(5):
            off = 12 + i * 8
            seg_off = struct.unpack_from('<I', hdr, off)[0]
            seg_len = struct.unpack_from('<I', hdr, off + 4)[0]
            segs.append((seg_off, seg_len))

        # BankData
        self.f.seek(segs[0][0])
        bd = self.f.read(96)
        self.num_entries = struct.unpack_from('<I', bd, 4)[0]
        self.meta_elem_size = struct.unpack_from('<I', bd, 72)[0]

        self.meta_base      = segs[1][0]
        self.wave_data_base = segs[4][0]

    def close(self):
        self.f.close()

    def get_entry_meta(self, idx):
        """Return (n_channels, sample_rate, n_block_align, play_offset, play_length)."""
        self.f.seek(self.meta_base + idx * self.meta_elem_size)
        entry = self.f.read(self.meta_elem_size)
        fmt = struct.unpack_from('<I', entry, 4)[0]

        tag       = fmt & 0x3
        n_ch      = (fmt >> 2) & 0x7
        rate      = (fmt >> 5) & 0x3FFFF
        blk_raw   = (fmt >> 23) & 0xFF
        n_blk_align = (blk_raw + 22) * n_ch

        play_off = struct.unpack_from('<I', entry, 8)[0]
        play_len = struct.unpack_from('<I', entry, 12)[0]
        return n_ch, rate, n_blk_align, play_off, play_len

    def read_wave_data(self, play_offset, play_length):
        self.f.seek(self.wave_data_base + play_offset)
        return self.f.read(play_length)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('xwb', help='Path to SOUND.xwb or SOUND_S.xwb')
    parser.add_argument('-o', '--out', default='wav_out', help='Output folder')
    parser.add_argument('--index', nargs='+', type=int,
                        help='Extract specific wave indices only')
    parser.add_argument('--csv', default=None,
                        help='Extract indices listed in this manifest CSV')
    parser.add_argument('--prefix', nargs='+', default=None,
                        help='With --csv, only rows whose hca_virtual_path starts with one of these')
    args = parser.parse_args()

    xwb_path = args.xwb
    out_dir  = args.out
    csv_path = args.csv

    os.makedirs(out_dir, exist_ok=True)
    reader = XwbReader(xwb_path)
    print(f'{xwb_path}  ({reader.num_entries} entries)')

    # Determine which indices to extract
    if args.index:
        indices = args.index
    elif args.csv:
        with open(csv_path, newline='', encoding='utf-8') as f:
            indices = [int(row['wave_entry']) for row in csv.DictReader(f)
                       if not args.prefix or row['hca_virtual_path'].lstrip('/').startswith(tuple(x.lstrip('/') for x in args.prefix))]
        print(f'  Loaded {len(indices)} indices from CSV')
    else:
        indices = range(reader.num_entries)

    total = len(list(indices)) if not isinstance(indices, range) else reader.num_entries
    done  = 0
    skip  = 0

    for idx in indices:
        if idx < 0 or idx >= reader.num_entries:
            print(f'  SKIP {idx}: out of range [0, {reader.num_entries})')
            skip += 1
            continue

        out_path = os.path.join(out_dir, f'{idx:05d}.wav')
        if os.path.exists(out_path):
            skip += 1
            done += 1
            continue

        n_ch, rate, n_blk, play_off, play_len = reader.get_entry_meta(idx)
        wave_data = reader.read_wave_data(play_off, play_len)
        write_wav(out_path, n_ch, rate, n_blk, wave_data)
        done += 1

        if done % 500 == 0 or done <= 5:
            print(f'  [{done}/{total}] {idx:05d}.wav  ch={n_ch} rate={rate} len={play_len}')

    reader.close()
    print(f'\nDone. {done} extracted ({skip} skipped/cached) -> {out_dir}')


if __name__ == '__main__':
    main()
