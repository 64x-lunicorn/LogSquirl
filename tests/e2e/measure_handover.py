#!/usr/bin/env python3
"""
Times handing a Log File over from a secondary instance to the primary one.

Starts one isolated primary instance (offscreen, own settings, Session and
single-instance lock -- see isolated_instance.py; the user's LogSquirl and
its settings are not touched), then launches a secondary instance with a Log
File RUNS times and reports the median of

- open:  secondary launched -> primary logs that it opens the Log File
- exit:  secondary launched -> secondary process has exited

Usage (macOS / Linux):

    python3 tests/e2e/measure_handover.py --binary build/output/logsquirl.app/Contents/MacOS/logsquirl
    python3 tests/e2e/measure_handover.py --binary build/output/logsquirl --runs 21

Compare an optimized build of this branch against one of origin/master.
"""

import argparse
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from isolated_instance import IsolatedLogSquirl, supported  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", required=True, type=Path,
                        help="the logsquirl executable (inside the .app bundle on macOS)")
    parser.add_argument("--runs", type=int, default=15)
    parser.add_argument("--warmup", type=int, default=2)
    args = parser.parse_args()

    if not supported():
        print("measure_handover.py needs macOS or Linux", file=sys.stderr)
        return 2

    opens: list[float] = []
    exits: list[float] = []

    with IsolatedLogSquirl(args.binary.resolve()) as env:
        env.start_primary()

        for run in range(args.warmup + args.runs):
            # A new Log File each run: the primary logs every file it is handed.
            log_file = env.root / f"handover_{run}.log"
            log_file.write_text("a line\n")

            start = time.perf_counter()
            secondary = env.launch_secondary(str(log_file))
            opened = env.wait_for_primary_line(env.opened_line(log_file), timeout=30)
            secondary.communicate(timeout=30)
            exited = time.perf_counter()

            if secondary.returncode != 0:
                print(f"run {run}: secondary exited with {secondary.returncode}", file=sys.stderr)
                return 1
            if run < args.warmup:
                continue

            opens.append((opened - start) * 1000)
            exits.append((exited - start) * 1000)
            print(f"run {run - args.warmup + 1:2d}: open {opens[-1]:7.1f} ms, "
                  f"exit {exits[-1]:7.1f} ms")

    print(f"median over {args.runs} runs: open {statistics.median(opens):.1f} ms, "
          f"exit {statistics.median(exits):.1f} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
