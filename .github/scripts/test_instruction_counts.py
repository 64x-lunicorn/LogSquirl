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
