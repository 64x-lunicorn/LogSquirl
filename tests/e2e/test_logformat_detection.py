"""
E2E tests for log format auto-detection feature.

Verifies that:
- logsquirl_grep still searches structured log files correctly
- the GUI opens structured log files without crashing
- format detection does not interfere with existing search behavior
"""

import subprocess
import time

import pytest

from conftest import grep_output_lines, run_grep, run_gui


class TestGrepOnStructuredLogs:
    """Test that grep searches still work on structured log files."""

    def test_grep_finds_pattern_in_syslog(self, logsquirl_grep_binary, test_data_dir):
        """Searching for 'sshd' in a syslog file should find matching lines."""
        result = run_grep(
            logsquirl_grep_binary, "sshd", test_data_dir / "syslog_sample.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) == 3
        assert all("sshd" in l for l in lines)

    def test_grep_finds_error_in_syslog(self, logsquirl_grep_binary, test_data_dir):
        """Searching for 'error' in syslog should find the error line."""
        result = run_grep(
            logsquirl_grep_binary, "error", test_data_dir / "syslog_sample.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) >= 1
        assert any("Could not load host key" in l for l in lines)

    def test_grep_regex_on_syslog(self, logsquirl_grep_binary, test_data_dir):
        """Regex patterns should work on structured log files."""
        # Match lines with PID in brackets like [12345]
        result = run_grep(
            logsquirl_grep_binary, r"\[\d+\]", test_data_dir / "syslog_sample.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        # Most lines have a PID in brackets; kernel line has [UFW BLOCK] instead
        assert len(lines) == 9

    def test_grep_no_match_in_syslog(self, logsquirl_grep_binary, test_data_dir):
        """Searching for a non-existent pattern in syslog should return no lines."""
        result = run_grep(
            logsquirl_grep_binary,
            "NONEXISTENT_PATTERN_XYZ",
            test_data_dir / "syslog_sample.txt",
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) == 0


class TestGrepOnPlainText:
    """Test that plain text files (no format match) still work."""

    def test_grep_on_unstructured_file(self, logsquirl_grep_binary, test_data_dir):
        """Grep should work normally on files with no detectable log format."""
        result = run_grep(
            logsquirl_grep_binary, "random", test_data_dir / "plain_text_sample.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) == 1
        assert "random" in lines[0]


@pytest.mark.slow
class TestGuiLogFormatDetection:
    """GUI smoke tests for log format detection."""

    def test_gui_opens_syslog_no_crash(self, logsquirl_binary, test_data_dir):
        """Opening a syslog file should not crash the GUI."""
        test_file = test_data_dir / "syslog_sample.txt"
        try:
            proc = subprocess.Popen(
                [str(logsquirl_binary), "-n", str(test_file)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            time.sleep(3)
            proc.terminate()
            proc.wait(timeout=5)
            # Should not have segfaulted (-11) or aborted (-6)
            assert proc.returncode not in (-11, -6, 139, 134)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    def test_gui_opens_plain_text_no_crash(self, logsquirl_binary, test_data_dir):
        """Opening a plain text file (no format) should not crash the GUI."""
        test_file = test_data_dir / "plain_text_sample.txt"
        try:
            proc = subprocess.Popen(
                [str(logsquirl_binary), "-n", str(test_file)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            time.sleep(3)
            proc.terminate()
            proc.wait(timeout=5)
            assert proc.returncode not in (-11, -6, 139, 134)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
