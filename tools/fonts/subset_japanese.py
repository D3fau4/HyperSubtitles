"""
Builds external/fonts/NotoSansJP-Regular.otf, the Japanese font embedded in the
editor UI (the ja transcriptions), by subsetting Noto Sans JP to:

- every character of cp932 (Shift-JIS as used by the game's event scripts:
  kana, JIS level 1/2 kanji, NEC/IBM extensions, full-width forms), and
- any other character found in data/lines/*.json that the font has.

Usage (needs fonttools: pip install fonttools):
  curl -LO https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/SubsetOTF/JP/NotoSansJP-Regular.otf
  python tools/fonts/subset_japanese.py NotoSansJP-Regular.otf
"""

import argparse
import json
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont

REPO = Path(__file__).resolve().parents[2]
DEFAULT_OUT = REPO / "external" / "fonts" / "NotoSansJP-Regular.otf"


def cp932_characters() -> set[str]:
    chars = {chr(c) for c in range(0x20, 0x7F)}
    for lead in list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD)):
        for trail in range(0x40, 0xFD):
            try:
                chars.update(bytes((lead, trail)).decode("cp932"))
            except UnicodeDecodeError:
                pass
    for single in range(0xA1, 0xE0):  # half-width katakana
        chars.update(bytes((single,)).decode("cp932"))
    return chars


def data_characters() -> set[str]:
    chars: set[str] = set()
    for path in (REPO / "data" / "lines").glob("*.json"):
        for entry in json.loads(path.read_text(encoding="utf-8")).values():
            for lang in ("ja", "en"):
                chars.update(entry.get(lang, {}).get("text", ""))
    return chars


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", help="NotoSansJP-Regular.otf from notofonts/noto-cjk")
    parser.add_argument("-o", "--output", default=str(DEFAULT_OUT))
    args = parser.parse_args()

    cmap = TTFont(args.source).getBestCmap()
    wanted = cp932_characters() | data_characters()
    unicodes = sorted(ord(c) for c in wanted if ord(c) in cmap)
    # cp932 maps its user-defined area to the Private Use Area: nothing to draw there.
    missing = sorted(c for c in wanted if ord(c) not in cmap and not c.isspace() and not 0xE000 <= ord(c) <= 0xF8FF)

    options = subset.Options()
    options.layout_features = []      # ImGui does no shaping
    options.name_IDs = ["*"]          # keep copyright/license names (OFL)
    options.notdef_outline = True
    options.hinting = False
    font = subset.load_font(args.source, options)
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=unicodes)
    subsetter.subset(font)
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    subset.save_font(font, args.output, options)

    size = Path(args.output).stat().st_size
    print(f"{args.output}: {len(unicodes)} characters, {size / 1024 / 1024:.2f} MB")
    if missing:
        print(f"not in the font ({len(missing)}): {''.join(missing[:80])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
