# Fast means a Budget per Benchmark Scenario and Log File size, derived from measurements

LogSquirl's promise is to be fast, and the Safari Rule (CONTRIBUTING.md) says it must never get slower. Until #676 neither said how fast. The e2e performance suite compares a laptop's run with `baseline.json`, recorded on another laptop. The Performance workflow compares master with the median of master's last six runs. Both answer only "slower than before?". Neither can say whether opening a 1 GB Log File in 0.3 s is the product working or the product broken. The suite has measured the events a user waits for since #667–#670 and #686 (merged in ba28414b): the first Log Line displayed, the Index finished, the first Match, a QuickFind keystroke, a frame, a Log Line followed, a Session restored, a read while indexing. So the numbers to set a promise on now exist.

A second question came with #686 (#705). Both comparisons call a benchmark slower only when it is slower by the tolerance **and** by an absolute margin: 1 ms in the suite, 10 ms in the Performance workflow. A read while indexing takes microseconds. Holding the index lock while parsing (the #289 regression) made `getNbLine`'s p50 go from 0.1 µs to 60 µs and `getExpandedLines`' p99 from 10 µs to 90 µs. That is several hundred times slower, and still inside both margins.

## Decision

### What is budgeted

- **A Benchmark Scenario** is what one Benchmark Run does: `open-and-index`, `search`, `quickfind`, `scroll`, `follow`, `session-restore`, `read-while-indexing`, plus `logsquirl_grep --benchmark-output` (`grep`) and the startup (`gui_startup_version`, the one case timed around a whole process). The scenarios' benchmarks are listed in `tests/e2e/README.md`. Memory is budgeted too, as far as it is measured (below).
- **A Budget** is the slowest value a scenario's headline benchmark may have, on one Log File size and one machine class: the open to the first Log Line and to the Index finished; a plain-text Search's first Match and finish, and a regular expression's finish; `logsquirl_grep` to its last Match; a QuickFind keystroke's p50 and p99; a frame's p99 for each view; a followed Log Line's p99 to displayed and to charted; a restored Session's tab in front and every tab; each read's p50 and p99 while indexing, and the indexing itself. The other benchmarks of a scenario (`_frame_max`, the other Search variants, `_index_cpu`, …) are for diagnosis. They are compared with the history and have no Budget.
- **The metric** of every timed Budget is the **median over the runs** of what one run reports (21 runs after 3 warmups, 7 after 1 on the 1 GB Log File and the other large cases), in seconds. Where the suite reports throughput, a Budget in seconds is one in GB/s too: 1 GB indexed in at most 320 ms is at least 3.1 GB/s, and a plain-text Search through it finished in at most 520 ms is at least 1.9 GB/s.
- **The machine class** is the one the Performance and Benchmarks workflows run on: a GitHub-hosted `ubuntu-24.04` x64 runner, 4 cores, 16 GB, AMD EPYC 7763 or 9V74. The build is RelWithDebInfo with LTO in the CI Build noble job's container, with `QT_QPA_PLATFORM=offscreen`. A Budget says nothing about a laptop: `baseline.json` stays the local comparison.

### One place

The Budgets live in **`tests/e2e/budgets.json`**, the file this ADR points to and the nightly run (#677) reads. It has one entry per Budget, keyed by the benchmark's name:

| Field | Meaning |
|---|---|
| `scenario`, `file`, `metric` | what is measured, on which Log File, as which statistic |
| `report`, `benchmark`, `field`, `scale` | where the value is: `e2e` is the suite's `benchmark_report.json` (key, `median_seconds`); `instruction-counts` is the instruction counts' `after.json` (`<binary> / <name>`, e.g. `peak_heap_bytes`). `scale` turns the field into the unit. |
| `unit`, `budget` | the Budget |
| `measured`, `worst`, `headroom_percent` | the values it was derived from, by source, the slowest of them, and how far above that the Budget is |
| `noise_margin_percent` | how far above the Budget one run may land before the Budget counts as broken: broken when the value is above `budget × (1 + noise_margin_percent / 100)` |
| `source` | the measurements cited, keys of `sources` (workflow, run, commit, CPU, link) |
| `min_delta_seconds` (optional) | the benchmark's own absolute margin for the comparison with the history (below) |

`.github/scripts/perf-budgets.py check --budgets tests/e2e/budgets.json --e2e benchmark_report.json [--instruction-counts after.json]` checks a run against it. A Budget is `broken`, `missing` (a budgeted benchmark the report does not hold: a scenario that stops running must not keep its Budget), `not-measured` (its report was not given) or `ok`. Once the Budgets are accepted (below), the exit status is 1 on broken or missing. Its tests also check the file itself: every Budget cites a known source, is no lower than the slowest value it was derived from, and states its headroom correctly. **Changing a Budget means changing this ADR and the file in one pull request.**

### Proposed until the maintainer accepts them

The Budgets are a product decision, and the maintainer approves them in the review of the pull request that adds them (#676). Until then nothing fails on them. `budgets.json` says so in its `status`: while it starts with `proposed`, `perf-budgets.py check` and the nightly run (`perf-history.py`) check every Budget and show it in the table, but a broken or missing Budget is no finding: it files no issue, it does not make the run red, and `perf-budgets.py` exits 0. The maintainer accepts the Budgets by changing `status` to `accepted`, in that pull request or a later one. From the next run on, a broken or missing Budget is a finding. Setting `status` back to `proposed` suspends them again.

### How a Budget is derived

From the three runs of the code of ba28414b on the machine class that measured every scenario:

- **P**: Performance run [36962491329](https://github.com/64x-lunicorn/LogSquirl/actions/runs/36962491329), master at ba28414b, EPYC 7763
- **B1**: Benchmarks run [36938651506](https://github.com/64x-lunicorn/LogSquirl/actions/runs/36938651506), the after side of #703 (266ac7289169), EPYC 9V74
- **B2**: Benchmarks run [36955768456](https://github.com/64x-lunicorn/LogSquirl/actions/runs/36955768456), the after side of #703 (ad30606b101e, the tree of ba28414b), EPYC 9V74

The earlier runs on `perf-data` (36336158447, 36408027284, 36956982426) predate the scenarios and measured only the old grep cases and the startup.

- **Budget** = the slowest of the three medians × 1.25, rounded up to two significant digits. The headroom is that 25 % plus the rounding: 25–31 %. The slowest is often the EPYC 7763. `logsquirl_grep` on 1 GB took 1.36 s there and 0.80 s on the 9V74, so a Budget from the faster runner would break on every run that lands on the slower one.
- **Noise margin** = three interquartile ranges of the runs behind that slowest median, in percent of it, rounded up to 5 and at least 5. It is 0 for a count that does not vary (the memory). It is the same scale the comparison with the history uses (below).
- **No Budget for a benchmark that scatters by more than its own median** (three IQRs above it). That is the p99 read of `getNbLine` on 100 MB (median 1 µs, IQR 23 µs) and the p99 reads of all three on 1 GB: the longest reads of a run on a shared runner are its preemptions. They are compared with the history, whose margin takes their spread into account, and get a Budget when a quieter measurement shows them steady.
- **At the resolution of the reports**: until #705 the suite kept six decimals of a second, so `getNbLine`'s p50 was recorded as 0 or 1 µs. Its Budget is 5 µs, above that bound and far below the 60 µs of the #289 regression.

### The Budgets

| Scenario | Benchmark | Log File | Measured P / B1 / B2 | Budget | Headroom | Noise margin |
|---|---|---|---|---:|---:|---:|
| startup | `gui_startup_version` | – | 14.2 ms / 14.3 ms / 13.4 ms | 18 ms | 26 % | 25 % |
| open-and-index | `gui_open_1mb_first_line` | 1 MB, one Log Line (test_data/random_block_1Mb.txt) | 42.2 ms / 40 ms / 40.5 ms | 53 ms | 26 % | 20 % |
| open-and-index | `gui_open_1mb_indexed` | 1 MB, one Log Line (test_data/random_block_1Mb.txt) | 46 ms / 44 ms / 44.3 ms | 58 ms | 26 % | 25 % |
| open-and-index | `gui_open_log_100mb_first_line` | 100 MB Log File | 46.2 ms / 42 ms / 41.5 ms | 58 ms | 26 % | 25 % |
| open-and-index | `gui_open_log_100mb_indexed` | 100 MB Log File | 66.2 ms / 64.2 ms / 66.9 ms | 84 ms | 26 % | 15 % |
| open-and-index | `gui_open_log_1gb_first_line` | 1 GB Log File | 45.1 ms / 42.7 ms / 42.2 ms | 57 ms | 26 % | 45 % |
| open-and-index | `gui_open_log_1gb_indexed` | 1 GB Log File | 239 ms / 245.2 ms / 249.1 ms | 320 ms | 28 % | 10 % |
| search | `gui_search_log_100mb_plain_first_match` | 100 MB Log File | 59.8 ms / 57.7 ms / 58.6 ms | 75 ms | 25 % | 10 % |
| search | `gui_search_log_100mb_plain_finished` | 100 MB Log File | 57.2 ms / 55.6 ms / 56.6 ms | 72 ms | 26 % | 10 % |
| search | `gui_search_log_100mb_regex_finished` | 100 MB Log File | 69.5 ms / 65.6 ms / 66.1 ms | 87 ms | 25 % | 15 % |
| grep | `grep_log_100mb_simple` | 100 MB Log File | 138.3 ms / 106.4 ms / 107.9 ms | 180 ms | 30 % | 10 % |
| search | `gui_search_log_1gb_plain_first_match` | 1 GB Log File | 114.6 ms / 117 ms / 118.8 ms | 150 ms | 26 % | 35 % |
| search | `gui_search_log_1gb_plain_finished` | 1 GB Log File | 410.6 ms / 395.2 ms / 397.1 ms | 520 ms | 27 % | 20 % |
| search | `gui_search_log_1gb_regex_finished` | 1 GB Log File | 485.4 ms / 463.3 ms / 461.3 ms | 610 ms | 26 % | 5 % |
| grep | `grep_log_1gb_simple` | 1 GB Log File | 1.36 s / 795.9 ms / 811.9 ms | 1.70 s | 25 % | 10 % |
| quickfind | `gui_quickfind_log_100mb_keystroke_p50` | 100 MB Log File | 3 ms / 2.2 ms / 2.3 ms | 3.8 ms | 25 % | 10 % |
| quickfind | `gui_quickfind_log_100mb_keystroke_p99` | 100 MB Log File | 4.1 ms / 3.5 ms / 3.3 ms | 5.1 ms | 25 % | 20 % |
| quickfind | `gui_quickfind_log_1gb_keystroke_p50` | 1 GB Log File | 3 ms / 2.2 ms / 2.2 ms | 3.8 ms | 26 % | 10 % |
| quickfind | `gui_quickfind_log_1gb_keystroke_p99` | 1 GB Log File | 3.5 ms / 2.9 ms / 2.9 ms | 4.4 ms | 26 % | 20 % |
| scroll | `gui_scroll_text_frame_p99` | 100 MB scroll Log File | 1.5 ms / 1.5 ms / 1.5 ms | 1.9 ms | 26 % | 5 % |
| scroll | `gui_scroll_text_highlighters_frame_p99` | 100 MB scroll Log File | 1.9 ms / 1.7 ms / 1.7 ms | 2.4 ms | 28 % | 10 % |
| scroll | `gui_scroll_table_frame_p99` | 100 MB scroll Log File | 2.8 ms / 2.4 ms / 2.4 ms | 3.5 ms | 26 % | 15 % |
| scroll | `gui_scroll_table_highlighters_frame_p99` | 100 MB scroll Log File | 3.3 ms / 2.7 ms / 2.7 ms | 4.2 ms | 28 % | 20 % |
| scroll | `gui_scroll_text_ansi_hidden_frame_p99` | 20 MB scroll Log File with ANSI colors | 1.5 ms / 1.5 ms / 1.5 ms | 2 ms | 30 % | 10 % |
| scroll | `gui_scroll_text_ansi_colors_frame_p99` | 20 MB scroll Log File with ANSI colors | 2.2 ms / 2.1 ms / 2.1 ms | 2.8 ms | 28 % | 5 % |
| follow | `gui_follow_10_per_s_display_p99` | written by the scenario (1000 Log Lines, then 5 s of appends) | 265.9 ms / 264.9 ms / 265.1 ms | 340 ms | 28 % | 5 % |
| follow | `gui_follow_10_per_s_chart_p99` | written by the scenario (1000 Log Lines, then 5 s of appends) | 530.8 ms / 530.3 ms / 530.5 ms | 670 ms | 26 % | 5 % |
| follow | `gui_follow_1000_per_s_display_p99` | written by the scenario (1000 Log Lines, then 5 s of appends) | 256.9 ms / 256.6 ms / 256.3 ms | 330 ms | 28 % | 5 % |
| follow | `gui_follow_1000_per_s_chart_p99` | written by the scenario (1000 Log Lines, then 5 s of appends) | 513.3 ms / 514.5 ms / 514.3 ms | 650 ms | 26 % | 5 % |
| session-restore | `gui_session_restore_small_current_tab_usable` | 3 tabs: 1 MB (in front), 1.5 MB and 512 KB random blocks | 153.2 ms / 136.5 ms / 139.8 ms | 200 ms | 31 % | 25 % |
| session-restore | `gui_session_restore_small_all_tabs_indexed` | 3 tabs: 1 MB (in front), 1.5 MB and 512 KB random blocks | 162.8 ms / 144.8 ms / 149.9 ms | 210 ms | 29 % | 25 % |
| session-restore | `gui_session_restore_log_220mb_current_tab_usable` | 3 tabs: 100 MB Log File (in front), 100 MB scroll Log File, 20 MB scroll Log File with ANSI colors | 161.3 ms / 151.6 ms / 152.3 ms | 210 ms | 30 % | 10 % |
| session-restore | `gui_session_restore_log_220mb_all_tabs_indexed` | 3 tabs: 100 MB Log File (in front), 100 MB scroll Log File, 20 MB scroll Log File with ANSI colors | 201.1 ms / 191.4 ms / 190.9 ms | 260 ms | 29 % | 10 % |
| read-while-indexing | `gui_read_while_indexing_log_100mb_nb_line_p50` | 100 MB Log File | 0 µs / 0 µs / 0 µs | 5 µs | – | 0 % |
| read-while-indexing | `gui_read_while_indexing_log_100mb_line_string_p50` | 100 MB Log File | 20 µs / 18 µs / 19 µs | 25 µs | 25 % | 60 % |
| read-while-indexing | `gui_read_while_indexing_log_100mb_line_string_p99` | 100 MB Log File | 34 µs / 32 µs / 30 µs | 43 µs | 26 % | 90 % |
| read-while-indexing | `gui_read_while_indexing_log_100mb_expanded_lines_p50` | 100 MB Log File | 20 µs / 20 µs / 20 µs | 25 µs | 25 % | 15 % |
| read-while-indexing | `gui_read_while_indexing_log_100mb_expanded_lines_p99` | 100 MB Log File | 23 µs / 22 µs / 23 µs | 29 µs | 26 % | 30 % |
| read-while-indexing | `gui_read_while_indexing_log_100mb_index_wall` | 100 MB Log File | 50.5 ms / 49.5 ms / 48.7 ms | 64 ms | 27 % | 20 % |
| read-while-indexing | `gui_read_while_indexing_log_1gb_nb_line_p50` | 1 GB Log File | 1 µs / 0 µs / 0 µs | 5 µs | – | 0 % |
| read-while-indexing | `gui_read_while_indexing_log_1gb_line_string_p50` | 1 GB Log File | 15 µs / 13 µs / 12 µs | 19 µs | 27 % | 100 % |
| read-while-indexing | `gui_read_while_indexing_log_1gb_expanded_lines_p50` | 1 GB Log File | 22 µs / 19 µs / 19 µs | 28 µs | 27 % | 30 % |
| read-while-indexing | `gui_read_while_indexing_log_1gb_index_wall` | 1 GB Log File | 223.6 ms / 226.9 ms / 226 ms | 290 ms | 28 % | 10 % |
| memory | `index_memory_per_million_log_lines` ¹ | 2 million Log Lines' positions, in memory | 3.24 MB | 4.10 MB | 26 % | 0 % |

¹ From the instruction counts of CI Build run [36961939625](https://github.com/64x-lunicorn/LogSquirl/actions/runs/36961939625) (ba28414b, `instruction-counts/after.json`): the peak heap of `logsquirl_linepositionarray_benchmark` / *append, line by line*, 6,489,088 bytes for 2 million Log Lines, per million. Counted under Valgrind, so it is deterministic and machine independent. It is the Index's line positions only. **Process memory per million Log Lines is not measured** by any scenario, so it has no Budget. A scenario that reports the resident memory after `open-and-index` would give it one.

The frame Budgets are far below the 16.7 ms of a frame at 60 Hz, which the scroll scenario counts separately (*Frames Over Budget*). A Budget holds what was measured, not what would still be acceptable.

### The comparison with the history scales with the benchmark (#705)

The absolute margin of both comparisons is no longer one value for every benchmark. A median is a regression when it is above

```
reference + max(tolerance × reference, min_delta, 3 × IQR)
min_delta = min(cap, max(reference / 2, 1 µs))
```

- `tolerance` and `cap`: 5 % and 1 ms in the suite (`_meta.tolerance_percent`, `_meta.min_delta_seconds`), as before; 10 % and 10 ms for the wall-clock series of the nightly Performance workflow, where #677 applies the rule at a change point (`perf_changepoint.py`, BUILD.md *Nightly performance*) instead of 30 % against the latest run.
- `reference` is the baseline's median in the suite and, in the history, the median of the up to 14 runs before a change point (#677). `IQR` is the interquartile range of the reference's runs: `iqr_seconds` of the baseline entry; in the history the IQR of those runs' medians, or the median of their `iqr_seconds` (which `perf-data` keeps from now on) when that is larger.
- A benchmark of milliseconds keeps the margin it had: 2 ms may still become 3 ms in the suite. A benchmark below twice the cap gets at most half of itself, so 0.1 µs → 60 µs and 10 µs → 90 µs are regressions in both. A benchmark that scatters gets three times its spread.
- Below 1 µs nothing is told apart: that is the reports' old resolution, and about what a clock read and a cache miss cost on a shared runner. The suite keeps nine decimals of a second from now on.
- A benchmark may set its own `min_delta_seconds`, which replaces `min_delta`: beside its entry in `baseline.json` (kept by `--update-baseline`), and for the history beside its Budget in `tests/e2e/budgets.json`, which `perf-history.py` reads (#677).

The rule is one module, `.github/scripts/perf_margin.py`, which `tests/e2e/conftest.py` and `perf-history.py` both load. Tests on each side hold the #705 numbers.

## Considered Options

- **Budgets from the local machine (`baseline.json`, Apple M5).** That is where the numbers are steadiest, but no automated run happens there, and a promise nobody checks is not one.
- **Budgets from the fastest runner, or from the median of the three runs.** They would be tighter, but a nightly run on an EPYC 7763 would break the `grep`, QuickFind and scroll Budgets without any change in the code.
- **A round product number per scenario** (open 1 GB in under a second, a keystroke under 16 ms). It is easy to say, but nothing measured supports it, and it would sit so far above the measurements that a regression of several times would keep it. The ticket asks for budgets from measurements. A round number belongs in the user documentation, not in a check.
- **Instruction counts as the Budget for every scenario** (#677 counts the Catch2 benchmarks every night, under Callgrind, and files an issue at a change point in them). They are stable across runners, but they do not see waiting: a read blocked on the index lock or indexing that fell back to one thread costs no instruction more (#686). Wall-clock Budgets stay, and instruction counts can get Budgets of their own once the nightly run counts the scenarios.
- **For #705, an absolute margin per benchmark only** (`min_delta_seconds` beside each entry). It works, but every new benchmark would need someone to pick a number, and one left out would get 1 ms or 10 ms again. A margin that scales by default, with the per-benchmark value as an exception, cannot be forgotten.
- **For #705, the IQR alone, or a fraction of the median alone.** A benchmark whose runs agree to the microsecond has an IQR near 0: the IQR alone would make 5 % of 2 ms a regression, which the 1 ms margin was added to prevent. A fraction alone ignores a benchmark that scatters by itself. Both together, capped by the old value, keep every case that passed before for a benchmark of milliseconds.
- **Removing the absolute margin.** Then a grep case on 1 MB, about a millisecond, turns red on every scheduling hiccup, which is why the margin exists.

## Consequences

- "Fast" has numbers: 44 Budgets over every scenario on the machine class, each tracing back to a run and a commit. Whether they hold is now a question with an answer, and #677 asks it every night.
- The Budgets are a snapshot of 26.11's code on two CPU types, with 25 % headroom. A deliberate slowdown beyond that, or a faster product that should promise more, changes the ADR and the file together. Lowering a Budget after an optimization is encouraged, but it is a change to this ADR, not a side effect of a run.
- Three runs are few. The noise margins come from within-run spread, not from weeks of history. Once the nightly run has a few weeks on `perf-data`, the Budgets and margins should be derived again from that history; the derivation stays the same.
- A benchmark of microseconds can now fail the Performance workflow and the local suite. The first nightly runs may show a sub-millisecond benchmark that moves by more than half between runner types. If one does, it gets its own `min_delta_seconds`, with the run that showed it cited.
- The Performance workflow already compares runs of two CPU types (`logsquirl_grep` on 1 GB differs by 70 % between them). That is not new, and the change-point detection of #677, which leaves a series room for its own scatter, and the comparison within one CPU model of #685 are the answer to it, not this ADR.
- Process memory and the scattering p99 reads on 1 GB have no Budget yet. That is a gap, recorded here rather than covered with a guess.
