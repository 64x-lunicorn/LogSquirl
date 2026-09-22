"""Tests for releases.py, the release naming the release scripts share (#385).
No network."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent))
import releases  # noqa: E402


@pytest.mark.parametrize("lower, higher", [
    ("26.7.0", "26.10.0"),
    ("26.07.0", "26.07.1"),
    ("26.07.0.741", "26.07.0.760"),
    ("25.12.9", "26.01.0"),
])
def test_versions_order_as_numbers(lower, higher):
    assert releases.version_key(lower) < releases.version_key(higher)


def test_leading_zeros_do_not_matter():
    assert releases.version_key("26.07.0") == releases.version_key("26.7.0")


@pytest.mark.parametrize("version", ["", "26.07", "26.x.0", "v26.07.0", "26.07.0-beta1", "26..0"])
def test_a_malformed_version_is_named(version):
    with pytest.raises(releases.ReleaseError, match=f"{version!r} is not"):
        releases.version_key(version)
