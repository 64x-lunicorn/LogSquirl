"""
E2E tests for handing Log Files over from a secondary to the primary instance.

Each test starts its own primary instance with its own settings, Session and
single-instance lock (see isolated_instance.py), so a LogSquirl the user runs
is not involved.
"""

import platform
import re
import signal
import subprocess
import time
from pathlib import Path

import pytest

from isolated_instance import IsolatedLogSquirl, supported

pytestmark = [
    pytest.mark.slow,
    pytest.mark.skipif(not supported(), reason="isolated instances need macOS, Linux or Windows"),
]


@pytest.fixture
def instances(logsquirl_binary):
    with IsolatedLogSquirl(logsquirl_binary) as env:
        env.start_primary()
        yield env


@pytest.fixture
def log_file(instances):
    path = instances.root / "handed_over.log"
    path.write_text("first line\nsecond line\n")
    return path


def test_secondary_instance_hands_log_file_to_primary(instances, log_file):
    secondary = instances.launch_secondary(str(log_file))

    assert secondary.wait(timeout=15) == 0
    instances.wait_for_primary_line(instances.opened_line(log_file), timeout=15)


@pytest.mark.skipif(
    platform.system() == "Windows",
    reason="Windows has no SIGSTOP/SIGCONT to pause a process",
)
def test_handover_while_primary_instance_is_busy(instances, log_file):
    # A stopped process is as busy as a primary can get: its event loop runs
    # no code at all while the secondary sends.
    instances.primary.send_signal(signal.SIGSTOP)
    try:
        secondary = instances.launch_secondary(str(log_file))
        assert secondary.wait(timeout=15) == 0
    finally:
        instances.primary.send_signal(signal.SIGCONT)

    instances.wait_for_primary_line(instances.opened_line(log_file), timeout=15)


def test_secondary_instance_does_not_start_like_a_primary(instances, log_file):
    secondary = instances.launch_secondary("-d", "2", str(log_file))
    output, _ = secondary.communicate(timeout=15)

    assert secondary.returncode == 0
    assert "Handing over 1 file(s) to the primary instance" in output
    # Logged by a primary once the crash handler runs and translations are in.
    assert "LogSquirl instance" not in output
    assert "Configuration::retrieveFromStorage" not in output


# -- standard input handed over (#623) ---------------------------------------

_SPOOL_OPENED = re.compile(r'Success loading file "(.*stream\.log)"')


def _wait_for_spool_opened(instances, known: set[str], timeout: float = 15) -> str:
    """The spool file of standard input the primary opens next, as a path."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for line in list(instances.log_lines):
            match = _SPOOL_OPENED.search(line)
            if match and match.group(1) not in known:
                return match.group(1)
        time.sleep(0.05)
    raise TimeoutError("primary instance did not open a spool file of standard input")


def _wait_for_content(path: Path, content: str, timeout: float = 15) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists() and path.read_text() == content:
            return
        time.sleep(0.05)
    raise TimeoutError(f"{path} never held {content!r}")


@pytest.mark.skipif(
    platform.system() == "Windows",
    reason="standard input redirected into the Windows GUI executable is not verified",
)
def test_piped_standard_input_opens_in_the_running_instance_as_it_arrives(instances):
    secondary = instances.launch_secondary("-", stdin=subprocess.PIPE)
    try:
        secondary.stdin.write("1\n")
        secondary.stdin.flush()

        spool = Path(_wait_for_spool_opened(instances, set()))
        _wait_for_content(spool, "1\n")
        # Still reading for the primary instance.
        assert secondary.poll() is None

        secondary.stdin.write("2\n3\n4\n5\n")
        secondary.stdin.close()
        assert secondary.wait(timeout=15) == 0
        # The primary owns the file: it stays with its tab.
        assert spool.read_text() == "1\n2\n3\n4\n5\n"
    finally:
        if secondary.poll() is None:
            secondary.kill()


@pytest.mark.skipif(
    platform.system() == "Windows",
    reason="standard input redirected into the Windows GUI executable is not verified",
)
def test_piped_standard_input_twice_opens_two_tabs_beside_a_log_file(instances, log_file):
    spools = set()
    for content in ("first\n", "second\n"):
        secondary = instances.launch_secondary(str(log_file), "-", stdin=subprocess.PIPE)
        secondary.communicate(input=content, timeout=15)
        assert secondary.returncode == 0
        spool = _wait_for_spool_opened(instances, spools)
        _wait_for_content(Path(spool), content)
        spools.add(spool)

    assert len(spools) == 2
    instances.wait_for_primary_line(instances.opened_line(log_file), timeout=15)
