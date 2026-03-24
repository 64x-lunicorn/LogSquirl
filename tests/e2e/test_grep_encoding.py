"""
E2E tests for logsquirl_grep encoding support.

Tests that the grep tool correctly handles files in different
character encodings: UTF-8, UTF-16LE, UTF-16BE, and files
with ANSI color escape sequences.
"""

import pytest

from conftest import grep_output_lines, run_grep


class TestGrepUtf8:
    """Test search on UTF-8 encoded files."""

    def test_grep_utf8_finds_pattern(self, logsquirl_grep_binary, test_data_dir):
        """Search in UTF-8 file should find expected matches."""
        result = run_grep(
            logsquirl_grep_binary, "Solver", test_data_dir / "utf8_tab_test.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) == 6

    def test_grep_utf8_tab_handling(self, logsquirl_grep_binary, test_data_dir):
        """Tab characters in UTF-8 file should not break search."""
        result = run_grep(
            logsquirl_grep_binary, "INFO", test_data_dir / "utf8_tab_test.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) > 0


class TestGrepUtf16:
    """Test search on UTF-16 encoded files."""

    def test_grep_utf16le_loads_without_crash(self, logsquirl_grep_binary, test_data_dir):
        """UTF-16LE file should be loaded and searched without crashing."""
        result = run_grep(
            logsquirl_grep_binary, "Solver", test_data_dir / "utf16_tab_test.txt"
        )
        assert result.returncode == 0

    def test_grep_utf16be_loads_without_crash(self, logsquirl_grep_binary, test_data_dir):
        """UTF-16BE file should be loaded and searched without crashing."""
        result = run_grep(
            logsquirl_grep_binary, "Solver", test_data_dir / "utf16be_tab_test.txt"
        )
        assert result.returncode == 0

    def test_grep_large_utf16le_no_crash(self, logsquirl_grep_binary, test_data_dir):
        """1MB UTF-16LE file should complete without crashing."""
        result = run_grep(
            logsquirl_grep_binary, "test", test_data_dir / "random_block_1Mb_utf16le.txt"
        )
        assert result.returncode == 0

    def test_grep_large_utf16be_no_crash(self, logsquirl_grep_binary, test_data_dir):
        """1MB UTF-16BE file should complete without crashing."""
        result = run_grep(
            logsquirl_grep_binary, "test", test_data_dir / "random_block_1Mb_utf16be.txt"
        )
        assert result.returncode == 0


class TestGrepChineseText:
    """Test search on Chinese text files."""

    def test_grep_chinese_utf16_no_crash(self, logsquirl_grep_binary, test_data_dir):
        """Chinese UTF-16 text should be searchable without crash."""
        result = run_grep(
            logsquirl_grep_binary, ".", test_data_dir / "Chinese-Lipsum.utf16.txt"
        )
        assert result.returncode == 0

    def test_grep_chinese_utf16be_no_crash(self, logsquirl_grep_binary, test_data_dir):
        """Chinese UTF-16BE text should be searchable without crash."""
        result = run_grep(
            logsquirl_grep_binary, ".", test_data_dir / "Chinese-Lipsum.utf16be.txt"
        )
        assert result.returncode == 0


class TestGrepAnsiColors:
    """Test search on files with ANSI escape sequences."""

    def test_grep_ansi_finds_pattern(self, logsquirl_grep_binary, test_data_dir):
        """Search in ANSI color file should find text content."""
        result = run_grep(
            logsquirl_grep_binary, "Test", test_data_dir / "ansi_colors_example.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) > 0
        assert all("Test" in l for l in lines)
