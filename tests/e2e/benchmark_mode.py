"""
Runs the application's benchmark mode and reads its report (#666).

``logsquirl_grep --benchmark-output <file>`` writes a report of the same format
for its one Search (scenario ``grep``, #667): run_grep_benchmark() runs it.

``logsquirl --benchmark <scenario> <Log File>...`` runs one scenario and
writes what happened as one JSON object; BUILD.md, "Benchmark mode", documents
the command line and every field. This module is what the e2e suites use to
run it and to check that a report has the shape they read, so a suite notices a
report of another format version instead of reading it wrongly.

The application isolates a benchmark run itself, and it is still started only
through an isolated instance here, as every e2e test starts the application.
"""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass
from pathlib import Path

from isolated_instance import IsolatedLogSquirl

FORMAT = "logsquirl-benchmark"
FORMAT_VERSION = 1

# The exit codes of a benchmark run.
EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_USAGE = 2

_EVENT_FIELDS = {"name": str, "since_process_start_ms": (int, float), "since_scenario_start_ms": (int, float)}
_REPORT_FIELDS = {
    "format": str,
    "format_version": int,
    "scenario": str,
    "outcome": str,
    "application": dict,
    "platform": dict,
    "options": dict,
    "log_files": list,
    "process": dict,
    "scenario_started_ms": (int, float),
    "events": list,
    "results": dict,
}


class ReportFormatError(AssertionError):
    """A report that is not a format version 1 benchmark report."""


def check_report(report: dict) -> dict:
    """Checks the fields every format version 1 report has; returns the report."""
    if report.get("format") != FORMAT:
        raise ReportFormatError(f"not a benchmark report: format {report.get('format')!r}")
    if report.get("format_version") != FORMAT_VERSION:
        raise ReportFormatError(
            f"format version {report.get('format_version')!r}, this reader knows {FORMAT_VERSION}"
        )
    for field, kind in _REPORT_FIELDS.items():
        if not isinstance(report.get(field), kind):
            raise ReportFormatError(f"{field!r} is missing or not {kind}: {report.get(field)!r}")
    if report["outcome"] not in ("passed", "failed"):
        raise ReportFormatError(f"unknown outcome {report['outcome']!r}")
    if report["outcome"] == "failed" and not isinstance(report.get("failure"), str):
        raise ReportFormatError("a failed report says why in 'failure'")
    if report["process"].get("start_source") not in ("os", "main"):
        raise ReportFormatError(f"unknown start_source {report['process'].get('start_source')!r}")
    if not isinstance(report["process"].get("main_entered_ms"), (int, float)):
        raise ReportFormatError("'process' has no main_entered_ms")
    for event in report["events"]:
        for field, kind in _EVENT_FIELDS.items():
            if not isinstance(event.get(field), kind):
                raise ReportFormatError(f"event field {field!r} is missing or wrong: {event!r}")
        if "data" in event and not isinstance(event["data"], dict):
            raise ReportFormatError(f"event data is not an object: {event!r}")
    for log_file in report["log_files"]:
        if not isinstance(log_file.get("path"), str) or not isinstance(log_file.get("size_bytes"), int):
            raise ReportFormatError(f"log file entry is malformed: {log_file!r}")
    return report


def seconds_since_scenario_start(report: dict, name: str) -> float:
    """When the first event of that name happened, in seconds since the scenario started."""
    return event(report, name)["since_scenario_start_ms"] / 1000.0


def event(report: dict, name: str) -> dict:
    """The first event of that name; raises KeyError when there is none."""
    for candidate in report["events"]:
        if candidate["name"] == name:
            return candidate
    raise KeyError(f"no event {name!r} in {[e['name'] for e in report['events']]}")


@dataclass
class BenchmarkRun:
    process: subprocess.CompletedProcess
    report: dict | None


def run_benchmark(
    instance: IsolatedLogSquirl,
    scenario: str,
    log_files: list[Path],
    report_path: Path,
    options: dict[str, str] | None = None,
    timeout: float = 300.0,
) -> BenchmarkRun:
    """Runs one benchmark scenario in an isolated instance and reads its report.

    The report is None when the run wrote none (a usage error); a report that
    was written is checked with check_report().
    """
    args = ["--benchmark", scenario, "--benchmark-output", str(report_path)]
    for name, value in (options or {}).items():
        args += ["--benchmark-option", f"{name}={value}"]
    # The instance's own timeout is a second line of defence behind the run's.
    args += ["--benchmark-timeout", str(int(timeout))]
    args += [str(path) for path in log_files]

    if report_path.exists():
        report_path.unlink()
    process = instance.run(*args, timeout=timeout + 60)
    report = None
    if report_path.exists():
        report = check_report(json.loads(report_path.read_text(encoding="utf-8")))
    return BenchmarkRun(process, report)


def run_grep_benchmark(
    binary: Path,
    pattern: str,
    log_file: Path,
    report_path: Path,
    timeout: float = 300.0,
) -> BenchmarkRun:
    """Runs logsquirl_grep on one Log File with a benchmark report (#667).

    The tool searches as it always does and writes the matched Log Lines to
    stdout; with --benchmark-output it also writes a report of scenario
    ``grep`` whose events are timed from the moment the Log File is opened, so
    the process startup is not part of them. The report is None when the run
    wrote none: a logsquirl_grep older than the option rejects it.
    """
    if report_path.exists():
        report_path.unlink()
    process = subprocess.run(
        [str(binary), "--benchmark-output", str(report_path), "-e", pattern, str(log_file)],
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    report = None
    if report_path.exists():
        report = check_report(json.loads(report_path.read_text(encoding="utf-8")))
    return BenchmarkRun(process, report)
