"""
Run LogSquirl instances that cannot see the user's own LogSquirl.

A primary instance started here gets its own settings, its own Session and
its own single-instance lock:

- settings and Session: on macOS the app bundle is cloned into a temporary
  directory and made portable (a ``logsquirl.conf`` next to the executable);
  on Linux ``HOME`` and the ``XDG_*`` directories point into the temporary
  directory.
- single-instance lock: the lock file and the local socket live in
  ``QDir::tempPath()``, so ``TMPDIR`` points into the temporary directory.
  A running LogSquirl of the user is neither found nor disturbed.

Windows is not supported: its named pipes are not scoped by a directory, so
a test instance could hand files over to the user's LogSquirl.

Instances run with ``QT_QPA_PLATFORM=offscreen``.
"""

from __future__ import annotations

import os
import platform
import queue
import shutil
import subprocess
import tempfile
import threading
import time
from pathlib import Path

# Version checking is off: a "new version" message box would block the
# primary instance's event loop.
_SETTINGS = """[General]
versionchecker.enabled=false
session.loadLast=false
view.showSplashScreen=false
"""


def supported() -> bool:
    return platform.system() in ("Darwin", "Linux")


class IsolatedLogSquirl:
    """A throwaway environment for primary and secondary LogSquirl instances."""

    def __init__(self, binary: Path):
        if not supported():
            raise RuntimeError("isolated LogSquirl instances need macOS or Linux")

        # Short path: the local socket's path must stay below 104 characters.
        self.root = Path(tempfile.mkdtemp(prefix="lsq"))
        tmp = self.root / "t"
        tmp.mkdir()

        self.env = dict(os.environ)
        self.env["TMPDIR"] = str(tmp)
        self.env["QT_QPA_PLATFORM"] = "offscreen"

        if platform.system() == "Darwin":
            bundle = self._find_bundle(binary)
            clone = self.root / bundle.name
            # -c clones on APFS (instant); plain copy elsewhere.
            if subprocess.run(["cp", "-Rc", str(bundle), str(clone)],
                              capture_output=True).returncode != 0:
                shutil.copytree(bundle, clone, symlinks=True)
            self.binary = clone / binary.relative_to(bundle)
            (self.binary.parent / "logsquirl.conf").write_text(_SETTINGS)
        else:
            home = self.root / "home"
            config = home / ".config"
            (config / "logsquirl").mkdir(parents=True)
            (config / "logsquirl" / "logsquirl.conf").write_text(_SETTINGS)
            self.env["HOME"] = str(home)
            self.env["XDG_CONFIG_HOME"] = str(config)
            self.env["XDG_CACHE_HOME"] = str(home / ".cache")
            self.env["XDG_DATA_HOME"] = str(home / ".local" / "share")
            self.binary = binary

        self.primary: subprocess.Popen | None = None
        self._lines: queue.Queue[tuple[float, str]] = queue.Queue()

    @staticmethod
    def _find_bundle(binary: Path) -> Path:
        for parent in binary.parents:
            if parent.suffix == ".app":
                return parent
        raise RuntimeError(f"{binary} is not inside an app bundle")

    def start_primary(self, timeout: float = 60.0) -> None:
        """Starts the primary instance and waits until its event loop runs."""
        self.primary = subprocess.Popen(
            [str(self.binary), "-n", "-d", "2"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=self.env,
            text=True,
            errors="replace",
        )
        threading.Thread(target=self._read_primary, daemon=True).start()
        # startBackgroundTasks is the last thing main() does before app.exec().
        self.wait_for_primary_line("startBackgroundTasks", timeout)

    def _read_primary(self) -> None:
        assert self.primary and self.primary.stdout
        for line in self.primary.stdout:
            self._lines.put((time.perf_counter(), line))

    def wait_for_primary_line(self, text: str, timeout: float) -> float:
        """Returns the perf_counter() time the primary logged a line containing text."""
        deadline = time.perf_counter() + timeout
        while True:
            remaining = deadline - time.perf_counter()
            if remaining <= 0:
                raise TimeoutError(f"primary instance did not log {text!r}")
            try:
                stamp, line = self._lines.get(timeout=remaining)
            except queue.Empty:
                continue
            if text in line:
                return stamp

    @staticmethod
    def opened_line(log_file: Path) -> str:
        """What the primary logs once it has opened a Log File."""
        return f'Success loading file "{log_file}"'

    def launch_secondary(self, *args: str) -> subprocess.Popen:
        return subprocess.Popen(
            [str(self.binary), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=self.env,
            text=True,
            errors="replace",
        )

    def close(self) -> None:
        if self.primary and self.primary.poll() is None:
            self.primary.kill()
            self.primary.wait(timeout=10)
        shutil.rmtree(self.root, ignore_errors=True)

    def __enter__(self) -> "IsolatedLogSquirl":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
