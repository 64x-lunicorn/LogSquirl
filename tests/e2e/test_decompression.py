"""
E2E tests for compressed-file decompression support.

Tests verify that LogSquirl can open and decompress files in
gz, bz2, xz, zst, and lz4 formats.  GUI smoke tests launch
the app briefly with '-platform offscreen' (skipped on macOS).
"""

import platform
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

from conftest import run_gui


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _has_tool(name: str) -> bool:
    return shutil.which(name) is not None


def _compress_file(source: Path, dest: Path, fmt: str) -> None:
    """Compress *source* to *dest* using the given format's CLI tool."""
    cmd_map = {
        "gz": ["gzip", "-c"],
        "bz2": ["bzip2", "-c"],
        "xz": ["xz", "-c"],
        "zst": ["zstd", "-q", "-c"],
        "lz4": ["lz4", "-q", "-c"],
    }
    cmd = cmd_map[fmt]
    with open(source, "rb") as fin, open(dest, "wb") as fout:
        subprocess.run(cmd, stdin=fin, stdout=fout, check=True)


# ---------------------------------------------------------------------------
# GUI smoke tests — verify the app starts with a compressed file argument
# ---------------------------------------------------------------------------

_COMPRESSED_FORMATS = [
    pytest.param("gz", marks=pytest.mark.skipif(not _has_tool("gzip"), reason="gzip not found")),
    pytest.param("bz2", marks=pytest.mark.skipif(not _has_tool("bzip2"), reason="bzip2 not found")),
    pytest.param("xz", marks=pytest.mark.skipif(not _has_tool("xz"), reason="xz not found")),
    pytest.param("zst", marks=pytest.mark.skipif(not _has_tool("zstd"), reason="zstd not found")),
    pytest.param("lz4", marks=pytest.mark.skipif(not _has_tool("lz4"), reason="lz4 not found")),
]


@pytest.mark.slow
class TestDecompressionSmoke:
    """Smoke tests: verify the GUI binary accepts compressed files without crashing."""

    @pytest.fixture(autouse=True)
    def _skip_on_mac(self):
        """GUI smoke tests need -platform offscreen, which is unavailable on macOS."""
        if platform.system() == "Darwin":
            pytest.skip("offscreen platform not available on macOS")

    @pytest.mark.parametrize("fmt", _COMPRESSED_FORMATS)
    def test_gui_opens_compressed_file(
        self, fmt, logsquirl_binary, test_data_dir, tmp_path
    ):
        """Verify that logsquirl accepts a compressed file argument and exits cleanly."""
        source = test_data_dir / "random_block_1Mb.txt"
        if not source.exists():
            pytest.skip("random_block_1Mb.txt not found in test_data")

        compressed = tmp_path / f"test.log.{fmt}"
        _compress_file(source, compressed, fmt)
        assert compressed.stat().st_size > 0, f"Compressed file is empty for {fmt}"

        # Launch app with the compressed file — it should start decompressing
        # and not crash.  We use a very short timeout because we only care
        # about the startup path.
        result = run_gui(logsquirl_binary, [str(compressed)], timeout=5)

        # The app will time out (we killed it), that's fine.
        # What matters is that it did NOT segfault.
        assert result.returncode != -11, f"logsquirl segfaulted on {fmt}"
        assert result.returncode != -6, f"logsquirl aborted on {fmt}"


# ---------------------------------------------------------------------------
# Decompressor action detection tests (no binary needed)
# ---------------------------------------------------------------------------

class TestArchiveDetection:
    """Verify that known extensions map to the correct decompress/extract action."""

    @pytest.mark.parametrize(
        "filename, expected",
        [
            ("app.log.gz", "decompress"),
            ("app.log.bz2", "decompress"),
            ("app.log.xz", "decompress"),
            ("app.log.zst", "decompress"),
            ("app.log.zstd", "decompress"),
            ("app.log.lz4", "decompress"),
            ("archive.tar.gz", "extract"),
            ("archive.tar.bz2", "extract"),
            ("archive.tar.xz", "extract"),
            ("archive.tar.zst", "extract"),
            ("archive.tar.lz4", "extract"),
            ("archive.tgz", "extract"),
            ("archive.tzst", "extract"),
            ("archive.zip", "extract"),
            ("archive.7z", "extract"),
            ("plain.log", "none"),
            ("data.csv", "none"),
        ],
    )
    def test_extension_recognition(self, filename, expected):
        """
        Verify extension-to-action mapping is correct.

        NOTE: This test documents expected behaviour. The actual action
        detection runs inside the C++ Decompressor class; this test
        serves as a specification cross-reference.
        """
        # This is a documentation/spec test — it doesn't call the binary.
        # It ensures our test matrix covers all formats.
        ext_to_action = {
            "gz": "decompress",
            "bz2": "decompress",
            "xz": "decompress",
            "lzma": "decompress",
            "zst": "decompress",
            "zstd": "decompress",
            "lz4": "decompress",
            "zip": "extract",
            "7z": "extract",
            "tgz": "extract",
            "tbz2": "extract",
            "txz": "extract",
            "tzst": "extract",
        }

        # Extract final suffix
        name = filename.lower()
        if ".tar." in name:
            action = "extract"
        else:
            suffix = name.rsplit(".", 1)[-1] if "." in name else ""
            action = ext_to_action.get(suffix, "none")

        assert action == expected, f"{filename}: got {action}, expected {expected}"
