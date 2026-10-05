#!/usr/bin/env python3
"""Fails when a test shows a widget without waiting until it is exposed (#754).

A test that shows a widget and right away measures its painting or its
geometry depends on the window already being exposed; if it is not, Qt defers
the paint and the test fails now and then (#750). So every `show()` under
tests/ is followed by a wait for exposure: the next statement calls
QTest::qWaitForWindowExposed, QTest::qWaitForWindowActive or
QTest::qWaitForWindowFocused (an activateWindow() or raise() may come in
between). The helper showUntilExposed() in tests/helpers/shown_widget.h does
both in one call. Every test file is held to this, without exception (#758).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

TESTS = "tests"
SUFFIXES = {".cpp", ".h", ".hpp"}
HELPER = "tests/helpers/shown_widget.h"

SHOW = re.compile(r"(?<![\w])show\s*\(\s*\)\s*;?")
WAIT = re.compile(r"\b(?:qWaitForWindowExposed|qWaitForWindowActive|qWaitForWindowFocused)\s*\(")
# What may stand between a show() and its wait: asking for activation, which
# qWaitForWindowActive then waits for.
BETWEEN = re.compile(r"^(?:\s*[\w.\->()*&]*\b(?:activateWindow|raise)\s*\(\s*\)\s*;)+\s*$")
RAW_STRING = re.compile(r'R"([^()\\\s]{0,16})\(')


def _continues_identifier(source: str, i: int) -> bool:
    return i > 0 and (source[i - 1].isalnum() or source[i - 1] == "_")


def _in_number(source: str, i: int) -> bool:
    """Whether the quote at i stands inside a numeric literal."""
    start = i
    while start > 0 and (source[start - 1].isalnum() or source[start - 1] in "_'"):
        start -= 1
    return start < i and source[start].isdigit()


def code_only(source: str) -> str:
    """source with its comments and the contents of its string and character
    literals blanked out, line breaks kept, so that line numbers still hold."""
    out: list[str] = []
    i, n = 0, len(source)

    def blank(text: str) -> str:
        return "".join("\n" if c == "\n" else " " for c in text)

    while i < n:
        c = source[i]
        if source.startswith("//", i):
            end = source.find("\n", i)
            end = n if end < 0 else end
            out.append(blank(source[i:end]))
            i = end
        elif source.startswith("/*", i):
            end = source.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append(blank(source[i:end]))
            i = end
        elif (raw := RAW_STRING.match(source, i)) and not _continues_identifier(source, i):
            close = source.find(")" + raw.group(1) + '"', raw.end())
            end = n if close < 0 else close + len(raw.group(1)) + 2
            out.append('""' + blank(source[i + 2:end]))
            i = end
        elif c == "'" and _in_number(source, i):
            out.append(c)  # a digit separator, as in 1'000
            i += 1
        elif c in "\"'":
            j = i + 1
            while j < n and source[j] != c and source[j] != "\n":
                j += 2 if source[j] == "\\" else 1
            closed = j < n and source[j] == c
            out.append(c + blank(source[i + 1:j]) + (c if closed else ""))
            # An unclosed one ends at its line, whose line feed stays.
            i = j + 1 if closed else j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _waits_after(lines: list[str], index: int, rest: str) -> bool:
    """Whether the next statement after a show(), which leaves rest of its
    line index, waits for exposure."""
    for line in [rest] + lines[index + 1:]:
        if not line.strip() or BETWEEN.match(line):
            continue
        return bool(WAIT.search(line))
    return False


def unwaited_shows(source: str) -> list[int]:
    """The 1-based lines of source holding a show() that no wait follows."""
    # Split on line feeds only, as editors and compilers count lines.
    lines = code_only(source).split("\n")
    unwaited = []
    for index, line in enumerate(lines):
        for match in SHOW.finditer(line):
            if not _waits_after(lines, index, line[match.end():]):
                unwaited.append(index + 1)
                break
    return unwaited


def _test_sources(root: Path) -> list[str]:
    return sorted(path.relative_to(root).as_posix() for path in (root / TESTS).rglob("*")
                  if path.suffix in SUFFIXES and path.is_file())


def problems(root: Path) -> list[str]:
    """Every show() under root's tests that no wait follows, as GitHub
    annotations naming its file and line."""
    found = []
    for name in _test_sources(root):
        lines = unwaited_shows((root / name).read_text(encoding="utf-8", errors="replace"))
        found.extend(f"::error file={name},line={line}::{name}:{line}: show() is not followed by a wait "
                     f"until the widget is exposed; use showUntilExposed() from {HELPER}"
                     for line in lines)
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path("."), help="the repository root")
    args = parser.parse_args(argv)

    found = problems(args.root)
    for problem in found:
        print(problem)
    if found:
        return 1
    print(f"Every show() under {TESTS}/ waits until its widget is exposed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
