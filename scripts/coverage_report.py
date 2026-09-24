#!/usr/bin/env python3
"""Line coverage of LogSquirl's own modules, from a build made with -DENABLE_COVERAGE=ON (#444).

Reads the .gcda files the instrumented test binaries left in the build tree,
asks gcov (GCC's, or `xcrun llvm-cov gcov` on macOS) what each line did, and
sums the lines up per module: the directories under src/. Third party code, the
tests and generated files are not counted.

    cmake -B build -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
    cmake --build build
    python3 scripts/coverage_report.py --build-dir build --reset
    (cd build && ctest)
    python3 scripts/coverage_report.py --build-dir build

The `coverage` target of an ENABLE_COVERAGE build runs the three steps.

There is no threshold: the report says what is covered, it does not judge it.
Uses the standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
from collections import defaultdict
from pathlib import Path


def find_gcda(build_dir: Path) -> list[Path]:
    """Every coverage data file of the project's own targets."""
    return sorted(p for p in build_dir.rglob("*.gcda") if "_deps" not in p.parts)


def reset(build_dir: Path) -> int:
    files = find_gcda(build_dir)
    for path in files:
        path.unlink()
    print(f"removed {len(files)} coverage data files")
    return 0


def parse_gcov_text(text: str) -> dict[str, dict[int, int]]:
    """{source file: {line number: execution count}} from `gcov -t` output.

    A line has the form `count:line:source`; a count is a number (with a
    trailing `*` for a line with unexecuted blocks), `#####` or `=====` for a
    line that never ran, and `-` for a line that holds no code.
    """
    files: dict[str, dict[int, int]] = {}
    current: dict[int, int] | None = None
    for raw in text.splitlines():
        parts = raw.split(":", 2)
        if len(parts) < 3:
            continue
        count_text, line_text, rest = parts[0].strip(), parts[1].strip(), parts[2]
        if not line_text.isdigit():
            continue
        line = int(line_text)
        if line == 0:
            if rest.startswith("Source:"):
                current = files.setdefault(rest[len("Source:"):].strip(), {})
            continue
        if current is None or count_text == "-":
            continue
        if count_text in ("#####", "====="):
            count = 0
        else:
            digits = count_text.rstrip("*")
            if not digits.isdigit():
                continue
            count = int(digits)
        current[line] = current.get(line, 0) + count
    return files


def run_gcov(gcov: list[str], gcda: Path) -> dict[str, dict[int, int]]:
    result = subprocess.run(
        [*gcov, "-t", gcda.name],
        cwd=gcda.parent,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    if result.returncode != 0:
        print(f"warning: gcov failed for {gcda}: {result.stderr.strip()[:200]}", file=sys.stderr)
        return {}
    return parse_gcov_text(result.stdout)


def module_of(path: Path, source_root: Path) -> str | None:
    """The module a source file belongs to: its directory under src/."""
    try:
        relative = path.resolve().relative_to(source_root / "src")
    except ValueError:
        return None
    return relative.parts[0] if len(relative.parts) > 1 else None


def collect(build_dir: Path, source_root: Path, gcov: list[str]):
    lines: dict[Path, dict[int, int]] = defaultdict(dict)
    for gcda in find_gcda(build_dir):
        for name, counts in run_gcov(gcov, gcda).items():
            path = Path(name)
            if not path.is_absolute():
                path = gcda.parent / path
            path = path.resolve()
            if "autogen" in path.parts or path.name.startswith("moc_"):
                continue
            merged = lines[path]
            for line, count in counts.items():
                merged[line] = merged.get(line, 0) + count

    modules: dict[str, dict[str, int]] = defaultdict(lambda: {"lines": 0, "hit": 0, "files": 0})
    files: dict[str, dict[str, int]] = {}
    for path, counts in lines.items():
        module = module_of(path, source_root)
        if module is None:
            continue
        total = len(counts)
        hit = sum(1 for count in counts.values() if count > 0)
        modules[module]["lines"] += total
        modules[module]["hit"] += hit
        modules[module]["files"] += 1
        files[str(path.relative_to(source_root))] = {"lines": total, "hit": hit}
    return modules, files


def percent(hit: int, total: int) -> float:
    return 100.0 * hit / total if total else 0.0


def render_markdown(modules, files) -> str:
    total_lines = sum(m["lines"] for m in modules.values())
    total_hit = sum(m["hit"] for m in modules.values())
    out = ["## Line coverage per module", "", "| Module | Files | Lines | Covered | % |", "|---|---:|---:|---:|---:|"]
    for name in sorted(modules):
        m = modules[name]
        out.append(f"| {name} | {m['files']} | {m['lines']} | {m['hit']} | {percent(m['hit'], m['lines']):.1f} |")
    out.append(f"| **all** | {sum(m['files'] for m in modules.values())} | {total_lines} | {total_hit} | {percent(total_hit, total_lines):.1f} |")
    uncovered = sorted(name for name, f in files.items() if f["lines"] and f["hit"] == 0)
    if uncovered:
        out += ["", "Files no test executed a line of:", ""]
        out += [f"- `{name}`" for name in uncovered]
    return "\n".join(out) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument(
        "--gcov",
        default=os.environ.get("GCOV", "gcov"),
        help='gcov command, e.g. "xcrun llvm-cov gcov" (default: $GCOV or gcov)',
    )
    parser.add_argument("--reset", action="store_true", help="delete the coverage data of earlier runs and stop")
    parser.add_argument("--json", type=Path, help="also write the numbers as JSON")
    parser.add_argument("--markdown", type=Path, help="also write the table as Markdown (e.g. $GITHUB_STEP_SUMMARY)")
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    if args.reset:
        return reset(build_dir)

    if not find_gcda(build_dir):
        print(f"no .gcda files under {build_dir}: build with -DENABLE_COVERAGE=ON and run the tests", file=sys.stderr)
        return 1

    modules, files = collect(build_dir, args.source_root.resolve(), shlex.split(args.gcov))
    if not modules:
        print("coverage data found, but none of it belongs to a module under src/", file=sys.stderr)
        return 1

    markdown = render_markdown(modules, files)
    print(markdown)
    if args.markdown:
        with args.markdown.open("a", encoding="utf-8") as handle:
            handle.write(markdown)
    if args.json:
        args.json.write_text(json.dumps({"modules": modules, "files": files}, indent=2, sort_keys=True), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
