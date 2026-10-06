"""Tests for benchmark-pr-comment.py (#674): turning the Benchmarks workflow's
comparison of a pull request into the one pull request comment, from an
artifact that pull request code wrote and that is therefore untrusted."""

from __future__ import annotations

import importlib.util
import io
import json
import sys
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location(
    "benchmark_pr_comment", Path(__file__).with_name("benchmark-pr-comment.py"))
pc = importlib.util.module_from_spec(_SPEC)
sys.modules["benchmark_pr_comment"] = pc
_SPEC.loader.exec_module(pc)

RUN_URL = "https://github.com/64x-lunicorn/LogSquirl/actions/runs/123"


def benchmark(key: str, before: float | None, after: float | None,
              change: float | None, verdict: str, suite: str = "Catch2") -> dict:
    return {
        "suite": suite,
        "key": key,
        "statistic": "mean" if suite == "Catch2" else "median",
        "before": None if before is None else {"value_ns": before},
        "after": None if after is None else {"value_ns": after},
        "change_percent": change,
        "verdict": verdict,
    }


def comparison(*benchmarks: dict, before: str = "merge base with master (0123456789ab)",
               after: str = "pull request #42 merged into master (ba9876543210)") -> dict:
    return {"before": {"label": before}, "after": {"label": after}, "benchmarks": list(benchmarks)}


def write(tmp_path: Path, data) -> Path:
    path = tmp_path / "comparison.json"
    path.write_text(data if isinstance(data, str) else json.dumps(data), encoding="utf-8")
    return path


# ---------------------------------------------------------------------------
# The pull request number
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("text", ["42", "42\n", " 7 ", "1234567"])
def test_a_pull_request_number_is_read_as_plain_digits(tmp_path, text):
    path = tmp_path / "number"
    path.write_text(text, encoding="utf-8")
    assert pc.read_pull_request_number(path) == int(text.strip())


@pytest.mark.parametrize("text", ["", "0", "-1", "042", "4 2", "42; rm -rf /", "$(id)",
                                  "1e3", "12345678901", "４２", "42\n43"])
def test_anything_but_a_pull_request_number_is_refused(tmp_path, text):
    path = tmp_path / "number"
    path.write_text(text, encoding="utf-8")
    with pytest.raises(ValueError):
        pc.read_pull_request_number(path)


def test_a_missing_number_file_is_refused(tmp_path):
    with pytest.raises(ValueError):
        pc.read_pull_request_number(tmp_path / "missing")


def test_the_number_command_prints_only_a_valid_number(tmp_path, capsys):
    path = tmp_path / "number"
    path.write_text("42\n", encoding="utf-8")
    assert pc.main(["number", str(path)]) == 0
    assert capsys.readouterr().out == "42\n"

    path.write_text("42 && echo pwned", encoding="utf-8")
    assert pc.main(["number", str(path)]) == 1
    assert capsys.readouterr().out == ""


# ---------------------------------------------------------------------------
# The comment
# ---------------------------------------------------------------------------

def test_the_comment_starts_with_the_hidden_marker_that_finds_it_again(tmp_path):
    body = pc.render(pc.load_comparison(write(tmp_path, comparison())), RUN_URL, "success")
    assert body.startswith(pc.MARKER + "\n")
    # Its own marker, not the one of the instruction count comment (#671).
    assert pc.MARKER == "<!-- logsquirl-benchmarks-wall-clock -->"


def test_the_marker_command_prints_the_marker_the_workflow_looks_for(capsys):
    assert pc.main(["marker"]) == 0
    assert capsys.readouterr().out == pc.MARKER + "\n"


def bot_comments(*comments: tuple[int, str]) -> list[str]:
    """The bot's comments as the workflow's `gh api --jq` lists them: one JSON
    object per line."""
    return [json.dumps({"id": id_, "body": body}) for id_, body in comments]


def test_the_earlier_comment_is_found_behind_other_bot_comments():
    # #764: the instruction count comment came first, and broke the lookup.
    lines = bot_comments((1, "<!-- logsquirl-instruction-counts -->\n### Instruction counts"),
                         (2, pc.MARKER + "\n### Benchmarks\n\nbefore | after"))
    assert pc.find_comment_id(lines) == 2


def test_without_an_earlier_comment_none_is_found():
    lines = bot_comments((1, "<!-- logsquirl-instruction-counts -->\n### Instruction counts"))
    assert pc.find_comment_id(lines) is None
    assert pc.find_comment_id([]) is None


def test_the_first_of_several_earlier_comments_is_the_one_updated():
    lines = bot_comments((5, pc.MARKER + "\nolder"), (9, pc.MARKER + "\nnewer"))
    assert pc.find_comment_id(lines) == 5


def test_a_comment_only_quoting_the_marker_is_not_taken_for_it():
    lines = bot_comments((3, "See " + pc.MARKER))
    assert pc.find_comment_id(lines) is None


@pytest.mark.parametrize("line", ["not json", '["an", "array"]', '{"id": "7", "body": ""}',
                                  '{"id": 7}'])
def test_a_line_that_is_no_comment_is_refused(line):
    with pytest.raises(ValueError):
        pc.find_comment_id([line])


def test_the_comment_id_command_prints_the_id_or_nothing(monkeypatch, capsys):
    monkeypatch.setattr(sys, "stdin", io.StringIO(
        "\n".join(bot_comments((1, "other"), (2, pc.MARKER + "\nbody"))) + "\n"))
    assert pc.main(["comment-id"]) == 0
    assert capsys.readouterr().out == "2\n"

    monkeypatch.setattr(sys, "stdin", io.StringIO(""))
    assert pc.main(["comment-id"]) == 0
    assert capsys.readouterr().out == ""


def test_the_comment_id_command_fails_on_input_that_is_no_comment(monkeypatch, capsys):
    monkeypatch.setattr(sys, "stdin", io.StringIO("[1, \"x\"]\n"))
    assert pc.main(["comment-id"]) == 1
    assert capsys.readouterr().err.startswith("error: ")


def test_the_comment_has_one_row_per_benchmark_with_both_times_and_the_change(tmp_path):
    data = comparison(
        benchmark("logsquirl_logdata_benchmark / index", 2_000_000, 1_500_000, -25.0, "faster"),
        benchmark("logsquirl_quickfind_benchmark / next", 1000, 1010, 1.0, "no clear change"),
        benchmark("open_1gb", 3e9, 3.6e9, 20.0, "slower", suite="e2e performance"),
    )
    body = pc.render(pc.load_comparison(write(tmp_path, data)), RUN_URL, "success")
    assert "| logsquirl\\_logdata\\_benchmark / index | 2.000 ms | 1.500 ms | -25.0% | faster |" in body
    assert "| logsquirl\\_quickfind\\_benchmark / next | 1.000 µs | 1.010 µs | +1.0% | no clear change |" in body
    assert "| open\\_1gb | 3.000 s | 3.600 s | +20.0% | slower |" in body
    assert "<summary>Catch2: every benchmark (2)</summary>" in body
    assert "<summary>e2e performance: every benchmark (1)</summary>" in body
    assert "1 faster, 1 slower, 1 without a clear change" in body
    assert f"({RUN_URL})" in body
    assert "merge base with master (0123456789ab)" in body
    assert "not a gate" in body


def test_clear_changes_are_listed_before_the_full_tables(tmp_path):
    data = comparison(
        benchmark("a", 1000, 1000, 0.0, "no clear change"),
        benchmark("b", 1000, 2000, 100.0, "slower"),
    )
    body = pc.render(pc.load_comparison(write(tmp_path, data)), RUN_URL, "success")
    clear = body.index("### Clear changes")
    assert clear < body.index("<details>")
    assert body.index("| b |") < body.index("<details>")


def test_a_benchmark_on_one_side_only_shows_a_dash_for_the_other(tmp_path):
    data = comparison(benchmark("new one", None, 5000, None, "only after"))
    body = pc.render(pc.load_comparison(write(tmp_path, data)), RUN_URL, "success")
    assert "| new one | – | 5.000 µs | – | only after |" in body


def test_text_from_the_artifact_cannot_break_the_table_mention_anyone_or_inject_html(tmp_path):
    data = comparison(
        benchmark("a|b <img src=x onerror=alert(1)> @octocat [x](http://evil) `c`\nnext",
                  1000, 1000, 0.0, "no clear change"),
        before="<script>alert(1)</script> @everyone",
        after="**bold** | pipe",
    )
    body = pc.render(pc.load_comparison(write(tmp_path, data)), RUN_URL, "success")
    assert "<img" not in body and "<script" not in body
    assert "@octocat" not in body and "@everyone" not in body
    assert "[x](http://evil)" not in body
    assert "a\\|b" in body
    assert "**bold**" not in body
    for line in body.splitlines():
        if line.startswith("| a"):
            # Still exactly one row of five cells: every pipe but the four
            # separators is escaped.
            assert line.replace("\\|", "").count("|") == 6


def test_a_verdict_other_than_the_known_ones_is_not_shown_as_given(tmp_path):
    data = comparison(benchmark("a", 1000, 900, -10.0, "<b>fastest ever</b>"))
    body = pc.render(pc.load_comparison(write(tmp_path, data)), RUN_URL, "success")
    assert "fastest ever" not in body
    assert "| a | 1.000 µs | 900.0 ns | -10.0% | ? |" in body


@pytest.mark.parametrize("bad", [
    "not json",
    "[]",
    json.dumps({"benchmarks": "no list"}),
    json.dumps({"benchmarks": [{"suite": "Catch2", "key": "a", "before": {"value_ns": "1e3"},
                                "after": None, "change_percent": None, "verdict": "only before"}]}),
    json.dumps({"benchmarks": [{"suite": "Catch2", "key": "a", "before": {"value_ns": 1e308 * 10},
                                "after": None, "change_percent": None, "verdict": "only before"}]}),
    json.dumps({"benchmarks": [{"suite": "Catch2", "key": 5, "before": None,
                                "after": None, "change_percent": None, "verdict": "only before"}]}),
])
def test_a_comparison_that_is_not_what_the_workflow_writes_is_refused(tmp_path, bad):
    with pytest.raises(ValueError):
        pc.load_comparison(write(tmp_path, bad))


def test_a_huge_comparison_is_cut_to_fit_a_comment(tmp_path):
    data = comparison(*[benchmark(f"benchmark {i} " + "x" * 150, 1000, 2000, 100.0, "slower")
                        for i in range(2000)])
    body = pc.render(pc.load_comparison(write(tmp_path, data)), RUN_URL, "success")
    assert len(body) <= pc.MAX_COMMENT_CHARS
    assert "rows left out" in body
    assert f"({RUN_URL})" in body


def test_without_a_comparison_the_comment_says_the_run_did_not_finish(tmp_path):
    body = pc.render(None, RUN_URL, "failure")
    assert body.startswith(pc.MARKER + "\n")
    assert "did not produce a comparison" in body
    assert "failure" in body
    assert f"({RUN_URL})" in body


def test_a_failed_run_with_a_comparison_says_some_benchmarks_are_missing(tmp_path):
    data = comparison(benchmark("a", 1000, 1000, 0.0, "no clear change"))
    body = pc.render(pc.load_comparison(write(tmp_path, data)), RUN_URL, "failure")
    assert "| a |" in body
    assert "did not finish cleanly" in body


def test_the_run_url_must_be_one_of_this_repositorys_runs():
    with pytest.raises(ValueError):
        pc.render(None, "https://evil.example/actions/runs/1", "failure")
    with pytest.raises(ValueError):
        pc.render(None, RUN_URL + ")[x](http://evil", "failure")


def test_the_body_command_writes_the_comment(tmp_path):
    source = write(tmp_path, comparison(benchmark("a", 1000, 1000, 0.0, "no clear change")))
    out = tmp_path / "body.md"
    assert pc.main(["body", "--comparison", str(source), "--run-url", RUN_URL,
                    "--conclusion", "success", "--out", str(out)]) == 0
    assert out.read_text(encoding="utf-8").startswith(pc.MARKER)


def test_the_body_command_reports_an_unusable_comparison_as_missing(tmp_path):
    source = write(tmp_path, "garbage")
    out = tmp_path / "body.md"
    assert pc.main(["body", "--comparison", str(source), "--run-url", RUN_URL,
                    "--conclusion", "success", "--out", str(out)]) == 0
    text = out.read_text(encoding="utf-8")
    assert "did not produce a comparison" in text
    assert "could not be read" in text


def test_the_body_command_without_a_comparison_file(tmp_path):
    out = tmp_path / "body.md"
    assert pc.main(["body", "--comparison", str(tmp_path / "missing.json"), "--run-url", RUN_URL,
                    "--conclusion", "failure", "--out", str(out)]) == 0
    assert "did not produce a comparison" in out.read_text(encoding="utf-8")
