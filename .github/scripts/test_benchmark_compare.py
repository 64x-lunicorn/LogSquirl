"""Tests for benchmark-compare.py (#276): reading Catch2 XML and e2e performance
reports of two builds and writing the before/after table and JSON."""

from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "benchmark_compare", Path(__file__).with_name("benchmark-compare.py"))
bc = importlib.util.module_from_spec(_SPEC)
sys.modules["benchmark_compare"] = bc
_SPEC.loader.exec_module(bc)


def catch2_xml(binary: str, results: list[tuple[str, float, float, float]],
               section: str | None = None) -> str:
    """A Catch2 XML report: one test case, benchmarks as (name, mean, low, high) in ns."""
    benchmarks = "\n".join(
        f"""<BenchmarkResults name="{name}" samples="20" resamples="100000" iterations="1" clockResolution="20" estimatedDuration="1e+06">
        <!--All values in nano seconds-->
        <mean value="{mean}" lowerBound="{low}" upperBound="{high}" ci="0.95"/>
        <standardDeviation value="10" lowerBound="5" upperBound="15" ci="0.95"/>
        <outliers variance="0.1" lowMild="0" lowSevere="0" highMild="0" highSevere="0"/>
      </BenchmarkResults>"""
        for name, mean, low, high in results)
    if section is not None:
        benchmarks = f'<Section name="{section}" filename="x.cpp" line="1">{benchmarks}</Section>'
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<Catch name="{binary}">
  <Group name="{binary}">
    <TestCase name="Indexing a Log File" tags="[logdata-benchmark]" filename="x.cpp" line="1">
      {benchmarks}
      <OverallResult success="true"/>
    </TestCase>
    <OverallResults successes="1" failures="0" expectedFailures="0"/>
  </Group>
  <OverallResults successes="1" failures="0" expectedFailures="0"/>
</Catch>
"""


def e2e_report(benchmarks: dict[str, list[float]]) -> str:
    """An e2e benchmark_report.json with the measured runs (seconds) of each benchmark."""
    body = {}
    for name, runs in benchmarks.items():
        ordered = sorted(runs)
        body[name] = {
            "median_seconds": ordered[len(ordered) // 2],
            "mean_seconds": sum(runs) / len(runs),
            "p5_seconds": ordered[0],
            "p95_seconds": ordered[-1],
            "runs": runs,
        }
    return json.dumps({"date": "2026-09-17", "system": {}, "config": {}, "benchmarks": body})


def write_side(root: Path, side: str, xml: dict[str, str] | None = None,
               e2e: str | None = None) -> Path:
    directory = root / side
    (directory / "catch2").mkdir(parents=True)
    for name, text in (xml or {}).items():
        (directory / "catch2" / f"{name}.xml").write_text(text, encoding="utf-8")
    if e2e is not None:
        (directory / "e2e").mkdir()
        (directory / "e2e" / "benchmark_report.json").write_text(e2e, encoding="utf-8")
    return directory


def test_a_catch2_report_yields_the_mean_and_its_confidence_interval(tmp_path):
    side = write_side(tmp_path, "before", xml={
        "logsquirl_logdata_benchmark": catch2_xml(
            "logsquirl_logdata_benchmark", [("short lines: whole Log File", 1.5e9, 1.4e9, 1.6e9)]),
    })
    [result] = bc.read_side(side)
    assert result.suite == "Catch2"
    assert result.key == "logsquirl_logdata_benchmark / Indexing a Log File / short lines: whole Log File"
    assert (result.value_ns, result.low_ns, result.high_ns) == (1.5e9, 1.4e9, 1.6e9)


def test_sections_are_part_of_a_catch2_benchmark_name(tmp_path):
    side = write_side(tmp_path, "before", xml={
        "b": catch2_xml("b", [("case", 10, 9, 11)], section="with tabs"),
    })
    [result] = bc.read_side(side)
    assert result.key == "b / Indexing a Log File / with tabs / case"


def test_an_e2e_report_yields_the_median_and_its_runs_in_nanoseconds(tmp_path):
    side = write_side(tmp_path, "after", e2e=e2e_report({"grep_search_1mb_simple": [0.1, 0.2, 0.3]}))
    [result] = bc.read_side(side)
    assert result.suite == "e2e performance"
    assert result.key == "grep_search_1mb_simple"
    assert result.value_ns == pytest.approx(0.2e9)
    assert result.runs_ns == pytest.approx([0.1e9, 0.2e9, 0.3e9])


def test_an_unreadable_catch2_report_is_skipped_with_a_warning(tmp_path, capsys):
    # A benchmark binary that crashed leaves a truncated XML file behind.
    side = write_side(tmp_path, "before", xml={"crashed": "<Catch name="})
    assert bc.read_side(side) == []
    assert "crashed.xml" in capsys.readouterr().err


def test_non_overlapping_confidence_intervals_are_a_clear_change():
    before = bc.Result("Catch2", "x", 100.0, 95.0, 105.0)
    faster = bc.Result("Catch2", "x", 50.0, 45.0, 55.0)
    slower = bc.Result("Catch2", "x", 200.0, 190.0, 210.0)
    overlapping = bc.Result("Catch2", "x", 104.0, 99.0, 109.0)
    assert bc.compare(before, faster).verdict == "faster"
    assert bc.compare(before, faster).change_percent == pytest.approx(-50.0)
    assert bc.compare(before, slower).verdict == "slower"
    assert bc.compare(before, overlapping).verdict == "no clear change"


def test_e2e_runs_are_compared_with_welchs_t_statistic():
    before_runs = [1.00e9, 1.01e9, 0.99e9, 1.02e9, 0.98e9, 1.00e9]
    shifted = [r * 1.2 for r in before_runs]
    noisy = [0.5e9, 1.5e9, 1.0e9, 0.7e9, 1.3e9, 1.1e9]
    before = bc.Result("e2e performance", "x", 1.0e9, runs_ns=before_runs)
    assert bc.compare(before, bc.Result("e2e performance", "x", 1.2e9, runs_ns=shifted)).verdict == "slower"
    assert bc.compare(before, bc.Result("e2e performance", "x", 1.05e9, runs_ns=noisy)).verdict == "no clear change"


def test_a_benchmark_on_only_one_side_is_reported_as_such(tmp_path):
    before = write_side(tmp_path, "before", xml={"b": catch2_xml("b", [("old", 10, 9, 11)])})
    after = write_side(tmp_path, "after", xml={"b": catch2_xml("b", [("new", 10, 9, 11)])})
    comparisons = bc.compare_sides(bc.read_side(before), bc.read_side(after))
    assert [(c.key, c.verdict) for c in comparisons] == [
        ("b / Indexing a Log File / new", "only after"),
        ("b / Indexing a Log File / old", "only before"),
    ]


def test_the_markdown_has_one_row_per_benchmark_with_both_times_and_the_change(tmp_path):
    before = write_side(tmp_path, "before",
                        xml={"b": catch2_xml("b", [("case", 2.0e6, 1.9e6, 2.1e6)])},
                        e2e=e2e_report({"gui_startup_version": [0.1, 0.1, 0.1]}))
    after = write_side(tmp_path, "after",
                       xml={"b": catch2_xml("b", [("case", 1.0e6, 0.9e6, 1.1e6)])},
                       e2e=e2e_report({"gui_startup_version": [0.1, 0.1, 0.1]}))
    comparisons = bc.compare_sides(bc.read_side(before), bc.read_side(after))
    markdown = bc.to_markdown(comparisons, before_label="master (abc1234)", after_label="perf/x (def5678)")
    assert "| b / Indexing a Log File / case | 2.000 ms | 1.000 ms | -50.0% | faster |" in markdown
    assert "| gui_startup_version | 100.0 ms | 100.0 ms | +0.0% | no clear change |" in markdown
    assert "master (abc1234)" in markdown and "perf/x (def5678)" in markdown
    assert markdown.index("### Catch2") < markdown.index("### e2e performance")


def test_the_cli_writes_the_markdown_and_the_json(tmp_path):
    before = write_side(tmp_path, "before", xml={"b": catch2_xml("b", [("case", 900, 850, 950)])})
    after = write_side(tmp_path, "after", xml={"b": catch2_xml("b", [("case", 1000, 980, 1020)])})
    markdown_path = tmp_path / "out" / "summary.md"
    json_path = tmp_path / "out" / "comparison.json"
    assert bc.main([
        "--before", str(before), "--after", str(after),
        "--before-label", "master", "--after-label", "branch",
        "--markdown", str(markdown_path), "--json", str(json_path),
    ]) == 0
    assert "| b / Indexing a Log File / case | 900.0 ns | 1.000 µs | +11.1% | slower |" in markdown_path.read_text(encoding="utf-8")
    data = json.loads(json_path.read_text(encoding="utf-8"))
    assert data["before"] == {"label": "master"}
    assert data["after"] == {"label": "branch"}
    [entry] = data["benchmarks"]
    assert entry["key"] == "b / Indexing a Log File / case"
    assert entry["verdict"] == "slower"
    assert entry["before"]["value_ns"] == 900
    assert entry["after"]["high_ns"] == 1020


def test_times_are_shown_in_a_readable_unit():
    assert bc.format_ns(512) == "512.0 ns"
    assert bc.format_ns(1_234.0) == "1.234 µs"
    assert bc.format_ns(98_765_432) == "98.77 ms"
    assert bc.format_ns(2.5e9) == "2.500 s"
