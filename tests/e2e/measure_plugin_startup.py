#!/usr/bin/env python3
"""
Times the first window against a slow plugin at start-up (#303).

Installs the slow converter plugin built for the tests
(``logsquirl_test_slow_converter``, which sleeps ``--delay-ms`` in its init)
into an isolated LogSquirl (offscreen, own settings, Session and
single-instance lock -- see isolated_instance.py; the user's LogSquirl and its
settings are not touched), starts it RUNS times and reports the median of

- window:  launched -> main() has shown the first window and enters the
           event loop (it logs ``startBackgroundTasks``)
- plugin:  launched -> the slow plugin has been initialised

Before #303 the window waits for the plugin (window > plugin); after it, the
window comes first and the plugin loads behind it (window < plugin).

Linux only: there the isolated instance's HOME and XDG directories hold the
application data directory too. On macOS it would still be the user's, and
LogSquirl would load the plugins installed there and create plugin
configuration directories in it. The ``[applicationplugins]`` case of
``logsquirl_itests`` times the same on every platform.

Usage (Linux):

    cmake --build build --target logsquirl logsquirl_test_slow_converter
    python3 tests/e2e/measure_plugin_startup.py \\
        --binary build/output/logsquirl \\
        --plugin build/output/liblogsquirl_test_slow_converter.so

To compare with origin/master, build its ``logsquirl`` and pass it as
``--binary``, with ``--plugin`` still the library built from this branch.
"""

import argparse
import json
import platform
import shutil
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from isolated_instance import IsolatedLogSquirl, supported  # noqa: E402

PLUGIN_ID = "io.github.logsquirl.test.slow-converter"


def plugin_directory(env: IsolatedLogSquirl) -> Path:
    """The plugin directory next to the binary, where LogSquirl looks first."""
    return env.binary.parent / "plugins"


def install_plugin(directory: Path, library: Path) -> Path:
    plugin_dir = directory / "slow-converter"
    plugin_dir.mkdir(parents=True, exist_ok=True)
    (plugin_dir / "plugin.json").write_text(json.dumps({
        "id": PLUGIN_ID,
        "name": "Slow Converter",
        "version": "1.0.0",
        "type": "converter",
        "library": str(library),
        "api_version": 1,
    }))
    return plugin_dir


def measure_once(binary: Path, library: Path, delay_ms: int) -> tuple[float, float]:
    with IsolatedLogSquirl(binary) as env:
        installed = install_plugin(plugin_directory(env), library)
        env.env["LOGSQUIRL_TEST_PLUGIN_INIT_DELAY_MS"] = str(delay_ms)
        try:
            start = time.perf_counter()
            env.primary = subprocess.Popen(
                [str(env.binary), "-n", "-d", "2"],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env.env,
                text=True,
                errors="replace",
            )
            threading.Thread(target=env._read_primary, daemon=True).start()
            # Whichever comes first, both lines are queued in the order logged.
            stamps: dict[str, float] = {}
            deadline = start + 60
            while len(stamps) < 2:
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    raise TimeoutError(f"LogSquirl did not log both lines, got {stamps}")
                stamp, line = env._lines.get(timeout=remaining)
                if "startBackgroundTasks" in line:
                    stamps.setdefault("window", stamp)
                elif "registered extensions" in line and PLUGIN_ID in line:
                    stamps.setdefault("plugin", stamp)
            return (stamps["window"] - start) * 1000, (stamps["plugin"] - start) * 1000
        finally:
            # The plugin directory sits next to the build's binary.
            shutil.rmtree(installed, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", required=True, type=Path,
                        help="the logsquirl executable")
    parser.add_argument("--plugin", required=True, type=Path,
                        help="the built logsquirl_test_slow_converter library")
    parser.add_argument("--delay-ms", type=int, default=500)
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--warmup", type=int, default=1)
    args = parser.parse_args()

    if not supported() or platform.system() != "Linux":
        print("measure_plugin_startup.py needs Linux", file=sys.stderr)
        return 2

    windows: list[float] = []
    plugins: list[float] = []
    for run in range(args.warmup + args.runs):
        window, plugin = measure_once(args.binary.resolve(), args.plugin.resolve(), args.delay_ms)
        if run < args.warmup:
            continue
        windows.append(window)
        plugins.append(plugin)
        print(f"run {run - args.warmup + 1:2d}: window {window:7.1f} ms, plugin {plugin:7.1f} ms")

    print(f"median over {args.runs} runs with a {args.delay_ms} ms plugin: "
          f"window {statistics.median(windows):.1f} ms, plugin {statistics.median(plugins):.1f} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
