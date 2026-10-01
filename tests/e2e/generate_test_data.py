#!/usr/bin/env python3
"""
Generate large test data files for LogSquirl performance benchmarks.

Creates 10 MB, 50 MB, and 100 MB test files by replicating and varying
the existing 1 MB random block data -- a single Log Line of 1 MB, repeated --
and 100 MB and 1 GB Log Files of ordinary Log Lines, written by
generate_log_file() (#667), and the Log Files the scroll scenario scrolls
through (#669): 100 MB of Log Lines a built-in Log Format recognizes, and 20 MB
of them with ANSI color sequences. Files are placed in test_data/ and are listed in
.gitignore (not committed to the repository).

Usage:
    python generate_test_data.py [--force] [--max-mb N]

Options:
    --force     Regenerate files even if they already exist
    --max-mb N  Leave out the files larger than N MB: the 1 GB Log File is for
                the Benchmarks and Performance workflows, a laptop can do
                without it (--max-mb 100)
"""

import argparse
import functools
import sys
from pathlib import Path


def find_repo_root() -> Path:
    """Walk up from this file to find the repository root."""
    current = Path(__file__).resolve().parent
    for _ in range(10):
        if (current / "CMakeLists.txt").exists() and (current / "test_data").is_dir():
            return current
        current = current.parent
    raise RuntimeError("Could not find repository root")


def generate_file(source: Path, target: Path, target_mb: int, force: bool = False):
    """Generate a large test file by replicating source data with unique line IDs."""
    if target.exists() and not force:
        size_mb = target.stat().st_size / (1024 * 1024)
        print(f"  {target.name} already exists ({size_mb:.1f} MB) — skipping (use --force to regenerate)")
        return

    print(f"  Generating {target.name} ({target_mb} MB)...", end=" ", flush=True)

    source_lines = source.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
    if not source_lines:
        print("ERROR: source file is empty")
        return

    target_bytes = target_mb * 1024 * 1024
    written = 0
    chunk_id = 0

    with open(target, "w", encoding="utf-8") as f:
        while written < target_bytes:
            for line in source_lines:
                if written >= target_bytes:
                    break
                # Inject a unique chunk marker every 10000 lines to vary content
                if chunk_id % 10000 == 0:
                    marker = f"[BENCH_CHUNK_{chunk_id:08d}] "
                    out_line = marker + line
                else:
                    out_line = line
                f.write(out_line)
                written += len(out_line.encode("utf-8"))
                chunk_id += 1

    actual_mb = target.stat().st_size / (1024 * 1024)
    print(f"done ({actual_mb:.1f} MB, {chunk_id} lines)")


# What the Log Lines of a generated Log File say, in turn. One in about a
# hundred is an ERROR, as in a log someone searches for its errors.
_LOG_MESSAGES = (
    "request {id} handled in {ms} ms",
    "cache lookup for key user:{id} hit",
    "connection from 10.0.{a}.{b} accepted",
    "scheduled job {id} started",
    "retrying upstream call {id} (attempt {attempt})",
    "session {id} renewed for user {a}{b}",
    "flushed {ms} records to disk",
)


def _log_line_parts(number: int) -> tuple[str, str, str, str]:
    """Timestamp, level, thread and message of Log Line number of a generated Log File."""
    seconds = number // 1000
    if number % 101 == 0:
        level, message = "ERROR", f"request {number} timed out after {number % 9000 + 1000} ms"
    elif number % 13 == 0:
        level, message = "WARN ", f"slow response for request {number}: {number % 900 + 100} ms"
    else:
        level = "DEBUG" if number % 3 == 0 else "INFO "
        message = _LOG_MESSAGES[number % len(_LOG_MESSAGES)].format(
            id=number, ms=number % 997, a=number % 256, b=(number // 256) % 256,
            attempt=number % 5 + 1,
        )
    timestamp = (
        f"2026-09-{1 + seconds // 86400 % 30:02d} {seconds // 3600 % 24:02d}:"
        f"{seconds // 60 % 60:02d}:{seconds % 60:02d}.{number % 1000:03d}"
    )
    return timestamp, level, f"worker-{number % 16:02d}", message


def log_lines(first: int, count: int) -> str:
    """Log Lines first .. first + count - 1 of a generated Log File, deterministic."""
    lines = []
    for number in range(first, first + count):
        timestamp, level, thread, message = _log_line_parts(number)
        lines.append(f"{timestamp} {level} [{thread}] {message}\n")
    return "".join(lines)


# The classes the Log Lines of a scroll Log File name, in turn.
_LOG_CLASSES = (
    "c.e.http.RequestHandler",
    "c.e.cache.LookupService",
    "c.e.net.Acceptor",
    "c.e.jobs.Scheduler",
    "c.e.http.UpstreamClient",
    "c.e.auth.SessionStore",
    "c.e.store.Flusher",
)

# The ANSI color sequences a colored logger writes around the level, the thread
# and the class, by level.
_ANSI_LEVEL_COLORS = {"ERROR": "\x1b[1;31m", "WARN ": "\x1b[33m", "INFO ": "\x1b[32m",
                      "DEBUG": "\x1b[2;37m"}
_ANSI_RESET = "\x1b[0m"


def scroll_log_lines(first: int, count: int, ansi: bool = False) -> str:
    """Log Lines first .. first + count - 1 of a scroll Log File (#669), deterministic.

    The Log Lines of log_lines() with the class that logged them, as a Java
    logger writes them -- ``<timestamp> <level> [<thread>] [<class>] <message>``
    -- which a built-in Log Format recognizes, so they have a Table View. With
    ansi, the level, the thread and the class are wrapped in ANSI color
    sequences, as a colored console logger writes them.
    """
    lines = []
    for number in range(first, first + count):
        timestamp, level, thread, message = _log_line_parts(number)
        logger = _LOG_CLASSES[number % len(_LOG_CLASSES)]
        if ansi:
            lines.append(
                f"{timestamp} {_ANSI_LEVEL_COLORS[level]}{level}{_ANSI_RESET} "
                f"\x1b[36m[{thread}]{_ANSI_RESET} \x1b[35m[{logger}]{_ANSI_RESET} {message}\n"
            )
        else:
            lines.append(f"{timestamp} {level} [{thread}] [{logger}] {message}\n")
    return "".join(lines)


def generate_log_file(target: Path, target_mb: int, force: bool = False, lines=log_lines):
    """Generate a Log File of ordinary Log Lines of about 80 bytes, up to target_mb MB.

    lines(first, count) writes them: log_lines(), or one of the Log Lines of
    the scroll Log Files.
    """
    if target.exists() and not force:
        size_mb = target.stat().st_size / (1024 * 1024)
        print(f"  {target.name} already exists ({size_mb:.1f} MB) — skipping (use --force to regenerate)")
        return

    print(f"  Generating {target.name} ({target_mb} MB of Log Lines)...", end=" ", flush=True)

    target_bytes = target_mb * 1024 * 1024
    written = 0
    number = 0
    block = 20_000
    with open(target, "wb") as f:
        while written < target_bytes:
            data = lines(number, block).encode("utf-8")
            if written + len(data) > target_bytes:
                # Cut at the last whole Log Line that still fits.
                data = data[: data.rfind(b"\n", 0, target_bytes - written) + 1]
                if not data:
                    break
            f.write(data)
            written += len(data)
            number += data.count(b"\n")

    actual_mb = target.stat().st_size / (1024 * 1024)
    print(f"done ({actual_mb:.1f} MB, {number} lines)")


# The Log Files the scroll scenario is measured on (#669): Log Lines a built-in
# Log Format recognizes, and the same Log Lines with ANSI color sequences.
# Scrolling paints a screen at a time, whatever the size of the Log File, so
# the ANSI one need not be large.
SCROLL_LOG_FILE = "generated_scroll_100Mb.log"
SCROLL_ANSI_MB = 20
SCROLL_ANSI_LOG_FILE = f"generated_scroll_ansi_{SCROLL_ANSI_MB}Mb.log"


def generate_utf16_file(source: Path, target: Path, target_mb: int, force: bool = False):
    """Generate a large UTF-16LE test file."""
    if target.exists() and not force:
        size_mb = target.stat().st_size / (1024 * 1024)
        print(f"  {target.name} already exists ({size_mb:.1f} MB) — skipping (use --force to regenerate)")
        return

    print(f"  Generating {target.name} ({target_mb} MB, UTF-16LE)...", end=" ", flush=True)

    source_data = source.read_bytes()
    if not source_data:
        print("ERROR: source file is empty")
        return

    source_text = source_data.decode("utf-16-le", errors="replace") if source_data[:2] == b'\xff\xfe' else source_data.decode("utf-8", errors="replace")
    source_lines = source_text.splitlines(keepends=True)

    target_bytes = target_mb * 1024 * 1024
    written = 0
    chunk_id = 0

    with open(target, "wb") as f:
        # Write BOM
        f.write(b'\xff\xfe')
        written += 2

        while written < target_bytes:
            for line in source_lines:
                if written >= target_bytes:
                    break
                encoded = line.encode("utf-16-le")
                f.write(encoded)
                written += len(encoded)
                chunk_id += 1

    actual_mb = target.stat().st_size / (1024 * 1024)
    print(f"done ({actual_mb:.1f} MB)")


def main():
    parser = argparse.ArgumentParser(description="Generate large test data files for benchmarks")
    parser.add_argument("--force", action="store_true", help="Regenerate files even if they exist")
    parser.add_argument(
        "--max-mb", type=int, default=None,
        help="leave out the files larger than this many MB (the 1 GB Log File is for CI)",
    )
    args = parser.parse_args()

    def wanted(target_mb: int) -> bool:
        return args.max_mb is None or target_mb <= args.max_mb

    repo_root = find_repo_root()
    test_data = repo_root / "test_data"

    source_1mb = test_data / "random_block_1Mb.txt"
    source_utf16 = test_data / "random_block_1Mb_utf16le.txt"

    if not source_1mb.exists():
        print(f"ERROR: Source file not found: {source_1mb}")
        sys.exit(1)

    print("Generating large test data files for LogSquirl benchmarks:")
    print(f"  Source: {source_1mb}")
    print()

    # UTF-8 test files
    for target_mb in [10, 50, 100]:
        if wanted(target_mb):
            target = test_data / f"random_block_{target_mb}Mb.txt"
            generate_file(source_1mb, target, target_mb, args.force)

    # Log Files of ordinary Log Lines, at the sizes LogSquirl is made for (#667)
    for target_mb, name in ((100, "generated_100Mb.log"), (1024, "generated_1Gb.log")):
        if wanted(target_mb):
            generate_log_file(test_data / name, target_mb, args.force)

    # Log Files to scroll through, with a Log Format and with ANSI colors (#669)
    if wanted(100):
        generate_log_file(test_data / SCROLL_LOG_FILE, 100, args.force, lines=scroll_log_lines)
    if wanted(SCROLL_ANSI_MB):
        generate_log_file(test_data / SCROLL_ANSI_LOG_FILE, SCROLL_ANSI_MB, args.force,
                          lines=functools.partial(scroll_log_lines, ansi=True))

    # UTF-16LE test files
    if source_utf16.exists() and wanted(10):
        target = test_data / "random_block_10Mb_utf16le.txt"
        generate_utf16_file(source_utf16, target, 10, args.force)

    print()
    print("Done. These files are in .gitignore and should not be committed.")


if __name__ == "__main__":
    main()
