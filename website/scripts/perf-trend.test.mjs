// Tests for src/perf-trend.mjs (#678): reading the perf-data branch, building
// one chart per benchmark grouped by Benchmark Scenario, and drawing it as SVG.
// Run: npm test
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { test } from 'node:test';

import {
  buildTrend,
  formatValue,
  loadBudgets,
  loadRuns,
  niceTicks,
  readPerfTrend,
  recentRuns,
  releaseMarkers,
  renderChart,
} from '../src/perf-trend.mjs';

function run(day, { cpu = 'AMD EPYC 7763 64-Core Processor', usable = true, benchmarks = {}, counts = null, commit = 'c'.repeat(40) } = {}) {
  const entry = {
    schema: 1,
    recorded_at: `2026-10-${String(day).padStart(2, '0')}T03:00:00Z`,
    commit,
    ref: 'refs/heads/master',
    run_id: `${day}-1`,
    version: `26.10.0.${700 + day}`,
    accepted: false,
    system: { cpu },
    usable,
    benchmarks: Object.fromEntries(Object.entries(benchmarks).map(([name, median]) => [name, { median_seconds: median }])),
  };
  if (counts) {
    entry.instruction_counts = {
      cpu: 'AMD EPYC 7763',
      log_file_mb: 32,
      failed_binaries: [],
      benchmarks: Object.fromEntries(Object.entries(counts).map(([name, values]) => [name, values])),
    };
  }
  return entry;
}

function dataDir(runs, { trial = [], extra = {} } = {}) {
  const dir = mkdtempSync(join(tmpdir(), 'perf-data-'));
  mkdirSync(join(dir, 'history'));
  mkdirSync(join(dir, 'trial'));
  runs.forEach((entry, i) => writeFileSync(join(dir, 'history', `${entry.recorded_at}-${i}.json`), JSON.stringify(entry)));
  trial.forEach((entry, i) => writeFileSync(join(dir, 'trial', `t-${i}.json`), JSON.stringify(entry)));
  for (const [name, text] of Object.entries(extra)) writeFileSync(join(dir, 'history', name), text);
  return dir;
}

const budgets = {
  status: 'proposed: awaits the maintainer',
  budgets: {
    gui_open_log_1gb_indexed: {
      scenario: 'open-and-index', report: 'e2e', benchmark: 'gui_open_log_1gb_indexed',
      field: 'median_seconds', unit: 's', budget: 0.32, noise_margin_percent: 10,
      file: '1 GB Log File, generated', metric: 'open to the Index finished, median of the runs',
    },
    index_memory_per_million_log_lines: {
      scenario: 'memory', report: 'instruction-counts',
      benchmark: 'logsquirl_linepositionarray_benchmark / Line positions of an Index / append, line by line',
      field: 'peak_heap_bytes', scale: 0.5, unit: 'bytes per million Log Lines', budget: 4100000, noise_margin_percent: 0,
      file: "2 million Log Lines' positions, in memory", metric: 'peak heap per million Log Lines',
    },
  },
};

const MEMORY = 'logsquirl_linepositionarray_benchmark / Line positions of an Index / append, line by line';

// ---------------------------------------------------------------------------
// Reading the branch
// ---------------------------------------------------------------------------

test('a missing data directory reads as no runs, not as an error', () => {
  assert.deepEqual(loadRuns(join(tmpdir(), 'no-such-perf-data')), { runs: [], skipped: [] });
  assert.deepEqual(loadRuns(undefined), { runs: [], skipped: [] });
  assert.deepEqual(loadRuns(''), { runs: [], skipped: [] });
});

test('only the runs of master are read, oldest first; trial runs are left out', () => {
  const dir = dataDir([run(3), run(1), run(2)], { trial: [run(4)] });
  const { runs } = loadRuns(dir);
  assert.deepEqual(runs.map((r) => r.recorded_at.slice(8, 10)), ['01', '02', '03']);
});

test('an unreadable run file is skipped and named, the others are read', () => {
  const dir = dataDir([run(1)], { extra: { 'broken.json': '{ not json', 'no-date.json': '{}' } });
  const { runs, skipped } = loadRuns(dir);
  assert.equal(runs.length, 1);
  assert.deepEqual(skipped.sort(), ['broken.json', 'no-date.json']);
});

test('missing or unreadable Budgets read as none', () => {
  assert.equal(loadBudgets(join(tmpdir(), 'no-such-budgets.json')), null);
  const dir = mkdtempSync(join(tmpdir(), 'budgets-'));
  writeFileSync(join(dir, 'budgets.json'), '{ nope');
  assert.equal(loadBudgets(join(dir, 'budgets.json')), null);
});

test('readPerfTrend without data is an empty trend, so the build does not fail', () => {
  const trend = readPerfTrend({ dataDir: undefined, budgetsFile: join(tmpdir(), 'none.json'), releases: [] });
  assert.equal(trend.runCount, 0);
  assert.deepEqual(trend.sections, []);
});

test('the page shows the runs of the last WINDOW_DAYS before the latest one', () => {
  const runs = [run(1), run(5), run(9)];
  assert.deepEqual(recentRuns(runs, 4).map((r) => r.run_id), ['5-1', '9-1']);
  assert.deepEqual(recentRuns([], 4), []);
});

// ---------------------------------------------------------------------------
// Building the series
// ---------------------------------------------------------------------------

test('wall-clock charts are grouped by Benchmark Scenario, Budgets first, in the ADR order', () => {
  const runs = [
    run(1, { benchmarks: { gui_open_log_1gb_indexed: 0.24, gui_open_log_1gb_index_cpu: 0.9, grep_log_1gb_simple: 1.3, gui_startup_version: 0.014 } }),
    run(2, { benchmarks: { gui_open_log_1gb_indexed: 0.25, gui_open_log_1gb_index_cpu: 0.9, grep_log_1gb_simple: 1.2, gui_startup_version: 0.015 } }),
  ];
  const trend = buildTrend(runs, budgets);
  const wall = trend.sections.find((s) => s.metric === 'wall-clock');
  assert.deepEqual(wall.scenarios.map((s) => s.name), ['startup', 'open-and-index', 'grep']);
  const open = wall.scenarios[1];
  assert.deepEqual(open.charts.map((c) => [c.key, c.headline]), [
    ['gui_open_log_1gb_indexed', true],
    ['gui_open_log_1gb_index_cpu', false],
  ]);
  assert.equal(open.charts[0].budget.value, 0.32);
  assert.equal(open.charts[0].budget.brokenAbove.toFixed(3), '0.352');
  assert.equal(open.charts[1].budget, null);
});

test('a wall-clock series is split by runner CPU model, and unusable runs are kept but marked', () => {
  const runs = [
    run(1, { cpu: 'Intel Xeon', benchmarks: { gui_open_log_1gb_indexed: 0.3 } }),
    run(2, { benchmarks: { gui_open_log_1gb_indexed: 0.24 } }),
    run(3, { benchmarks: { gui_open_log_1gb_indexed: 0.5 }, usable: false }),
    run(4, { benchmarks: { gui_open_log_1gb_indexed: 0.25 } }),
  ];
  const chart = buildTrend(runs, budgets).sections[0].scenarios[0].charts[0];
  assert.deepEqual(chart.groups.map((g) => g.label), ['Intel Xeon', 'AMD EPYC 7763 64-Core Processor']);
  const amd = chart.groups[1];
  assert.deepEqual(amd.points.map((p) => [p.value, p.usable]), [[0.24, true], [0.5, false], [0.25, true]]);
  assert.equal(amd.points[0].version, '26.10.0.702');
});

test('a benchmark the latest run no longer measures is not charted (a renamed benchmark)', () => {
  const runs = [
    run(1, { benchmarks: { grep_search_1mb_simple: 0.01 } }),
    run(2, { benchmarks: { grep_1mb_simple: 0.01 } }),
  ];
  const keys = buildTrend(runs, null).sections[0].scenarios.flatMap((s) => s.charts.map((c) => c.key));
  assert.deepEqual(keys, ['grep_1mb_simple']);
});

test('instruction counts are charted per benchmark binary, a counted Budget under its scenario', () => {
  const counts = {
    [MEMORY]: { instructions: 1000, allocations: 3, peak_heap_bytes: 6489088 },
    'logsquirl_search_benchmark / plain': { instructions: 5e9, allocations: 1, peak_heap_bytes: 10 },
  };
  const runs = [run(1, { benchmarks: { gui_startup_version: 0.014 } }), run(2, { benchmarks: { gui_startup_version: 0.014 }, counts })];
  const trend = buildTrend(runs, budgets);
  const wall = trend.sections.find((s) => s.metric === 'wall-clock');
  const memory = wall.scenarios.find((s) => s.name === 'memory').charts[0];
  assert.equal(memory.unit, 'bytes per million Log Lines');
  assert.deepEqual(memory.groups[0].points.map((p) => p.value), [3244544]);
  assert.equal(memory.budget.value, 4100000);

  const counted = trend.sections.find((s) => s.metric === 'instructions');
  assert.deepEqual(counted.scenarios.map((s) => s.name), ['logsquirl_linepositionarray_benchmark', 'logsquirl_search_benchmark']);
  const search = counted.scenarios[1].charts[0];
  assert.equal(search.title, 'plain');
  assert.equal(search.unit, 'instructions');
  assert.equal(search.groups[0].label, 'AMD EPYC 7763, 32 MiB Log Files');
  assert.deepEqual(search.groups[0].points.map((p) => p.value), [5e9]);
});

test('the time domain spans every run, and a single run gets a day on each side', () => {
  const one = buildTrend([run(5, { benchmarks: { gui_startup_version: 0.01 } })], null);
  assert.equal(one.domain[1] - one.domain[0], 2 * 86400000);
  const two = buildTrend([run(1, { benchmarks: { a: 1 } }), run(3, { benchmarks: { a: 1 } })], null);
  assert.deepEqual(two.domain.map((t) => new Date(t).toISOString().slice(0, 10)), ['2026-10-01', '2026-10-03']);
});

test('the CPU models keep one color each across all charts, in order of first appearance', () => {
  const runs = [
    run(1, { cpu: 'Intel Xeon', benchmarks: { gui_startup_version: 0.01 } }),
    run(2, { benchmarks: { gui_startup_version: 0.01, grep_log_1gb_simple: 1 } }),
  ];
  const trend = buildTrend(runs, null);
  assert.deepEqual(trend.legend.map((l) => [l.label, l.slot]), [['Intel Xeon', 0], ['AMD EPYC 7763 64-Core Processor', 1]]);
  const grep = trend.sections[0].scenarios.find((s) => s.name === 'grep').charts[0];
  assert.equal(grep.groups[0].slot, 1);
});

// ---------------------------------------------------------------------------
// Releases, formatting and drawing
// ---------------------------------------------------------------------------

test('releases inside the time domain are marked; legacy releases and those outside are not', () => {
  const releases = [
    { version: '26.10.0', label: 'v26.10.0', date: '2026-10-02', channel: 'stable' },
    { version: '26.10.0-beta3', label: 'v26.10.0-beta3', date: '2026-09-20', channel: 'beta' },
    { version: '22.06', label: 'v22.06', date: '2026-10', channel: 'legacy' },
  ];
  const domain = [Date.parse('2026-09-27T00:00:00Z'), Date.parse('2026-10-05T00:00:00Z')];
  assert.deepEqual(releaseMarkers(releases, domain).map((m) => m.label), ['v26.10.0']);
});

test('values are formatted in the unit of their chart', () => {
  assert.equal(formatValue(0.32, 's'), '320 ms');
  assert.equal(formatValue(1.7, 's'), '1.70 s');
  assert.equal(formatValue(0.000025, 's'), '25 µs');
  assert.equal(formatValue(4100000, 'bytes per million Log Lines'), '4.10 MB');
  assert.equal(formatValue(5.25e9, 'instructions'), '5.25 G');
  assert.equal(formatValue(1234, 'instructions'), '1,234');
});

test('axis ticks are round numbers covering the range', () => {
  assert.deepEqual(niceTicks(0, 0.35, 4), [0, 0.1, 0.2, 0.3, 0.4]);
  const ticks = niceTicks(4.9e9, 5.1e9, 4);
  assert.ok(ticks[0] <= 4.9e9 && ticks.at(-1) >= 5.1e9);
});

test('a chart draws its Budget, its release markers and one line per CPU model', () => {
  const runs = [
    run(1, { cpu: 'Intel Xeon', benchmarks: { gui_open_log_1gb_indexed: 0.3 } }),
    run(2, { benchmarks: { gui_open_log_1gb_indexed: 0.24 } }),
    run(3, { benchmarks: { gui_open_log_1gb_indexed: 0.25 } }),
  ];
  const trend = buildTrend(runs, budgets);
  const chart = trend.sections[0].scenarios[0].charts[0];
  const markers = [{ label: 'v26.10.0', t: Date.parse('2026-10-02T12:00:00Z') }];
  const svg = renderChart(chart, { domain: trend.domain, markers });
  assert.match(svg, /^<svg [^>]*role="img"/);
  assert.match(svg, /class="perf-budget"/);
  assert.match(svg, /Budget 320 ms/);
  assert.match(svg, /class="perf-release"/);
  assert.match(svg, />v26\.10\.0</);
  assert.equal((svg.match(/<polyline /g) ?? []).length, 1); // the Xeon has a single point, no line
  assert.equal((svg.match(/<circle /g) ?? []).length, 3);
  assert.match(svg, /<title>2026-10-02 · 26\.10\.0\.702 \(cccccccccccc\) · 240 ms · AMD EPYC 7763 64-Core Processor<\/title>/);
});

test('an unusable run is drawn hollow and breaks no line', () => {
  const runs = [
    run(1, { benchmarks: { gui_startup_version: 0.014 } }),
    run(2, { benchmarks: { gui_startup_version: 0.05 }, usable: false }),
    run(3, { benchmarks: { gui_startup_version: 0.015 } }),
  ];
  const trend = buildTrend(runs, null);
  const svg = renderChart(trend.sections[0].scenarios[0].charts[0], { domain: trend.domain, markers: [] });
  assert.equal((svg.match(/class="perf-point perf-unusable/g) ?? []).length, 1);
  const line = /<polyline [^>]*points="([^"]+)"/.exec(svg)[1];
  assert.equal(line.trim().split(' ').length, 2);
  assert.match(svg, /unusable: within-run CV above 20 %/);
});

test('a chart without a Budget marks only its latest and its unusable runs, to keep the page small', () => {
  const runs = [1, 2, 3, 4].map((d) => run(d, { benchmarks: { gui_open_log_1gb_index_cpu: 0.9 }, usable: d !== 2 }));
  const trend = buildTrend(runs, budgets);
  const svg = renderChart(trend.sections[0].scenarios[0].charts[0], { domain: trend.domain, markers: [] });
  assert.equal((svg.match(/<circle /g) ?? []).length, 2);
  assert.match(svg, /<title>2026-10-04 · /);
  assert.equal((svg.match(/<polyline /g) ?? []).length, 1);
});

test('text from the data is escaped in the SVG', () => {
  const runs = [run(1, { cpu: 'A <b>&"CPU"', benchmarks: { gui_startup_version: 0.01 } })];
  const trend = buildTrend(runs, null);
  const svg = renderChart(trend.sections[0].scenarios[0].charts[0], { domain: trend.domain, markers: [] });
  assert.ok(!svg.includes('<b>'));
  assert.match(svg, /A &lt;b&gt;&amp;&quot;CPU&quot;/);
});
