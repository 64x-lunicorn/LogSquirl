# LogSquirl E2E Tests

End-to-end integration and performance regression tests for LogSquirl.

## Prerequisites

- Python ≥ 3.10
- LogSquirl built locally (binaries in `build/output/`)
- Test data files present in `test_data/`

## Setup

```bash
cd tests/e2e
pip install -e .
```

This installs `pytest` (≥ 7.0) as the only dependency.

## Running Tests

```bash
# Run all tests
pytest -v --binary-dir=../../build/output

# Run only grep tests
pytest -v --binary-dir=../../build/output -k "grep"

# Run only GUI smoke tests
pytest -v --binary-dir=../../build/output -k "gui"

# Run only performance benchmarks
pytest -v --binary-dir=../../build/output -m performance

# Skip performance tests
pytest -v --binary-dir=../../build/output -m "not performance"

# Skip slow tests (GUI startup tests with sleep)
pytest -v --binary-dir=../../build/output -m "not slow"
```

If `--binary-dir` is omitted, the tests look for binaries in `../../build/output` relative to
the repository root.

## Performance Baselines

Performance tests compare measured execution times against `baseline.json`. The "Safari Rule"
applies: **LogSquirl must never get slower.** A 5% tolerance is allowed.

### First Run (Establishing Baselines)

On the first run all baseline values are `null`, so performance tests pass unconditionally.
Create an initial baseline:

```bash
pytest -v -m performance --update-baseline
```

This writes measured timings into `baseline.json`. Commit the updated file.

### Updating Baselines

After intentional performance changes (optimizations), update the baseline:

```bash
pytest -v -m performance --update-baseline
```

Review the diff in `baseline.json` before committing — values should only decrease.

### Benchmarks

| Name                       | Description                           |
|----------------------------|---------------------------------------|
| `grep_search_1mb_simple`   | Simple pattern search in 1 MB file    |
| `grep_search_1mb_regex`    | Complex regex search in 1 MB file     |
| `grep_search_1_5mb_simple` | Simple pattern search in 1.5 MB file  |
| `grep_search_utf16_1mb`    | Search in 1 MB UTF-16LE file          |
| `gui_startup_version`      | GUI process startup (--version)       |
| `gui_load_1mb`             | GUI load and display 1 MB file        |

## Test Structure

```
tests/e2e/
├── conftest.py              # Fixtures, helpers, CLI options
├── baseline.json            # Performance baseline data
├── pyproject.toml           # Python project config
├── README.md                # This file
├── test_grep_search.py      # Basic search functionality (7 tests)
├── test_grep_encoding.py    # Encoding handling (9 tests)
├── test_grep_edge_cases.py  # Edge cases and error handling (10 tests)
├── test_gui_smoke.py        # GUI smoke tests (6 tests)
└── test_performance.py      # Performance regression tests (6 tests)
```

Total: **40 tests** (34 functional + 6 performance)

## Adding New Tests

1. Create a test function in the appropriate file (or add a new `test_*.py` file).
2. Use fixtures from `conftest.py`: `logsquirl_grep_binary`, `logsquirl_binary`, `test_data_dir`.
3. For grep tests, use `run_grep()` and `grep_output_lines()` to filter internal log messages.
4. For GUI tests, use `run_gui()` for short-lived commands or `subprocess.Popen` for startup tests.
5. For performance tests, use `measure_execution()` and `assert_performance()`, add a slot in
   `baseline.json`, and mark the test with `@pytest.mark.performance`.

## Developer Workflow

Every code change must pass the full E2E suite **before** opening a pull request.

### Before you start coding

```bash
# Make sure the test suite is green on the current branch
cd tests/e2e
source .venv/bin/activate
pytest -v --binary-dir=../../build/output
```

### After making changes

```bash
# 1. Rebuild the project
cd <repo_root>
cmake --build build --parallel

# 2. Run C++ unit tests
cd build && ctest --verbose --output-on-failure && cd ..

# 3. Run the full E2E suite
cd tests/e2e
source .venv/bin/activate
pytest -v --binary-dir=../../build/output
```

- **All 40 tests must pass** before you push.
- If performance tests fail, your change introduced a regression. Profile and fix it.
- If you intentionally improved performance, update the baseline and commit it with your PR:
  ```bash
  pytest -m performance --update-baseline
  git add baseline.json
  ```
- If you added a new binary feature, add a corresponding E2E test in the appropriate file.

### Quick reference

| What you changed       | What to run                          |
|------------------------|--------------------------------------|
| Grep/search logic      | `pytest -k grep`                     |
| Encoding handling      | `pytest -k encoding`                 |
| GUI / UI code          | `pytest -k gui`                      |
| Performance-sensitive   | `pytest -m performance`              |
| Everything             | `pytest -v`                          |

## CI Integration

These tests can be added to the CI pipeline by adding a step after the build:

```yaml
- name: Run E2E tests
  run: |
    pip install -e tests/e2e
    pytest tests/e2e -v --binary-dir=build/output
```
