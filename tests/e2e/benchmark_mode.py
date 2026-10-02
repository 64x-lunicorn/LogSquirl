"""
Runs the application's benchmark mode and reads its report (#666).

``logsquirl_grep --benchmark-output <file>`` writes a report of the same format
for its one Search (scenario ``grep``, #667): run_grep_benchmark() runs it.

The ``search`` scenario runs one Search of a SearchVariant in the GUI (#668);
SearchVariant.known_match_count() counts what it must find, independently of
LogSquirl.

``logsquirl --benchmark <scenario> <Log File>...`` runs one scenario and
writes what happened as one JSON object; BUILD.md, "Benchmark mode", documents
the command line and every field. This module is what the e2e suites use to
run it and to check that a report has the shape they read, so a suite notices a
report of another format version instead of reading it wrongly.

The application isolates a benchmark run itself, and it is still started only
through an isolated instance here, as every e2e test starts the application.
"""

from __future__ import annotations

import functools
import json
import re
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


class ScenarioUnknown(Exception):
    """The logsquirl run knows no scenario of that name: a build older than it."""


def run_known_scenario(
    instance: IsolatedLogSquirl,
    scenario: str,
    log_files: list[Path],
    report_path: Path,
    options: dict[str, str] | None = None,
    timeout: float = 300.0,
) -> BenchmarkRun:
    """run_benchmark(), raising ScenarioUnknown when the binary has no such scenario.

    The before side of the Benchmarks workflow runs the suite on the binaries
    of its own commit, which may predate a scenario.
    """
    run = run_benchmark(instance, scenario, log_files, report_path, options, timeout)
    if (
        run.process.returncode == EXIT_USAGE
        and run.report is None
        and f"no benchmark scenario '{scenario}'" in run.process.stderr
    ):
        raise ScenarioUnknown(scenario)
    return run


@dataclass(frozen=True)
class SearchVariant:
    """One way of searching a Log File, as the search scenario runs it (#668)."""

    label: str
    pattern: str
    # The Search Line's regular expression and match case buttons.
    regex: bool = False
    match_case: bool = True
    description: str = ""

    def options(self) -> dict[str, str]:
        """The --benchmark-option values of the search scenario."""
        return {
            "pattern": self.pattern,
            "regex": "true" if self.regex else "false",
            "match_case": "true" if self.match_case else "false",
        }

    def matches(self, line: str) -> bool:
        """Whether a Log Line, without its line feed, is a Match: read by Python, not LogSquirl."""
        if self.regex:
            flags = 0 if self.match_case else re.IGNORECASE
            return re.search(self.pattern, line, flags) is not None
        if self.match_case:
            return self.pattern in line
        return self.pattern.casefold() in line.casefold()

    def known_match_count(self, log_file: Path) -> int:
        """How many Log Lines of the Log File are Matches, counted here."""
        stat = log_file.stat()
        return _known_match_count(self, str(log_file), stat.st_size, stat.st_mtime_ns)


@functools.lru_cache(maxsize=None)
def _known_match_count(variant: SearchVariant, path: str, size: int, mtime_ns: int) -> int:
    # The size and modification time make a rewritten Log File counted anew.
    with open(path, encoding="utf-8", newline="\n") as log:
        return sum(1 for line in log if variant.matches(line.rstrip("\n")))


# The Searches the search scenario is measured with, on the generated Log Files
# of generate_test_data.py: one Log Line in 101 an ERROR, which times out after
# a four-digit number of milliseconds, one in 13 (but not an ERROR) a WARN.
SEARCH_VARIANTS = (
    SearchVariant("plain", "ERROR", description="Plain text, the ERROR Log Lines"),
    SearchVariant("regex", r"timed out after [0-9]{4} ms", regex=True,
                  description="A regular expression, the ERROR Log Lines"),
    SearchVariant("no_match", "ZZZZ_NEVER_MATCH_99999", description="Plain text without a Match"),
    SearchVariant("case_insensitive", "error", match_case=False,
                  description="Plain text, case ignored, the ERROR Log Lines"),
    SearchVariant("alternation", "ERROR|WARN", regex=True,
                  description="An alternation, the ERROR and WARN Log Lines"),
)
