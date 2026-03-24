"""
E2E smoke tests for the LogSquirl GUI application.

Tests that the GUI binary starts, handles basic CLI flags,
and doesn't crash when opening files. All tests run headless
using -platform offscreen on Linux/Windows (macOS uses native).
"""

import subprocess
import signal
import time

import pytest

from conftest import run_gui


class TestGuiVersion:
    """Test --version flag."""

    def test_gui_version_exits_zero(self, logsquirl_binary):
        """--version should print version info and exit 0."""
        result = run_gui(logsquirl_binary, ["--version"])
        assert result.returncode == 0
        assert "logsquirl" in result.stdout.lower()

    def test_gui_version_contains_version_number(self, logsquirl_binary):
        """Version output should contain a version-like string (digits and dots)."""
        result = run_gui(logsquirl_binary, ["--version"])
        output = result.stdout
        # Version format is like "logsquirl 26.3.0" or "logsquirl YY.MM.PATCH"
        assert any(c.isdigit() for c in output), f"No version number in: {output}"


class TestGuiHelp:
    """Test --help flag."""

    def test_gui_help_exits_zero(self, logsquirl_binary):
        """--help should print usage and exit 0."""
        result = run_gui(logsquirl_binary, ["--help"])
        assert result.returncode == 0


class TestGuiStartup:
    """Test that the GUI can start and be terminated cleanly."""

    def test_gui_new_session_no_crash(self, logsquirl_binary):
        """Starting with -n (new session) should not crash immediately."""
        try:
            proc = subprocess.Popen(
                [str(logsquirl_binary), "-n"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            # Give it a moment to start
            time.sleep(2)
            proc.terminate()
            proc.wait(timeout=5)
            # Should not have segfaulted (-11) or aborted (-6)
            assert proc.returncode not in (-11, -6, 139, 134)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    def test_gui_open_file_no_crash(self, logsquirl_binary, test_data_dir):
        """Opening a file from the CLI should not crash."""
        test_file = test_data_dir / "utf8_tab_test.txt"
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

    def test_gui_open_multiple_files_no_crash(self, logsquirl_binary, test_data_dir):
        """Opening multiple files should not crash."""
        files = [
            str(test_data_dir / "utf8_tab_test.txt"),
            str(test_data_dir / "ansi_colors_example.txt"),
        ]
        try:
            proc = subprocess.Popen(
                [str(logsquirl_binary), "-n"] + files,
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

    def test_gui_open_large_file_no_crash(self, logsquirl_binary, test_data_dir):
        """Opening the 1.5MB file should not crash."""
        test_file = test_data_dir / "random_block_1.5Mb.txt"
        try:
            proc = subprocess.Popen(
                [str(logsquirl_binary), "-n", str(test_file)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            time.sleep(4)
            proc.terminate()
            proc.wait(timeout=5)
            assert proc.returncode not in (-11, -6, 139, 134)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
