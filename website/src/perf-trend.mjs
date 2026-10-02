// The performance trend page, built from the perf-data branch (#678).
//
// The nightly Performance workflow (.github/workflows/performance.yml, #677)
// records every run of master as one JSON file in history/ on the perf-data
// branch (.github/scripts/perf-history.py writes them; BUILD.md, "Nightly
// performance"). The website build reads that directory when the environment
// variable LOGSQUIRL_PERF_DATA names a checkout of the branch, and draws:
//
//   - wall-clock: one chart per benchmark the latest run measured, grouped by
//     Benchmark Scenario (ADR 0018); a benchmark with a Budget comes first and
//     has its Budget drawn as a line (tests/e2e/budgets.json). A median
//     compares only within one runner CPU model, so each model is its own
//     line; a run whose within-run CV made it unusable is a hollow point
//     outside the line.
//   - instruction counts: one chart per counted benchmark, grouped by its
//     benchmark binary, one line per CPU model and Log File size (counts
//     compare only within one).
//   - a counted Budget (the memory one) under its scenario with the wall-clock.
//
// Each chart has the releases of the release pages (src/releases.mjs) marked
// on one time axis shared by all charts. The charts are SVG written at build
// time: no script runs in the reader's browser and nothing is fetched from
// elsewhere. Without the variable, or without runs, the page says so and the
// build goes on: the branch is data, and the site must build without it.

import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

// From the website directory, where the site is built, as src/releases.mjs
// does: the build bundles this module elsewhere, so its own URL is no anchor.
export const REPO_ROOT = join(process.cwd(), '..');
export const BUDGETS_FILE = join(REPO_ROOT, 'tests', 'e2e', 'budgets.json');
export const PERF_DATA_ENV = 'LOGSQUIRL_PERF_DATA';
export const DATA_BRANCH_URL = 'https://github.com/64x-lunicorn/LogSquirl/tree/perf-data';

const DAY = 86400000;
// The page shows the runs of this many days before the latest one: a point per
// night and chart adds to every build of the page. Older runs stay on the
// branch.
export const WINDOW_DAYS = 120;
// The categorical color slots (perf-trend.css); a fifth series and beyond is
// drawn in the neutral "other" slot instead of a repeated color.
export const SLOTS = 4;
// perf-history.py MAX_USABLE_CV_PERCENT.
const MAX_USABLE_CV_PERCENT = 20;

// The Benchmark Scenarios in the order of ADR 0018's table, and the scenario
// of a wall-clock benchmark without a Budget by its name (perf-history.py
// SCENARIO_PREFIXES, which tests/e2e/test_performance.py's names follow).
const SCENARIO_ORDER = [
  'startup', 'open-and-index', 'search', 'grep', 'quickfind', 'scroll', 'follow',
  'session-restore', 'read-while-indexing', 'memory', 'other',
];
const SCENARIO_PREFIXES = [
  ['gui_startup', 'startup'],
  ['gui_open_', 'open-and-index'],
  ['gui_search_', 'search'],
  ['grep_', 'grep'],
  ['gui_quickfind_', 'quickfind'],
  ['gui_scroll_', 'scroll'],
  ['gui_follow_', 'follow'],
  ['gui_session_restore_', 'session-restore'],
  ['gui_read_while_indexing_', 'read-while-indexing'],
];

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

/** The recorded runs of master in dataDir/history, oldest first, and the
 *  names of the files that could not be read. No directory is no runs. */
export function loadRuns(dataDir) {
  const dir = dataDir ? join(dataDir, 'history') : null;
  if (!dir || !existsSync(dir)) return { runs: [], skipped: [] };
  const runs = [];
  const skipped = [];
  for (const name of readdirSync(dir).filter((n) => n.endsWith('.json')).sort()) {
    try {
      const entry = JSON.parse(readFileSync(join(dir, name), 'utf8'));
      if (typeof entry?.recorded_at !== 'string' || Number.isNaN(Date.parse(entry.recorded_at))) {
        throw new Error('no recorded_at');
      }
      runs.push({ ...entry, _file: name });
    } catch {
      skipped.push(name);
    }
  }
  runs.sort((a, b) => a.recorded_at.localeCompare(b.recorded_at) || a._file.localeCompare(b._file));
  return { runs, skipped };
}

/** The runs (oldest first) recorded within days before the latest one. */
export function recentRuns(runs, days = WINDOW_DAYS) {
  if (!runs.length) return [];
  const since = Date.parse(runs.at(-1).recorded_at) - days * DAY;
  return runs.filter((run) => Date.parse(run.recorded_at) >= since);
}

/** tests/e2e/budgets.json, or null when it is missing or unreadable. */
export function loadBudgets(file = BUDGETS_FILE) {
  try {
    return JSON.parse(readFileSync(file, 'utf8'));
  } catch {
    return null;
  }
}

// ---------------------------------------------------------------------------
// Series
// ---------------------------------------------------------------------------

function scenarioOf(name) {
  return SCENARIO_PREFIXES.find(([prefix]) => name.startsWith(prefix))?.[1] ?? 'other';
}

function isNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

function countsGroup(run) {
  const counts = run.instruction_counts ?? {};
  return `${counts.cpu || 'unknown CPU'}, ${counts.log_file_mb ?? '?'} MiB Log Files`;
}

function pointOf(run, value, label) {
  return {
    t: Date.parse(run.recorded_at),
    value,
    version: run.version ?? '',
    commit: String(run.commit ?? '').slice(0, 12),
    usable: true,
    group: label,
  };
}

/** The groups (lines) of one chart from (run, value, group label) triples. */
function groupsOf(samples, slotOf) {
  const groups = new Map();
  for (const point of samples) {
    if (!groups.has(point.group)) groups.set(point.group, { label: point.group, slot: slotOf(point.group), points: [] });
    groups.get(point.group).points.push(point);
  }
  return [...groups.values()];
}

function describe(entry) {
  return entry.file && entry.file !== 'none' ? `${entry.metric}; ${entry.file}` : entry.metric ?? '';
}

function budgetOf(entry) {
  if (!entry || !isNumber(entry.budget)) return null;
  const margin = isNumber(entry.noise_margin_percent) ? entry.noise_margin_percent : 0;
  return { value: entry.budget, brokenAbove: entry.budget * (1 + margin / 100), noiseMarginPercent: margin };
}

function byScenario(charts, order) {
  const scenarios = new Map();
  for (const chart of charts) {
    if (!scenarios.has(chart.scenario)) scenarios.set(chart.scenario, { name: chart.scenario, charts: [] });
    scenarios.get(chart.scenario).charts.push(chart);
  }
  const rank = (name) => (order.includes(name) ? order.indexOf(name) : order.length);
  return [...scenarios.values()].sort((a, b) => rank(a.name) - rank(b.name) || a.name.localeCompare(b.name));
}

/**
 * The trend of runs (oldest first): sections (wall-clock, instruction
 * counts), each a list of scenarios with their charts, the time domain all
 * charts share and the legend of the line groups (CPU models), whose color
 * slots stay the same in every chart.
 */
export function buildTrend(runs, budgets) {
  const legend = [];
  const slotOf = (label) => {
    let item = legend.find((l) => l.label === label);
    if (!item) {
      item = { label, slot: legend.length < SLOTS ? legend.length : 'other' };
      legend.push(item);
    }
    return item.slot;
  };
  const budgetEntries = Object.entries(budgets?.budgets ?? {});
  const e2eBudgets = new Map(budgetEntries.filter(([, e]) => e.report === 'e2e').map(([, e]) => [e.benchmark, e]));
  const latest = runs.at(-1);
  // Slots in order of first appearance: the runner CPU models, then the
  // groups of the counts.
  for (const run of runs) slotOf(run.system?.cpu || 'unknown CPU');
  for (const run of runs) if (run.instruction_counts?.benchmarks) slotOf(countsGroup(run));

  // Wall-clock: what the latest run measured; Budgets first, in their order.
  const measured = Object.keys(latest?.benchmarks ?? {});
  const budgeted = [...e2eBudgets.keys()].filter((name) => measured.includes(name));
  const others = measured.filter((name) => !e2eBudgets.has(name)).sort();
  const wallCharts = [...budgeted, ...others].map((name) => {
    const budget = e2eBudgets.get(name);
    const field = budget?.field ?? 'median_seconds';
    const scale = isNumber(budget?.scale) ? budget.scale : 1;
    const samples = [];
    for (const run of runs) {
      const value = run.benchmarks?.[name]?.[field];
      if (!isNumber(value)) continue;
      const point = pointOf(run, value * scale, run.system?.cpu || 'unknown CPU');
      point.usable = run.usable !== false;
      samples.push(point);
    }
    return {
      key: name,
      title: name,
      scenario: budget?.scenario ?? scenarioOf(name),
      headline: Boolean(budget),
      description: budget ? describe(budget) : '',
      unit: budget?.unit ?? 's',
      budget: budgetOf(budget),
      groups: groupsOf(samples, slotOf),
    };
  });

  // The counted Budgets (memory) and the instruction counts, from the latest
  // run that has counts.
  const latestCounted = runs.findLast((run) => run.instruction_counts?.benchmarks);
  const counted = Object.keys(latestCounted?.instruction_counts?.benchmarks ?? {});
  const countSeries = (name, field, scale) => {
    const samples = [];
    for (const run of runs) {
      const value = run.instruction_counts?.benchmarks?.[name]?.[field];
      if (isNumber(value)) samples.push(pointOf(run, value * scale, countsGroup(run)));
    }
    return groupsOf(samples, slotOf);
  };
  for (const [key, entry] of budgetEntries) {
    if (entry.report !== 'instruction-counts' || !counted.includes(entry.benchmark)) continue;
    wallCharts.push({
      key,
      title: key,
      scenario: entry.scenario ?? 'other',
      headline: true,
      description: describe(entry),
      unit: entry.unit ?? '',
      budget: budgetOf(entry),
      groups: countSeries(entry.benchmark, entry.field, isNumber(entry.scale) ? entry.scale : 1),
    });
  }
  const countCharts = counted.sort().map((name) => {
    const [binary, ...rest] = name.split(' / ');
    return {
      key: name,
      title: rest.length ? rest.join(' / ') : name,
      scenario: binary,
      headline: false,
      description: '',
      unit: 'instructions',
      budget: null,
      groups: countSeries(name, 'instructions', 1),
    };
  });

  const sections = [];
  if (wallCharts.length) sections.push({ metric: 'wall-clock', scenarios: byScenario(wallCharts, SCENARIO_ORDER) });
  if (countCharts.length) sections.push({ metric: 'instructions', scenarios: byScenario(countCharts, []) });

  let domain = null;
  if (runs.length) {
    const times = runs.map((run) => Date.parse(run.recorded_at));
    domain = [Math.min(...times), Math.max(...times)];
    if (domain[0] === domain[1]) domain = [domain[0] - DAY, domain[1] + DAY];
  }
  return { sections, domain, legend };
}

/** The releases (src/releases.mjs) inside domain, as markers; not the klogg era. */
export function releaseMarkers(releases, domain) {
  if (!domain) return [];
  return releases
    .filter((release) => release.channel !== 'legacy' && /^\d{4}-\d{2}-\d{2}$/.test(release.date))
    .map((release) => ({ label: release.label, slug: release.slug, t: Date.parse(`${release.date}T12:00:00Z`) }))
    .filter((marker) => marker.t >= domain[0] && marker.t <= domain[1])
    .sort((a, b) => a.t - b.t);
}

/** Everything the page shows: the trend of the perf-data checkout in dataDir. */
export function readPerfTrend({ dataDir = process.env[PERF_DATA_ENV], budgetsFile = BUDGETS_FILE, releases = [] } = {}) {
  const loaded = loadRuns(dataDir);
  const { skipped } = loaded;
  for (const name of skipped) console.warn(`perf-trend: skipped unreadable run ${name}`);
  const runs = recentRuns(loaded.runs);
  const budgets = loadBudgets(budgetsFile);
  const trend = buildTrend(runs, budgets);
  return {
    ...trend,
    runCount: runs.length,
    allRunCount: loaded.runs.length,
    skipped,
    first: runs[0]?.recorded_at ?? null,
    latest: runs.at(-1) ?? null,
    budgetsAccepted: String(budgets?.status ?? '').startsWith('accepted'),
    markers: releaseMarkers(releases, trend.domain),
  };
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

function significant(value, digits = 3) {
  return String(Number(value.toPrecision(digits)));
}

/** A value as text in the unit of its chart. */
export function formatValue(value, unit) {
  if (!isNumber(value)) return '–';
  const size = Math.abs(value);
  if (unit === 's') {
    if (size === 0) return '0 s';
    if (size < 0.001) return `${significant(value * 1e6)} µs`;
    if (size < 1) return `${significant(value * 1e3)} ms`;
    return `${value.toFixed(2)} s`;
  }
  if (unit.startsWith('bytes')) {
    if (size >= 1e9) return `${(value / 1e9).toFixed(2)} GB`;
    if (size >= 1e6) return `${(value / 1e6).toFixed(2)} MB`;
    if (size >= 1e3) return `${(value / 1e3).toFixed(2)} KB`;
    return `${value} B`;
  }
  if (unit === 'instructions') {
    if (size >= 1e9) return `${(value / 1e9).toFixed(2)} G`;
    if (size >= 1e6) return `${(value / 1e6).toFixed(2)} M`;
    return Math.round(value).toLocaleString('en-US');
  }
  return `${significant(value)} ${unit}`.trim();
}

/** Round axis ticks from at or below lo to at or above hi, about count steps. */
export function niceTicks(lo, hi, count = 4) {
  if (!(hi > lo)) {
    const pad = Math.abs(lo) * 0.05 || 1;
    [lo, hi] = [lo - pad, hi + pad];
  }
  const raw = (hi - lo) / count;
  const power = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * power).find((s) => s >= raw * 0.999);
  const start = Math.floor(lo / step + 1e-9) * step;
  const ticks = [];
  for (let i = 0; start + i * step < hi + step * 0.999; i++) ticks.push(Number((start + i * step).toPrecision(12)));
  return ticks;
}

function escape(text) {
  return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function day(t) {
  return new Date(t).toISOString().slice(0, 10);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

const WIDTH = 300;
const HEIGHT = 180;
const PLOT = { left: 56, right: 10, top: 22, bottom: 24 };

function round(value) {
  return Math.round(value * 10) / 10;
}

/** One chart as an SVG element; perf-trend.css styles its classes. A chart
 *  with a Budget marks every run, with its date, version, commit and runner on
 *  hover; any other only its latest run and the unusable ones, since a marked
 *  point per night and chart is most of what the page weighs. */
export function renderChart(chart, { domain, markers = [] }) {
  const points = chart.groups.flatMap((g) => g.points);
  const values = points.map((p) => p.value);
  if (chart.budget) values.push(chart.budget.value);
  // Times start at zero, so a Budget's distance reads true; counts without a
  // Budget zoom in, where +2 % is the gate.
  const zeroBased = chart.budget || chart.unit !== 'instructions';
  const lo = zeroBased ? 0 : Math.min(...values);
  const ticks = niceTicks(lo, Math.max(...values) * (zeroBased ? 1.05 : 1), 4);
  const [y0, y1] = [ticks[0], ticks.at(-1)];
  const plotWidth = WIDTH - PLOT.left - PLOT.right;
  const plotHeight = HEIGHT - PLOT.top - PLOT.bottom;
  const x = (t) => round(PLOT.left + ((t - domain[0]) / (domain[1] - domain[0])) * plotWidth);
  const y = (v) => round(PLOT.top + (1 - (v - y0) / (y1 - y0)) * plotHeight);
  const right = WIDTH - PLOT.right;
  const bottom = HEIGHT - PLOT.bottom;

  const latest = points.reduce((a, b) => (b.t >= (a?.t ?? -Infinity) ? b : a), null);
  const summary = [
    `${chart.title}`,
    latest ? `latest ${formatValue(latest.value, chart.unit)} on ${day(latest.t)}` : 'no runs',
    chart.budget ? `Budget ${formatValue(chart.budget.value, chart.unit)}` : '',
  ].filter(Boolean).join(', ');

  const out = [`<svg viewBox="0 0 ${WIDTH} ${HEIGHT}" class="perf-chart" role="img" aria-label="${escape(summary)}">`];
  for (const tick of ticks) {
    out.push(`<line class="perf-grid" x1="${PLOT.left}" x2="${right}" y1="${y(tick)}" y2="${y(tick)}"/>`);
    out.push(`<text class="perf-axis" x="${PLOT.left - 6}" y="${y(tick) + 3.5}" text-anchor="end">${escape(formatValue(tick, chart.unit))}</text>`);
  }
  out.push(`<text class="perf-axis" x="${PLOT.left}" y="${HEIGHT - 6}">${day(domain[0])}</text>`);
  out.push(`<text class="perf-axis" x="${right}" y="${HEIGHT - 6}" text-anchor="end">${day(domain[1])}</text>`);

  let lastLabel = -Infinity;
  for (const marker of markers) {
    const mx = x(marker.t);
    out.push(`<line class="perf-release" x1="${mx}" x2="${mx}" y1="${PLOT.top - 4}" y2="${bottom}"><title>${escape(marker.label)} released</title></line>`);
    if (mx - lastLabel >= 60 && mx <= right - 40) {
      out.push(`<text class="perf-release-label" x="${mx + 3}" y="${PLOT.top - 8}">${escape(marker.label)}</text>`);
      lastLabel = mx;
    }
  }

  if (chart.budget) {
    const by = y(chart.budget.value);
    const text = `Budget ${formatValue(chart.budget.value, chart.unit)}`;
    out.push(`<line class="perf-budget" x1="${PLOT.left}" x2="${right}" y1="${by}" y2="${by}"><title>${escape(text)}, broken above ${escape(formatValue(chart.budget.brokenAbove, chart.unit))}</title></line>`);
    out.push(`<text class="perf-budget-label" x="${right - 2}" y="${by - 4}" text-anchor="end">${escape(text)}</text>`);
  }

  for (const group of chart.groups) {
    const usable = group.points.filter((p) => p.usable);
    if (usable.length >= 2) {
      out.push(`<polyline class="perf-line perf-slot-${group.slot}" points="${usable.map((p) => `${x(p.t)},${y(p.value)}`).join(' ')}"/>`);
    }
    const latestOfGroup = group.points.at(-1);
    for (const p of group.points) {
      if (!chart.headline && p.usable && p !== latestOfGroup) continue;
      const note = p.usable ? '' : ` · unusable: within-run CV above ${MAX_USABLE_CV_PERCENT} %`;
      const title = `${day(p.t)} · ${p.version} (${p.commit}) · ${formatValue(p.value, chart.unit)} · ${group.label}${note}`;
      out.push(`<circle class="perf-point${p.usable ? '' : ' perf-unusable'} perf-slot-${group.slot}" cx="${x(p.t)}" cy="${y(p.value)}" r="3"><title>${escape(title)}</title></circle>`);
    }
  }
  out.push('</svg>');
  return out.join('');
}
