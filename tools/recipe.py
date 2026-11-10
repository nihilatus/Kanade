"""Recipe parser matching src/chord_core.cpp.

Walks UTF-8 bytes, not characters. A token the firmware would add_skip is a
skip here too. test/recipe_vectors.tsv is the shared oracle: this module and
the native test both read it. The chord-map build rejects any output that
parses with a skip, an overflow, or a failed fit.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

# include/chord_core.h
RECIPE_CAP = 96
SKIP_CAP = 32
TAP_GAP = 12
GAP_NONE = 0
TOKEN_LIMIT = 24

# src/chord_core.cpp named_key / modifier_kind. tools/test_recipe_vectors.py
# checks these tables against that file.
NAMED: tuple[tuple[str, str, int], ...] = (
    ("Backspace", "Backspace", 0),
    ("Space", "Space", 0),
    ("Enter", "Enter", 0),
    ("Esc", "Esc", 0),
    ("ESC", "Esc", 0),
    ("[ESC]", "Esc", 0),
    ("Up", "Up", 0),
    ("Down", "Down", 0),
    ("Left", "Left", 0),
    ("Right", "Right", 0),
    ("Tab", "Tab", 0),
    ("Hankaku", "Raw", 0x35),
    ("Henkan", "Raw", 0x8A),
    ("Muhenkan", "Raw", 0x8B),
    ("Kana", "Raw", 0x88),
    ("Eisuu", "Raw", 0x91),
    ("Yen", "Raw", 0x89),
    ("Ro", "Raw", 0x87),
    ("Delete", "Raw", 0x4C),
    ("Insert", "Raw", 0x49),
    ("Home", "Raw", 0x4A),
    ("End", "Raw", 0x4D),
    ("PageUp", "Raw", 0x4B),
    ("PageDown", "Raw", 0x4E),
    ("CapsLock", "Raw", 0x39),
    ("F1", "Raw", 0x3A),
    ("F2", "Raw", 0x3B),
    ("F3", "Raw", 0x3C),
    ("F4", "Raw", 0x3D),
    ("F5", "Raw", 0x3E),
    ("F6", "Raw", 0x3F),
    ("F7", "Raw", 0x40),
    ("F8", "Raw", 0x41),
    ("F9", "Raw", 0x42),
    ("F10", "Raw", 0x43),
    ("F11", "Raw", 0x44),
    ("F12", "Raw", 0x45),
)

MODIFIERS: tuple[tuple[str, str], ...] = (
    ("Shift", "Shift"),
    ("Ctrl", "Ctrl"),
    ("Alt", "Alt"),
    ("Gui", "Gui"),
)

_NAMED_BYTES = {name.encode("ascii"): (kind, code) for name, kind, code in NAMED}
_MODIFIER_BYTES = {name.encode("ascii"): kind for name, kind in MODIFIERS}


@dataclass
class Event:
    op: str
    kind: str
    code: int
    gap: int


@dataclass
class Skip:
    length: int
    too_long: bool
    text: bytes


@dataclass
class Parsed:
    events: list[Event]
    skips: list[Skip]
    overflow_skips: bool
    failed: bool


def _plain(value: int) -> bool:
    return 0x21 <= value <= 0x7E


def unescape(data: bytes) -> bytes:
    """Vector-file escapes: \\\\ \\t \\n \\r \\xNN. \\xNN is one byte, not a code point."""
    out = bytearray()
    i = 0
    while i < len(data):
        if data[i] != 0x5C:
            out.append(data[i])
            i += 1
            continue
        if i + 1 >= len(data):
            raise ValueError("trailing backslash")
        mark = data[i + 1]
        i += 2
        if mark == 0x5C:
            out.append(0x5C)
        elif mark == ord("t"):
            out.append(0x09)
        elif mark == ord("n"):
            out.append(0x0A)
        elif mark == ord("r"):
            out.append(0x0D)
        elif mark == ord("x"):
            if i + 1 >= len(data):
                raise ValueError("short hex escape")
            try:
                out.append(int(data[i : i + 2], 16))
            except ValueError as exc:
                raise ValueError("bad hex escape") from exc
            i += 2
        else:
            raise ValueError("unknown escape")
    return bytes(out)


class _Parser:
    def __init__(self) -> None:
        self.events: list[Event] = []
        self.skips: list[Skip] = []
        self.overflow_skips = False
        self.failed = False

    def _put(self, op: str, kind: str, code: int, gap: int) -> bool:
        if len(self.events) >= RECIPE_CAP:
            self.failed = True
            return False
        self.events.append(Event(op, kind, code, gap))
        return True

    def _add_skip(self, token: bytes | None, length: int, too_long: bool) -> None:
        if len(self.skips) >= SKIP_CAP:
            self.overflow_skips = True
            return
        text = b""
        if not too_long and token is not None:
            text = token[: TOKEN_LIMIT - 1]
        self.skips.append(Skip(length, too_long, text))

    def _take_combo(self, token: bytes) -> bool:
        # True when the token contains '+' and is not the lone plus character.
        if b"+" not in token:
            return False
        if token == b"+":
            return False
        length = len(token)
        if length == 0 or length >= TOKEN_LIMIT:
            self._add_skip(token, length, True)
            return True

        buf = bytearray(token)
        starts: list[int] = []
        cursor = 0
        while len(starts) < 6:
            starts.append(cursor)
            plus = buf.find(0x2B, cursor)
            if plus < 0:
                break
            # "Shift++" keeps a final "+" as its own part.
            if plus + 1 == len(buf):
                break
            buf[plus] = 0
            cursor = plus + 1

        def part(index: int) -> bytes:
            start = starts[index]
            end = start
            while end < len(buf) and buf[end] != 0:
                end += 1
            return bytes(buf[start:end])

        n = len(starts)
        last = part(n - 1)
        last_is_plus = last == b"+"
        if n < 2 or (b"+" in last and not last_is_plus) or n - 1 > 4:
            self._add_skip(token, length, False)
            return True

        mods: list[str] = []
        for index in range(n - 1):
            kind = _MODIFIER_BYTES.get(part(index))
            if kind is None:
                self._add_skip(token, length, False)
                return True
            mods.append(kind)

        named = _NAMED_BYTES.get(last)
        if len(last) == 1 and _plain(last[0]):
            key_kind, key_code = "Char", last[0]
        elif named is not None:
            key_kind, key_code = named
        else:
            self._add_skip(token, length, False)
            return True

        presses = (n - 1) + 1
        room = RECIPE_CAP - len(self.events)
        if presses + 1 > room:
            self.failed = True
            return True
        for kind in mods:
            if not self._put("P", kind, 0, GAP_NONE):
                return True
        if not self._put("P", key_kind, key_code, TAP_GAP):
            return True
        if not self._put("R", "Char", 0, TAP_GAP):
            return True
        return True

    def _take_token(self, token: bytes) -> bool:
        # False means the whole recipe does not fit. A skip still returns True.
        room = RECIPE_CAP - len(self.events)
        named = _NAMED_BYTES.get(token)
        if named is not None:
            if room < 2:
                return False
            kind, code = named
            if not self._put("P", kind, code, TAP_GAP) or not self._put("R", "Char", 0, TAP_GAP):
                return False
            return True

        if self._take_combo(token):
            return not self.failed

        length = len(token)
        if length + 1 > room:
            return False
        if any(not _plain(byte) for byte in token):
            self._add_skip(token, length, False)
            return True
        for byte in token:
            if not self._put("W", "Char", byte, TAP_GAP):
                return False
        if not self._put("R", "Char", 0, GAP_NONE):
            return False
        return True

    def parse(self, recipe: bytes) -> Parsed:
        index = 0
        size = len(recipe)
        while index < size:
            while index < size and recipe[index] == 0x20:
                index += 1
            if index >= size:
                break
            start = index
            while index < size and recipe[index] != 0x20:
                index += 1
            length = index - start
            if length == 0 or length >= TOKEN_LIMIT:
                self._add_skip(None, length, True)
                continue
            if len(self.events) >= RECIPE_CAP:
                self.failed = True
                self.events.clear()
                break
            token = recipe[start:index]
            if not self._take_token(token):
                self.failed = True
                self.events.clear()
                break
        return Parsed(self.events, self.skips, self.overflow_skips, self.failed)


def parse_recipe_bytes(recipe: bytes) -> Parsed:
    return _Parser().parse(recipe)


def parse_recipe(recipe: str) -> Parsed:
    return parse_recipe_bytes(recipe.encode("utf-8"))


def recipe_build_error(output: str) -> str | None:
    """Why this factory output must not ship. None when the device would send it all."""
    parsed = parse_recipe(output)
    if parsed.failed:
        return f"recipe does not fit in the event buffer: {output!r}"
    if parsed.overflow_skips or parsed.skips:
        shown: list[str] = []
        for skip in parsed.skips:
            if skip.too_long:
                shown.append(f"len {skip.length}")
            else:
                shown.append(skip.text.decode("utf-8", "backslashreplace"))
        detail = ", ".join(shown) if shown else "dropped tokens"
        if parsed.overflow_skips:
            detail += "; further skips were dropped"
        return f"recipe would be skipped on device ({detail}): {output!r}"
    if output and not parsed.events:
        return f"recipe produces no keys: {output!r}"
    return None


@dataclass(frozen=True)
class VectorCase:
    line_no: int
    name: str
    recipe: bytes
    failed: bool
    overflow: bool
    events: tuple[tuple[str, str, int, int], ...]
    skips: tuple[tuple[int, bool, bytes], ...]


def _flag(text: bytes, label: str) -> bool:
    if text == b"0":
        return False
    if text == b"1":
        return True
    raise ValueError(f"bad {label} flag {text!r}")


def _parse_events(field: bytes) -> tuple[tuple[str, str, int, int], ...]:
    if not field:
        return ()
    events = []
    for piece in field.split(b";"):
        parts = piece.split(b":")
        if len(parts) != 4:
            raise ValueError(f"bad event {piece!r}")
        op = parts[0].decode("ascii")
        if op not in ("W", "P", "R"):
            raise ValueError(f"bad op {op!r}")
        kind = parts[1].decode("ascii")
        code = int(parts[2], 10)
        gap = int(parts[3], 10)
        if not 0 <= code <= 255 or not 0 <= gap <= 255:
            raise ValueError(f"bad event {piece!r}")
        events.append((op, kind, code, gap))
    return tuple(events)


def _parse_skips(field: bytes) -> tuple[tuple[int, bool, bytes], ...]:
    if not field:
        return ()
    skips = []
    for piece in field.split(b";"):
        parts = piece.split(b":", 2)
        if len(parts) != 3:
            raise ValueError(f"bad skip {piece!r}")
        length = int(parts[0], 10)
        too_long = _flag(parts[1], "too_long")
        text = unescape(parts[2])
        skips.append((length, too_long, text))
    return tuple(skips)


def load_vectors(path: Path) -> list[VectorCase]:
    cases: list[VectorCase] = []
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
    for line_no, line in enumerate(raw.splitlines(), start=1):
        if not line or line.startswith(b"#"):
            continue
        fields = line.split(b"\t")
        if len(fields) != 6:
            raise ValueError(f"{path}:{line_no}: expected 6 fields, got {len(fields)}")
        name_b, recipe_b, failed_b, overflow_b, events_b, skips_b = fields
        cases.append(
            VectorCase(
                line_no=line_no,
                name=name_b.decode("utf-8"),
                recipe=unescape(recipe_b),
                failed=_flag(failed_b, "failed"),
                overflow=_flag(overflow_b, "overflow"),
                events=_parse_events(events_b),
                skips=_parse_skips(skips_b),
            )
        )
    return cases


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def vectors_path() -> Path:
    return project_root() / "test" / "recipe_vectors.tsv"
