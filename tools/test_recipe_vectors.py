#!/usr/bin/env python3
"""Run test/recipe_vectors.tsv against tools/recipe.py.

Also checks that recipe.py's key tables match src/chord_core.cpp, and that
every output in the machine CSV would be sent rather than skipped.

`pio test -e native` runs this first. From the repo root:

    python tools/test_recipe_vectors.py
"""

from __future__ import annotations

import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] if "__file__" in globals() else None


def _root() -> Path:
    if ROOT is not None:
        return ROOT
    from recipe import project_root

    return project_root()


def _import_recipe():
    tools_dir = _root() / "tools"
    if str(tools_dir) not in sys.path:
        sys.path.insert(0, str(tools_dir))
    import recipe

    return recipe


def _constexpr(header: str, name: str) -> int:
    match = re.search(
        rf"static constexpr (?:size_t|uint8_t|uint16_t|uint32_t|int) {name} = (\d+);",
        header,
    )
    if match is None:
        raise ValueError(f"{name} missing from include/settings.h")
    return int(match.group(1))


def _cpp_tables(source: str) -> tuple[list[tuple[str, str, int]], list[tuple[str, str]]]:
    named_at = source.find("static const Item kNamed[]")
    named_end = source.find("};", named_at)
    if named_at < 0 or named_end < 0:
        raise ValueError("kNamed table missing from chord_core.cpp")
    named_block = source[named_at:named_end]
    named = []
    for match in re.finditer(
        r'\{\s*"([^"]+)"\s*,\s*ChordKeyKind::(\w+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\}',
        named_block,
    ):
        named.append((match.group(1), match.group(2), int(match.group(3), 0)))

    mod_at = source.find("static bool modifier_kind")
    mod_end = source.find("static void add_skip", mod_at)
    if mod_at < 0 or mod_end < 0:
        raise ValueError("modifier_kind missing from chord_core.cpp")
    mod_block = source[mod_at:mod_end]
    names = re.findall(r'strcmp\(word, "([^"]+)"\)', mod_block)
    kinds = re.findall(r"ChordKeyKind::(\w+)", mod_block)
    return named, list(zip(names, kinds))


def _check_cpp_mirror(recipe) -> list[str]:
    root = _root()
    header = (root / "include" / "settings.h").read_text(encoding="utf-8")
    source = (root / "src" / "chord_core.cpp").read_text(encoding="utf-8")
    errors = []
    expected = {
        "kChordRecipeCap": recipe.RECIPE_CAP,
        "kChordSkipCap": recipe.SKIP_CAP,
        "kChordTokenMax": recipe.TOKEN_LIMIT,
        "kChordTapGapMs": recipe.TAP_GAP,
        "kChordGapNone": recipe.GAP_NONE,
    }
    for name, value in expected.items():
        got = _constexpr(header, name)
        if got != value:
            errors.append(f"{name} is {got} in settings.h and {value} in recipe.py")
    named, mods = _cpp_tables(source)
    if named != list(recipe.NAMED):
        errors.append(f"named keys differ:\n  cpp {named}\n  py  {list(recipe.NAMED)}")
    if mods != list(recipe.MODIFIERS):
        errors.append(f"modifiers differ:\n  cpp {mods}\n  py  {list(recipe.MODIFIERS)}")
    return errors


def _show_events(events) -> str:
    return ";".join(f"{op}:{kind}:{code}:{gap}" for op, kind, code, gap in events)


def _show_skips(skips) -> str:
    shown = []
    for length, too_long, text in skips:
        rendered = text.decode("utf-8", "backslashreplace")
        shown.append(f"{length}:{int(too_long)}:{rendered}")
    return ";".join(shown)


def _check_vectors(recipe) -> list[str]:
    path = recipe.vectors_path()
    errors = []
    try:
        cases = recipe.load_vectors(path)
    except (OSError, ValueError) as exc:
        return [str(exc)]
    if not cases:
        return [f"{path} has no cases"]
    for case in cases:
        got = recipe.parse_recipe_bytes(case.recipe)
        where = f"{path.name}:{case.line_no} {case.name}"
        got_events = tuple((ev.op, ev.kind, ev.code, ev.gap) for ev in got.events)
        got_skips = tuple((sk.length, sk.too_long, sk.text) for sk in got.skips)
        if got.failed != case.failed or got.overflow_skips != case.overflow:
            errors.append(
                f"{where}: failed/overflow got {int(got.failed)}/{int(got.overflow_skips)} "
                f"want {int(case.failed)}/{int(case.overflow)}"
            )
        if got_events != case.events:
            errors.append(
                f"{where}: events\n  got  {_show_events(got_events)}\n  want {_show_events(case.events)}"
            )
        if got_skips != case.skips:
            errors.append(
                f"{where}: skips\n  got  {_show_skips(got_skips)}\n  want {_show_skips(case.skips)}"
            )
        if not case.overflow and got.skip_total != len(got.skips):
            errors.append(
                f"{where}: skip_total {got.skip_total} want {len(got.skips)}"
            )
    return errors


def _check_machine_csv(recipe) -> list[str]:
    path = _root() / "data" / "chord_map.machine.csv"
    errors = []
    editable = []
    with path.open(encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        fieldnames = list(reader.fieldnames or [])
        if "user_edit" not in fieldnames:
            errors.append(f"{path.name}: missing user_edit column")
        for row in reader:
            output = row.get("output") or ""
            reason = recipe.recipe_build_error(output)
            if reason:
                errors.append(f"{path.name} id {row.get('id')}: {reason}")
            user_edit = row.get("user_edit") or ""
            if user_edit not in ("no", "label_output"):
                errors.append(
                    f"{path.name} id {row.get('id')}: bad user_edit {user_edit!r}"
                )
            if user_edit == "label_output":
                editable.append(row.get("id"))
    if len(editable) != 14:
        errors.append(
            f"{path.name}: expected 14 label_output rows, got {len(editable)} ({editable})"
        )
    return errors


def _check_human_user_edit() -> list[str]:
    path = _root() / "esp32-jis-chordboard-premapping.csv"
    errors = []
    with path.open(encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        if list(reader.fieldnames or [])[-1:] != ["user_edit"]:
            # Full header checked by the build script; here only require the column.
            if "user_edit" not in (reader.fieldnames or []):
                return [f"{path.name}: missing user_edit column"]
        for lineno, row in enumerate(reader, start=2):
            label = (row.get("label") or "").strip()
            output = (row.get("output") or "").strip()
            if not label and not output:
                continue
            user_edit = (row.get("user_edit") or "").strip()
            if user_edit not in ("no", "label_output"):
                errors.append(f"{path.name} line {lineno}: bad user_edit {user_edit!r}")
                continue
            expect = "label_output" if 11 <= lineno <= 24 else "no"
            if user_edit != expect:
                errors.append(
                    f"{path.name} line {lineno}: expected user_edit={expect}, got {user_edit}"
                )
    return errors


def _check_build_helper(recipe) -> list[str]:
    errors = []
    if recipe.recipe_build_error("a") is not None:
        errors.append("plain output was rejected")
    if recipe.recipe_build_error("") is not None:
        errors.append("empty output was rejected")
    if recipe.recipe_build_error("Backspce") is not None:
        errors.append("a plain typo is typed, not skipped")
    skipped = recipe.recipe_build_error("Shift+Nope")
    if skipped is None or "Shift+Nope" not in skipped:
        errors.append(f"bad combo was not rejected: {skipped!r}")
    if recipe.recipe_build_error("b" * 24) is not None:
        errors.append("24-byte plain text was rejected")
    if recipe.recipe_build_error("b" * recipe.TOKEN_LIMIT) is not None:
        errors.append("token at kChordTokenMax was rejected")
    long_token = recipe.recipe_build_error("b" * (recipe.TOKEN_LIMIT + 1))
    if long_token is None or f"len {recipe.TOKEN_LIMIT + 1}" not in long_token:
        errors.append(f"long token was not rejected: {long_token!r}")
    return errors


def main() -> int:
    recipe = _import_recipe()
    errors = []
    errors.extend(_check_cpp_mirror(recipe))
    errors.extend(_check_vectors(recipe))
    errors.extend(_check_machine_csv(recipe))
    errors.extend(_check_human_user_edit())
    errors.extend(_check_build_helper(recipe))
    errors.extend(recipe.check_config_page())
    if errors:
        print(f"recipe vectors: {len(errors)} error(s)")
        for error in errors:
            print(error)
        return 1
    count = len(recipe.load_vectors(recipe.vectors_path()))
    print(f"recipe vectors: {count} cases agree")
    return 0


try:
    Import("env")  # type: ignore[name-defined]
except NameError:
    env = None

if env is not None:
    ROOT = Path(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]
    _status = main()
    if _status:
        env.Exit(_status)  # type: ignore[name-defined]
elif __name__ == "__main__":
    sys.exit(main())
