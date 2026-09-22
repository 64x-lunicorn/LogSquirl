"""
E2E tests for handing Log Files over from a secondary to the primary instance.

Each test starts its own primary instance with its own settings, Session and
single-instance lock (see isolated_instance.py), so a LogSquirl the user runs
is not involved.
"""

import platform
import signal

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


@pytest.mark.xfail(
    platform.system() == "Windows",
    reason="the secondary hands off and exits 0, but the primary is never seen "
    "to log the file as loaded on Windows -- #388",
    strict=False,
)
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
