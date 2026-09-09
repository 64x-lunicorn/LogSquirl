# Decoration path micro-benchmarks

Micro-benchmarks for `LineDecorator::verdictFor` and `LineDecorator::decorate`
(`src/highlighting/include/linedecorator.h`), the hot path that runs for
every visible Log Line on every scroll, on files of millions of lines.

This is deliberately separate from the `tests/e2e` performance suite: the
E2E baseline measures the `logsquirl_grep` binary, process startup and file
loading, none of which touch colour composition. This benchmark produces
the number that a future optimisation of the decoration path should be
measured against.

It is cheap to run because the Line Decorator is pure and needs no GUI:
`logsquirl_decoration_benchmark` links only `logsquirl_highlighting` (no
`logsquirl_ui`, no `Qt6::Widgets`) and creates no `QApplication`.

## Building

```bash
cd build_root
cmake --build . --target logsquirl_decoration_benchmark
```

The binary is placed at `build_root/output/logsquirl_decoration_benchmark`,
alongside the other test binaries.

## Running

```bash
./output/logsquirl_decoration_benchmark
```

This runs all cases with Catch2's default benchmarking settings (100
samples). Useful options (see `--help` for the full list):

```bash
# Run only this file's cases (in case other [decoration-benchmark]-tagged
# cases are added elsewhere)
./output/logsquirl_decoration_benchmark "[decoration-benchmark]"

# More samples for a tighter confidence interval, at the cost of runtime
./output/logsquirl_decoration_benchmark --benchmark-samples 200

# Save the run for later comparison
./output/logsquirl_decoration_benchmark > run-before.txt
```

## Comparing two runs

Catch2 2.x has no built-in run-comparison tool (unlike the `tests/e2e`
Python suite, which diffs against `baseline.json` automatically). To
compare before/after an optimisation:

1. Build and run on the base commit, redirecting output to a file:
   `./output/logsquirl_decoration_benchmark > run-before.txt`
2. Make the change, rebuild, and run again:
   `./output/logsquirl_decoration_benchmark > run-after.txt`
3. `diff run-before.txt run-after.txt`, or eyeball the `mean` column per
   benchmark — Catch2 reports mean, low/high mean (95% CI), and std dev for
   each case.

Run on the same machine, ideally otherwise idle, since these are wall-clock
timings with no cross-machine baseline. A regression worth chasing is one
outside the reported confidence interval, not a percent-level wobble.

## Cases

- **common no-match line**: a short, typical log line, nothing matches.
  The baseline every other case should be compared against.
- **very long line**: ~24,000 characters, no match.
- **many highlighters, one whole-line match**: 200 Highlighters in the
  active set, only the last one (a whole-line match) applies — the cost of
  walking the whole set before it matches.
- **many matches on one line**: a single pattern (main search + QuickFind)
  matching 2000 times on one line — the cost of `HighlightedMatchRanges`
  overlap splitting at scale.
- **tab-heavy line**: 200 tab-separated fields.
- **translate to display space: old per-match re-expansion** / **...:
  rawToDisplayColumns**: isolates the raw-to-display column translation
  step from `decorate()` itself, comparing the per-match prefix
  re-expansion issue #80 replaced (`AbstractLogView::drawTextArea` used to
  re-run `untabify()` on the whole prefix for every match) against the
  `rawToDisplayColumns()` mapping built once per line that replaced it.
  Same input for both: the 2000 raw-space matches from "many matches on one
  line". This step lives in the view, not `LineDecorator`, so it isn't
  exercised by any of the other cases above.
- **long line, one early match: old per-match re-expansion** / **...:
  rawToDisplayColumns (limited)**: the case the mapping approach could
  regress if built naively — a ~24,000 character line with a single match
  near the start. The view limits `rawToDisplayColumns()` to the furthest
  raw column any match actually reaches (`QStringView{ logLine }.left(
  furthestRawColumn )`), instead of mapping the whole line for one early
  match; this pair confirms that keeps the cost comparable to the old
  per-match approach instead of regressing to an O(line length) cost paid
  regardless of how few matches there are.

## Recorded baseline

Measured 2026-09-09 on Apple Silicon (macOS, Debug build — release numbers
will be lower across the board; what matters is the relative shape and the
delta on a future re-run on the same machine/config):

| Benchmark                                                    | Mean       |
|-----------------------------------------------------------------|-----------:|
| common no-match line                                          |   100.0 µs |
| very long line                                                |    1.127 ms |
| many highlighters, one whole-line match                       |   31.38 ms |
| many matches on one line                                      |  593.9 ms |
| tab-heavy line                                                 |  569.8 µs |
| translate to display space: old per-match re-expansion         |   76.70 ms |
| translate to display space: rawToDisplayColumns                |  0.4199 ms |
| long line, one early match: old per-match re-expansion         |  1.285 µs |
| long line, one early match: rawToDisplayColumns (limited)      |  2.359 µs |

The "many highlighters" and "many matches on one line" cases are
pathological stress cases, not representative of typical log lines — they
exist to give the next optimisation ticket a concrete, reproducible number
to chase, per this ticket's purpose. They are not currently regarded as
regressions to fix by this ticket.

The translation cases were added by issue #80 (unifying the decoration
coordinate space). On the 2000-match input, the mapping-based translation
is about **155x faster** than the per-match re-expansion it replaced
(76.70 ms → 0.42 ms). On the one-early-match input, the mapping approach
is about 1 µs slower than the old approach (2.36 µs vs 1.29 µs) — both
values are negligible in absolute terms (well under a millisecond, for a
line drawn at most a few dozen times per repaint), and this is what
motivated limiting `rawToDisplayColumns()` to the furthest raw column
actually needed rather than always mapping the whole line: an earlier,
unlimited version of this change cost the full ~1 ms "very long line"
`decorate()` price *again*, per line, for even a single early match. The
first five cases are unaffected by #80 (within noise of the numbers
recorded above) — that change lives entirely in the view's translation
step, not in `LineDecorator`.
