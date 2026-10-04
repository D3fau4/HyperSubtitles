"""
Voice line database for HyperSubtitles.

data/lines/battle.json and data/lines/event.json hold one entry per voice id:

  "80101001": {
    "character": 1,
    "ja": {"text": "...", "source": "whisper", "duration": 2.41},
    "en": {"text": "...", "source": "script", "duration": 2.73},
    "es": "",
    "status": "pending"
  }

character uses the game's event speaker ids (data/characters.json): event lines
take it from the script, battle lines from the 4-digit id prefix ("battle" field).
ja / en are present when that language has audio or text. duration is present
only when the audio exists. source is "script" (game event script), "whisper"
or "manual" (edited by hand; never overwritten by build).

Commands:
  build             merge audio, Whisper transcripts and event script text into data/lines
  export-po         write a PO for translation (msgctxt = CATEGORY/id, msgstr = es)
  import-po         read es translations back from a PO
  export-subtitles  write the subtitles.json loaded by the DLL (es, else en; one entry
                    per language that has audio, each with its own duration)
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

CATEGORIES = ("BATTLE", "EVENT")
LANGUAGES = (("ja", "VOICE"), ("en", "VOICE_EN"))
REPO = Path(__file__).resolve().parents[2]
DEFAULT_DATA = REPO / "data" / "lines"


def wav_duration(path: Path) -> float:
    with path.open("rb") as f:
        if f.read(4) != b"RIFF":
            raise ValueError(f"not a RIFF file: {path}")
        f.read(4)
        if f.read(4) != b"WAVE":
            raise ValueError(f"not a WAVE file: {path}")
        fmt = None
        data_size = None
        while True:
            header = f.read(8)
            if len(header) < 8:
                break
            chunk_id, size = struct.unpack("<4sI", header)
            body = f.read(size + (size & 1))
            if chunk_id == b"fmt ":
                fmt = body[:size]
            elif chunk_id == b"data":
                data_size = size
        if fmt is None or data_size is None:
            raise ValueError(f"missing fmt or data chunk: {path}")
    tag, channels, rate, avg_bytes, block_align = struct.unpack_from("<HHIIH", fmt)
    if tag == 0x0002 and len(fmt) >= 20:
        samples_per_block = struct.unpack_from("<H", fmt, 18)[0]
        blocks, rest = divmod(data_size, block_align)
        samples = blocks * samples_per_block
        if rest:
            samples += max(0, (rest - 7 * channels) * 2 // channels + 2)
        return samples / rate
    return data_size / avg_bytes


def load_category(data_dir: Path, category: str) -> dict:
    path = data_dir / f"{category.lower()}.json"
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def save_category(data_dir: Path, category: str, lines: dict) -> Path:
    data_dir.mkdir(parents=True, exist_ok=True)
    path = data_dir / f"{category.lower()}.json"
    text = json.dumps(dict(sorted(lines.items())), ensure_ascii=False, indent=2) + "\n"
    path.write_text(text, encoding="utf-8", newline="\n")
    return path


def battle_characters() -> dict[str, int]:
    path = REPO / "data" / "characters.json"
    if not path.exists():
        return {}
    characters = json.loads(path.read_text(encoding="utf-8"))
    return {info["battle"]: int(cid) for cid, info in characters.items() if "battle" in info}


def default_character(category: str, voice_id: str, script: dict | None, battle: dict[str, int]) -> int:
    if script is not None:
        return script["speaker"]
    if category == "BATTLE":
        return battle.get(voice_id[:4], -1)
    return -1


def read_transcript(transcripts: tuple[Path, bool] | None, folder: str, category: str, voice_id: str) -> str | None:
    if transcripts is None:
        return None
    root, flat = transcripts
    candidates = [root / folder / category / f"{voice_id}.txt"]
    if flat:
        candidates += [root / category / f"{voice_id}.txt", root / f"{voice_id}.txt"]
    for path in candidates:
        if path.exists():
            return path.read_text(encoding="utf-8", errors="replace").strip()
    return None


def build(args: argparse.Namespace) -> int:
    script_text = {}
    if args.event_text:
        script_text = json.loads(Path(args.event_text).read_text(encoding="utf-8"))

    transcripts = {}
    for lang, folder in LANGUAGES:
        override = getattr(args, f"transcripts_{lang}")
        if override:
            transcripts[lang] = (Path(override), True)
        else:
            transcripts[lang] = (Path(args.transcripts), False) if args.transcripts else None

    wav_root = Path(args.wav_root)
    battle = battle_characters()
    for category in CATEGORIES:
        lines = load_category(args.data, category)
        audio = {lang: {} for lang, _ in LANGUAGES}
        for lang, folder in LANGUAGES:
            directory = wav_root / folder / category
            if directory.is_dir():
                for wav in directory.glob("*.wav"):
                    audio[lang][wav.stem] = wav

        ids = set(lines) | set(audio["ja"]) | set(audio["en"])
        if category == "EVENT":
            ids |= {k for k in script_text if k in audio["ja"] or k in audio["en"] or k in lines}

        for voice_id in sorted(ids):
            entry = lines.setdefault(voice_id, {})
            script = script_text.get(voice_id) if category == "EVENT" else None
            entry.setdefault("character", default_character(category, voice_id, script, battle))

            for lang, folder in LANGUAGES:
                current = entry.get(lang, {})
                updated = dict(current)
                wav = audio[lang].get(voice_id)
                if wav is not None:
                    updated["duration"] = round(wav_duration(wav), 3)
                if current.get("source") != "manual":
                    if lang == "en" and script is not None:
                        updated["text"] = script["text"]
                        updated["source"] = "script"
                    elif wav is not None or "duration" in current:
                        text = read_transcript(transcripts[lang], folder, category, voice_id)
                        if text is not None:
                            updated["text"] = text
                            updated["source"] = "whisper"
                if updated:
                    entry[lang] = {key: updated[key] for key in ("text", "source", "duration") if key in updated}

            entry.setdefault("es", "")
            entry.setdefault("status", "pending")
            lines[voice_id] = {key: entry[key] for key in ("character", "ja", "en", "es", "status") if key in entry}

        path = save_category(args.data, category, lines)
        with_ja = sum(1 for e in lines.values() if e.get("ja", {}).get("text"))
        with_en = sum(1 for e in lines.values() if e.get("en", {}).get("text"))
        print(f"{path}: {len(lines)} lines, ja text {with_ja}, en text {with_en}")
    return 0


def po_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def po_unescape(value: str) -> str:
    out = []
    i = 0
    while i < len(value):
        c = value[i]
        if c == "\\" and i + 1 < len(value):
            i += 1
            out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(value[i], value[i]))
        else:
            out.append(c)
        i += 1
    return "".join(out)


def export_po(args: argparse.Namespace) -> int:
    out = [
        'msgid ""',
        'msgstr ""',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        '"Language: es\\n"',
        "",
    ]
    count = 0
    for category in CATEGORIES:
        for voice_id, entry in sorted(load_category(args.data, category).items()):
            source = entry.get("en", {}).get("text") or entry.get("ja", {}).get("text")
            if not source:
                continue
            ja = entry.get("ja", {}).get("text")
            if ja and ja != source:
                for line in ja.splitlines():
                    out.append(f"#. ja: {line}")
            out.append(f"#. character: {entry.get('character', -1)}")
            out.append(f'msgctxt "{category}/{voice_id}"')
            out.append(f'msgid "{po_escape(source)}"')
            out.append(f'msgstr "{po_escape(entry.get("es", ""))}"')
            out.append("")
            count += 1
    Path(args.output).write_text("\n".join(out), encoding="utf-8", newline="\n")
    print(f"{args.output}: {count} entries")
    return 0


def parse_po(path: Path) -> dict[str, str]:
    entries = {}
    current: dict[str, str] = {}
    field = None

    def flush():
        if current.get("msgctxt"):
            entries[current["msgctxt"]] = current.get("msgstr", "")

    for raw in path.read_text(encoding="utf-8-sig").splitlines():
        line = raw.strip()
        if not line:
            flush()
            current, field = {}, None
            continue
        if line.startswith("#"):
            continue
        for key in ("msgctxt", "msgid", "msgstr"):
            if line.startswith(key + " "):
                field = key
                current[key] = po_unescape(line[len(key) + 1:].strip()[1:-1])
                break
        else:
            if field and line.startswith('"'):
                current[field] += po_unescape(line[1:-1])
    flush()
    return entries


def import_po(args: argparse.Namespace) -> int:
    translations = parse_po(Path(args.po))
    updated = 0
    for category in CATEGORIES:
        lines = load_category(args.data, category)
        for voice_id, entry in lines.items():
            text = translations.get(f"{category}/{voice_id}")
            if text is None or text == entry.get("es", ""):
                continue
            entry["es"] = text
            if text and entry.get("status", "pending") == "pending":
                entry["status"] = "translated"
            updated += 1
        save_category(args.data, category, lines)
    print(f"{updated} translations updated")
    return 0


def export_subtitles(args: argparse.Namespace) -> int:
    subtitles = []
    for category in CATEGORIES:
        for voice_id, entry in sorted(load_category(args.data, category).items()):
            for lang, folder in LANGUAGES:
                audio = entry.get(lang, {})
                if "duration" not in audio:
                    continue
                text = entry.get("es") or entry.get("en", {}).get("text")
                if not text:
                    continue
                subtitles.append({
                    "audioFile": f"/{folder}/{category}/{voice_id}.hca",
                    "character": entry.get("character", -1),
                    "text": text,
                    "duration": audio["duration"],
                })
    output = Path(args.output)
    output.write_text(
        json.dumps({"subtitles": subtitles}, ensure_ascii=False, indent=4) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(f"{output}: {len(subtitles)} subtitles")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="HyperSubtitles voice line database.")
    parser.add_argument("--data", type=Path, default=DEFAULT_DATA, help="Folder with battle.json / event.json")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("build", help="Merge audio, transcripts and event script text")
    p.add_argument("--wav-root", default=str(REPO / "tools" / "transcribe" / "wav_out"), help="Folder with VOICE/ and VOICE_EN/")
    p.add_argument("--transcripts", default=str(REPO / "tools" / "transcribe" / "transcripts"), help="Whisper output folder (VOICE/..., VOICE_EN/...)")
    p.add_argument("--transcripts-ja", default=None, help="Whisper folder for Japanese only (also accepts flat <id>.txt)")
    p.add_argument("--transcripts-en", default=None, help="Whisper folder for English only (also accepts flat <id>.txt)")
    p.add_argument("--event-text", default=None, help="JSON from tools/extract/parse_event_scripts.py")
    p.set_defaults(func=build)

    p = sub.add_parser("export-po", help="Write a PO for translation")
    p.add_argument("-o", "--output", default=str(REPO / "data" / "es.po"))
    p.set_defaults(func=export_po)

    p = sub.add_parser("import-po", help="Read translations back from a PO")
    p.add_argument("po")
    p.set_defaults(func=import_po)

    p = sub.add_parser("export-subtitles", help="Write the subtitles.json loaded by the DLL")
    p.add_argument("-o", "--output", default=str(REPO / "data" / "subtitles.json"))
    p.set_defaults(func=export_subtitles)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
