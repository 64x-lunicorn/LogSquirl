#!/usr/bin/env python3
"""Builds LogSquirl with profile-guided optimization, and BOLT on Linux, and
measures it against the same build without (#682).

The PGO workflow (.github/workflows/pgo.yml) runs this on each platform; it
works the same on a developer machine (BUILD.md, "Profile-guided
optimization"). Every step is a subcommand, so the workflow shows each one as
its own step and its own time:

  build    configure and build one build directory with LOGSQUIRL_PGO=OFF,
           GENERATE or USE, timed
  train    run the training workload, the benchmark mode's scenarios of the e2e
           performance suite, on one set of binaries
  merge    merge the raw profile the training left into what a USE build reads
  bolt-instrument / bolt-optimize
           (Linux) instrument the optimized executables with llvm-bolt, and
           rewrite them from the profile their training run wrote
  measure  run the Catch2 micro-benchmarks and the e2e performance suite on
           several sets of binaries, one benchmark at a time across all sides,
           into <results>/<side>/catch2/*.xml and <results>/<side>/e2e/, the
           layout benchmark-compare.py reads
  collect  copy what the measurement runs out of a build output, so another
           job can measure it
  times    the build times the steps recorded, and what PGO adds to a build

No profile is kept between runs: the training runs the code of the commit
being built, so the profile can never be stale (#682).

Build time: each `build`, `train`, `merge` and `bolt-*` step adds its wall
time under its phase name to a JSON file (--timings). The builds run without a
compiler cache, so each one compiles everything, and `times` sets what a
release would build with PGO -- the instrumented build, the training, the
merge and the optimized build, plus BOLT where used -- against the one plain
build it replaces.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

CLANG_PROFILE = "logsquirl.profdata"  # cmake/ProfileGuidedOptimization.cmake
TOOLCHAINS = ("clang", "gcc", "msvc")
# The executables BOLT rewrites: the ones the scenarios run. The micro-
# benchmarks are separate executables that nothing trains BOLT on.
BOLT_EXECUTABLES = ("logsquirl", "logsquirl_grep")
# What llvm-bolt does with the profile; the options of the LLVM BOLT
# documentation's own recipe (bolt/README.md, "Optimizing the binary"),
# available from LLVM 16 on.
BOLT_OPTIONS = (
    "-reorder-blocks=ext-tsp",
    "-reorder-functions=hfsort",
    "-split-functions",
    "-split-all-cold",
    "-split-eh",
    "-dyno-stats",
    "-icf=1",
    "-use-gnu-stack",
    # The rewritten executable's debug information follows it, so the
    # .debug file split from it afterwards still symbolicates a crash.
    "-update-debug-sections",
)

# Phases of the `times` table. A release with PGO runs the PGO phases instead
# of the one plain build.
PLAIN_PHASE = "plain build"
PGO_PHASES = ("instrumented build", "training", "merge", "optimized build")
# A build for BOLT is the optimized build linked for BOLT (LOGSQUIRL_BOLT).
BOLT_BUILD_PHASE = "optimized build for BOLT"
BOLT_PHASES = ("BOLT instrumentation", "BOLT training", "BOLT optimization")


def log(message: str) -> None:
    print(message, flush=True)


def run(command: list[str], **kwargs) -> None:
    log("+ " + shlex.join(str(part) for part in command))
    subprocess.run([str(part) for part in command], check=True, **kwargs)


def run_keeping_diagnostics(command: list[str]) -> list[str]:
    """Run like run(), passing the output through, and return its lines that
    name a warning option."""
    log("+ " + shlex.join(str(part) for part in command))
    kept = []
    with subprocess.Popen([str(part) for part in command], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, errors="replace") as process:
        for line in process.stdout:
            sys.stdout.write(line)
            if "[-W" in line:
                kept.append(line.rstrip())
    sys.stdout.flush()
    if process.returncode != 0:
        raise subprocess.CalledProcessError(process.returncode, command)
    return kept


# ---------------------------------------------------------------------------
# Timings
# ---------------------------------------------------------------------------

def record_time(path: Path | None, phase: str, seconds: float) -> None:
    """Add seconds to phase in the timings file (a phase may run in parts)."""
    if path is None:
        return
    timings = json.loads(path.read_text()) if path.exists() else {}
    timings[phase] = round(timings.get(phase, 0.0) + seconds, 1)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(timings, indent=2, sort_keys=True) + "\n")


class Timer:
    def __init__(self, path: Path | None, phase: str):
        self.path = path
        self.phase = phase

    def __enter__(self):
        self.start = time.monotonic()
        return self

    def __exit__(self, exc_type, *_):
        seconds = time.monotonic() - self.start
        if exc_type is not None:
            # A phase that failed did not take what a finished one takes.
            log(f"{self.phase}: failed after {seconds:.0f} s, not recorded")
            return
        record_time(self.path, self.phase, seconds)
        log(f"{self.phase}: {seconds:.0f} s")


def minutes(seconds: float) -> str:
    return f"{seconds / 60:.1f} min"


def times_markdown(timings: dict[str, float], title: str) -> str:
    """The build time table, and what PGO (and BOLT) add to a plain build."""
    lines = [f"### Build time: {title}", "", "| Phase | Wall time |", "|---|---:|"]
    for phase in (PLAIN_PHASE, *PGO_PHASES, BOLT_BUILD_PHASE, *BOLT_PHASES):
        if phase in timings:
            lines.append(f"| {phase} | {minutes(timings[phase])} |")
    plain = timings.get(PLAIN_PHASE)
    lines.append("")
    if not plain:
        lines.append("No plain build was timed: no increase to state.")
        return "\n".join(lines) + "\n"
    with_bolt = (*PGO_PHASES[:-1], BOLT_BUILD_PHASE, *BOLT_PHASES)
    for label, phases in (("PGO", PGO_PHASES), ("PGO and BOLT", with_bolt)):
        if not all(phase in timings for phase in phases):
            continue
        total = sum(timings[phase] for phase in phases)
        increase = total - plain
        lines.append(
            f"- With {label}: {minutes(total)} instead of {minutes(plain)}, "
            f"{minutes(increase)} more ({increase / plain * 100:+.0f} %)."
        )
    lines.append("")
    lines.append(
        "Every build here compiles from scratch, without a compiler cache; the "
        "release builds' cache shortens the plain and the instrumented build, "
        "not the optimized one (a cache does not see the profile)."
    )
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------

def cmake_configure_command(source: Path, build_dir: Path, mode: str, bolt: bool,
                            profile_dir: Path | None, extra: list[str]) -> list[str]:
    command = ["cmake", "-S", source, "-B", build_dir, f"-DLOGSQUIRL_PGO={mode}",
               f"-DLOGSQUIRL_BOLT={'ON' if bolt else 'OFF'}"]
    if profile_dir is not None:
        command.append(f"-DLOGSQUIRL_PGO_DIR={profile_dir}")
    return command + extra


def build_phase(mode: str, bolt: bool) -> str:
    if mode == "USE" and bolt:
        return BOLT_BUILD_PHASE
    return {"OFF": PLAIN_PHASE, "GENERATE": "instrumented build", "USE": "optimized build"}[mode]


def benchmark_targets(source: Path) -> list[str]:
    """The Catch2 benchmark executables tests/benchmarks/CMakeLists.txt adds
    (the same reading as build-benchmarks.sh)."""
    text = (source / "tests" / "benchmarks" / "CMakeLists.txt").read_text()
    return re.findall(r"^\s*add_executable\(\s*([A-Za-z0-9_]+)", text, flags=re.MULTILINE)


# The two diagnostics a USE build accepts as warnings (ADR 0019,
# cmake/ProfileGuidedOptimization.cmake). Clang has no group of its own for a
# profile's hash mismatch: it reports it under -Wbackend-plugin, which carries
# other diagnostics of the code generation as well. No compiler flag accepts the
# one without the others, so the build of this script holds the group to it.
HASH_MISMATCH = "profile hash mismatch"
MISSING_PROFILE = "missing profile"


def accepted_diagnostics(lines: list[str]) -> tuple[dict[str, int], list[str]]:
    """How often a USE build printed each accepted diagnostic, and the
    -Wbackend-plugin lines that are not the hash mismatch accepted."""
    counts = {HASH_MISMATCH: 0, MISSING_PROFILE: 0}
    unaccepted = []
    for line in lines:
        if "[-Wbackend-plugin]" in line:
            if "function control flow change detected (hash mismatch)" in line:
                counts[HASH_MISMATCH] += 1
            else:
                unaccepted.append(line)
        elif "[-Wmissing-profile]" in line:
            counts[MISSING_PROFILE] += 1
    return counts, unaccepted


def cmd_build(args) -> int:
    phase = args.phase or build_phase(args.mode, args.bolt)
    extra = shlex.split(args.cmake_args or "")
    targets = list(args.targets)
    if args.benchmarks:
        targets += benchmark_targets(args.source)
    build = ["cmake", "--build", args.build_dir, "--target", *targets]
    with Timer(args.timings, phase):
        run(cmake_configure_command(args.source, args.build_dir, args.mode, args.bolt,
                                    args.profile_dir, extra))
        if args.mode != "USE":
            run(build)
            return 0
        diagnostics = run_keeping_diagnostics(build)
    counts, unaccepted = accepted_diagnostics(diagnostics)
    log("Accepted as warnings (ADR 0019): " + ", ".join(f"{count} {name}" for name, count in counts.items()))
    if unaccepted:
        for line in unaccepted:
            log(line)
        log("error: the build printed -Wbackend-plugin diagnostics other than the profile hash mismatch "
            "that a USE build accepts (ADR 0019); they would fail any other build")
        return 1
    return 0


# ---------------------------------------------------------------------------
# train
# ---------------------------------------------------------------------------

def training_command(python: str, binary_dir: Path, runs: int, keyword: str | None) -> list[str]:
    """The e2e performance suite as training: every scenario, a few runs each.

    Nothing is compared or reported: an instrumented binary is slower than any
    baseline, and the point is only that the scenarios' code runs.
    """
    command = [python, "-m", "pytest", "-m", "performance", "-p", "no:cacheprovider",
               "--require-binaries", f"--binary-dir={binary_dir}",
               "--bench-runs", str(runs), "--bench-warmup", "0",
               "--no-baseline-compare", "--bench-report", "none"]
    if keyword:
        command += ["-k", keyword]
    return command


def cmd_train(args) -> int:
    env = os.environ.copy()
    if args.profile_dir is not None:
        # MSVC's instrumented binaries write their .pgc files here, beside the
        # .pgd the merge step reads (cmake/ProfileGuidedOptimization.cmake).
        args.profile_dir.mkdir(parents=True, exist_ok=True)
        env["VCPROFILE_PATH"] = str(args.profile_dir.resolve())
    command = training_command(args.python, args.binary_dir.resolve(), args.runs, args.keyword)
    with Timer(args.timings, args.phase):
        log("+ " + shlex.join(command))
        status = subprocess.run(command, cwd=args.source / "tests" / "e2e", env=env).returncode
    if status != 0:
        # An instrumented binary is slower; a scenario that fails on it still
        # trained the code it ran. Whether a usable profile came out of it is
        # for the merge to say.
        log(f"::warning::The training run ended with status {status}; the profile covers what ran")
    return 0


# ---------------------------------------------------------------------------
# merge
# ---------------------------------------------------------------------------

def llvm_profdata_command(system: str) -> list[str]:
    """llvm-profdata: Xcode's on macOS, else the first one on PATH."""
    if system == "Darwin":
        return ["xcrun", "llvm-profdata"]
    found = shutil.which("llvm-profdata")
    if found:
        return [found]
    for version in range(30, 13, -1):
        found = shutil.which(f"llvm-profdata-{version}")
        if found:
            return [found]
    raise SystemExit("error: no llvm-profdata on PATH")


@dataclass
class MergePlan:
    commands: list[list[str]]
    problem: str | None = None


def merge_plan(toolchain: str, profile_dir: Path, system: str, profdata: list[str] | None = None) -> MergePlan:
    """What merges the training's raw profile in profile_dir, or why it cannot."""
    if toolchain == "clang":
        raw = sorted(profile_dir.glob("*.profraw"))
        if not raw:
            return MergePlan([], f"no .profraw in {profile_dir}: did the training run the instrumented binaries?")
        tool = profdata or llvm_profdata_command(system)
        return MergePlan([[*tool, "merge", "-o", str(profile_dir / CLANG_PROFILE), *map(str, raw)]])
    if toolchain == "gcc":
        # Every run already added its counts to the .gcda files.
        if not any(profile_dir.rglob("*.gcda")):
            return MergePlan([], f"no .gcda in {profile_dir}: did the training run the instrumented binaries?")
        return MergePlan([])
    if toolchain == "msvc":
        pgds = sorted(profile_dir.glob("*.pgd"))
        if not pgds:
            return MergePlan([], f"no .pgd in {profile_dir}: was the build instrumented (LOGSQUIRL_PGO=GENERATE)?")
        trained = [pgd for pgd in pgds if any(profile_dir.glob(f"{pgd.stem}!*.pgc"))]
        if not any(pgd.stem == "logsquirl" for pgd in trained):
            return MergePlan([], f"no logsquirl!*.pgc in {profile_dir}: the training did not run logsquirl.exe "
                                 "with VCPROFILE_PATH there")
        # pgomgr merges every <name>!<n>.pgc beside the .pgd when given none.
        return MergePlan([["pgomgr", "/merge", str(pgd)] for pgd in trained])
    raise ValueError(f"unknown toolchain {toolchain}")


def cmd_merge(args) -> int:
    with Timer(args.timings, args.phase):
        plan = merge_plan(args.toolchain, args.profile_dir, platform.system())
        if plan.problem:
            log(f"::error::{plan.problem}")
            return 1
        for command in plan.commands:
            run(command)
    return 0


# ---------------------------------------------------------------------------
# BOLT
# ---------------------------------------------------------------------------

def find_tool(name: str) -> str:
    """name, or the highest name-<version> on PATH (Ubuntu's bolt-18 package)."""
    found = shutil.which(name)
    if found:
        return found
    for version in range(30, 15, -1):
        found = shutil.which(f"{name}-{version}")
        if found:
            return found
    raise SystemExit(f"error: no {name} on PATH")


BOLT_RUNTIME = "libbolt_rt_instr.a"


def bolt_runtime_library(bolt: str) -> Path | None:
    """The instrumentation runtime of an llvm-bolt, in the lib directory beside
    its real bin directory.

    llvm-bolt looks for it beside the path it was started by, so Ubuntu's
    /usr/bin/llvm-bolt-18, a link to /usr/lib/llvm-18/bin/llvm-bolt, looks in
    /usr/lib and finds nothing; the library is in /usr/lib/llvm-18/lib, from
    the libbolt-18-dev package."""
    library = Path(bolt).resolve().parent.parent / "lib" / BOLT_RUNTIME
    return library if library.is_file() else None


def bolt_instrument_commands(bolt: str, binary_dir: Path, out_dir: Path, profile_dir: Path,
                             runtime: Path) -> list[list[str]]:
    commands = []
    for name in BOLT_EXECUTABLES:
        commands.append([bolt, str(binary_dir / name), "-instrument",
                         f"--runtime-instrumentation-lib={runtime}",
                         f"--instrumentation-file={profile_dir / name}.fdata",
                         "--instrumentation-file-append-pid",
                         "-o", str(out_dir / name)])
    return commands


def bolt_profiles(profile_dir: Path, name: str) -> list[Path]:
    """The .fdata files the BOLT-instrumented runs of one executable wrote."""
    return sorted(profile_dir.glob(f"{name}.fdata.*"))


def bolt_optimize_command(bolt: str, binary: Path, out: Path, fdata: Path) -> list[str]:
    return [bolt, str(binary), "-o", str(out), f"-data={fdata}", *BOLT_OPTIONS]


# What the e2e suite runs from one directory: the executables BOLT rewrites,
# and the crash handler logsquirl starts from its own directory with Sentry.
SCENARIO_EXECUTABLES = (*BOLT_EXECUTABLES, "logsquirl_crashpad_handler")


def split_debug_commands(objcopy: str, binary: Path) -> list[list[str]]:
    """What src/app/CMakeLists.txt does after linking logsquirl without BOLT.

    The release ships the stripped executable and keeps <name>.debug for
    Sentry; a build for BOLT leaves both to this, after BOLT has run.
    """
    debug = binary.with_name(binary.name + ".debug")
    return [
        [objcopy, "--only-keep-debug", "--compress-debug-sections=zlib", str(binary), str(debug)],
        [objcopy, "--strip-debug", "--strip-unneeded", str(binary)],
        [objcopy, f"--add-gnu-debuglink={debug}", str(binary)],
    ]


def collect_entries(output: Path) -> list[str]:
    """What of a build output the measurement runs: the scenarios' executables
    (the app bundle on macOS), the micro-benchmarks, and on Windows the
    libraries beside them."""
    names = []
    for entry in sorted(output.iterdir()):
        stem = entry.name[:-4] if entry.name.endswith(".exe") else entry.name
        if entry.name == "logsquirl.app" and entry.is_dir():
            names.append(entry.name)
        elif not entry.is_file():
            continue
        elif stem in SCENARIO_EXECUTABLES or stem.endswith("_benchmark") or entry.suffix == ".dll":
            names.append(entry.name)
    return names


def cmd_collect(args) -> int:
    args.out_dir.mkdir(parents=True, exist_ok=True)
    names = collect_entries(args.output)
    if not any(name in ("logsquirl", "logsquirl.exe", "logsquirl.app") for name in names):
        log(f"::error::no logsquirl in {args.output}")
        return 1
    for name in names:
        source = args.output / name
        if source.is_dir():
            shutil.copytree(source, args.out_dir / name, symlinks=True, dirs_exist_ok=True)
        else:
            shutil.copy2(source, args.out_dir / name)
    log(f"collected {len(names)} entries of {args.output} into {args.out_dir}")
    return 0


def copy_binaries(source: Path, target: Path) -> list[str]:
    """Copy the scenarios' executables of one build output; returns the names copied.

    Only those: the micro-benchmarks are the same binaries with and without
    BOLT, so a BOLT side without them measures nothing twice.
    """
    target.mkdir(parents=True, exist_ok=True)
    copied = []
    for name in SCENARIO_EXECUTABLES:
        if (source / name).is_file():
            shutil.copy2(source / name, target / name)
            copied.append(name)
    return copied


def cmd_bolt_instrument(args) -> int:
    bolt = find_tool("llvm-bolt")
    runtime = bolt_runtime_library(bolt)
    if runtime is None:
        log(f"::error::no {BOLT_RUNTIME} beside {Path(bolt).resolve()}: llvm-bolt cannot instrument without it "
            "(Ubuntu: libbolt-18-dev)")
        return 1
    args.profile_dir.mkdir(parents=True, exist_ok=True)
    with Timer(args.timings, "BOLT instrumentation"):
        copy_binaries(args.binary_dir, args.out_dir)
        # Absolute: the instrumented executables write it from wherever the
        # training starts them.
        profile_dir = args.profile_dir.resolve()
        for command in bolt_instrument_commands(bolt, args.binary_dir, args.out_dir, profile_dir, runtime):
            run(command)
    return 0


def cmd_bolt_optimize(args) -> int:
    bolt = find_tool("llvm-bolt")
    merge_fdata = find_tool("merge-fdata")
    with Timer(args.timings, "BOLT optimization"):
        copy_binaries(args.binary_dir, args.out_dir)
        for name in BOLT_EXECUTABLES:
            parts = bolt_profiles(args.profile_dir, name)
            if not parts:
                log(f"::error::no {name}.fdata.* in {args.profile_dir}: the BOLT training did not run {name}")
                return 1
            merged = args.profile_dir / f"{name}.fdata"
            log(f"+ {merge_fdata} {len(parts)} profiles > {merged}")
            with merged.open("wb") as out:
                subprocess.run([merge_fdata, *map(str, parts)], stdout=out, check=True)
            run(bolt_optimize_command(bolt, args.binary_dir / name, args.out_dir / name, merged))
        if args.split_debug:
            objcopy = find_tool("objcopy")
            for command in split_debug_commands(objcopy, args.out_dir / "logsquirl"):
                run(command)
    return 0


# ---------------------------------------------------------------------------
# measure
# ---------------------------------------------------------------------------

def benchmark_binaries(binary_dir: Path) -> list[str]:
    suffix = ".exe" if platform.system() == "Windows" else ""
    return sorted(path.name for path in binary_dir.glob(f"*_benchmark{suffix}") if path.is_file())


def measure_plan(sides: dict[str, Path]) -> list[tuple[str, str]]:
    """(binary, side) in the order they run: each binary on every side before the next.

    Interleaved rather than one side after the other, so a runner that gets
    slower over the job shifts every side alike instead of one of them.
    """
    names = sorted({name for directory in sides.values() for name in benchmark_binaries(directory)})
    plan = []
    for name in names:
        for side, directory in sides.items():
            if (directory / name).is_file():
                plan.append((name, side))
    return plan


def parse_sides(values: list[str]) -> dict[str, Path]:
    sides = {}
    for value in values:
        name, separator, directory = value.partition("=")
        if not separator or not name or not directory:
            raise SystemExit(f"error: --side wants NAME=DIRECTORY, not {value!r}")
        if name in sides:
            raise SystemExit(f"error: side {name!r} given twice")
        sides[name] = Path(directory)
    return sides


def cmd_measure(args) -> int:
    sides = parse_sides(args.side)
    env = os.environ.copy()
    env["LOGSQUIRL_BENCHMARK_LOG_FILE_MB"] = str(args.log_file_mb)
    status = 0
    for side in sides:
        (args.results / side / "catch2").mkdir(parents=True, exist_ok=True)
        (args.results / side / "e2e").mkdir(parents=True, exist_ok=True)

    if not args.skip_catch2:
        for name, side in measure_plan(sides):
            out = args.results / side / "catch2" / (Path(name).stem + ".xml")
            command = [str(sides[side].resolve() / name), "--reporter", "xml", "--out", str(out),
                       "--benchmark-samples", str(args.benchmark_samples)]
            log(f"::group::{name} ({side})")
            try:
                result = subprocess.run(command, env=env, timeout=args.timeout_minutes * 60)
                if result.returncode != 0:
                    log(f"::error::{name} failed on the {side} side")
                    status = 1
            except subprocess.TimeoutExpired:
                log(f"::error::{name} ran longer than {args.timeout_minutes} minutes on the {side} side and was stopped")
                status = 1
            log("::endgroup::")

    e2e = args.source / "tests" / "e2e"
    for side, directory in sides.items():
        log(f"::group::e2e performance suite ({side})")
        report = e2e / "benchmark_report.json"
        report.unlink(missing_ok=True)
        command = [args.python, "-m", "pytest", "-m", "performance", "-p", "no:cacheprovider",
                   "--require-binaries", f"--binary-dir={directory.resolve()}",
                   "--bench-runs", str(args.e2e_runs), "--bench-report", "json",
                   "--no-baseline-compare"]
        if args.keyword:
            command += ["-k", args.keyword]
        log("+ " + shlex.join(command))
        if subprocess.run(command, cwd=e2e, env=env).returncode != 0:
            log(f"::error::The e2e performance suite failed on the {side} side")
            status = 1
        if report.exists():
            shutil.move(str(report), args.results / side / "e2e" / "benchmark_report.json")
        log("::endgroup::")
    return status


# ---------------------------------------------------------------------------
# times
# ---------------------------------------------------------------------------

def merged_timings(paths: list[Path]) -> dict[str, float]:
    """The phases of several timings files, as if one run had recorded them all."""
    timings: dict[str, float] = {}
    for path in paths:
        if path.exists():
            for phase, seconds in json.loads(path.read_text()).items():
                timings[phase] = timings.get(phase, 0.0) + seconds
    return timings


def cmd_times(args) -> int:
    timings = merged_timings([args.timings, *args.also])
    text = times_markdown(timings, args.title)
    if args.markdown:
        args.markdown.write_text(text)
    print(text)
    return 0


# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--timings", type=Path, help="JSON file each step adds its wall time to")
    parser.add_argument("--source", type=Path, default=Path.cwd(), help="the source tree (default: .)")
    sub = parser.add_subparsers(dest="command", required=True)

    build = sub.add_parser("build", help="configure and build one build directory")
    build.add_argument("--build-dir", type=Path, required=True)
    build.add_argument("--mode", choices=("OFF", "GENERATE", "USE"), required=True)
    build.add_argument("--bolt", action="store_true", help="link for BOLT (Linux)")
    build.add_argument("--profile-dir", type=Path, help="LOGSQUIRL_PGO_DIR (default: <build dir>/pgo-profile)")
    build.add_argument("--cmake-args", help="further configure arguments, one shell-quoted string")
    build.add_argument("--phase", help="name of the time recorded (default: after the mode)")
    build.add_argument("--targets", nargs="+", default=["logsquirl", "logsquirl_grep"])
    build.add_argument("--benchmarks", action="store_true", help="also build every Catch2 benchmark")
    build.set_defaults(func=cmd_build)

    train = sub.add_parser("train", help="run the training workload")
    train.add_argument("--binary-dir", type=Path, required=True)
    train.add_argument("--profile-dir", type=Path, help="where MSVC's binaries write their .pgc files")
    train.add_argument("--runs", type=int, default=1, help="runs per scenario (default 1)")
    train.add_argument("-k", dest="keyword", help="pytest -k expression to train on part of the suite")
    train.add_argument("--phase", default="training")
    train.add_argument("--python", default=sys.executable)
    train.set_defaults(func=cmd_train)

    merge = sub.add_parser("merge", help="merge the training's raw profile")
    merge.add_argument("--toolchain", choices=TOOLCHAINS, required=True)
    merge.add_argument("--profile-dir", type=Path, required=True)
    merge.add_argument("--phase", default="merge")
    merge.set_defaults(func=cmd_merge)

    instrument = sub.add_parser("bolt-instrument", help="instrument the executables with llvm-bolt")
    instrument.add_argument("--binary-dir", type=Path, required=True)
    instrument.add_argument("--out-dir", type=Path, required=True)
    instrument.add_argument("--profile-dir", type=Path, required=True)
    instrument.set_defaults(func=cmd_bolt_instrument)

    optimize = sub.add_parser("bolt-optimize", help="rewrite the executables from their BOLT profile")
    optimize.add_argument("--binary-dir", type=Path, required=True)
    optimize.add_argument("--out-dir", type=Path, required=True)
    optimize.add_argument("--profile-dir", type=Path, required=True)
    optimize.add_argument("--split-debug", action="store_true",
                          help="then split off logsquirl.debug and strip logsquirl, as a build without BOLT does")
    optimize.set_defaults(func=cmd_bolt_optimize)

    collect = sub.add_parser("collect", help="copy what the measurement runs out of a build output")
    collect.add_argument("--output", type=Path, required=True, help="the build's output directory")
    collect.add_argument("--out-dir", type=Path, required=True)
    collect.set_defaults(func=cmd_collect)

    measure = sub.add_parser("measure", help="run the benchmarks on several sets of binaries")
    measure.add_argument("--side", action="append", required=True, help="NAME=BINARY_DIR, once per side")
    measure.add_argument("--results", type=Path, required=True)
    measure.add_argument("--benchmark-samples", type=int, default=20)
    measure.add_argument("--log-file-mb", type=int, default=256)
    measure.add_argument("--e2e-runs", type=int, default=21)
    measure.add_argument("--timeout-minutes", type=int, default=30)
    measure.add_argument("--skip-catch2", action="store_true")
    measure.add_argument("-k", dest="keyword", help="pytest -k expression for the e2e suite")
    measure.add_argument("--python", default=sys.executable)
    measure.set_defaults(func=cmd_measure)

    times = sub.add_parser("times", help="the build time table")
    times.add_argument("--title", default=platform.system())
    times.add_argument("--markdown", type=Path)
    times.add_argument("--also", type=Path, action="append", default=[],
                       help="another timings file to count in, e.g. the plain build's from another job")
    times.set_defaults(func=cmd_times)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.command == "times" and args.timings is None:
        parser.error("times needs --timings")
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
