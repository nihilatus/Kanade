#!/usr/bin/env python3
"""Compile src/chord_core.cpp into web/chord_core.wasm.

The practice page loads that module. PlatformIO does not. The Emscripten
version is wasm/emsdk-version.txt; CI rebuilds with that version and checks
the bytes still match the file in git.

    python tools/build_chord_wasm.py
    python tools/build_chord_wasm.py --output build/chord_core.wasm
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = ROOT / "wasm" / "emsdk-version.txt"

EXPORTS = [
    "chord_wasm_reset",
    "chord_wasm_set_rules",
    "chord_wasm_step",
    "chord_wasm_set_mode",
    "chord_wasm_find",
    "chord_wasm_release_ms",
    "chord_wasm_bounce_ms",
    "chord_wasm_rule_cap",
    "chord_wasm_rules_ptr",
    "chord_wasm_last_ptr",
]


def pinned_version() -> str:
    text = VERSION_FILE.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", text):
        raise SystemExit(f"bad emsdk version in {VERSION_FILE}: {text!r}")
    return text


def emcc_version(emcc: str) -> str:
    out = subprocess.run(
        [emcc, "--version"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    match = re.search(r"(\d+\.\d+\.\d+)", out)
    if match is None:
        raise SystemExit(f"could not read emcc version from:\n{out}")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build web/chord_core.wasm")
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "web" / "chord_core.wasm",
        help="wasm output path",
    )
    args = parser.parse_args()

    want = pinned_version()
    emcc = shutil.which("emcc")
    if emcc is None:
        raise SystemExit(
            f"emcc not on PATH. Install emsdk {want} and activate it "
            "(see wasm/emsdk-version.txt)."
        )
    got = emcc_version(emcc)
    if got != want:
        raise SystemExit(
            f"emcc is {got}, but wasm/emsdk-version.txt pins {want}. "
            "Activate that emsdk version so the wasm matches CI."
        )

    output = args.output
    if not output.is_absolute():
        output = ROOT / output
    output.parent.mkdir(parents=True, exist_ok=True)

    exported = "[" + ",".join(f'"_{name}"' for name in EXPORTS) + "]"
    # One emcc invocation. -ffile-prefix-map keeps the host checkout path out
    # of the module so a Windows build and the Linux CI build match.
    link_cmd = [
        emcc,
        str(ROOT / "wasm" / "chord_api.cpp"),
        str(ROOT / "src" / "chord_core.cpp"),
        "-I",
        str(ROOT / "include"),
        "-std=c++17",
        "-O2",
        "-g0",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-ident",
        f"-ffile-prefix-map={ROOT}=.",
        "-Wall",
        "-Wextra",
        "-s",
        "STANDALONE_WASM=1",
        "-s",
        "SUPPORT_LONGJMP=0",
        "-s",
        "DISABLE_EXCEPTION_CATCHING=1",
        "-s",
        "FILESYSTEM=0",
        "-s",
        "ASSERTIONS=0",
        "-s",
        "STACK_OVERFLOW_CHECK=0",
        "-s",
        "ALLOW_MEMORY_GROWTH=0",
        "-s",
        "INITIAL_MEMORY=262144",
        "-s",
        f"EXPORTED_FUNCTIONS={exported}",
        "-s",
        "ERROR_ON_UNDEFINED_SYMBOLS=1",
        "--no-entry",
        "-o",
        str(output),
    ]
    subprocess.run(link_cmd, check=True, env=build_env())
    print(f"wrote {output}")
    return 0


def build_env() -> dict[str, str]:
    env = os.environ.copy()
    env["SOURCE_DATE_EPOCH"] = "0"
    env.setdefault("TZ", "UTC")
    return env


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as err:
        raise SystemExit(err.returncode) from err
