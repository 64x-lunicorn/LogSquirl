"""
E2E tests for logsquirl_grep encoding support.

Tests that the grep tool correctly handles files in different
character encodings: UTF-8, UTF-16LE, UTF-16BE, and files
with ANSI color escape sequences.
"""

import pytest

from conftest import grep_output_lines, run_grep, run_grep_bytes

# Three Log Lines with non-ASCII text, two of which a search for "^hit"
# matches.  Written in the encoding each test needs.
NON_ASCII_LINES = [
    "hit: Grüße aus München, Ärger mit Übergängen",
    "miss: nothing to see",
    "hit: Öl bei 42 °C, Größe 5 µm",
]
NON_ASCII_TEXT = "".join(f"{line}\n" for line in NON_ASCII_LINES)
NON_ASCII_MATCHES = "".join(
    f"{line}\n" for line in NON_ASCII_LINES if line.startswith("hit")
)


def write_log(tmp_path, name: str, encoding: str):
    """Write the non-ASCII log lines in the given encoding and return the path."""
    path = tmp_path / name
    path.write_bytes(NON_ASCII_TEXT.encode(encoding))
    return path


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


class TestGrepDetectedEncoding:
    """Test that log lines are read in the encoding they are detected as."""

    def test_utf8_matches_are_byte_identical(
        self, logsquirl_grep_binary, tmp_path
    ):
        """Matches from a UTF-8 file are printed byte for byte as they are in it."""
        log_file = write_log(tmp_path, "utf8.log", "utf-8")
        result = run_grep_bytes(logsquirl_grep_binary, "^hit", log_file)
        assert result.returncode == 0
        assert result.stdout == NON_ASCII_MATCHES.encode("utf-8")

    def test_utf8_non_ascii_pattern_matches(self, logsquirl_grep_binary, tmp_path):
        """A pattern with non-ASCII text matches the log lines it appears in."""
        log_file = write_log(tmp_path, "utf8.log", "utf-8")
        result = run_grep_bytes(logsquirl_grep_binary, "München", log_file)
        assert result.returncode == 0
        assert result.stdout == f"{NON_ASCII_LINES[0]}\n".encode("utf-8")

    def test_utf16le_prints_as_utf8(self, logsquirl_grep_binary, tmp_path):
        """A UTF-16LE file with a byte order mark prints as UTF-8 text."""
        log_file = write_log(tmp_path, "utf16le.log", "utf-16")
        result = run_grep_bytes(logsquirl_grep_binary, "^hit", log_file)
        assert result.returncode == 0
        assert result.stdout == NON_ASCII_MATCHES.encode("utf-8")

    def test_utf16be_prints_as_utf8(self, logsquirl_grep_binary, tmp_path):
        """A UTF-16BE file with a byte order mark prints as UTF-8 text."""
        log_file = tmp_path / "utf16be.log"
        log_file.write_bytes(b"\xfe\xff" + NON_ASCII_TEXT.encode("utf-16-be"))
        result = run_grep_bytes(logsquirl_grep_binary, "^hit", log_file)
        assert result.returncode == 0
        assert result.stdout == NON_ASCII_MATCHES.encode("utf-8")

    def test_latin1_prints_as_utf8(self, logsquirl_grep_binary, tmp_path):
        """A Latin-1 file prints as UTF-8 text."""
        # Only ä ö ü ß Ä Ö Ü, which every Latin encoding the detection may
        # land on decodes the same way.
        lines = [
            "hit: Größe und München, Ärger mit Türen",
            "miss: nothing to see",
            "hit: schöne Grüße, äußere Wärme, Öl und Übung",
        ]
        text = "".join(f"{line}\n" for line in lines)
        log_file = tmp_path / "latin1.log"
        log_file.write_bytes(text.encode("latin-1"))
        expected = "".join(f"{line}\n" for line in lines if line.startswith("hit"))

        result = run_grep_bytes(logsquirl_grep_binary, "^hit", log_file)
        assert result.returncode == 0
        assert result.stdout == expected.encode("utf-8")


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
