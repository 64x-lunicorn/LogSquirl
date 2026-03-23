"""
E2E tests for logsquirl_grep edge cases and robustness.

Tests boundary conditions: empty files, tiny files, large files,
missing files, files without trailing newlines, and binary-safe handling.
"""

import subprocess
import tempfile
from pathlib import Path

import pytest

from conftest import grep_output_lines, run_grep


class TestGrepEmptyFile:
    """Test behavior with empty or minimal files."""

    def test_grep_empty_file(self, logsquirl_grep_binary, tmp_path):
        """Searching in an empty file should not crash and return no matches."""
        empty_file = tmp_path / "empty.txt"
        empty_file.write_text("")
        result = run_grep(logsquirl_grep_binary, "test", empty_file)
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) == 0

    def test_grep_small_file(self, logsquirl_grep_binary, test_data_dir):
        """100-byte file should be handled correctly."""
        result = run_grep(
            logsquirl_grep_binary, ".", test_data_dir / "random_block_100byte.txt"
        )
        assert result.returncode == 0


class TestGrepNoTrailingNewline:
    """Test files without trailing newlines."""

    def test_grep_no_lf_512k(self, logsquirl_grep_binary, test_data_dir):
        """512KB file without trailing newline should complete without crash."""
        result = run_grep(
            logsquirl_grep_binary, ".", test_data_dir / "random_block_512k_no_lf.txt"
        )
        assert result.returncode == 0

    def test_grep_no_lf_1mb(self, logsquirl_grep_binary, test_data_dir):
        """1MB file without trailing newline should complete without crash."""
        result = run_grep(
            logsquirl_grep_binary, ".", test_data_dir / "random_block_1Mb_no_lf.txt"
        )
        assert result.returncode == 0


class TestGrepLargeFiles:
    """Test with larger files."""

    def test_grep_512k_file(self, logsquirl_grep_binary, test_data_dir):
        """512KB file should be processed without issues."""
        result = run_grep(
            logsquirl_grep_binary, ".", test_data_dir / "random_block_512k.txt"
        )
        assert result.returncode == 0

    def test_grep_1mb_file(self, logsquirl_grep_binary, test_data_dir):
        """1MB file should be processed without issues."""
        result = run_grep(
            logsquirl_grep_binary, ".", test_data_dir / "random_block_1Mb.txt"
        )
        assert result.returncode == 0

    def test_grep_1_5mb_file(self, logsquirl_grep_binary, test_data_dir):
        """1.5MB file (largest test file) should be processed without issues."""
        result = run_grep(
            logsquirl_grep_binary, ".", test_data_dir / "random_block_1.5Mb.txt"
        )
        assert result.returncode == 0


class TestGrepTabCharacters:
    """Test files with tab characters."""

    def test_grep_utf8_with_tabs(self, logsquirl_grep_binary, test_data_dir):
        """UTF-8 file with tab characters should be searchable."""
        result = run_grep(
            logsquirl_grep_binary, "INFO", test_data_dir / "utf8_tab_test.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) > 0


class TestGrepErrorHandling:
    """Test error handling for invalid inputs."""

    def test_grep_nonexistent_file(self, logsquirl_grep_binary, tmp_path):
        """Searching a non-existent file should not hang (may crash or return error)."""
        fake_file = tmp_path / "does_not_exist.txt"
        try:
            result = subprocess.run(
                [str(logsquirl_grep_binary), "-e", "test", str(fake_file)],
                capture_output=True,
                text=True,
                timeout=10,
            )
            # We don't assert specific exit code — just that it didn't hang
        except subprocess.TimeoutExpired:
            pytest.fail("logsquirl_grep hung on non-existent file")

    def test_grep_no_pattern_flag(self, logsquirl_grep_binary, test_data_dir):
        """Running without -e pattern should not hang (may show usage or exit)."""
        try:
            result = subprocess.run(
                [str(logsquirl_grep_binary), str(test_data_dir / "utf8_tab_test.txt")],
                capture_output=True,
                text=True,
                timeout=10,
            )
            # Just verify it terminates — specific behavior depends on CLI parser
        except subprocess.TimeoutExpired:
            pytest.fail("logsquirl_grep hung when called without -e pattern")
