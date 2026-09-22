"""
The suite leaves the developer's own LogSquirl alone (#328).

This is the guard behind the isolated_gui fixture: it records the places the
user's LogSquirl keeps its settings, Session, cache, Log Formats and plugins,
runs an isolated instance the way the smoke tests do, and checks that not one
byte there changed -- and that the instance really did put its own data in its
temporary directory, so a silently broken isolation fails here instead of
going unnoticed.

macOS is what makes the check worth having: there QStandardPaths comes from
Core Foundation, which ignores HOME, so an instance with nothing but a
temporary HOME still reads and writes the user's real plugin directory.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path

import pytest

from isolated_instance import supported, user_data_locations

pytestmark = [
    pytest.mark.slow,
    pytest.mark.skipif(not supported(), reason="isolated instances need macOS, Linux or Windows"),
]

# Files above this are recorded by size and modification time only: hashing a
# large crash dump would say nothing more than those two do.
_MAX_HASHED_BYTES = 16 * 1024 * 1024


def _real_path(path: str) -> str:
    """One spelling for a path, so a comparison is about the place, not the name.

    macOS puts the temporary directory under /var, a symlink to /private/var,
    and logs it both ways.
    """
    return os.path.realpath(path)


def _fingerprint(path: Path) -> dict[str, tuple]:
    """What every file and directory below path looks like now.

    Only metadata and a digest are kept; nothing of the user's is copied
    anywhere.
    """
    state: dict[str, tuple] = {}
    if not path.exists() and not path.is_symlink():
        return state

    def record(entry: Path) -> None:
        info = entry.lstat()
        if entry.is_symlink():
            state[str(entry)] = ("link", os.readlink(entry))
        elif entry.is_dir():
            state[str(entry)] = ("dir", info.st_mode)
        elif info.st_size > _MAX_HASHED_BYTES:
            state[str(entry)] = ("big", info.st_size, info.st_mtime_ns)
        else:
            digest = hashlib.sha256(entry.read_bytes()).hexdigest()
            state[str(entry)] = ("file", info.st_size, info.st_mtime_ns, digest)

    record(path)
    if path.is_dir() and not path.is_symlink():
        for entry in sorted(path.rglob("*")):
            record(entry)
    return state


def _fingerprint_user_data() -> dict[str, tuple]:
    state: dict[str, tuple] = {}
    for location in user_data_locations():
        state.update(_fingerprint(location))
    return state


def test_user_locations_exist_to_be_protected():
    """The guard below only means something where there is something to guard.

    Not a failure on a fresh machine or a CI runner -- it says the stronger
    check is not available here.
    """
    present = [str(p) for p in user_data_locations() if p.exists()]
    if not present:
        pytest.skip("this machine has no LogSquirl data of its own yet")
    assert present


def test_isolated_instance_leaves_user_data_untouched(isolated_gui, test_data_dir):
    """An isolated instance changes nothing of the user's and keeps its own data.

    Read as one claim in two halves: the user's locations are byte for byte
    what they were, and the instance's own settings, Session and plugin
    directory are inside its temporary directory.
    """
    before = _fingerprint_user_data()

    log_file = isolated_gui.root / "guarded.log"
    log_file.write_text("first line\nsecond line\n")

    # The same thing the smoke tests do: a primary instance with -n, which
    # clears inactive window sessions, opening a Log File.
    isolated_gui.start_primary(str(log_file))
    # The Log File is opened once the plugins have loaded, so waiting for it
    # means the plugin directories have been scanned as well.
    isolated_gui.wait_for_primary_line(isolated_gui.opened_line(log_file), timeout=30)

    # SIGTERM, not a kill: the instance runs its shutdown, which is where it
    # saves its Session and its settings.
    isolated_gui.primary.terminate()
    isolated_gui.primary.wait(timeout=30)

    after = _fingerprint_user_data()

    changed = sorted(
        path for path in set(before) | set(after) if before.get(path) != after.get(path)
    )
    assert not changed, (
        "an isolated LogSquirl changed the user's own data:\n  "
        + "\n  ".join(changed)
    )

    # The other half: the instance did keep its data somewhere, and that
    # somewhere is its temporary directory. Without this the check above
    # would also pass for an instance that never started.
    scanned = [
        _real_path(line.split("Scanning for plugins in:", 1)[1].strip().strip('"'))
        for line in isolated_gui.log_lines
        if "Scanning for plugins in:" in line
    ]
    assert scanned, "the instance logged no plugin directory at all"
    root = _real_path(str(isolated_gui.root))
    outside = [directory for directory in scanned if not directory.startswith(root)]
    assert not outside, f"plugins were looked for outside {root}: {outside}"

    assert _real_path(str(isolated_gui.app_data_dir)).startswith(root)
    assert _real_path(str(isolated_gui.home)).startswith(root)


def test_short_lived_instance_leaves_user_data_untouched(isolated_gui):
    """--version and a start-and-terminate run change nothing of the user's either.

    The paths the smoke tests take, which never reach an event loop.
    """
    before = _fingerprint_user_data()

    version = isolated_gui.run("--version")
    assert version.returncode == 0
    isolated_gui.start_and_terminate("-n", settle=2.0)

    after = _fingerprint_user_data()
    changed = sorted(
        path for path in set(before) | set(after) if before.get(path) != after.get(path)
    )
    assert not changed, (
        "a short-lived isolated LogSquirl changed the user's own data:\n  "
        + "\n  ".join(changed)
    )
