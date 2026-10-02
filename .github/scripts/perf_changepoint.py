"""Finds where a benchmark's series of nightly runs moved up (#677).

The nightly Performance workflow keeps one value per benchmark and run on the
perf-data branch: the median wall-clock of the e2e suite, and the instruction
count of each Catch2 benchmark. A regression is a *change point* in that
series: a run from which on every run, up to the latest, is above the limit
of the runs before it.

For a series v[0..n-1], oldest first, the latest run last, and a candidate
change point c:

    reference(c) = median of the `window` runs before c
    limit(c)     = perf_margin's rule on reference(c): reference
                   + max(tolerance * reference, min_delta, 3 * spread)
    spread       = the interquartile range of those runs' values, or the
                   median IQR within those runs (a wall-clock run's own
                   scatter, iqr_seconds) when that is larger
    c holds      when v[c], v[c+1], ..., v[n-1] are all above limit(c) and at
                 least `min_history` runs come before c

The change point is the earliest c that holds. It is a regression once it
has lasted `persistence` runs (n - c >= persistence); before that, a latest
run above the limit is "pending". The run before c is the last good one, c the
first bad one: the commits between them are where to look.

Why this and not the latest run against the median of the last runs: the
reference is the level before the change, so a regression that lasts does not
pull the reference up and heal itself after a few runs, and the change point,
with it the commit range, stays where it happened. The interquartile range of
the reference runs gives a series that scatters (runners of several CPU
models) room for its own scatter, and one that does not none; persistence
keeps one odd run from counting. What it does not see is a slow creep of a
few percent per run that never steps over the limit at once.
"""

from __future__ import annotations

from dataclasses import dataclass
from statistics import median, quantiles

import perf_margin


@dataclass(frozen=True)
class Rule:
    window: int               # runs before a change point that form its reference
    min_history: int          # fewer runs before it: report only
    persistence: int          # runs a change must last to be a regression
    tolerance_percent: float  # how much above the reference is still the same level
    min_delta_cap: float      # perf_margin's cap of the absolute margin (0: none)


@dataclass
class Detection:
    status: str  # regression | pending | ok | faster | warming-up | new
    reference: float | None
    limit: float | None
    history_count: int
    change_index: int | None = None  # the first bad run, for a regression
    streak: int = 0                  # runs from the change point to the latest
    above_limit: bool = False        # the latest run is above the limit


def iqr(values: list[float]) -> float:
    """The interquartile range, 0 for fewer than two values."""
    if len(values) < 2:
        return 0.0
    q1, _, q3 = quantiles(values, n=4, method="inclusive")
    return q3 - q1


def _limit(before: list[float], spreads: list[float], rule: Rule, tolerance_percent: float,
           min_delta: float | None) -> tuple[float, float]:
    reference = median(before)
    spread = max(iqr(before), median(spreads) if spreads else 0.0)
    if min_delta is None and rule.min_delta_cap <= 0:
        min_delta = 0.0
    return reference, perf_margin.limit_seconds(
        reference, tolerance_percent=tolerance_percent,
        min_delta_cap_seconds=rule.min_delta_cap, spread_seconds=spread,
        min_delta_seconds=min_delta)


def detect(values: list[float], rule: Rule, *, tolerance_percent: float | None = None,
           min_delta: float | None = None,
           spreads: list[float | None] | None = None) -> Detection:
    """Where `values` (oldest first, the latest run last) moved up, if it did.

    tolerance_percent and min_delta replace the rule's for this series (a
    benchmark's own threshold, or a Budget's min_delta_seconds); spreads,
    beside values, is each run's own interquartile range where it has one.
    """
    tolerance = rule.tolerance_percent if tolerance_percent is None else tolerance_percent
    n = len(values)
    if n < 2:
        return Detection("new", None, None, 0)

    def window_before(c: int) -> list[float]:
        return values[max(0, c - rule.window):c]

    def spreads_before(c: int) -> list[float]:
        return [s for s in (spreads or [])[max(0, c - rule.window):c] if s is not None]

    for c in range(rule.min_history, n):
        reference, limit = _limit(window_before(c), spreads_before(c), rule, tolerance, min_delta)
        if all(v > limit for v in values[c:]):
            if n - c >= rule.persistence:
                return Detection("regression", reference, limit, len(window_before(c)),
                                 change_index=c, streak=n - c, above_limit=True)
            break

    before = window_before(n - 1)
    reference, limit = _limit(before, spreads_before(n - 1), rule, tolerance, min_delta)
    above = values[-1] > limit
    if len(before) < rule.min_history:
        status = "warming-up"
    elif above:
        status = "pending"
    elif values[-1] < reference - (limit - reference):
        status = "faster"
    else:
        status = "ok"
    return Detection(status, reference, limit, len(before), above_limit=above)
