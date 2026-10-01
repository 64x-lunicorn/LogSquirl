"""Tests for instruction-counts.py (#671): reading Callgrind dumps of the
benchmarks' fixed-work mode, comparing two sides, and the pull request comment
built from an artifact that is not trusted."""

from __future__ import annotations

import importlib.util
import json
import re
import sys
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "instruction_counts", Path(__file__).with_name("instruction-counts.py"))
ic = importlib.util.module_from_spec(_SPEC)
sys.modules["instruction_counts"] = ic
_SPEC.loader.exec_module(ic)

BEFORE_SHA = "a" * 40
AFTER_SHA = "b" * 40
HEAD_SHA = "c" * 40


def dump(trigger: str, totals: int | None, events: str = "Ir") -> str:
    """A Callgrind dump as `callgrind --dump-instr=no` writes one."""
    text = f"""# callgrind format
version: 1
creator: callgrind-3.22.0
pid: 4242
cmd:  /usr/local/build_root/output/logsquirl_decoration_benchmark --order decl

part: 3


desc: I1 cache:
desc: Timerange: Basic block 100 - 200
desc: Trigger: {trigger}

positions: line
events: {events}
summary: 999999999


ob=(1) /usr/local/build_root/output/logsquirl_decoration_benchmark
fl=(1) linedecorator.cpp
fn=(1) LineDecorator::decorate
12 {totals}
"""
    if totals is not None:
        text += f"\ntotals: {totals}\n"
    return text


def write_binary(root: Path, binary: str, dumps: list[str], exit_code: int | None = 0) -> None:
    directory = root / binary
    directory.mkdir(parents=True)
    for index, text in enumerate(dumps, start=1):
        (directory / f"callgrind.out.4242.{index}").write_text(text, encoding="utf-8")
    # The launcher of a binary that relaunches itself, and the final dump.
    (directory / "callgrind.out.4241").write_text(dump("Program termination", 0), encoding="utf-8")
    if exit_code is not None:
        (directory / "exit_code").write_text(f"{exit_code}\n", encoding="utf-8")


def side(benchmarks: dict[tuple[str, str], int], failed: list[str] | None = None) -> dict:
    return {
        "schema_version": 1,
        "benchmarks": [{"binary": b, "name": n, "instructions": i} for (b, n), i in benchmarks.items()],
        "failed_binaries": failed or [],
    }


def comparison(before: dict, after: dict) -> dict:
    return ic.compare(before, after, before_sha=BEFORE_SHA, after_sha=AFTER_SHA,
                      head_sha=HEAD_SHA, pull_request=17)


# ---------------------------------------------------------------------------
# Reading the dumps
# ---------------------------------------------------------------------------

def test_a_client_request_dump_yields_its_label_and_the_totals():
    assert ic.parse_dump(dump("Client Request: decoration path benchmarks / tab-heavy line", 123456)) == (
        "decoration path benchmarks / tab-heavy line", 123456)


def test_the_instruction_count_is_the_ir_column_of_the_totals():
    text = dump("Client Request: x / y", None, events="Ir Dr Dw") + "\ntotals: 700 20 30\n"
    assert ic.parse_dump(text) == ("x / y", 700)


def test_a_dump_that_no_benchmark_asked_for_has_no_label():
    assert ic.parse_dump(dump("Program termination", 55)) == (None, 55)


def test_a_dump_without_totals_is_an_error():
    with pytest.raises(ValueError, match="totals"):
        ic.parse_dump(dump("Client Request: x / y", None))


def test_collect_reads_every_binary_and_ignores_dumps_without_a_label(tmp_path):
    write_binary(tmp_path, "logsquirl_decoration_benchmark", [
        dump("Client Request: decoration path benchmarks / common no-match line", 1000),
        dump("Client Request: decoration path benchmarks / very long line", 2000),
    ])
    write_binary(tmp_path, "logsquirl_regex_matcher_benchmark", [
        dump("Client Request: Highlighting Log Lines / three Highlighters", 3000),
    ])
    result = ic.collect(tmp_path)
    assert result["schema_version"] == 1
    assert result["failed_binaries"] == []
    assert result["benchmarks"] == [
        {"binary": "logsquirl_decoration_benchmark",
         "name": "decoration path benchmarks / common no-match line", "instructions": 1000},
        {"binary": "logsquirl_decoration_benchmark",
         "name": "decoration path benchmarks / very long line", "instructions": 2000},
        {"binary": "logsquirl_regex_matcher_benchmark",
         "name": "Highlighting Log Lines / three Highlighters", "instructions": 3000},
    ]


def test_dumps_of_one_benchmark_are_added_up(tmp_path):
    # A BENCHMARK_ADVANCED that calls meter.measure twice, or a benchmark in a
    # test case Catch2 enters once per SECTION, dumps under the same label.
    write_binary(tmp_path, "b", [dump("Client Request: case / x", 10), dump("Client Request: case / x", 5)])
    assert ic.collect(tmp_path)["benchmarks"] == [{"binary": "b", "name": "case / x", "instructions": 15}]


def test_a_binary_that_exited_with_an_error_is_failed_and_its_counts_are_left_out(tmp_path):
    write_binary(tmp_path, "crashed", [dump("Client Request: case / x", 10)], exit_code=139)
    write_binary(tmp_path, "fine", [dump("Client Request: case / y", 20)])
    result = ic.collect(tmp_path)
    assert result["failed_binaries"] == ["crashed"]
    assert [b["binary"] for b in result["benchmarks"]] == ["fine"]


def test_a_binary_without_an_exit_code_did_not_run_to_the_end(tmp_path):
    write_binary(tmp_path, "killed", [dump("Client Request: case / x", 10)], exit_code=None)
    assert ic.collect(tmp_path)["failed_binaries"] == ["killed"]


# ---------------------------------------------------------------------------
# Comparing
# ---------------------------------------------------------------------------

def test_compare_gives_before_after_and_the_change_in_percent_per_benchmark():
    result = comparison(side({("b", "case / x"): 1000, ("b", "case / y"): 400}),
                        side({("b", "case / x"): 1100, ("b", "case / y"): 400}))
    assert result["schema_version"] == 1
    assert result["pull_request"] == 17
    assert result["head_sha"] == HEAD_SHA
    assert result["before"] == {"sha": BEFORE_SHA, "failed_binaries": []}
    assert result["after"] == {"sha": AFTER_SHA, "failed_binaries": []}
    assert result["benchmarks"] == [
        {"binary": "b", "name": "case / x",
         "metrics": {"instructions": {"before": 1000, "after": 1100, "change_percent": 10.0}}},
        {"binary": "b", "name": "case / y",
         "metrics": {"instructions": {"before": 400, "after": 400, "change_percent": 0.0}}},
    ]


def test_a_benchmark_on_one_side_only_has_no_change():
    result = comparison(side({("b", "case / old"): 10}), side({("b", "case / new"): 20}))
    assert [(b["name"], b["metrics"]["instructions"]) for b in result["benchmarks"]] == [
        ("case / new", {"before": None, "after": 20, "change_percent": None}),
        ("case / old", {"before": 10, "after": None, "change_percent": None}),
    ]


def test_failed_binaries_are_kept_per_side():
    result = comparison(side({}, failed=["x"]), side({}, failed=["y"]))
    assert result["before"]["failed_binaries"] == ["x"]
    assert result["after"]["failed_binaries"] == ["y"]


# ---------------------------------------------------------------------------
# The report
# ---------------------------------------------------------------------------

def test_the_report_has_a_row_per_benchmark_with_both_counts_and_the_change():
    text = ic.render_markdown(comparison(side({("logsquirl_decoration_benchmark", "case / x"): 1234567}),
                                         side({("logsquirl_decoration_benchmark", "case / x"): 1246913})))
    assert "| Benchmark | Before | After | Change |" in text
    assert "| decoration: case / x | 1,234,567 | 1,246,913 | +1.00 % |" in text


def test_only_changes_beyond_the_noise_are_listed_before_the_whole_table():
    text = ic.render_markdown(comparison(
        side({("b", "case / same"): 100_000, ("b", "case / more"): 100_000, ("b", "case / less"): 100_000}),
        side({("b", "case / same"): 100_100, ("b", "case / more"): 103_000, ("b", "case / less"): 90_000})))
    changed, _, everything = text.partition("<details>")
    assert "case / more" in changed and "case / less" in changed
    assert "case / same" not in changed
    assert "3 benchmarks, 2 changed by more than 0.5 %" in changed
    assert all(name in everything for name in ("case / same", "case / more", "case / less"))


def test_without_a_change_beyond_the_noise_the_report_says_so():
    text = ic.render_markdown(comparison(side({("b", "case / x"): 1000}), side({("b", "case / x"): 1001})))
    assert "1 benchmark, none changed by more than 0.5 %" in text


def test_new_and_missing_benchmarks_count_as_changed():
    text = ic.render_markdown(comparison(side({("b", "case / old"): 10}), side({("b", "case / new"): 20})))
    changed = text.partition("<details>")[0]
    assert "| b: case / new |  | 20 | new |" in changed
    assert "| b: case / old | 10 |  | missing |" in changed


def test_failed_binaries_are_named_in_the_report():
    text = ic.render_markdown(comparison(side({}), side({}, failed=["logsquirl_logdata_benchmark"])))
    assert "logsquirl\\_logdata\\_benchmark" in text
    assert "after" in text


def test_the_comment_carries_the_marker_that_finds_it_again():
    text = ic.render_markdown(comparison(side({}), side({})), comment=True)
    assert text.startswith(ic.MARKER)


def test_names_cannot_break_the_table_or_mention_anyone():
    text = ic.render_markdown(comparison(
        side({("b", "a | b <script> @someone `x` [l](http://x)"): 1}),
        side({("b", "a | b <script> @someone `x` [l](http://x)"): 1})))
    assert "<script>" not in text
    assert "@someone" not in text
    assert "a \\| b" in text
    assert re.search(r"(?<!\\)`", text.replace("`tests/benchmarks`", "")) is None
    assert "[l](" not in text


# ---------------------------------------------------------------------------
# The artifact as the comment workflow reads it
# ---------------------------------------------------------------------------

def valid() -> dict:
    return json.loads(json.dumps(comparison(side({("b", "case / x"): 1000}), side({("b", "case / x"): 1100}))))


def test_a_valid_comparison_passes_validation():
    assert ic.validate(valid()) == valid()


@pytest.mark.parametrize("breakage", [
    lambda c: c.update(schema_version=2),
    lambda c: c.update(pull_request="17"),
    lambda c: c.update(pull_request=True),
    lambda c: c.update(pull_request=0),
    lambda c: c.update(head_sha="c" * 39),
    lambda c: c["before"].update(sha="$(id)"),
    lambda c: c["after"].update(failed_binaries=["a b"]),
    lambda c: c["benchmarks"][0].update(binary="../x"),
    lambda c: c["benchmarks"][0].update(name=7),
    lambda c: c["benchmarks"][0].update(name="x" * 1000),
    lambda c: c["benchmarks"][0]["metrics"]["instructions"].update(before=-1),
    lambda c: c["benchmarks"][0]["metrics"]["instructions"].update(after=1.5),
    lambda c: c.update(benchmarks=[c["benchmarks"][0]] * 5000),
    lambda c: c.pop("benchmarks"),
])
def test_an_artifact_that_does_not_match_the_schema_is_rejected(breakage):
    data = valid()
    breakage(data)
    with pytest.raises(ValueError):
        ic.validate(data)


def test_the_change_shown_is_computed_from_the_counts_not_read_from_the_artifact():
    data = valid()
    data["benchmarks"][0]["metrics"]["instructions"]["change_percent"] = -99.0
    assert "+10.00 %" in ic.render_markdown(ic.validate(data))


# ---------------------------------------------------------------------------
# Posting the comment
# ---------------------------------------------------------------------------

class FakeGitHub:
    """Stands in for `gh api`: answers the pull request and its comments and records the writes."""

    def __init__(self, head_sha: str = HEAD_SHA, comments: list[dict] | None = None):
        self.head_sha = head_sha
        self.comments = comments or []
        self.writes: list[tuple[str, str, dict]] = []

    def __call__(self, method: str, path: str, body: dict | None = None):
        if method == "GET" and path == "repos/o/r/pulls/17":
            return {"number": 17, "head": {"sha": self.head_sha}}
        if method == "GET" and path == "repos/o/r/issues/17/comments":
            return self.comments
        self.writes.append((method, path, body))
        return {}


def test_the_first_report_is_posted_as_a_new_comment():
    github = FakeGitHub()
    assert ic.post_comment(ic.validate(valid()), repo="o/r", head_sha=HEAD_SHA, api=github) == "created"
    [(method, path, body)] = github.writes
    assert (method, path) == ("POST", "repos/o/r/issues/17/comments")
    assert body["body"].startswith(ic.MARKER)


def test_a_later_report_updates_the_bots_comment():
    github = FakeGitHub(comments=[
        {"id": 5, "user": {"login": "someone"}, "body": ic.MARKER + " quoted by a person"},
        {"id": 9, "user": {"login": "github-actions[bot]"}, "body": ic.MARKER + "\nold"},
    ])
    assert ic.post_comment(ic.validate(valid()), repo="o/r", head_sha=HEAD_SHA, api=github) == "updated"
    [(method, path, _)] = github.writes
    assert (method, path) == ("PATCH", "repos/o/r/issues/comments/9")


def test_an_artifact_naming_another_pull_request_is_not_posted():
    # The pull request number comes from the artifact; its head must be the
    # commit the measuring run was started for.
    github = FakeGitHub(head_sha="d" * 40)
    assert ic.post_comment(ic.validate(valid()), repo="o/r", head_sha=HEAD_SHA, api=github) == "skipped"
    assert github.writes == []


# ---------------------------------------------------------------------------
# The gate (#672)
# ---------------------------------------------------------------------------

def gate(before: dict, after: dict, labels: list[str] | None = None, thresholds: dict | None = None) -> dict:
    return ic.evaluate_gate(comparison(before, after), labels=labels or [],
                            thresholds={} if thresholds is None else thresholds)


def test_the_default_threshold_is_two_percent():
    assert ic.DEFAULT_THRESHOLD_PERCENT == 2.0
    assert ic.threshold_percent("b", "no such benchmark", {}) == 2.0


def test_a_benchmark_listed_as_varying_has_its_own_threshold():
    assert ic.threshold_percent("b", "noisy", {("b", "noisy"): 5.0}) == 5.0


def test_the_listed_thresholds_are_above_the_default_and_name_benchmark_binaries():
    assert ic.THRESHOLD_PERCENT
    for (binary, name), threshold in ic.THRESHOLD_PERCENT.items():
        assert threshold > ic.DEFAULT_THRESHOLD_PERCENT
        assert ic.BINARY.fullmatch(binary) and binary.endswith("_benchmark")
        assert " / " in name


def test_the_gate_uses_the_listed_thresholds_by_default():
    (binary, name), threshold = next(iter(ic.THRESHOLD_PERCENT.items()))
    data = comparison(side({(binary, name): 100_000}), side({(binary, name): 100_000 + int(threshold * 1000)}))
    assert ic.evaluate_gate(data, labels=[])["passed"] is True


def test_a_cost_within_the_threshold_passes():
    result = gate(side({("b", "case / x"): 100_000}), side({("b", "case / x"): 102_000}))
    assert result["passed"] is True
    assert result["over_threshold"] == [] and result["missing"] == [] and result["failed_binaries"] == []


def test_a_cost_beyond_the_threshold_fails():
    result = gate(side({("b", "case / x"): 100_000, ("b", "case / y"): 100}),
                  side({("b", "case / x"): 102_001, ("b", "case / y"): 100}))
    assert result["passed"] is False
    assert result["accepted"] is False
    assert result["over_threshold"] == [{"binary": "b", "name": "case / x", "threshold_percent": 2.0}]


def test_fewer_instructions_never_fail():
    assert gate(side({("b", "case / x"): 100_000}), side({("b", "case / x"): 50_000}))["passed"] is True


def test_a_benchmark_with_its_own_threshold_fails_only_beyond_it():
    thresholds = {("b", "noisy"): 5.0}
    assert gate(side({("b", "noisy"): 1000}), side({("b", "noisy"): 1040}), thresholds=thresholds)["passed"]
    result = gate(side({("b", "noisy"): 1000}), side({("b", "noisy"): 1051}), thresholds=thresholds)
    assert result["over_threshold"] == [{"binary": "b", "name": "noisy", "threshold_percent": 5.0}]


def test_the_label_accepts_a_cost_beyond_the_threshold():
    result = gate(side({("b", "case / x"): 1000}), side({("b", "case / x"): 1500}),
                  labels=["performance", "perf-accepted"])
    assert result["passed"] is True
    assert result["accepted"] is True
    assert [b["name"] for b in result["over_threshold"]] == ["case / x"]


def test_the_label_alone_is_not_an_acceptance_when_nothing_is_over():
    result = gate(side({("b", "case / x"): 1000}), side({("b", "case / x"): 1000}), labels=["perf-accepted"])
    assert result["passed"] is True
    assert result["accepted"] is False


def test_a_benchmark_missing_on_the_after_side_fails_even_with_the_label():
    result = gate(side({("b", "case / x"): 10, ("b", "case / gone"): 10}), side({("b", "case / x"): 10}),
                  labels=["perf-accepted"])
    assert result["passed"] is False
    assert result["missing"] == [{"binary": "b", "name": "case / gone"}]


def test_a_binary_that_failed_on_the_after_side_fails_even_with_the_label():
    result = gate(side({}), side({("b", "case / x"): 10}, failed=["logsquirl_logdata_benchmark"]),
                  labels=["perf-accepted"])
    assert result["passed"] is False
    assert result["failed_binaries"] == ["logsquirl_logdata_benchmark"]


def test_a_new_benchmark_passes():
    assert gate(side({}), side({("b", "case / new"): 10}))["passed"] is True


def test_a_binary_that_failed_on_the_before_side_does_not_fail_the_pull_request():
    # The after side's benchmark sources may not build on the base.
    assert gate(side({}, failed=["b"]), side({("b", "case / new"): 10}))["passed"] is True


def test_nothing_counted_at_all_fails():
    assert gate(side({}), side({}))["passed"] is False


def test_a_benchmark_counted_at_zero_instructions_before_cannot_be_compared_and_passes():
    assert gate(side({("b", "case / x"): 0}), side({("b", "case / x"): 10}))["passed"] is True


def test_the_gate_names_the_pull_request_and_commit_it_judged():
    result = gate(side({("b", "case / x"): 1}), side({("b", "case / x"): 1}))
    assert result["schema_version"] == 1
    assert result["pull_request"] == 17
    assert result["head_sha"] == HEAD_SHA


def test_thresholds_of_benchmarks_that_no_longer_exist_are_reported():
    data = comparison(side({("b", "case / x"): 1}), side({("b", "case / x"): 1}))
    assert ic.unused_thresholds(data, {("b", "case / x"): 3.0, ("b", "renamed"): 3.0}) == [("b", "renamed")]


# ---------------------------------------------------------------------------
# The gate in the report
# ---------------------------------------------------------------------------

def gated_report(before: dict, after: dict, labels: list[str] | None = None) -> str:
    data = comparison(before, after)
    return ic.render_markdown(data, gate=ic.evaluate_gate(data, labels=labels or [], thresholds={}))


def test_a_passing_gate_says_so_with_the_threshold():
    text = gated_report(side({("b", "case / x"): 1000}), side({("b", "case / x"): 1001}))
    assert "**Gate: passed.**" in text
    assert "+2.0 %" in text


def test_a_failing_gate_lists_what_costs_too_much_and_how_to_accept_it():
    text = gated_report(side({("b", "case / x"): 1000, ("b", "case / y"): 1000}),
                        side({("b", "case / x"): 1100, ("b", "case / y"): 1000}))
    gate_section = text.partition("**Gate: failed.**")[2].partition("**2 benchmarks")[0]
    assert "| Benchmark | Before | After | Change | Threshold |" in gate_section
    assert "| b: case / x | 1,000 | 1,100 | +10.00 % | +2.0 % |" in gate_section
    assert "`perf-accepted`" in gate_section
    assert "case / y" not in gate_section


def test_an_accepted_gate_lists_the_accepted_benchmarks():
    text = gated_report(side({("b", "case / x"): 1000}), side({("b", "case / x"): 1100}),
                        labels=["perf-accepted"])
    gate_section = text.partition("**Gate: accepted with `perf-accepted`.**")[2].partition("**1 benchmark")[0]
    assert "| b: case / x | 1,000 | 1,100 | +10.00 % | +2.0 % |" in gate_section


def test_a_missing_benchmark_is_named_as_failing_the_gate():
    text = gated_report(side({("b", "case / gone"): 10}), side({("b", "case / x"): 10}),
                        labels=["perf-accepted"])
    gate_section = text.partition("**Gate: failed.**")[2].partition("**2 benchmarks")[0]
    assert "b: case / gone" in gate_section
    assert "does not accept" in gate_section


def test_without_a_gate_the_report_has_no_gate_section():
    assert "Gate" not in ic.render_markdown(comparison(side({("b", "x / y"): 1}), side({("b", "x / y"): 1})))


# ---------------------------------------------------------------------------
# The gate as the comment workflow reads it
# ---------------------------------------------------------------------------

def valid_gate(labels: list[str] | None = None) -> dict:
    return json.loads(json.dumps(ic.evaluate_gate(valid(), labels=labels or [], thresholds={})))


def test_a_valid_gate_passes_validation():
    assert ic.validate_gate(valid_gate(["perf-accepted"]), ic.validate(valid())) == valid_gate(["perf-accepted"])


@pytest.mark.parametrize("breakage", [
    lambda g: g.update(schema_version=2),
    lambda g: g.update(passed="yes"),
    lambda g: g.update(accepted=1),
    lambda g: g.update(pull_request=18),
    lambda g: g.update(head_sha="d" * 40),
    lambda g: g.update(over_threshold=[{"binary": "b", "name": "not compared", "threshold_percent": 2.0}]),
    lambda g: g.update(over_threshold=[{"binary": "b", "name": "case / x", "threshold_percent": "2"}]),
    lambda g: g.update(over_threshold=[{"binary": "b", "name": "case / x", "threshold_percent": 1e9}]),
    lambda g: g.update(missing=[{"binary": "b"}]),
    lambda g: g.update(failed_binaries=["a b"]),
    lambda g: g.pop("missing"),
    lambda g: g.update(over_threshold=[g["over_threshold"][0]] * 5000),
])
def test_a_gate_that_does_not_match_the_schema_or_the_comparison_is_rejected(breakage):
    data = valid_gate()
    breakage(data)
    with pytest.raises(ValueError):
        ic.validate_gate(data, ic.validate(valid()))


def test_the_comment_carries_the_gate():
    github = FakeGitHub()
    data = ic.validate(valid())
    ic.post_comment(data, repo="o/r", head_sha=HEAD_SHA, api=github,
                    gate=ic.validate_gate(valid_gate(["perf-accepted"]), data))
    [(_, _, body)] = github.writes
    assert "**Gate: accepted with `perf-accepted`.**" in body["body"]


# ---------------------------------------------------------------------------
# The gate on the command line
# ---------------------------------------------------------------------------

def write_comparison(path: Path, before: dict, after: dict) -> Path:
    path.write_text(json.dumps(comparison(before, after)), encoding="utf-8")
    return path


def run_gate(tmp_path: Path, before: dict, after: dict, labels: list[str]) -> tuple[int, dict, str]:
    write_comparison(tmp_path / "comparison.json", before, after)
    (tmp_path / "labels.json").write_text(json.dumps(labels), encoding="utf-8")
    code = ic.main(["gate", "--comparison", str(tmp_path / "comparison.json"),
                    "--labels", str(tmp_path / "labels.json"),
                    "--json", str(tmp_path / "gate.json"), "--markdown", str(tmp_path / "gate.md")])
    return (code, json.loads((tmp_path / "gate.json").read_text(encoding="utf-8")),
            (tmp_path / "gate.md").read_text(encoding="utf-8"))


def test_the_gate_command_fails_on_a_cost_beyond_the_threshold(tmp_path):
    code, result, markdown = run_gate(tmp_path, side({("b", "case / x"): 1000}),
                                      side({("b", "case / x"): 1100}), [])
    assert code == 1
    assert result["passed"] is False
    assert "**Gate: failed.**" in markdown


def test_the_gate_command_passes_with_the_label(tmp_path):
    code, result, _ = run_gate(tmp_path, side({("b", "case / x"): 1000}),
                               side({("b", "case / x"): 1100}), ["perf-accepted"])
    assert code == 0
    assert result["accepted"] is True


def test_the_gate_command_fails_without_a_comparison(tmp_path):
    (tmp_path / "labels.json").write_text("[]", encoding="utf-8")
    assert ic.main(["gate", "--comparison", str(tmp_path / "none.json"),
                    "--labels", str(tmp_path / "labels.json")]) == 1


def test_the_gate_command_rejects_labels_that_are_not_a_list_of_names(tmp_path):
    write_comparison(tmp_path / "comparison.json", side({("b", "x / y"): 1}), side({("b", "x / y"): 1}))
    (tmp_path / "labels.json").write_text('{"perf-accepted": true}', encoding="utf-8")
    assert ic.main(["gate", "--comparison", str(tmp_path / "comparison.json"),
                    "--labels", str(tmp_path / "labels.json")]) == 1


def test_the_comment_command_reads_the_gate_when_given(tmp_path, monkeypatch):
    write_comparison(tmp_path / "comparison.json", side({("b", "case / x"): 1000}),
                     side({("b", "case / x"): 1100}))
    data = ic.validate(json.loads((tmp_path / "comparison.json").read_text(encoding="utf-8")))
    (tmp_path / "gate.json").write_text(json.dumps(ic.evaluate_gate(data, labels=[], thresholds={})),
                                        encoding="utf-8")
    github = FakeGitHub()
    monkeypatch.setattr(ic, "gh_api", github)
    assert ic.main(["comment", "--comparison", str(tmp_path / "comparison.json"),
                    "--gate", str(tmp_path / "gate.json"), "--repo", "o/r", "--head-sha", HEAD_SHA]) == 0
    [(_, _, body)] = github.writes
    assert "**Gate: failed.**" in body["body"]


def test_a_gate_file_that_is_not_usable_leaves_the_gate_out_of_the_comment(tmp_path, monkeypatch):
    write_comparison(tmp_path / "comparison.json", side({("b", "case / x"): 1000}),
                     side({("b", "case / x"): 1100}))
    (tmp_path / "gate.json").write_text('{"passed": true}', encoding="utf-8")
    github = FakeGitHub()
    monkeypatch.setattr(ic, "gh_api", github)
    assert ic.main(["comment", "--comparison", str(tmp_path / "comparison.json"),
                    "--gate", str(tmp_path / "gate.json"), "--repo", "o/r", "--head-sha", HEAD_SHA]) == 0
    [(_, _, body)] = github.writes
    assert "Gate" not in body["body"]
