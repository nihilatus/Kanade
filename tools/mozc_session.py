"""Drive Mozc via mozc_emacs_helper for headless romaji conversion.

Ubuntu package: emacs-mozc-bin (provides mozc_emacs_helper + mozc-server).
This is an approximation of Google 日本語入力, not the Windows binary.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import threading
from dataclasses import dataclass, field
from pathlib import Path
from queue import Empty, Queue
from typing import Iterable


HELPER_NAME = "mozc_emacs_helper"


def find_helper() -> str | None:
    path = shutil.which(HELPER_NAME)
    if path:
        return path
    # Common install location when PATH is thin (CI).
    candidate = Path("/usr/bin") / HELPER_NAME
    return str(candidate) if candidate.is_file() else None


class MozcUnavailableError(RuntimeError):
    """mozc_emacs_helper is missing or failed to start."""


class MozcProtocolError(RuntimeError):
    """Unexpected reply from mozc_emacs_helper."""


_ATOM_RE = re.compile(
    r"""
    \s*(?:
        "(?P<str>(?:\\.|[^"\\])*)"
      | (?P<num>-?\d+)
      | (?P<atom>[^\s"()]+)
    )
    """,
    re.VERBOSE,
)


def _unescape(s: str) -> str:
    out: list[str] = []
    i = 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            out.append(s[i + 1])
            i += 2
            continue
        out.append(s[i])
        i += 1
    return "".join(out)


def parse_sexp(text: str):
    """Parse one Mozc helper S-expression into nested lists / atoms."""

    pos = 0
    n = len(text)

    def skip_ws() -> None:
        nonlocal pos
        while pos < n and text[pos].isspace():
            pos += 1

    def read_value():
        nonlocal pos
        skip_ws()
        if pos >= n:
            raise MozcProtocolError(f"truncated S-expression: {text!r}")
        if text[pos] == "(":
            pos += 1
            items = []
            while True:
                skip_ws()
                if pos >= n:
                    raise MozcProtocolError(f"unclosed list: {text!r}")
                if text[pos] == ")":
                    pos += 1
                    return items
                items.append(read_value())
        m = _ATOM_RE.match(text, pos)
        if not m:
            raise MozcProtocolError(f"bad token at {pos} in {text!r}")
        pos = m.end()
        if m.group("str") is not None:
            return _unescape(m.group("str"))
        if m.group("num") is not None:
            return int(m.group("num"))
        atom = m.group("atom")
        if atom == "t":
            return True
        if atom == "nil":
            return False
        return atom

    value = read_value()
    skip_ws()
    if pos != n:
        # Helper replies are one top-level list; ignore trailing junk.
        pass
    return value


def _alist_get(node, key: str):
    """Return value for KEY in an association list ((k . v) ...)."""
    if not isinstance(node, list):
        return None
    for item in node:
        if (
            isinstance(item, list)
            and len(item) >= 3
            and item[0] == key
            and item[1] == "."
        ):
            return item[2]
        # Flat pair form without dotted cdr as separate atom is uncommon here.
        if isinstance(item, list) and len(item) == 2 and item[0] == key:
            return item[1]
    return None


def _walk_values(node, out: list[str]) -> None:
    if isinstance(node, list):
        if (
            len(node) >= 3
            and node[0] == "value"
            and node[1] == "."
            and isinstance(node[2], str)
        ):
            out.append(node[2])
        for child in node:
            _walk_values(child, out)


def _result_value(output) -> str | None:
    result = _alist_get(output, "result")
    if result is None:
        return None
    value = _alist_get(result, "value")
    return value if isinstance(value, str) else None


def _preedit_text(output) -> str:
    preedit = _alist_get(output, "preedit")
    if not isinstance(preedit, list):
        return ""
    parts: list[str] = []
    for item in preedit:
        if not isinstance(item, list) or not item:
            continue
        if item[0] != "segment":
            continue
        # segment may be (segment ((annotation ...)(value . "…") ...))
        # or (segment ((...) (...))) with one or more segment alists.
        body = item[1:]
        for seg in body:
            if isinstance(seg, list) and seg and isinstance(seg[0], list):
                # Single segment alist wrapped: ((annotation ...)(value . ...))
                val = _alist_get(seg, "value")
                if isinstance(val, str):
                    parts.append(val)
            elif isinstance(seg, list):
                val = _alist_get(seg, "value")
                if isinstance(val, str):
                    parts.append(val)
    return "".join(parts)


def _candidate_values(output) -> list[str]:
    found: list[str] = []
    # Prefer structured candidate lists when present.
    for key in ("all-candidate-words", "candidates"):
        block = _alist_get(output, key)
        if block is None:
            continue
        _walk_values(block, found)
    if found:
        # Preserve order, drop duplicates.
        seen: set[str] = set()
        ordered: list[str] = []
        for v in found:
            if v not in seen:
                seen.add(v)
                ordered.append(v)
        return ordered
    # Fallback: any value strings under output (rare).
    _walk_values(output, found)
    return found


def recipe_key_chars(recipe: str) -> list[str]:
    """Split a firmware recipe into printable chars to send to Mozc.

    Spaces only separate tokens (same as firmware); they are not typed.
    Each remaining printable ASCII character is one SendKey.
    """
    chars: list[str] = []
    for token in recipe.split():
        for ch in token:
            if 0x21 <= ord(ch) <= 0x7E:
                chars.append(ch)
    return chars


@dataclass
class MozcSession:
    """One long-lived mozc_emacs_helper process with a reusable session."""

    helper_path: str
    version: str = ""
    _proc: subprocess.Popen[str] | None = field(default=None, init=False, repr=False)
    _lines: Queue[str] = field(default_factory=Queue, init=False, repr=False)
    _reader: threading.Thread | None = field(default=None, init=False, repr=False)
    _event_id: int = field(default=0, init=False)
    _session_id: int | None = field(default=None, init=False)
    _lock: threading.Lock = field(default_factory=threading.Lock, init=False, repr=False)

    @classmethod
    def open(cls, helper_path: str | None = None) -> "MozcSession":
        path = helper_path or find_helper()
        if not path:
            raise MozcUnavailableError(
                "mozc_emacs_helper not found; install emacs-mozc-bin "
                "(sudo apt-get install -y emacs-mozc-bin)"
            )
        session = cls(helper_path=path)
        session._start()
        return session

    def _start(self) -> None:
        self._proc = subprocess.Popen(
            [self.helper_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        assert self._proc.stdout is not None
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        greeting = self._readline(timeout=10.0)
        parsed = parse_sexp(greeting)
        version = _alist_get(parsed, "version")
        self.version = version if isinstance(version, str) else ""
        self._create_session()

    def _read_loop(self) -> None:
        assert self._proc is not None and self._proc.stdout is not None
        for line in self._proc.stdout:
            self._lines.put(line.rstrip("\n"))
        self._lines.put("")

    def _readline(self, timeout: float = 5.0) -> str:
        try:
            line = self._lines.get(timeout=timeout)
        except Empty as exc:
            raise MozcProtocolError("timeout waiting for mozc_emacs_helper") from exc
        if line == "" and (self._proc is None or self._proc.poll() is not None):
            raise MozcProtocolError("mozc_emacs_helper exited")
        return line

    def _next_event(self) -> int:
        self._event_id += 1
        return self._event_id

    def _send_raw(self, command: str, timeout: float = 5.0):
        if self._proc is None or self._proc.stdin is None:
            raise MozcProtocolError("helper not running")
        self._proc.stdin.write(command + "\n")
        self._proc.stdin.flush()
        return parse_sexp(self._readline(timeout=timeout))

    def _create_session(self) -> None:
        eid = self._next_event()
        reply = self._send_raw(f"({eid} CreateSession)")
        sid = _alist_get(reply, "emacs-session-id")
        if not isinstance(sid, int):
            raise MozcProtocolError(f"CreateSession missing session id: {reply!r}")
        self._session_id = sid

    def _delete_session(self) -> None:
        if self._session_id is None:
            return
        eid = self._next_event()
        sid = self._session_id
        self._session_id = None
        try:
            self._send_raw(f"({eid} DeleteSession {sid})")
        except MozcProtocolError:
            pass

    def _reset_session(self) -> None:
        self._delete_session()
        self._create_session()

    def _send_key(self, key: str, timeout: float = 5.0):
        if self._session_id is None:
            self._create_session()
        assert self._session_id is not None
        eid = self._next_event()
        # Printable ASCII must be sent as Mozc keycodes (same as ord).
        # A quoted string like "a" is treated as a key-string and becomes ａ,
        # not romaji composition into あ.
        if len(key) == 1 and 0x21 <= ord(key) <= 0x7E:
            payload = str(ord(key))
        else:
            # Named keys: space, enter, escape, …
            payload = key
        reply = self._send_raw(
            f"({eid} SendKey {self._session_id} {payload})", timeout=timeout
        )
        return _alist_get(reply, "output")

    def compose_commit(self, recipe: str) -> str:
        """Type recipe chars and Enter; return committed string."""
        with self._lock:
            self._reset_session()
            for ch in recipe_key_chars(recipe):
                self._send_key(ch)
            output = self._send_key("enter")
            committed = _result_value(output)
            if committed is not None:
                return committed
            # Some edge replies may leave text only in preedit.
            return _preedit_text(output)

    def compose_accepted(self, recipe: str) -> bool:
        """Return True if Mozc consumes the recipe into a non-empty composition.

        Used for reading/symbol rows where the label is only a conversion hint
        for the user (Space + pick), not a fixed commit target.
        """
        chars = recipe_key_chars(recipe)
        if not chars:
            return False
        with self._lock:
            self._reset_session()
            output = None
            for ch in chars:
                output = self._send_key(ch)
            if output is None:
                return False
            consumed = _alist_get(output, "consumed")
            preedit = _preedit_text(output)
            ok = bool(consumed) and bool(preedit)
            try:
                self._send_key("escape")
            except MozcProtocolError:
                self._reset_session()
            return ok

    def compose_candidates(self, recipe: str) -> list[str]:
        """Type recipe chars, open conversion with Space, return candidate strings."""
        with self._lock:
            self._reset_session()
            for ch in recipe_key_chars(recipe):
                self._send_key(ch)
            output = self._send_key("space")
            cands = _candidate_values(output)
            # Escape to drop conversion state before next call.
            try:
                self._send_key("escape")
            except MozcProtocolError:
                self._reset_session()
            return cands

    def close(self) -> None:
        with self._lock:
            try:
                self._delete_session()
            except MozcProtocolError:
                pass
            if self._proc is not None:
                if self._proc.stdin:
                    try:
                        self._proc.stdin.close()
                    except OSError:
                        pass
                self._proc.terminate()
                try:
                    self._proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self._proc.kill()
                self._proc = None

    def __enter__(self) -> "MozcSession":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()


def smoke_check(recipes: Iterable[tuple[str, str]] | None = None) -> None:
    """Quick manual check: python -m tools path usage from repo root."""
    samples = list(recipes or (("nn", "ん"), ("ltu", "っ"), ("a", "あ")))
    with MozcSession.open() as mozc:
        print(f"mozc {mozc.version}")
        for recipe, expect in samples:
            got = mozc.compose_commit(recipe)
            status = "ok" if got == expect else "FAIL"
            print(f"{status} {recipe!r} -> {got!r} (want {expect!r})")


if __name__ == "__main__":
    smoke_check()
