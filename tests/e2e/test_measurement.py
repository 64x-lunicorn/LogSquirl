"""
How the performance suite turns runs into the numbers it reports (#667).

A benchmark reports what the application timed itself -- the events of a
benchmark report -- not the time a process took to start, run and exit: a run
returns its own durations, one per benchmark, and every benchmark gets the
statistics of its own. These tests need no LogSquirl binary.
"""

import json

import pytest

import conftest
from conftest import generate_benchmark_report, measure_events, summarize_runs


def test_summarize_runs_reports_the_statistics_of_the_runs():
    result = summarize_runs([0.5, 0.1, 0.3, 0.2, 0.4])

    assert result["median_seconds"] == 0.3
    assert result["min_seconds"] == 0.1
    assert result["max_seconds"] == 0.5
    assert result["runs"] == [0.1, 0.2, 0.3, 0.4, 0.5]
    assert result["total_count"] == 5


def test_summarize_runs_filters_outliers():
    result = summarize_runs([1.0] * 10 + [9.0])

    assert result["filtered_count"] == 10
    assert result["max_seconds"] == 1.0
    # The raw runs stay complete, for Welch's t-test.
    assert len(result["runs"]) == 11


def test_measure_events_reports_each_duration_a_run_returns():
    durations = iter([{"first": 0.01 * n, "second": 0.1 * n} for n in range(1, 8)])

    results = measure_events(lambda: next(durations), warmup=2, runs=5)

    assert set(results) == {"first", "second"}
    # The two warmup runs are not part of the statistics.
    assert results["first"]["runs"] == pytest.approx([0.03, 0.04, 0.05, 0.06, 0.07])
    assert results["second"]["median_seconds"] == pytest.approx(0.5)


def test_measure_events_reports_what_the_run_timed_not_how_long_it_took():
    def run():
        return {"event": 0.001}

    results = measure_events(run, warmup=0, runs=3)

    assert results["event"]["runs"] == [0.001, 0.001, 0.001]


def test_measure_events_needs_every_duration_in_every_run():
    durations = iter([{"a": 0.1, "b": 0.1}, {"a": 0.1}])

    with pytest.raises(AssertionError, match="b"):
        measure_events(lambda: next(durations), warmup=0, runs=2)


def test_the_json_report_keeps_what_each_benchmark_measures(tmp_path, monkeypatch):
    monkeypatch.setattr(conftest, "_REPORT_DIR", tmp_path)
    result = summarize_runs([0.1, 0.1, 0.1])
    result["measures"] = "the open to the last match written"
    result["throughput"] = {"file_size_bytes": 1, "file_size_mb": 1.0, "mb_per_sec": 10.0,
                            "lines_per_sec": 1.0}

    generate_benchmark_report({"grep_1mb_simple": result}, None, {}, report_format="json")

    report = json.loads((tmp_path / "benchmark_report.json").read_text(encoding="utf-8"))
    # benchmark-compare.py and perf-history.py read these fields.
    benchmark = report["benchmarks"]["grep_1mb_simple"]
    assert benchmark["median_seconds"] == 0.1
    assert benchmark["runs"] == [0.1, 0.1, 0.1]
    assert benchmark["measures"] == "the open to the last match written"


def test_the_markdown_report_says_what_each_benchmark_measures(tmp_path, monkeypatch):
    monkeypatch.setattr(conftest, "_REPORT_DIR", tmp_path)
    result = summarize_runs([0.1, 0.1, 0.1])
    result["measures"] = "the open to the last match written"
    result["throughput"] = {"file_size_bytes": 1, "file_size_mb": 1.0, "mb_per_sec": 10.0,
                            "lines_per_sec": 1.0}

    generate_benchmark_report({"grep_1mb_simple": result}, None, {}, report_format="markdown")

    markdown = (tmp_path / "benchmark_report.md").read_text(encoding="utf-8")
    assert "| grep_1mb_simple | the open to the last match written |" in markdown
    assert "| grep_1mb_simple | 1.0 MB | 0.1000s | 10.0 MB/s |" in markdown


def _baseline(median_seconds: float, **meta) -> dict:
    return {"_meta": {"tolerance_percent": 5, **meta},
            "benchmarks": {"case": {"median_seconds": median_seconds}}}


def test_the_limit_is_the_tolerance_above_the_baseline():
    assert conftest.max_allowed_seconds(1.0, _baseline(1.0)) == pytest.approx(1.05)


def test_a_benchmark_of_milliseconds_has_an_absolute_margin_too():
    # 5 % of 2 ms is a scheduling hiccup, not a regression.
    assert conftest.max_allowed_seconds(0.002, _baseline(0.002)) == pytest.approx(
        0.002 + conftest.MIN_DELTA_SECONDS
    )
    assert conftest.max_allowed_seconds(
        0.002, _baseline(0.002, min_delta_seconds=0.0)
    ) == pytest.approx(0.0021)


def test_a_slower_benchmark_within_the_absolute_margin_passes():
    measured = summarize_runs([0.0025 + n * 1e-6 for n in range(21)])
    entry = _baseline(0.002)
    entry["benchmarks"]["case"]["runs"] = [0.002 + n * 1e-6 for n in range(21)]

    conftest.assert_performance("case", measured, entry)


def test_a_slower_benchmark_beyond_both_margins_fails():
    measured = summarize_runs([0.0045 + n * 1e-6 for n in range(21)])
    entry = _baseline(0.002)
    entry["benchmarks"]["case"]["runs"] = [0.002 + n * 1e-6 for n in range(21)]

    with pytest.raises(AssertionError, match="regression"):
        conftest.assert_performance("case", measured, entry)


def test_the_reports_name_the_benchmarks_this_run_did_not_measure(tmp_path, monkeypatch):
    # The 1 GB cases run in CI; a laptop run without the generated Log File
    # says that it left them out instead of looking complete.
    monkeypatch.setattr(conftest, "_REPORT_DIR", tmp_path)
    results = {"grep_1mb_simple": summarize_runs([0.1, 0.1, 0.1])}

    for report_format in ("markdown", "json"):
        generate_benchmark_report(results, None, {}, report_format=report_format,
                                  not_measured=["grep_log_1gb_simple"])

    markdown = (tmp_path / "benchmark_report.md").read_text(encoding="utf-8")
    assert "## Not Measured in This Run" in markdown
    assert "- grep_log_1gb_simple" in markdown
    report = json.loads((tmp_path / "benchmark_report.json").read_text(encoding="utf-8"))
    assert report["not_measured"] == ["grep_log_1gb_simple"]
    assert list(report["benchmarks"]) == ["grep_1mb_simple"]
