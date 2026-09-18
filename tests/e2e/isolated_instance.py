"""
Run LogSquirl instances that cannot see the user's own LogSquirl.

Every E2E test that starts the application goes through here (#328): run on a
developer machine the suite must not read or change the developer's own
settings, Session, cache, Log Formats or plugins.

An instance started here gets:

- its own settings and Session: on macOS the app bundle is cloned into a
  temporary directory and made portable (a ``logsquirl.conf`` next to the
  executable); on Linux ``HOME`` and the ``XDG_*`` directories point into the
  temporary directory.
- its own QStandardPaths locations -- plugins, plugin configuration, Log
  Formats, theme files, the index cache, crash dumps. On Linux the ``XDG_*``
  directories move all of them. macOS resolves them through Core Foundation,
  which reads the home directory from the password database and so ignores
  ``HOME``; ``CFFIXED_USER_HOME`` is the override it does honour, and with it
  every location lands under the temporary directory. Qt's
  ``QStandardPaths::setTestModeEnabled`` is no help there: Qt 6.11 ignores
  test mode on macOS, its locations come back unchanged.
- its own single-instance lock: the lock file and the local socket live in
  ``QDir::tempPath()``, so ``TMPDIR`` points into the temporary directory.
  A running LogSquirl of the user is neither found nor disturbed.

Windows is not supported: its named pipes are not scoped by a directory, so
a test instance could hand files over to the user's LogSquirl. Tests that
start the application skip there (see the ``isolated_gui`` fixture).

Instances run with ``QT_QPA_PLATFORM=offscreen``.
"""

from __future__ import annotations

import os
import platform
import pty
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


def user_data_locations() -> list[Path]:
    """The places the user's own LogSquirl keeps its data.

    Nothing an isolated instance does may show up here -- that is what
    test_user_data_untouched.py checks.
    """
    home = Path(os.path.expanduser("~"))
    if platform.system() == "Darwin":
        library = home / "Library"
        return [
            library / "Preferences" / "com.logsquirl.logsquirl.plist",
            library / "Preferences" / "com.logsquirl.logsquirl_session.plist",
            library / "Preferences" / "logsquirl",
            library / "Application Support" / "logsquirl",
            library / "Caches" / "logsquirl",
        ]
    return [
        home / ".config" / "logsquirl",
        home / ".local" / "share" / "logsquirl",
        home / ".cache" / "logsquirl",
    ]


class IsolatedLogSquirl:
    """A throwaway environment for primary and secondary LogSquirl instances."""

    def __init__(self, binary: Path):
        if not supported():
            raise RuntimeError("isolated LogSquirl instances need macOS or Linux")

        # Short path: the local socket's path must stay below 104 characters.
        self.root = Path(tempfile.mkdtemp(prefix="lsq"))
        tmp = self.root / "t"
        tmp.mkdir()
        self.home = self.root / "home"
        self.home.mkdir()

        self.env = dict(os.environ)
        self.env["TMPDIR"] = str(tmp)
        self.env["QT_QPA_PLATFORM"] = "offscreen"
        self.env["HOME"] = str(self.home)

        if platform.system() == "Darwin":
            bundle = self._find_bundle(binary)
            clone = self.root / bundle.name
            # -c clones on APFS (instant); plain copy elsewhere.
            if subprocess.run(["cp", "-Rc", str(bundle), str(clone)],
                              capture_output=True).returncode != 0:
                shutil.copytree(bundle, clone, symlinks=True)
            self.binary = clone / binary.relative_to(bundle)
            (self.binary.parent / "logsquirl.conf").write_text(_SETTINGS)
            # See the module docstring: HOME alone does not move macOS's
            # QStandardPaths locations, CFFIXED_USER_HOME does.
            self.env["CFFIXED_USER_HOME"] = str(self.home)
            self.app_data_dir = self.home / "Library" / "Application Support" / "logsquirl"
        else:
            config = self.home / ".config"
            (config / "logsquirl").mkdir(parents=True)
            (config / "logsquirl" / "logsquirl.conf").write_text(_SETTINGS)
            self.env["XDG_CONFIG_HOME"] = str(config)
            self.env["XDG_CACHE_HOME"] = str(self.home / ".cache")
            self.env["XDG_DATA_HOME"] = str(self.home / ".local" / "share")
            self.binary = binary
            self.app_data_dir = self.home / ".local" / "share" / "logsquirl"

        # Exists so the application logs the directory it scans for plugins:
        # that line is how a test sees which plugins an instance would load.
        (self.app_data_dir / "plugins").mkdir(parents=True)

        self.primary: subprocess.Popen | None = None
        self._primary_output = None
        self.log_lines: list[str] = []
        self._lines: queue.Queue[tuple[float, str]] = queue.Queue()

    @staticmethod
    def _find_bundle(binary: Path) -> Path:
        for parent in binary.parents:
            if parent.suffix == ".app":
                return parent
        raise RuntimeError(f"{binary} is not inside an app bundle")

    # -- starting instances --------------------------------------------------

    def start_primary(
        self, *args: str, ready: str | None = "startBackgroundTasks", timeout: float = 60.0
    ) -> None:
        """Starts the primary instance and waits until it logged ready.

        The default waits for startBackgroundTasks, the last thing main() does
        before app.exec(). Pass ready=None to return the moment the process
        exists, for a measurement that times the startup itself.

        Its output goes through a pseudo terminal rather than a pipe. The
        application flushes an info or debug message only at a following
        warning, at exit, or when a message arrives a second after the last
        flush, so on a pipe the last lines of a startup sit in the buffer for
        as long as the instance stays idle -- including the line waited for
        here. A terminal makes the C library line-buffer, so every line
        arrives as it is logged.
        """
        reader, writer = pty.openpty()
        try:
            self.primary = subprocess.Popen(
                [str(self.binary), "-n", "-d", "2", *args],
                stdout=writer,
                stderr=writer,
                env=self.env,
            )
        finally:
            os.close(writer)
        self._primary_output = os.fdopen(reader, "r", errors="replace")
        threading.Thread(target=self._read_primary, daemon=True).start()
        if ready is not None:
            self.wait_for_primary_line(ready, timeout)

    def launch(self, *args: str) -> subprocess.Popen:
        """Starts an instance in this environment, without waiting for it."""
        return subprocess.Popen(
            [str(self.binary), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=self.env,
            text=True,
            errors="replace",
        )

    # The secondary instances of test_single_instance.py, named for what they
    # are there.
    launch_secondary = launch

    def run(self, *args: str, timeout: float = 30.0) -> subprocess.CompletedProcess:
        """Runs an instance that exits on its own (``--version``, ``--help``)."""
        return subprocess.run(
            [str(self.binary), *args],
            capture_output=True,
            text=True,
            errors="replace",
            env=self.env,
            timeout=timeout,
        )

    def start_and_terminate(
        self, *args: str, settle: float = 2.0, timeout: float = 10.0
    ) -> subprocess.CompletedProcess:
        """Starts an instance, lets it settle, terminates it and reports the end.

        What the smoke tests need: they only check that starting, opening Log
        Files and shutting down neither crash nor hang. An instance that does
        not answer SIGTERM within timeout is killed and raises TimeoutError.
        """
        process = self.launch(*args)
        try:
            time.sleep(settle)
            process.terminate()
            try:
                output, _ = process.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate(timeout=timeout)
                raise TimeoutError(
                    f"LogSquirl did not answer SIGTERM within {timeout}s: {args}"
                ) from None
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=timeout)
        return subprocess.CompletedProcess(process.args, process.returncode, output, "")

    # -- reading what the primary logs ---------------------------------------

    def _read_primary(self) -> None:
        assert self.primary
        stream = self._primary_output or self.primary.stdout
        assert stream
        try:
            for line in stream:
                self.log_lines.append(line)
                self._lines.put((time.perf_counter(), line))
        except OSError:
            # A pseudo terminal reports the end of its last writer as EIO.
            pass

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

    def close(self) -> None:
        if self.primary and self.primary.poll() is None:
            self.primary.kill()
            self.primary.wait(timeout=10)
        if self._primary_output:
            self._primary_output.close()
            self._primary_output = None
        shutil.rmtree(self.root, ignore_errors=True)

    def __enter__(self) -> "IsolatedLogSquirl":
        return self

    def __exit__(self, *exc) -> None:
        self.close()
