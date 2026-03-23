"""
E2E tests for logsquirl_grep core search functionality.

Tests pattern matching correctness, regex support, and output format
using the utf8_tab_test.txt file which contains structured log lines.
"""

import pytest

from conftest import grep_output_lines, run_grep


class TestGrepSimplePattern:
    """Test simple string pattern matching."""

    def test_grep_finds_known_pattern(self, logsquirl_grep_binary, test_data_dir):
        """Search for 'Solver' should find lines containing that word."""
        result = run_grep(logsquirl_grep_binary, "Solver", test_data_dir / "utf8_tab_test.txt")
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) > 0
        assert all("Solver" in l for l in lines)

    def test_grep_finds_exact_count(self, logsquirl_grep_binary, test_data_dir):
        """'Solver' appears in exactly 6 lines in utf8_tab_test.txt."""
        result = run_grep(logsquirl_grep_binary, "Solver", test_data_dir / "utf8_tab_test.txt")
        lines = grep_output_lines(result)
        assert len(lines) == 6


class TestGrepRegexPattern:
    """Test regex pattern matching."""

    def test_grep_regex_alternation(self, logsquirl_grep_binary, test_data_dir):
        """Regex alternation 'INFO|WARNING' should match lines with either word."""
        result = run_grep(
            logsquirl_grep_binary, "INFO|WARNING", test_data_dir / "utf8_tab_test.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) > 0
        assert all("INFO" in l or "WARNING" in l for l in lines)

    def test_grep_regex_with_quantifiers(self, logsquirl_grep_binary, test_data_dir):
        """Regex with quantifiers should work (e.g., matching PID numbers)."""
        # Match lines with bracketed numbers like [19653]
        result = run_grep(
            logsquirl_grep_binary, r"\[\d+\]", test_data_dir / "utf8_tab_test.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) > 0


class TestGrepNoMatch:
    """Test behavior when pattern finds no matches."""

    def test_grep_no_match_exits_cleanly(self, logsquirl_grep_binary, test_data_dir):
        """Searching for a non-existent pattern should exit 0 with no match lines."""
        result = run_grep(
            logsquirl_grep_binary,
            "ZZZZZ_NONEXISTENT_PATTERN_12345",
            test_data_dir / "utf8_tab_test.txt",
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) == 0


class TestGrepOutputContent:
    """Test that output contains expected content."""

    def test_grep_output_contains_full_lines(self, logsquirl_grep_binary, test_data_dir):
        """Matched lines should be complete (contain timestamp and message)."""
        result = run_grep(
            logsquirl_grep_binary, "Connecting", test_data_dir / "utf8_tab_test.txt"
        )
        lines = grep_output_lines(result)
        assert len(lines) > 0
        # Lines should contain full log entries with timestamps
        for line in lines:
            assert "2017-08-07" in line
            assert "Connecting" in line

    def test_grep_special_characters_in_pattern(self, logsquirl_grep_binary, test_data_dir):
        """Pattern with special regex chars (escaped) should match literally."""
        # Search for [main@115] which contains regex special chars
        result = run_grep(
            logsquirl_grep_binary, r"main@115", test_data_dir / "utf8_tab_test.txt"
        )
        assert result.returncode == 0
        lines = grep_output_lines(result)
        assert len(lines) > 0
        assert all("main@115" in l for l in lines)

    def test_grep_exit_code_always_zero(self, logsquirl_grep_binary, test_data_dir):
        """logsquirl_grep always exits with code 0, even with no matches."""
        result = run_grep(
            logsquirl_grep_binary, "WILL_NOT_MATCH", test_data_dir / "utf8_tab_test.txt"
        )
        assert result.returncode == 0
