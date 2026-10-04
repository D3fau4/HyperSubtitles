"""Checks lines.display_text against the cases shared with the editor
(Editor/tests/display_text_cases.json)."""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lines  # noqa: E402

CASES = Path(__file__).resolve().parents[2] / "Editor" / "tests" / "display_text_cases.json"


def main() -> int:
    failures = 0
    for case in json.loads(CASES.read_text(encoding="utf-8")):
        entry = {"text": case["translation"], "en": {"text": case["english"]}}
        got = lines.display_text(entry)
        if got != case["expected"]:
            print(f"FAIL: {case!r} -> {got!r}")
            failures += 1
    print("OK" if not failures else f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
