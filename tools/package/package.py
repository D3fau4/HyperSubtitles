"""
Builds a shareable release for one platform:
HyperSubtitles-<version>-<platform>/ and HyperSubtitles-<version>-<platform>.zip.

  mod/     winmm.dll + subtitles.json (exported from data/lines) + dialoguebox.json
           (the game is Windows only; on Linux it runs through Proton, so both get it)
  editor/  the editor for that platform + data/ (lines, characters, dialoguebox) + faces/
  LEEME.txt  (from LEEME-<platform>.txt)

The game font is not included: the mod loads it from the game folder.
Used by CI (.github/workflows/build.yml, job package) and locally:

  python tools/package/package.py --platform windows --dll Release/winmm.dll \\
      --editor Editor/build/Release/HyperSubtitlesEditor.exe
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PLATFORMS = {
    # platform: (editor file name in the package, LEEME line endings)
    "windows": ("HyperSubtitlesEditor.exe", "\r\n"),
    "linux": ("HyperSubtitlesEditor", "\n"),
}


def copy_text(src: Path, dst: Path) -> None:
    """Copies a text file with LF line endings (checkouts may have CRLF)."""
    dst.parent.mkdir(parents=True, exist_ok=True)
    text = src.read_text(encoding="utf-8").replace("\r\n", "\n")
    dst.write_text(text, encoding="utf-8", newline="\n")


def git_version() -> str:
    result = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], cwd=REPO, capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else "dev"


def write_zip(root: Path, archive: Path, executables: set[Path]) -> None:
    """Zips root (as its own top folder) with Unix permissions, so the Linux editor
    stays executable whatever OS builds the zip."""
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(root.rglob("*")):
            arcname = path.relative_to(root.parent).as_posix()
            if path.is_dir():
                info = zipfile.ZipInfo(arcname + "/")
                info.create_system = 3
                info.external_attr = (0o40755 << 16) | 0x10
                z.writestr(info, b"")
                continue
            info = zipfile.ZipInfo.from_file(path, arcname)
            info.create_system = 3  # Unix, or unzip ignores the permissions
            info.external_attr = (0o100755 if path in executables else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, path.read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a HyperSubtitles release zip.")
    parser.add_argument("--platform", choices=sorted(PLATFORMS), required=True)
    parser.add_argument("--dll", type=Path, required=True, help="winmm.dll (Release x86)")
    parser.add_argument("--editor", type=Path, required=True, help="Editor binary for --platform")
    parser.add_argument("--out", type=Path, default=REPO / "Releases", help="Output folder")
    parser.add_argument("--version", default=None, help="Defaults to the short commit hash")
    args = parser.parse_args()

    for path in (args.dll, args.editor):
        if not path.is_file():
            print(f"missing {path}", file=sys.stderr)
            return 1

    editor_name, leeme_newline = PLATFORMS[args.platform]
    version = args.version or git_version()
    name = f"HyperSubtitles-{version}-{args.platform}"
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
    editor_bin = editor / editor_name
    shutil.copy2(args.editor, editor_bin)
    editor_bin.chmod(0o755)
    for lines in sorted((data / "lines").glob("*.json")):
        copy_text(lines, editor / "data" / "lines" / lines.name)
    for name_ in ("characters.json", "dialoguebox.json"):
        copy_text(data / name_, editor / "data" / name_)
    (editor / "faces").mkdir()
    for face in sorted((REPO / "Dll1" / "faces").glob("*.png")):
        shutil.copy2(face, editor / "faces" / face.name)

    template = Path(__file__).parent / f"LEEME-{args.platform}.txt"
    leeme = template.read_text(encoding="utf-8").replace("\r\n", "\n").replace("{version}", version)
    (root / "LEEME.txt").write_text(leeme, encoding="utf-8", newline=leeme_newline)

    archive = args.out / f"{name}.zip"
    write_zip(root, archive, {editor_bin})
    print(f"{root}\n{archive}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
