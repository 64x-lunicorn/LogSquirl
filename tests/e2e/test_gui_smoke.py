"""
E2E smoke tests for the LogSquirl GUI application.

Tests that the GUI binary starts, handles basic CLI flags,
and doesn't crash when opening files.

Every test starts the application through the isolated_gui fixture, so it has
its own settings, Session, cache, Log Formats and plugin directories and runs
offscreen (#328). Nothing here can read or change the developer's own
LogSquirl data.
"""

import pytest

_CRASH_CODES = (-11, -6, 139, 134)


class TestGuiVersion:
    """Test --version flag."""

    def test_gui_version_exits_zero(self, isolated_gui):
        """--version should print version info and exit 0."""
        result = isolated_gui.run("--version")
        assert result.returncode == 0
        assert "logsquirl" in result.stdout.lower()

    # The version the binary reports is empty in the Linux CI build: the
    # target that writes generated/version.h is in ALL, while CI builds
    # --target ci_build, so nothing orders it before the sources that include
    # the header. Mac happens to win that race and Linux does not, which is
    # why this is not strict. Tracked as its own ticket; surfaced only once
    # the E2E suite actually ran in CI (#366).
    @pytest.mark.xfail(
        reason="the compiled-in version can be empty: generate_version is not ordered before the build",
        strict=False,
    )
    def test_gui_version_contains_version_number(self, isolated_gui):
        """Version output should contain a version-like string (digits and dots)."""
        result = isolated_gui.run("--version")
        output = result.stdout
        # Version format is like "logsquirl 26.3.0" or "logsquirl YY.MM.PATCH"
        assert any(c.isdigit() for c in output), f"No version number in: {output}"


class TestGuiHelp:
    """Test --help flag."""

    def test_gui_help_exits_zero(self, isolated_gui):
        """--help should print usage and exit 0."""
        result = isolated_gui.run("--help")
        assert result.returncode == 0


class TestGuiStartup:
    """Test that the GUI can start and be terminated cleanly."""

    def test_gui_new_session_no_crash(self, isolated_gui):
        """Starting with -n (new session) should not crash immediately."""
        result = isolated_gui.start_and_terminate("-n", settle=2.0)
        # Should not have segfaulted (-11) or aborted (-6)
        assert result.returncode not in _CRASH_CODES

    def test_gui_open_file_no_crash(self, isolated_gui, test_data_dir):
        """Opening a file from the CLI should not crash."""
        test_file = test_data_dir / "utf8_tab_test.txt"
        result = isolated_gui.start_and_terminate("-n", str(test_file), settle=3.0)
        assert result.returncode not in _CRASH_CODES

    def test_gui_open_multiple_files_no_crash(self, isolated_gui, test_data_dir):
        """Opening multiple files should not crash."""
        files = [
            str(test_data_dir / "utf8_tab_test.txt"),
            str(test_data_dir / "ansi_colors_example.txt"),
        ]
        result = isolated_gui.start_and_terminate("-n", *files, settle=3.0)
        assert result.returncode not in _CRASH_CODES

    def test_gui_open_large_file_no_crash(self, isolated_gui, test_data_dir):
        """Opening the 1.5MB file should not crash."""
        test_file = test_data_dir / "random_block_1.5Mb.txt"
        result = isolated_gui.start_and_terminate("-n", str(test_file), settle=4.0)
        assert result.returncode not in _CRASH_CODES
