#!/usr/bin/env python3
"""Fails when a test shows a widget without waiting until it is exposed (#754).

A test that shows a widget and right away measures its painting or its
geometry depends on the window already being exposed; if it is not, Qt defers
the paint and the test fails now and then (#750). This is the one full
statement of the rule; the helper, BUILD.md and CI point here.

Every `show()` in a .cpp, .h or .hpp under tests/ (`x.show()`, `x->show()` or a
bare `show()` on this; not showPopup(), setVisible() or showMaximized()) is
followed by a wait until that same widget is exposed. Comments and string
literals are not code. The next statement, past any activateWindow() or
raise(), is one of:

- showUntilExposed( x ) from tests/helpers/shown_widget.h, which shows and
  waits in one call and is what a test normally uses instead of show();
- REQUIRE( ... ) or CHECK( ... ) of a call to QTest::qWaitForWindowExposed,
  qWaitForWindowActive or qWaitForWindowFocused on x;
- such a call assigned to a variable, as in a callback that cannot REQUIRE,
  that a later REQUIRE( variable ) or CHECK( variable ) in the file checks.

A wait whose result is dropped, one inside a lambda or an if, and one on
another widget do not count. x, &x, *x, x.get() and x-> name the same widget.

A test that must act between show() and the wait marks the show() line with a
comment `// shown-widget-wait: deferred, <reason>`; it then passes if a checked
wait on the same widget, of a form above, follows anywhere later in the file.
Every test file is held to this, without an allowlist (#758).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

TESTS = "tests"
SUFFIXES = {".cpp", ".h", ".hpp"}
HELPER = "tests/helpers/shown_widget.h"

SHOW = re.compile(r"(?<![\w])show\s*\(\s*\)")
# The object a show() is called on: what precedes its "." or "->".
RECEIVER = re.compile(r"([\w:()\[\]*&]+(?:\s*(?:\.|->)\s*[\w:()\[\]]+)*)\s*(?:\.|->)\s*$")
WAIT_CALL = r"(?:QTest::)?(?:qWaitForWindowExposed|qWaitForWindowActive|qWaitForWindowFocused)\s*\((.*)\)"
CHECKED_WAIT = re.compile(r"(?:REQUIRE|CHECK)\s*\(\s*" + WAIT_CALL + r"\s*\)", re.S)
ASSIGNED_WAIT = re.compile(r"(?:(?:const\s+)?(?:bool|auto)\s+)?(\w+)\s*=\s*" + WAIT_CALL, re.S)
SHOW_UNTIL_EXPOSED = re.compile(r"showUntilExposed\s*\((.*)\)", re.S)
# What may stand between a show() and its wait: asking for activation, which
# qWaitForWindowActive then waits for.
BETWEEN = re.compile(r"[\w.\->()*&\s]*\b(?:activateWindow|raise)\s*\(\s*\)")
DEFERRED = re.compile(r"//\s*shown-widget-wait:\s*deferred,\s*\S")
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


def _normalized(expression: str) -> str:
    """The object an expression names, however it is spelled: w, &w, *w,
    w.get() and this all name the same."""
    expression = re.sub(r"\s+", "", expression)
    expression = re.sub(r"\.get\(\)$", "", expression.lstrip("&*"))
    return "" if expression == "this" else expression


def _first_argument(arguments: str) -> str:
    depth = 0
    for i, c in enumerate(arguments):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            return arguments[:i]
    return arguments


def _statement(code: str, start: int) -> tuple[str, int]:
    """The statement that starts at start, without its ";", and where it ends;
    a statement that its block closes before its ";" is cut there."""
    depth = 0
    for i in range(start, len(code)):
        c = code[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                return code[start:i].strip(), i
            depth -= 1
        elif c == ";" and depth == 0:
            return code[start:i].strip(), i + 1
    return code[start:].strip(), len(code)


def _is_checked_wait(statement: str, shown: str, after: str) -> bool:
    """Whether statement waits until shown is exposed and its result is
    checked: inside a REQUIRE or CHECK, or kept in a variable that after
    checks; or whether it is showUntilExposed( shown )."""
    if match := SHOW_UNTIL_EXPOSED.fullmatch(statement) or CHECKED_WAIT.fullmatch(statement):
        return _normalized(_first_argument(match.group(1))) == shown
    if match := ASSIGNED_WAIT.fullmatch(statement):
        checked = re.compile(r"\b(?:REQUIRE|CHECK)\s*\(\s*" + re.escape(match.group(1)) + r"\s*\)")
        return _normalized(_first_argument(match.group(2))) == shown and bool(checked.search(after))
    return False


def _waits_next(code: str, end: int, shown: str) -> bool:
    """Whether the statement after a show() that ends at end, past any
    activateWindow() or raise(), is a checked wait until shown is exposed."""
    while True:
        statement, end = _statement(code, end)
        if not BETWEEN.fullmatch(statement):
            return _is_checked_wait(statement, shown, code[end:])


def _waits_later(code: str, end: int, shown: str) -> bool:
    """Whether any statement after end is a checked wait until shown is exposed."""
    for boundary in re.finditer(r"[;{}]", code[end:]):
        statement, stop = _statement(code, end + boundary.end())
        if _is_checked_wait(statement, shown, code[stop:]):
            return True
    return False


def _deferred(raw_line: str, code_line: str) -> bool:
    """Whether a line carries the deferred marker in a comment: blanked out,
    but not as the contents of a string, which keeps its opening quote."""
    match = DEFERRED.search(raw_line)
    return (bool(match) and not code_line[match.start():match.end()].strip()
            and not code_line[:match.start()].rstrip().endswith(('"', "'")))


def unwaited_shows(source: str) -> list[int]:
    """The 1-based lines of source holding a show() that no wait follows."""
    code = code_only(source)
    # Split on line feeds only, as editors and compilers count lines.
    raw_lines, code_lines = source.split("\n"), code.split("\n")
    unwaited: list[int] = []
    for show in SHOW.finditer(code):
        line = code.count("\n", 0, show.start())
        if unwaited and unwaited[-1] == line + 1:
            continue
        line_start = code.rfind("\n", 0, show.start()) + 1
        receiver = RECEIVER.search(code, line_start, show.start())
        shown = _normalized(receiver.group(1)) if receiver else ""
        end = show.end()
        if code.startswith(";", end):
            end += 1
        if _waits_next(code, end, shown):
            continue
        if _deferred(raw_lines[line], code_lines[line]) and _waits_later(code, end, shown):
            continue
        unwaited.append(line + 1)
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
