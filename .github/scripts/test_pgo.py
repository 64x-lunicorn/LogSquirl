"""Tests for pgo.py (#682): the build time a release with PGO adds, what merges
a training's profile per compiler, what BOLT runs, and in which order the
measurement runs the sides."""

from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path

import pytest

_SPEC = importlib.util.spec_from_file_location("pgo", Path(__file__).with_name("pgo.py"))
pgo = importlib.util.module_from_spec(_SPEC)
sys.modules["pgo"] = pgo
_SPEC.loader.exec_module(pgo)


# --- timings ---------------------------------------------------------------

def test_a_phase_run_in_parts_adds_up(tmp_path):
    timings = tmp_path / "timings.json"
    pgo.record_time(timings, "training", 60.04)
    pgo.record_time(timings, "training", 30.0)
    pgo.record_time(timings, "plain build", 600.0)
    assert json.loads(timings.read_text()) == {"plain build": 600.0, "training": 90.0}


def test_a_failed_phase_is_not_recorded(tmp_path):
    timings = tmp_path / "timings.json"
    with pgo.Timer(timings, "plain build"):
        pass
    with pytest.raises(RuntimeError):
        with pgo.Timer(timings, "optimized build"):
            raise RuntimeError("the build failed")
    assert list(json.loads(timings.read_text())) == ["plain build"]


def test_no_timings_file_records_nothing():
    pgo.record_time(None, "training", 1.0)


def test_the_increase_is_the_pgo_phases_against_the_one_plain_build():
    text = pgo.times_markdown({
        "plain build": 600.0,
        "instrumented build": 660.0,
        "training": 300.0,
        "merge": 6.0,
        "optimized build": 630.0,
    }, "macOS")
    assert "| plain build | 10.0 min |" in text
    # 660 + 300 + 6 + 630 = 1596 s against 600 s.
    assert "With PGO: 26.6 min instead of 10.0 min, 16.6 min more (+166 %)." in text
    assert "PGO and BOLT" not in text


def test_bolt_takes_the_build_for_bolt_and_adds_its_phases():
    text = pgo.times_markdown({
        "plain build": 600.0,
        "instrumented build": 600.0,
        "training": 300.0,
        "merge": 0.0,
        "optimized build": 600.0,
        "optimized build for BOLT": 660.0,
        "BOLT instrumentation": 30.0,
        "BOLT training": 240.0,
        "BOLT optimization": 30.0,
    }, "Linux")
    assert "With PGO: 25.0 min instead of 10.0 min, 15.0 min more (+150 %)." in text
    # 600 + 300 + 0 + 660 + 30 + 240 + 30: the build for BOLT replaces the
    # optimized build, it does not come on top of it.
    assert "With PGO and BOLT: 31.0 min instead of 10.0 min, 21.0 min more (+210 %)." in text


def test_the_phase_follows_mode_and_bolt():
    assert pgo.build_phase("OFF", False) == "plain build"
    assert pgo.build_phase("GENERATE", True) == "instrumented build"
    assert pgo.build_phase("USE", False) == "optimized build"
    assert pgo.build_phase("USE", True) == "optimized build for BOLT"


def test_without_a_plain_build_no_increase_is_claimed():
    text = pgo.times_markdown({"instrumented build": 600.0}, "Windows")
    assert "no increase to state" in text
    assert "With PGO" not in text


def test_an_incomplete_pgo_run_states_no_total():
    text = pgo.times_markdown({"plain build": 600.0, "instrumented build": 600.0}, "Linux")
    assert "With PGO" not in text


def test_the_timings_of_two_jobs_are_read_as_one(tmp_path):
    plain = tmp_path / "plain.json"
    plain.write_text(json.dumps({"plain build": 600.0}))
    built = tmp_path / "pgo.json"
    built.write_text(json.dumps({"instrumented build": 600.0, "training": 300.0, "merge": 1.0,
                                 "optimized build": 600.0}))
    assert pgo.merged_timings([built, plain, tmp_path / "missing.json"]) == {
        "plain build": 600.0, "instrumented build": 600.0, "training": 300.0, "merge": 1.0,
        "optimized build": 600.0}
    markdown = tmp_path / "times.md"
    assert pgo.main(["--timings", str(built), "times", "--also", str(plain), "--title", "Linux",
                     "--markdown", str(markdown)]) == 0
    assert "With PGO: 25.0 min instead of 10.0 min" in markdown.read_text()


# --- build -----------------------------------------------------------------

def test_the_configure_command_names_mode_bolt_and_profile(tmp_path):
    command = pgo.cmake_configure_command(Path("src"), Path("b"), "USE", True, Path("/p"), ["-G", "Ninja"])
    assert command == ["cmake", "-S", Path("src"), "-B", Path("b"), "-DLOGSQUIRL_PGO=USE",
                       "-DLOGSQUIRL_BOLT=ON", "-DLOGSQUIRL_PGO_DIR=/p", "-G", "Ninja"]
    command = pgo.cmake_configure_command(Path("src"), Path("b"), "OFF", False, None, [])
    assert "-DLOGSQUIRL_BOLT=OFF" in command
    assert not any(str(part).startswith("-DLOGSQUIRL_PGO_DIR") for part in command)


def test_the_benchmark_targets_are_read_from_their_cmake_file(tmp_path):
    (tmp_path / "tests" / "benchmarks").mkdir(parents=True)
    (tmp_path / "tests" / "benchmarks" / "CMakeLists.txt").write_text(
        "add_executable(a_benchmark a.cpp)\n  add_executable( b_benchmark\n b.cpp)\n"
        "# add_executable(commented_out x.cpp)\ntarget_link_libraries(a_benchmark x)\n")
    assert pgo.benchmark_targets(tmp_path) == ["a_benchmark", "b_benchmark"]
    # And the real file has some.
    assert pgo.benchmark_targets(Path(__file__).parents[2])


def test_a_use_build_accepts_the_hash_mismatch_and_the_missing_profile_only():
    lines = [
        "warning: main.cpp: function control flow change detected (hash mismatch) main "
        "Hash = 942389666994816328 up to 101 count discarded [-Wbackend-plugin]",
        "a.cpp:3:5: warning: 'f' profile count data file not found [-Wmissing-profile]",
        "b.cpp:7:1: warning: 'g' profile count data file not found [-Wmissing-profile]",
        "c.cpp:1:1: warning: unused variable 'x' [-Wunused-variable]",
    ]
    counts, unaccepted = pgo.accepted_diagnostics(lines)
    assert counts == {pgo.HASH_MISMATCH: 1, pgo.MISSING_PROFILE: 2}
    assert unaccepted == []


def test_any_other_backend_plugin_diagnostic_fails_a_use_build():
    other = "warning: x.cpp: stack frame size (90000) exceeds limit (80000) in 'f' [-Wbackend-plugin]"
    counts, unaccepted = pgo.accepted_diagnostics([other])
    assert counts == {pgo.HASH_MISMATCH: 0, pgo.MISSING_PROFILE: 0}
    assert unaccepted == [other]


def test_the_build_output_passes_through_and_its_diagnostics_are_kept(capsys):
    script = ("print('[1/2] Building a.cpp'); "
              "print('warning: x [-Wbackend-plugin]'); print('[2/2] Linking')")
    kept = pgo.run_keeping_diagnostics([sys.executable, "-c", script])
    assert kept == ["warning: x [-Wbackend-plugin]"]
    assert "[2/2] Linking" in capsys.readouterr().out


def test_a_failing_build_still_fails():
    with pytest.raises(pgo.subprocess.CalledProcessError):
        pgo.run_keeping_diagnostics([sys.executable, "-c", "raise SystemExit(2)"])


# --- train -----------------------------------------------------------------

def test_the_training_runs_every_scenario_without_comparing():
    command = pgo.training_command("python3", Path("/bin-dir"), 2, None)
    assert command[:5] == ["python3", "-m", "pytest", "-m", "performance"]
    assert "--binary-dir=/bin-dir" in command
    assert command[command.index("--bench-runs") + 1] == "2"
    assert command[command.index("--bench-warmup") + 1] == "0"
    assert "--no-baseline-compare" in command
    assert "--update-baseline" not in command
    assert "-k" not in command
    assert pgo.training_command("python3", Path("/b"), 1, "grep")[-2:] == ["-k", "grep"]


# --- merge -----------------------------------------------------------------

def test_clang_merges_every_raw_profile_into_the_one_file_cmake_reads(tmp_path):
    (tmp_path / "default_1.profraw").write_bytes(b"")
    (tmp_path / "default_2.profraw").write_bytes(b"")
    plan = pgo.merge_plan("clang", tmp_path, "Linux", profdata=["llvm-profdata-18"])
    assert plan.problem is None
    assert plan.commands == [["llvm-profdata-18", "merge", "-o", str(tmp_path / "logsquirl.profdata"),
                              str(tmp_path / "default_1.profraw"), str(tmp_path / "default_2.profraw")]]


def test_the_profile_file_is_the_one_the_cmake_module_names():
    module = (Path(__file__).parents[2] / "cmake" / "ProfileGuidedOptimization.cmake").read_text()
    assert f"set(LOGSQUIRL_PGO_CLANG_PROFILE {pgo.CLANG_PROFILE})" in module


def test_macos_uses_xcodes_llvm_profdata():
    assert pgo.llvm_profdata_command("Darwin") == ["xcrun", "llvm-profdata"]


def test_clang_without_raw_profiles_is_a_problem(tmp_path):
    plan = pgo.merge_plan("clang", tmp_path, "Linux", profdata=["llvm-profdata"])
    assert plan.commands == []
    assert "no .profraw" in plan.problem


def test_gcc_needs_no_merge_but_a_gcda(tmp_path):
    assert "no .gcda" in pgo.merge_plan("gcc", tmp_path, "Linux").problem
    nested = tmp_path / "#usr#local#build_pgo#src"
    nested.mkdir()
    (nested / "x.cpp.gcda").write_bytes(b"")
    plan = pgo.merge_plan("gcc", tmp_path, "Linux")
    assert plan.problem is None and plan.commands == []


def test_msvc_merges_each_trained_pgd(tmp_path):
    for name in ("logsquirl.pgd", "logsquirl!1.pgc", "logsquirl!2.pgc",
                 "logsquirl_grep.pgd", "logsquirl_grep!1.pgc", "logsquirl_tests.pgd"):
        (tmp_path / name).write_bytes(b"")
    plan = pgo.merge_plan("msvc", tmp_path, "Windows")
    assert plan.problem is None
    # The test binary never ran: nothing to merge for it.
    assert plan.commands == [["pgomgr", "/merge", str(tmp_path / "logsquirl.pgd")],
                             ["pgomgr", "/merge", str(tmp_path / "logsquirl_grep.pgd")]]


def test_msvc_without_a_trained_logsquirl_is_a_problem(tmp_path):
    assert "no .pgd" in pgo.merge_plan("msvc", tmp_path, "Windows").problem
    (tmp_path / "logsquirl.pgd").write_bytes(b"")
    (tmp_path / "logsquirl_grep.pgd").write_bytes(b"")
    (tmp_path / "logsquirl_grep!1.pgc").write_bytes(b"")
    assert "VCPROFILE_PATH" in pgo.merge_plan("msvc", tmp_path, "Windows").problem


def test_an_unknown_toolchain_is_refused(tmp_path):
    with pytest.raises(ValueError):
        pgo.merge_plan("icc", tmp_path, "Linux")


# --- BOLT ------------------------------------------------------------------

def test_bolt_instruments_the_scenario_executables_one_profile_per_process():
    commands = pgo.bolt_instrument_commands("llvm-bolt-18", Path("/in"), Path("/out"), Path("/prof"))
    assert [command[1] for command in commands] == ["/in/logsquirl", "/in/logsquirl_grep"]
    first = commands[0]
    assert first[0] == "llvm-bolt-18"
    assert "-instrument" in first
    assert "--instrumentation-file=/prof/logsquirl.fdata" in first
    # Several processes run during the training; each writes its own file.
    assert "--instrumentation-file-append-pid" in first
    assert first[-2:] == ["-o", "/out/logsquirl"]


def test_bolt_optimizes_from_the_merged_profile():
    command = pgo.bolt_optimize_command("llvm-bolt", Path("/in/logsquirl"), Path("/out/logsquirl"),
                                        Path("/prof/logsquirl.fdata"))
    assert command[:4] == ["llvm-bolt", "/in/logsquirl", "-o", "/out/logsquirl"]
    assert "-data=/prof/logsquirl.fdata" in command
    assert "-reorder-blocks=ext-tsp" in command


def test_bolt_profiles_are_the_per_process_files(tmp_path):
    for name in ("logsquirl.fdata.101", "logsquirl.fdata.102", "logsquirl_grep.fdata.7", "logsquirl.fdata"):
        (tmp_path / name).write_bytes(b"")
    assert [path.name for path in pgo.bolt_profiles(tmp_path, "logsquirl")] == [
        "logsquirl.fdata.101", "logsquirl.fdata.102"]


def test_after_bolt_the_debug_information_is_split_as_cmake_does(tmp_path):
    binary = tmp_path / "logsquirl"
    debug = str(tmp_path / "logsquirl.debug")
    assert pgo.split_debug_commands("objcopy", binary) == [
        ["objcopy", "--only-keep-debug", "--compress-debug-sections=zlib", str(binary), debug],
        ["objcopy", "--strip-debug", "--strip-unneeded", str(binary)],
        ["objcopy", f"--add-gnu-debuglink={debug}", str(binary)],
    ]
    # The same three steps src/app/CMakeLists.txt runs without BOLT.
    cmake = (Path(__file__).parents[2] / "src" / "app" / "CMakeLists.txt").read_text()
    assert "--only-keep-debug --compress-debug-sections=zlib $<TARGET_FILE:logsquirl>" in cmake
    assert "--strip-debug --strip-unneeded $<TARGET_FILE:logsquirl>" in cmake


def test_collect_takes_the_scenarios_benchmarks_and_libraries(tmp_path):
    output = tmp_path / "output"
    (output / "logsquirl.app" / "Contents" / "MacOS").mkdir(parents=True)
    for name in ("logsquirl_grep", "logsquirl_logdata_benchmark", "logsquirl_tests", "libCatch2.a",
                 "logsquirl.exe", "logsquirl_grep.exe", "x_benchmark.exe", "Qt6Core.dll", "logsquirl.pdb",
                 "logsquirl_crashpad_handler"):
        (output / name).write_bytes(b"")
    assert pgo.collect_entries(output) == [
        "Qt6Core.dll", "logsquirl.app", "logsquirl.exe", "logsquirl_crashpad_handler", "logsquirl_grep",
        "logsquirl_grep.exe", "logsquirl_logdata_benchmark", "x_benchmark.exe",
    ]


def test_collect_copies_the_app_bundle_with_its_links(tmp_path):
    output = tmp_path / "output"
    macos = output / "logsquirl.app" / "Contents" / "MacOS"
    macos.mkdir(parents=True)
    (macos / "logsquirl").write_bytes(b"x")
    (output / "logsquirl.app" / "Contents" / "Current").symlink_to("MacOS")
    assert pgo.main(["collect", "--output", str(output), "--out-dir", str(tmp_path / "bin")]) == 0
    copied = tmp_path / "bin" / "logsquirl.app" / "Contents"
    assert (copied / "MacOS" / "logsquirl").read_bytes() == b"x"
    assert (copied / "Current").is_symlink()


def test_collect_without_logsquirl_fails(tmp_path):
    output = tmp_path / "output"
    output.mkdir()
    (output / "logsquirl_grep").write_bytes(b"")
    assert pgo.main(["collect", "--output", str(output), "--out-dir", str(tmp_path / "bin")]) == 1


def test_only_the_scenario_executables_are_copied(tmp_path):
    source = tmp_path / "output"
    source.mkdir()
    for name in ("logsquirl", "logsquirl_grep", "logsquirl_logdata_benchmark", "logsquirl_tests", "libx.a"):
        (source / name).write_bytes(b"x")
    copied = pgo.copy_binaries(source, tmp_path / "bolt")
    assert copied == ["logsquirl", "logsquirl_grep"]
    assert sorted(path.name for path in (tmp_path / "bolt").iterdir()) == ["logsquirl", "logsquirl_grep"]


# --- measure ---------------------------------------------------------------

def _bin(directory: Path, *names: str) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    for name in names:
        (directory / name).write_bytes(b"")
    return directory


def test_each_benchmark_runs_on_every_side_before_the_next(tmp_path, monkeypatch):
    monkeypatch.setattr(pgo.platform, "system", lambda: "Linux")
    sides = {
        "plain": _bin(tmp_path / "plain", "b_benchmark", "a_benchmark", "logsquirl"),
        "pgo": _bin(tmp_path / "pgo", "a_benchmark", "b_benchmark"),
        # The BOLT side has the scenarios' executables only.
        "bolt": _bin(tmp_path / "bolt", "logsquirl"),
    }
    assert pgo.measure_plan(sides) == [
        ("a_benchmark", "plain"), ("a_benchmark", "pgo"),
        ("b_benchmark", "plain"), ("b_benchmark", "pgo"),
    ]


def test_sides_are_named_and_unique():
    assert pgo.parse_sides(["plain=out/a", "pgo=out/b"]) == {"plain": Path("out/a"), "pgo": Path("out/b")}
    with pytest.raises(SystemExit):
        pgo.parse_sides(["plain"])
    with pytest.raises(SystemExit):
        pgo.parse_sides(["plain=a", "plain=b"])


def test_times_needs_a_timings_file():
    with pytest.raises(SystemExit):
        pgo.main(["times"])
