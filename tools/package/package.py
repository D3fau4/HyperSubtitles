"""
Builds the shareable release: HyperSubtitles-<version>/ and HyperSubtitles-<version>.zip.

  mod/     winmm.dll + subtitles.json (exported from data/lines) + dialoguebox.json
  editor/  HyperSubtitlesEditor.exe + data/ (lines, characters, dialoguebox) + faces/
  LEEME.txt

The game font is not included: the mod loads it from the game folder.
Used by CI (.github/workflows/build.yml, job package) and locally:

  python tools/package/package.py --dll Release/winmm.dll \\
      --editor Editor/build/Release/HyperSubtitlesEditor.exe
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def copy_text(src: Path, dst: Path) -> None:
    """Copies a text file with LF line endings (checkouts may have CRLF)."""
    dst.parent.mkdir(parents=True, exist_ok=True)
    text = src.read_text(encoding="utf-8").replace("\r\n", "\n")
    dst.write_text(text, encoding="utf-8", newline="\n")


def git_version() -> str:
    result = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], cwd=REPO, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else "dev"


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the HyperSubtitles release zip.")
    parser.add_argument("--dll", type=Path, required=True, help="winmm.dll (Release x86)")
    parser.add_argument("--editor", type=Path, required=True, help="HyperSubtitlesEditor.exe")
    parser.add_argument("--out", type=Path, default=REPO / "Releases", help="Output folder")
    parser.add_argument("--version", default=None, help="Defaults to the short commit hash")
    args = parser.parse_args()

    for path in (args.dll, args.editor):
        if not path.is_file():
            print(f"missing {path}", file=sys.stderr)
            return 1

    version = args.version or git_version()
    name = f"HyperSubtitles-{version}"
    root = args.out / name
    if root.exists():
        shutil.rmtree(root)
    data = REPO / "data"

    mod = root / "mod"
    mod.mkdir(parents=True)
    shutil.copy2(args.dll, mod / "winmm.dll")
    subprocess.run([sys.executable, str(REPO / "tools" / "lines" / "lines.py"), "--data", str(data / "lines"),
                    "export-subtitles", "-o", str(mod / "subtitles.json")], check=True)
    copy_text(data / "dialoguebox.json", mod / "dialoguebox.json")

    editor = root / "editor"
    editor.mkdir()
    shutil.copy2(args.editor, editor / args.editor.name)
    for lines in sorted((data / "lines").glob("*.json")):
        copy_text(lines, editor / "data" / "lines" / lines.name)
    for name_ in ("characters.json", "dialoguebox.json"):
        copy_text(data / name_, editor / "data" / name_)
    (editor / "faces").mkdir()
    for face in sorted((REPO / "Dll1" / "faces").glob("*.png")):
        shutil.copy2(face, editor / "faces" / face.name)

    leeme = (Path(__file__).parent / "LEEME.txt").read_text(encoding="utf-8").replace("{version}", version)
    (root / "LEEME.txt").write_text(leeme.replace("\r\n", "\n"), encoding="utf-8", newline="\r\n")

    archive = shutil.make_archive(str(args.out / name), "zip", root_dir=args.out, base_dir=name)
    print(f"{root}\n{archive}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
