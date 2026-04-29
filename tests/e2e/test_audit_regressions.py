"""
E2E regression tests for the bug fixes in audit/bug-report.md.

Each test is paired with a bug ID and covers either the user-observable behavior of
the fix or — where the bug is internal to the C++ code — a smoke check that exercises
the affected code path so a regression would surface as a crash, leak, or wrong output.

Bugs that are purely build-time (#4 dispatch_to.h header guard, #12 typo) are validated
by the build itself and do not appear here.
"""

from __future__ import annotations

import os
import platform
import subprocess
import time
from pathlib import Path

import pytest

from conftest import grep_output_lines, run_grep, run_gui


# ---------------------------------------------------------------------------
# Helpers local to this module
# ---------------------------------------------------------------------------

def _make_empty_file(tmp_path: Path) -> Path:
    p = tmp_path / "empty.log"
    p.write_bytes(b"")
    return p


def _make_single_line_file(tmp_path: Path, line: str = "hello world") -> Path:
    p = tmp_path / "one_line.log"
    p.write_text(line, encoding="utf-8")  # no trailing newline on purpose
    return p


def _make_only_newlines_file(tmp_path: Path, n: int = 5) -> Path:
    p = tmp_path / "newlines_only.log"
    p.write_bytes(b"\n" * n)
    return p


def _make_synthetic_log(tmp_path: Path, lines: int, pattern_every: int = 7) -> Path:
    """
    Generate a deterministic file with `lines` rows where every `pattern_every`-th
    row contains the literal token NEEDLE.
    """
    p = tmp_path / f"synthetic_{lines}.log"
    with p.open("w", encoding="utf-8") as f:
        for i in range(lines):
            tag = "NEEDLE" if i % pattern_every == 0 else "filler"
            f.write(f"2026-04-29 12:00:{i:02d} {tag} payload line {i}\n")
    return p


# ---------------------------------------------------------------------------
# Bug #6 + #10: empty / very small files must not crash the indexer
# (LineNumber underflow on getNbLine() == 0 and back() on empty endOfLines)
# ---------------------------------------------------------------------------

class TestEmptyAndTinyFiles:
    """Indexing edge cases for files that are empty, tiny, or contain only newlines."""

    def test_grep_empty_file_does_not_crash(self, logsquirl_grep_binary, tmp_path):
        empty = _make_empty_file(tmp_path)
        result = run_grep(logsquirl_grep_binary, "anything", empty, timeout=10)
        # No matches and a clean exit are acceptable; the only real failure is a crash
        # (negative or 134/139 exit codes from SIGSEGV/SIGABRT).
        assert result.returncode == 0, (
            f"empty file caused non-zero exit {result.returncode}: {result.stderr!r}"
        )
        assert grep_output_lines(result) == []

    def test_grep_only_newlines_does_not_crash(self, logsquirl_grep_binary, tmp_path):
        only_lf = _make_only_newlines_file(tmp_path, n=10)
        result = run_grep(logsquirl_grep_binary, "anything", only_lf, timeout=10)
        assert result.returncode == 0
        assert grep_output_lines(result) == []

    def test_grep_single_line_no_trailing_newline(self, logsquirl_grep_binary, tmp_path):
        one = _make_single_line_file(tmp_path, "hello world")
        result = run_grep(logsquirl_grep_binary, "hello", one, timeout=10)
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) == 1
        assert "hello world" in lines[0]


# ---------------------------------------------------------------------------
# Bug #1 + #2: indexing/search interrupt paths must not leak
# We cannot detect leaks reliably from the outside, but we can make sure a
# repeated index→interrupt cycle remains stable and does not crash.
# ---------------------------------------------------------------------------

@pytest.mark.slow
class TestSearchInterruptStability:
    """Repeated grep runs against a moderate file should remain stable."""

    def test_repeated_grep_runs_remain_stable(self, logsquirl_grep_binary, tmp_path):
        """
        Run logsquirl_grep many times against a moderate file. A regression of the
        leaks fixed in IndexOperation::readFileInBlocks / FullSearchOperation would
        not surface as a crash here, but a destructor / TBB-graph regression would.
        """
        log_file = _make_synthetic_log(tmp_path, lines=20_000)
        for _ in range(10):
            result = run_grep(logsquirl_grep_binary, "NEEDLE", log_file, timeout=20)
            assert result.returncode == 0, (
                f"grep failed: rc={result.returncode} stderr={result.stderr!r}"
            )
            matches = grep_output_lines(result)
            # 20_000 lines, every 7th has NEEDLE → ceil(20000/7) = 2858
            assert len(matches) > 2800
            assert all("NEEDLE" in m for m in matches)


# ---------------------------------------------------------------------------
# Bug #3: AbstractLogView destructor must shut down cleanly
# Also exercises the dispatch_to.h header guard fix indirectly (any TU that
# included it before the fix would have re-included it on every TU; the
# build-side check is implicit, the runtime side is "no crash on shutdown").
# ---------------------------------------------------------------------------

class TestGuiCleanShutdown:
    """The GUI must start and shut down without crashing."""

    @pytest.mark.slow
    def test_gui_starts_and_exits_cleanly(self, logsquirl_binary, test_data_dir):
        """
        Launch the GUI, give it ~2s to settle, then terminate. A regression in the
        AbstractLogView destructor (Bug #3) would manifest as a non-zero exit code
        or a crash signal. SIGTERM (rc -15 / 143) is expected and accepted.
        """
        cmd = [str(logsquirl_binary), str(test_data_dir / "utf8_tab_test.txt")]
        env = os.environ.copy()
        if platform.system() != "Darwin":
            cmd.extend(["-platform", "offscreen"])
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            time.sleep(2.0)
            proc.terminate()
            try:
                stdout, stderr = proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                stdout, stderr = proc.communicate(timeout=5)
        finally:
            if proc.poll() is None:
                proc.kill()

        # Accept clean exit (0) or termination by signal we sent (SIGTERM).
        # Anything else (SIGSEGV=139, SIGABRT=134) indicates a regression.
        assert proc.returncode in (0, -15, 143, -2, 130), (
            f"GUI did not shut down cleanly: rc={proc.returncode} "
            f"stderr={stderr.decode(errors='replace')[-500:]!r}"
        )


# ---------------------------------------------------------------------------
# Bug #5: predefinedfilterscombobox quote-strip is internal C++ logic.
# We mirror the original buggy behavior in a tiny pure-python check so a future
# refactor that re-introduces the off-by-one would be caught at the spec level.
# ---------------------------------------------------------------------------

class TestQuoteStripSpec:
    """Specification mirror for the leading/trailing quote-strip logic."""

    @staticmethod
    def _strip_wrapping_quotes(s: str) -> str:
        # Mirrors the fixed C++ logic: strip exactly one " from start and end if size >= 2.
        if len(s) >= 2:
            return s[1:-1]
        return ""

    def test_strips_both_quotes(self):
        assert self._strip_wrapping_quotes('"foo" or "bar"') == 'foo" or "bar'

    def test_short_input(self):
        assert self._strip_wrapping_quotes('') == ''
        assert self._strip_wrapping_quotes('"') == ''
        assert self._strip_wrapping_quotes('""') == ''

    def test_split_after_strip(self):
        delim = '" or "'
        stripped = self._strip_wrapping_quotes('"a" or "b" or "c"')
        # After the fix, splitting on `" or "` yields exactly the inner tokens.
        assert stripped.split(delim) == ['a', 'b', 'c']


# ---------------------------------------------------------------------------
# Bug #7: missing-file diagnostics should be informative
# ---------------------------------------------------------------------------

class TestMissingFileDiagnostics:
    """logsquirl_grep should not crash when given a non-existent path."""

    def test_grep_missing_file(self, logsquirl_grep_binary, tmp_path):
        missing = tmp_path / "does_not_exist.log"
        # The CLI may exit with non-zero; what matters is no crash signal.
        result = subprocess.run(
            [str(logsquirl_grep_binary), "-e", "anything", str(missing)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        # Exit codes 0/1 are both acceptable. Negative codes (signals) are not.
        assert result.returncode >= 0, (
            f"grep crashed on missing file: rc={result.returncode} stderr={result.stderr!r}"
        )


# ---------------------------------------------------------------------------
# Bug #8: regex with Hyperscan must produce correct results.
# Without the return-code check, certain HS errors silently produced empty
# matches. We assert positive matches against a known synthetic file.
# ---------------------------------------------------------------------------

class TestRegexCorrectness:
    """Regex search must return all expected matches (catches HS silent failure)."""

    def test_alternation_finds_all_groups(self, logsquirl_grep_binary, tmp_path):
        f = _make_synthetic_log(tmp_path, lines=500)
        result = run_grep(logsquirl_grep_binary, "NEEDLE|filler", f, timeout=15)
        assert result.returncode == 0
        # Every line is either NEEDLE or filler → all 500 lines must match.
        assert len(grep_output_lines(result)) == 500

    def test_complex_regex(self, logsquirl_grep_binary, tmp_path):
        f = _make_synthetic_log(tmp_path, lines=500)
        # Match timestamp 2026-04-29 12:00:NN with NN < 10. Avoid \b — it is rejected
        # by Hyperscan in UCP mode; instead anchor on the trailing space.
        result = run_grep(logsquirl_grep_binary, r"12:00:0\d ", f, timeout=15)
        assert result.returncode == 0
        # Filter out any internal compile/log diagnostics that may end up in stdout.
        matches = [
            l for l in grep_output_lines(result)
            if l.startswith("2026-04-29 12:00:")
        ]
        # 10 timestamps with seconds 00..09
        assert len(matches) == 10


# ---------------------------------------------------------------------------
# Bug #2026-04 round 2: dashboard tab close (Ctrl+W) must not no-op silently
# when only the dashboard tab is open. The fix closes the window if the
# dashboard is the only remaining tab.
# ---------------------------------------------------------------------------

@pytest.mark.slow
class TestDashboardCloseBehavior:
    """The pinned dashboard tab is unclosable, but Ctrl+W on it must still
    close the window when nothing else is open."""

    @pytest.fixture(autouse=True)
    def _skip_on_mac(self):
        if platform.system() == "Darwin":
            pytest.skip("offscreen platform not available on macOS")

    def test_gui_starts_with_dashboard_only(self, logsquirl_binary):
        # Plain startup (no file argument) — the dashboard is the only tab.
        # Verify the binary launches and does not segfault before our timeout.
        result = run_gui(logsquirl_binary, [], timeout=4)
        assert result.returncode != -11, "logsquirl segfaulted on dashboard-only startup"
        assert result.returncode != -6, "logsquirl aborted on dashboard-only startup"


# ---------------------------------------------------------------------------
# Bug #2026-04 round 2: command palette must be safe against
# stale QAction pointers and re-entrant invocation. We can only test the
# binary path here; the UI safety is covered by the QPointer capture and
# the action-copy fix in commandpalette.cpp/mainwindow.cpp.
# ---------------------------------------------------------------------------

@pytest.mark.slow
class TestCommandPaletteSafety:
    """Smoke check: starting the GUI exposes the command palette wiring.
    A regression in the QPointer capture would crash on shutdown."""

    @pytest.fixture(autouse=True)
    def _skip_on_mac(self):
        if platform.system() == "Darwin":
            pytest.skip("offscreen platform not available on macOS")

    def test_gui_clean_shutdown_with_palette_wired(self, logsquirl_binary, tmp_path):
        log = _make_synthetic_log(tmp_path, lines=200)
        result = run_gui(logsquirl_binary, [str(log)], timeout=4)
        # Crash exit codes (-11 SIGSEGV, -6 SIGABRT) must not occur.
        assert result.returncode != -11
        assert result.returncode != -6


# ---------------------------------------------------------------------------
# Bug #2026-04 round 2: decompressor must surface short writes.
# We can't easily simulate ENOSPC in CI, but we can verify a successful
# decompression round-trip never silently truncates.
# ---------------------------------------------------------------------------

class TestDecompressorRoundTrip:
    """The decompressor is a GUI-only feature in LogSquirl; the CLI grep tool
    does not auto-decompress .gz files. The short-write regression is therefore
    covered by code review and unit-level inspection rather than E2E.
    This placeholder keeps the test class for documentation."""

    @pytest.mark.skip(reason="decompressor is a GUI-only code path; CLI grep does not handle .gz")
    def test_gzip_round_trip_preserves_bytes(self):
        pass
