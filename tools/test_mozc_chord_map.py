#!/usr/bin/env python3
"""Check chord-map outputs against Mozc (Google 日本語入力 approximation).

Reads data/chord_map.machine.csv:

- Kana / Mozc-composition rows: commit must equal `kana`.
- Reading/symbol rows kome..yajirushi (human CSV lines 19–24): do **not**
  compare `kana`/`label`; only require Mozc to accept the typed `output`.
- Skip function keys, modifier chords, and bare JIS punctuation.

Requires mozc_emacs_helper (Debian/Ubuntu: emacs-mozc-bin).

From the repo root:

    python tools/test_mozc_chord_map.py
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Human CSV lines 19–24: user picks the symbol via Space conversion.
# Machine table identifies them by output recipe.
ACCEPT_ONLY_OUTPUTS = frozenset(
    {
        "kome",
        "maru",
        "hosi",
        "dassyu",
        "kigou",
        "yajirushi",
    }
)

NAMED_OR_MOD = frozenset(
    {
        "Up",
        "Down",
        "Left",
        "Right",
        "Backspace",
        "Space",
        "Enter",
        "[ESC]",
        "Esc",
        "ESC",
        "Tab",
        "Delete",
        "Insert",
        "Home",
        "End",
        "PageUp",
        "PageDown",
        "CapsLock",
        "Hankaku",
        "Henkan",
        "Muhenkan",
        "Kana",
        "Eisuu",
        "Yen",
        "Ro",
    }
)

# Bare keys / layout chords: host JIS layout, not Mozc romaji composition.
LAYOUT_PUNCT = frozenset({",", ".", "/", "-", "[", "]"})


def _import_mozc():
    tools = ROOT / "tools"
    if str(tools) not in sys.path:
        sys.path.insert(0, str(tools))
    import mozc_session

    return mozc_session


def _is_modifier_chord(output: str) -> bool:
    if "+" not in output or output == "+":
        return False
    return output.startswith(("Shift+", "Ctrl+", "Alt+", "Gui+"))


def _classify(kana: str, output: str) -> str | None:
    """Return 'commit', 'accept', or None to skip."""
    if not output:
        return None
    if output in ACCEPT_ONLY_OUTPUTS:
        return "accept"
    if output in NAMED_OR_MOD or _is_modifier_chord(output):
        return None
    if output in LAYOUT_PUNCT:
        return None
    if not kana:
        return None
    chars_ok = all(0x21 <= ord(c) <= 0x7E or c == " " for c in output)
    if not chars_ok:
        return None
    return "commit"


def main() -> int:
    mozc_session = _import_mozc()
    csv_path = ROOT / "data" / "chord_map.machine.csv"
    if not csv_path.is_file():
        print(f"missing {csv_path}", file=sys.stderr)
        return 2

    rows = list(csv.DictReader(csv_path.open(encoding="utf-8")))
    cases: list[tuple[str, str, str, str]] = []
    skipped = 0
    for row in rows:
        kind = _classify(row.get("kana", ""), row.get("output", ""))
        if kind is None:
            skipped += 1
            continue
        cases.append((row["id"], kind, row.get("kana", ""), row["output"]))

    if not cases:
        print("no Mozc cases selected", file=sys.stderr)
        return 2

    helper = mozc_session.find_helper()
    if not helper:
        print(
            "mozc_emacs_helper not found; install emacs-mozc-bin "
            "(sudo apt-get install -y emacs-mozc-bin)",
            file=sys.stderr,
        )
        return 2

    failures: list[str] = []
    version = ""
    try:
        with mozc_session.MozcSession.open(helper) as mozc:
            version = mozc.version
            print(f"mozc {version} via {helper}")
            for row_id, kind, kana, output in cases:
                if kind == "accept":
                    if not mozc.compose_accepted(output):
                        failures.append(
                            f"id={row_id} accept output={output!r}: "
                            "Mozc did not consume into a composition"
                        )
                    continue
                got = mozc.compose_commit(output)
                if got != kana:
                    failures.append(
                        f"id={row_id} output={output!r}: "
                        f"got {got!r}, want {kana!r}"
                    )
    except mozc_session.MozcUnavailableError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except mozc_session.MozcProtocolError as exc:
        print(f"mozc protocol error: {exc}", file=sys.stderr)
        return 2

    commit_n = sum(1 for _, kind, _, _ in cases if kind == "commit")
    accept_n = sum(1 for _, kind, _, _ in cases if kind == "accept")
    print(
        f"checked {len(cases)} rows "
        f"({commit_n} commit, {accept_n} accept-only); skipped {skipped}"
    )
    if failures:
        print(f"{len(failures)} failure(s) (mozc {version}):", file=sys.stderr)
        for line in failures:
            print(line, file=sys.stderr)
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
