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


def test_the_markdown_report_counts_the_frames_over_budget(tmp_path, monkeypatch):
    # A scroll case reports how many of a run's frames were late (#669): a
    # count, not a time, so it is shown and not compared.
    monkeypatch.setattr(conftest, "_REPORT_DIR", tmp_path)
    result = summarize_runs([0.004, 0.005, 0.006])
    result["frames_over_budget"] = conftest.summarize_frames_over_budget(
        over_budget=[3, 1, 2], frame_counts=[241, 241, 242], budget_ms=1000 / 60
    )

    generate_benchmark_report({"gui_scroll_text_frame_p99": result}, None, {},
                              report_format="markdown")

    markdown = (tmp_path / "benchmark_report.md").read_text(encoding="utf-8")
    assert "## Frames Over Budget" in markdown
    assert "| gui_scroll_text_frame_p99 | 16.7 ms | 2 | 3 | 241 |" in markdown


def test_the_frames_over_budget_are_summarized_over_the_runs():
    summary = conftest.summarize_frames_over_budget(
        over_budget=[0, 4, 1, 1], frame_counts=[200, 210, 220, 230], budget_ms=16.7
    )

    assert summary == {"budget_ms": 16.7, "median": 1.0, "max": 4, "runs": [0, 4, 1, 1],
                       "frame_count_median": 215.0}


def test_the_markdown_report_says_whether_the_chart_kept_up(tmp_path, monkeypatch):
    # A follow case reports whether the chart following the Log File kept up
    # with the Text View in each run (#670): shown, never compared.
    monkeypatch.setattr(conftest, "_REPORT_DIR", tmp_path)
    result = summarize_runs([0.5, 0.6, 0.7])
    result["chart_following"] = conftest.summarize_chart_following(
        kept_up=[True, False, True], behind_p99_ms=[251.0, 1310.0, 249.0], budget_ms=1000
    )

    generate_benchmark_report({"gui_follow_10_per_s_chart_p99": result}, None, {},
                              report_format="markdown")

    markdown = (tmp_path / "benchmark_report.md").read_text(encoding="utf-8")
    assert "## Chart Following" in markdown
    assert "| gui_follow_10_per_s_chart_p99 | 1000 ms | 2 of 3 | 251 ms | 1310 ms |" in markdown


def test_the_chart_following_is_summarized_over_the_runs():
    summary = conftest.summarize_chart_following(
        kept_up=[True, True, False, True], behind_p99_ms=[250.0, 260.0, 1200.0, 240.0],
        budget_ms=1000,
    )

    assert summary == {"budget_ms": 1000, "kept_up_runs": 3, "runs": [True, True, False, True],
                       "behind_display_p99_ms_median": 255.0,
                       "behind_display_p99_ms_max": 1200.0}


def test_the_markdown_report_shows_the_indexing_parallelism(tmp_path, monkeypatch):
    # A read-while-indexing case reports how many cores its indexing kept
    # busy (#686): a ratio, not a time, so it is shown and not compared.
    monkeypatch.setattr(conftest, "_REPORT_DIR", tmp_path)
    result = summarize_runs([0.9, 1.0, 1.1])
    result["indexing_parallelism"] = conftest.summarize_indexing_parallelism([3.1, 2.9, 3.4])

    generate_benchmark_report({"gui_read_while_indexing_log_1gb_index_wall": result}, None, {},
                              report_format="markdown")

    markdown = (tmp_path / "benchmark_report.md").read_text(encoding="utf-8")
    assert "## Indexing Parallelism" in markdown
    assert "| gui_read_while_indexing_log_1gb_index_wall | 3.10 | 2.90 | 3.40 |" in markdown


def test_the_indexing_parallelism_is_summarized_over_the_runs():
    summary = conftest.summarize_indexing_parallelism([1.0, 3.0, 2.5, 2.0])

    assert summary == {"median": 2.25, "min": 1.0, "max": 3.0, "runs": [1.0, 3.0, 2.5, 2.0]}


def test_no_indexing_parallelism_where_the_cpu_time_is_unknown():
    # The platform did not tell the CPU time of any run.
    assert conftest.summarize_indexing_parallelism([]) is None


def _baseline(median_seconds: float, *, iqr_seconds: float | None = None,
              min_delta_seconds: float | None = None, **meta) -> dict:
    entry = {"median_seconds": median_seconds}
    if iqr_seconds is not None:
        entry["iqr_seconds"] = iqr_seconds
    if min_delta_seconds is not None:
        entry["min_delta_seconds"] = min_delta_seconds
    return {"_meta": {"tolerance_percent": 5, **meta}, "benchmarks": {"case": entry}}


def _limit(baseline: dict) -> float:
    return conftest.max_allowed_seconds(baseline["benchmarks"]["case"], baseline)


def test_the_limit_is_the_tolerance_above_the_baseline():
    assert _limit(_baseline(1.0)) == pytest.approx(1.05)


def test_a_benchmark_of_milliseconds_has_an_absolute_margin_too():
    # 5 % of 2 ms is a scheduling hiccup, not a regression.
    assert _limit(_baseline(0.002)) == pytest.approx(0.002 + conftest.MIN_DELTA_SECONDS)
    assert _limit(_baseline(0.002, min_delta_seconds=0.0)) == pytest.approx(0.0021)


def test_a_read_of_microseconds_is_not_inside_the_absolute_margin():
    # #705: with the index lock held while parsing, getNbLine's p50 went from
    # 0.1 µs to 60 µs and getExpandedLines' p99 from 10 µs to 90 µs.
    assert _limit(_baseline(0.1e-6)) < 60e-6
    assert _limit(_baseline(10e-6, iqr_seconds=3e-6)) < 90e-6


def test_the_absolute_margin_is_at_most_half_the_baseline():
    # A scroll frame of 0.3 ms may not take 1 ms more unnoticed.
    assert _limit(_baseline(0.0003)) == pytest.approx(0.00045)


def test_a_benchmark_that_scatters_gets_room_for_its_spread():
    assert _limit(_baseline(0.003, iqr_seconds=0.001)) == pytest.approx(0.006)


def test_the_meta_margin_caps_the_absolute_margin():
    # _meta.min_delta_seconds caps it as MIN_DELTA_SECONDS does: 5 ms of 20 ms.
    baseline = _baseline(0.02)
    baseline["_meta"]["min_delta_seconds"] = 0.005
    assert _limit(baseline) == pytest.approx(0.025)


def test_a_microsecond_regression_beyond_the_margins_fails():
    measured = summarize_runs([60e-6 + n * 1e-8 for n in range(21)])
    entry = _baseline(0.1e-6)
    entry["benchmarks"]["case"]["runs"] = [0.1e-6 + n * 1e-9 for n in range(21)]

    with pytest.raises(AssertionError, match="regression"):
        conftest.assert_performance("case", measured, entry)


def test_the_runs_keep_a_tenth_of_a_microsecond():
    # Six decimals of a second turned a read of 0.1 µs into 0 (#705).
    result = summarize_runs([0.1e-6, 0.12e-6, 0.11e-6])

    assert result["median_seconds"] == pytest.approx(0.11e-6)
    assert result["runs"] == pytest.approx([0.1e-6, 0.11e-6, 0.12e-6])


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


def test_a_new_baseline_keeps_the_margin_set_for_the_benchmark():
    # --update-baseline replaces the statistics, not the judgement beside them.
    old = {"median_seconds": 0.002, "iqr_seconds": 0.0001, "min_delta_seconds": 0.0005}
    measured = summarize_runs([0.001, 0.001, 0.001])

    entry = conftest.new_baseline_entry(old, measured)

    assert entry["median_seconds"] == 0.001
    assert entry["min_delta_seconds"] == 0.0005
    assert "min_delta_seconds" not in conftest.new_baseline_entry(None, measured)
