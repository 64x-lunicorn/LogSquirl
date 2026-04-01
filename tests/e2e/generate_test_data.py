#!/usr/bin/env python3
"""
Generate large test data files for LogSquirl performance benchmarks.

Creates 10 MB, 50 MB, and 100 MB test files by replicating and varying
the existing 1 MB random block data. Files are placed in test_data/ and
should be listed in .gitignore (not committed to the repository).

Usage:
    python generate_test_data.py [--force]

Options:
    --force  Regenerate files even if they already exist
"""

import argparse
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
    args = parser.parse_args()

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
        target = test_data / f"random_block_{target_mb}Mb.txt"
        generate_file(source_1mb, target, target_mb, args.force)

    # UTF-16LE test files
    if source_utf16.exists():
        target = test_data / "random_block_10Mb_utf16le.txt"
        generate_utf16_file(source_utf16, target, 10, args.force)

    print()
    print("Done. These files are in .gitignore and should not be committed.")


if __name__ == "__main__":
    main()
