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

### Benchmark Configuration

| Option              | Default | Description                                      |
|---------------------|---------|--------------------------------------------------|
| `--bench-runs`      | 21      | Number of measured runs per benchmark             |
| `--bench-warmup`    | 3       | Number of warmup runs (discarded)                 |
| `--bench-report`    | markdown| Report format: `markdown`, `json`, or `none`     |
| `--update-baseline` | off     | Write measured values to baseline.json            |

Example with custom run count:

```bash
pytest -v -m performance --bench-runs=21 --bench-warmup=3 --bench-report=markdown
```

### Statistical Methodology

- **Outlier filtering:** IQR × 1.5 — runs outside [Q1 − 1.5×IQR, Q3 + 1.5×IQR] are discarded
- **Regression detection:** Median must exceed baseline + tolerance (5%) AND Welch's t-test
  must confirm statistical significance (p < 0.05). Both conditions are required.
- **Stability monitoring:** Coefficient of variation (CV%) is computed for each benchmark.
  CV > 15% triggers a warning that the result may be unreliable.

### Benchmark Report

After each performance run a `benchmark_report.md` (or `.json`) is generated in `tests/e2e/`.
It includes:

- **Summary table:** median, mean ± std, CV%, percentiles, delta vs baseline
- **Throughput:** MB/s and lines/sec for file-based benchmarks
- **Stability analysis:** CV%, IQR, outlier count, verdict (Stable/Acceptable/Noisy/Unstable)
- **Environment:** OS, CPU, cores, RAM, Python version

### Large File Benchmarks

Some benchmarks use 10-100 MB files that must be generated before first use:

```bash
python tests/e2e/generate_test_data.py
```

These files are in `.gitignore` and not committed. Tests that need them will skip
gracefully if the files are missing. To include large file benchmarks:

```bash
pytest -v -m performance                        # all benchmarks (including large if available)
pytest -v -m "performance and not slow"          # quick benchmarks only (1-1.5 MB)
```

### Benchmarks

| Name                              | Description                                    | File Size |
|-----------------------------------|------------------------------------------------|-----------|
| `grep_search_1mb_simple`          | Simple pattern search                          | 1 MB      |
| `grep_search_1mb_regex`           | Complex regex search                           | 1 MB      |
| `grep_search_1_5mb_simple`        | Simple pattern search                          | 1.5 MB    |
| `grep_search_utf16_1mb`           | UTF-16LE encoding overhead                     | 1 MB      |
| `grep_search_1mb_no_match`        | No-match scan-only overhead                    | 1 MB      |
| `grep_search_1mb_alternation`     | Regex alternation (ERROR\|WARNING\|CRITICAL)   | 1 MB      |
| `grep_search_1mb_case_insensitive`| Case-insensitive regex                         | 1 MB      |
| `grep_search_10mb_simple`         | Simple pattern (real throughput)                | 10 MB     |
| `grep_search_10mb_regex`          | Complex regex at scale                         | 10 MB     |
| `grep_search_50mb_simple`         | L2/L3 cache boundary test                      | 50 MB     |
| `grep_search_100mb_simple`        | Stress test                                    | 100 MB    |
| `grep_search_utf16_10mb`          | UTF-16LE encoding at scale                     | 10 MB     |
| `gui_startup_version`             | GUI process startup (--version)                | —         |
| `gui_load_1mb`                    | GUI load and display file                      | 1 MB      |

## Test Structure

```
tests/e2e/
├── conftest.py              # Fixtures, helpers, statistics, CLI options, report generation
├── baseline.json            # Performance baseline data (schema v2)
├── generate_test_data.py    # Large test file generator (10/50/100 MB)
├── pyproject.toml           # Python project config
├── README.md                # This file
├── test_grep_search.py      # Basic search functionality (7 tests)
├── test_grep_encoding.py    # Encoding handling (9 tests)
├── test_grep_edge_cases.py  # Edge cases and error handling (10 tests)
├── test_gui_smoke.py        # GUI smoke tests (6 tests)
├── test_heavy_tabs.py       # Heavy tabs crash test
└── test_performance.py      # Performance regression tests (14 benchmarks)
```

## Adding New Tests

1. Create a test function in the appropriate file (or add a new `test_*.py` file).
2. Use fixtures from `conftest.py`: `logsquirl_grep_binary`, `logsquirl_binary`, `test_data_dir`.
3. For grep tests, use `run_grep()` and `grep_output_lines()` to filter internal log messages.
4. For GUI tests, use `run_gui()` for short-lived commands or `subprocess.Popen` for startup tests.
5. For performance tests, use `measure_execution()` and `assert_performance()`, add a slot in
   `baseline.json`, and mark the test with `@pytest.mark.performance`.
6. For large-file benchmarks, also add `@pytest.mark.slow` and use `_generated_file()` helper.

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

- **All tests must pass** before you push.
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
| Quick perf check       | `pytest -m "performance and not slow"` |
| Everything             | `pytest -v`                          |

## CI Integration

These tests can be added to the CI pipeline by adding a step after the build:

```yaml
- name: Run E2E tests
  run: |
    pip install -e tests/e2e
    pytest tests/e2e -v --binary-dir=build/output
```
