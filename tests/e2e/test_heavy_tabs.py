"""
E2E tests for files with extremely heavy tab usage.

Validates that LogSquirl handles log files containing lines with
thousands or tens of thousands of tab characters without hanging
or crashing — the scenario from tmp/stderr.log (ASAN crash dump
with ~37 million tabs on one line).

The test file test_data/heavy_tabs_crash.log is a representative
smaller version with 50,000 tabs on a single line.
"""

import subprocess
import time
from pathlib import Path

import pytest

from conftest import grep_output_lines, measure_execution, run_grep, run_gui


class TestGrepHeavyTabs:
    """Verify logsquirl_grep handles files with massive tab counts."""

    def test_grep_heavy_tabs_completes(self, logsquirl_grep_binary, test_data_dir):
        """Grep on a file with 50k tabs must complete within the timeout (no hang)."""
        filepath = test_data_dir / "heavy_tabs_crash.log"
        result = run_grep(logsquirl_grep_binary, "asan", filepath, timeout=10)
        assert result.returncode == 0

    def test_grep_heavy_tabs_finds_match(self, logsquirl_grep_binary, test_data_dir):
        """Grep must find the line containing 'asan' even though it has 50k tabs."""
        filepath = test_data_dir / "heavy_tabs_crash.log"
        result = run_grep(logsquirl_grep_binary, "asan", filepath, timeout=10)
        lines = grep_output_lines(result)
        assert any("asan" in l for l in lines), f"Expected 'asan' in output, got: {lines}"

    def test_grep_heavy_tabs_matches_normal_lines(self, logsquirl_grep_binary, test_data_dir):
        """Grep for a pattern on normal (non-tabbed) lines still works."""
        filepath = test_data_dir / "heavy_tabs_crash.log"
        result = run_grep(logsquirl_grep_binary, "AvpCore", filepath, timeout=10)
        lines = grep_output_lines(result)
        assert any("AvpCore" in l for l in lines)

    def test_grep_heavy_tabs_dot_pattern(self, logsquirl_grep_binary, test_data_dir):
        """'.' pattern (match all) must not hang on the heavy tabs file."""
        filepath = test_data_dir / "heavy_tabs_crash.log"
        result = run_grep(logsquirl_grep_binary, ".", filepath, timeout=10)
        assert result.returncode == 0
        lines = grep_output_lines(result)
        # File has 6 lines
        assert len(lines) == 6


class TestGrepHeavyTabsPerformance:
    """Verify grep on heavy-tab files completes quickly (no quadratic blowup)."""

    def test_grep_heavy_tabs_under_2_seconds(self, logsquirl_grep_binary, test_data_dir):
        """Grep for '.' on heavy_tabs_crash.log must complete in under 2 seconds."""
        filepath = test_data_dir / "heavy_tabs_crash.log"

        def search():
            run_grep(logsquirl_grep_binary, ".", filepath, timeout=5)

        result = measure_execution(search, warmup=1, runs=3)
        assert result["median_seconds"] < 2.0, (
            f"Grep on heavy tabs file took {result['median_seconds']:.3f}s "
            f"(expected < 2.0s — possible quadratic tab expansion regression)"
        )


class TestGuiHeavyTabs:
    """Verify the GUI does not hang or crash when opening heavy-tab files."""

    def test_gui_open_heavy_tabs_no_crash(self, logsquirl_binary, test_data_dir):
        """Opening the heavy tabs file in the GUI must not crash or hang."""
        test_file = test_data_dir / "heavy_tabs_crash.log"
        try:
            proc = subprocess.Popen(
                [str(logsquirl_binary), "-n", str(test_file)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            # Give it time to index and try to render — old code would hang here
            time.sleep(5)
            proc.terminate()
            proc.wait(timeout=5)
            # Should not have segfaulted (-11) or aborted (-6)
            assert proc.returncode not in (-11, -6, 139, 134), (
                f"GUI crashed with exit code {proc.returncode} on heavy tabs file"
            )
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            pytest.fail("GUI hung when opening heavy_tabs_crash.log (did not respond to SIGTERM)")


class TestGrepSyntheticHeavyTabs:
    """Test with dynamically generated files of varying tab density."""

    def test_grep_100k_tabs_single_line(self, logsquirl_grep_binary, tmp_path):
        """A file with 100,000 tabs on one line must not hang grep."""
        test_file = tmp_path / "100k_tabs.log"
        test_file.write_text("normal line\n" + "\t" * 100_000 + "marker\n" + "end\n")

        result = run_grep(logsquirl_grep_binary, "marker", test_file, timeout=10)
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert any("marker" in l for l in lines)

    def test_grep_many_lines_with_tabs(self, logsquirl_grep_binary, tmp_path):
        """A file with 1000 lines each having 100 tabs must complete quickly."""
        test_file = tmp_path / "many_tab_lines.log"
        content = ""
        for i in range(1000):
            content += f"line{i}" + "\t" * 100 + f"end{i}\n"
        test_file.write_text(content)

        result = run_grep(logsquirl_grep_binary, "end500", test_file, timeout=10)
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert any("end500" in l for l in lines)
