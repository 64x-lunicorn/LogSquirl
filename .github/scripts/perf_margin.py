"""How much slower than its reference a benchmark's median may be (#705, ADR 0018).

One rule for the e2e performance suite (tests/e2e/conftest.py, against
baseline.json) and the Performance workflow (perf-history.py, against the
median of the last recorded runs). Each passes its own tolerance and cap.

A median is a regression when it is above

    reference + max(tolerance * reference,
                    min_delta,
                    SPREAD_FACTOR * spread)

where min_delta, the absolute margin against scheduling noise, scales with
the benchmark:

    min_delta = min(cap, max(SCALE_FRACTION * reference, RESOLUTION_SECONDS))

unless the benchmark sets a margin of its own (`min_delta_seconds` beside its
entry), which replaces it. A benchmark of milliseconds keeps the cap (1 ms in
the suite, 10 ms in the history); a read of microseconds gets half of itself,
so a regression of several hundred times is never inside the margin; below
RESOLUTION_SECONDS nothing is told apart. `spread` is the interquartile range
of the reference's runs: a benchmark that scatters on its own gets room for
that, one that does not gets none.
"""

from __future__ import annotations

# The absolute margin is at most this fraction of the benchmark's reference.
SCALE_FRACTION = 0.5
# And at least this: the reports kept six decimals of a second until #705,
# and a clock read and a cache miss on a shared runner cost about as much.
RESOLUTION_SECONDS = 1e-6
# A benchmark may be this many interquartile ranges of its reference slower.
SPREAD_FACTOR = 3.0


def scaled_min_delta_seconds(reference: float, cap_seconds: float) -> float:
    """The absolute margin of a benchmark whose reference median is `reference`."""
    return min(cap_seconds, max(SCALE_FRACTION * reference, RESOLUTION_SECONDS))


def limit_seconds(reference: float, *, tolerance_percent: float, min_delta_cap_seconds: float,
                  spread_seconds: float = 0.0, min_delta_seconds: float | None = None) -> float:
    """The slowest median that still passes against `reference`."""
    if min_delta_seconds is None:
        min_delta_seconds = scaled_min_delta_seconds(reference, min_delta_cap_seconds)
    return reference + max(reference * tolerance_percent / 100,
                           min_delta_seconds,
                           SPREAD_FACTOR * spread_seconds)
