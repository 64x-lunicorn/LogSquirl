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

Windows (#348) gets the same three guarantees through different levers:

- its own settings and Session: no bundle to clone, so the environment
  redirection below is enough on its own -- ``PersistentInfo`` picks up
  ``QSettings::IniFormat`` under ``APPDATA`` (see persistentinfo.cpp).
- its own QStandardPaths locations: Qt reads ``APPDATA`` and ``LOCALAPPDATA``
  directly for AppDataLocation/AppConfigLocation/CacheLocation on Windows
  (unlike macOS, no known-folder lookup that would ignore the override), so
  pointing both into the temporary directory moves them the same way
  ``CFFIXED_USER_HOME`` does on macOS. ``LOGSQUIRL_TEST_MODE`` additionally
  asks the application to enable Qt's test mode, which is documented to
  append ``/qttest`` to those same locations -- a second layer nobody has
  verified from an actual Windows machine (see main.cpp).
- its own single-instance lock: the lock file moves because ``TEMP``/``TMP``
  (not ``TMPDIR``, which Windows ignores) point into the temporary directory,
  and the named pipe itself is scoped by ``LOGSQUIRL_INSTANCE_ID``, which
  logsquirlapp.h folds into the pipe name -- Windows named pipes are not
  scoped by a directory the way a Unix local socket in ``TMPDIR`` is (#320),
  so without this a test instance could still hand its files to, or be
  activated by, a real running LogSquirl.

Everything Windows-specific above is implemented per a documented Qt/Win32
lead, not verified against real Windows hardware -- e2e-windows CI is the
first place it actually runs.

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
    return platform.system() in ("Darwin", "Linux", "Windows")


def user_data_locations() -> list[Path]:
    """The places the user's own LogSquirl keeps its data.

    Nothing an isolated instance does may show up here -- that is what
    test_user_data_untouched.py checks.
    """
    system = platform.system()
    if system == "Windows":
        # PersistentInfo's settings (an ini file, org "logsquirl", app
        # "logsquirl"/"logsquirl_session") and QStandardPaths' plugins, Log
        # Formats, cache and crash dumps all land under one of these two
        # (persistentinfo.cpp, main.cpp's other QStandardPaths::* call sites).
        locations = []
        appdata = os.environ.get("APPDATA")
        if appdata:
            locations.append(Path(appdata) / "logsquirl")
        localappdata = os.environ.get("LOCALAPPDATA")
        if localappdata:
            locations.append(Path(localappdata) / "logsquirl")
        return locations
    home = Path(os.path.expanduser("~"))
    if system == "Darwin":
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
            raise RuntimeError("isolated LogSquirl instances need macOS, Linux or Windows")

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

        system = platform.system()
        if system == "Windows":
            # QDir::tempPath() (the single-instance lock file) reads
            # TEMP/TMP on Windows, not TMPDIR.
            self.env["TEMP"] = str(tmp)
            self.env["TMP"] = str(tmp)
            self.env["USERPROFILE"] = str(self.home)

            appdata = self.home / "AppData" / "Roaming"
            localappdata = self.home / "AppData" / "Local"
            appdata.mkdir(parents=True)
            localappdata.mkdir(parents=True)
            self.env["APPDATA"] = str(appdata)
            self.env["LOCALAPPDATA"] = str(localappdata)
            # See the module docstring: unlike macOS this is a confident
            # redirection (Qt reads these two variables directly on
            # Windows), backed up by the documented-but-unverified test-mode
            # lead below.
            self.env["LOGSQUIRL_TEST_MODE"] = "1"
            # Scopes the single-instance named pipe to this instance
            # (logsquirlapp.h); a real run never sets this, so its pipe name
            # is unaffected.
            self.env["LOGSQUIRL_INSTANCE_ID"] = self.root.name

            config_dir = appdata / "logsquirl"
            config_dir.mkdir(parents=True)
            (config_dir / "logsquirl.ini").write_text(_SETTINGS)

            self.binary = binary
            # Test mode puts "/qttest" between the known folder and the
            # application name of AppDataLocation (Qt's
            # qstandardpaths_win.cpp), so the instance looks for plugins
            # under Roaming/qttest/logsquirl, not Roaming/logsquirl (#403).
            # The settings above stay where they are: QSettings resolves its
            # ini path itself and does not know test mode.
            self.app_data_dir = appdata / "qttest" / "logsquirl"
        elif system == "Darwin":
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

        Its output goes through a pseudo terminal rather than a pipe on macOS
        and Linux. The application flushes an info or debug message only at a
        following warning, at exit, or when a message arrives a second after
        the last flush, so on a pipe the last lines of a startup sit in the
        buffer for as long as the instance stays idle -- including the line
        waited for here. A terminal makes the C library line-buffer, so every
        line arrives as it is logged.

        Windows has no pseudo terminal, so it reads a plain pipe instead: a
        wait_for_primary_line() there can lag up to the second described
        above rather than seeing a line the moment it is logged.
        """
        if platform.system() == "Windows":
            self.primary = subprocess.Popen(
                [str(self.binary), "-n", "-d", "2", *args],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=self.env,
                text=True,
                errors="replace",
            )
            self._primary_output = self.primary.stdout
        else:
            # Imported here, not at the top: pty is POSIX only.
            import pty

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
        """What the primary logs once it has opened a Log File.

        The path in that line comes from QFileInfo::absoluteFilePath(),
        which Qt always renders with forward slashes -- on every platform,
        not just where the native separator already is one. log_file.as_posix()
        matches that; str(log_file) would build an unmatchable line on
        Windows (#388).
        """
        return f'Success loading file "{log_file.as_posix()}"'

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
