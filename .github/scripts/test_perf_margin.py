"""Tests for perf_margin.py (#705): how much slower than its reference a
benchmark's median may be before it counts as a regression.

The e2e suite (tests/e2e/conftest.py) and the Performance workflow
(perf-history.py) share this rule; each passes its own tolerance and cap."""

from __future__ import annotations

import pytest

import perf_margin as pm

# The e2e suite's numbers: 5 % tolerance, an absolute margin of at most 1 ms.
SUITE = {"tolerance_percent": 5, "min_delta_cap_seconds": 0.001}
# The nightly wall-clock series' (perf-history.py WALL_CLOCK, at a change
# point): 10 %, at most 10 ms.
HISTORY = {"tolerance_percent": 10, "min_delta_cap_seconds": 0.010}


@pytest.mark.parametrize("rule", [SUITE, HISTORY], ids=["suite", "history"])
def test_a_read_of_a_tenth_of_a_microsecond_that_takes_60_us_is_a_regression(rule):
    # #705: holding the index lock while parsing made getNbLine's p50 go from
    # about 0.1 µs to 60 µs; both absolute margins hid it.
    assert 60e-6 > pm.limit_seconds(0.1e-6, **rule)


@pytest.mark.parametrize("rule", [SUITE, HISTORY], ids=["suite", "history"])
def test_a_read_of_10_us_that_takes_90_us_is_a_regression(rule):
    # #705: getExpandedLines' p99 went from 10 µs to 90 µs, with the spread
    # the read-while-indexing runs show in CI (an IQR of a few µs).
    assert 90e-6 > pm.limit_seconds(10e-6, spread_seconds=3e-6, **rule)


def test_a_benchmark_of_milliseconds_keeps_its_absolute_margin():
    # 5 % of 2 ms is a scheduling hiccup: up to 1 ms slower still passes.
    assert pm.limit_seconds(0.002, **SUITE) == pytest.approx(0.003)
    assert pm.limit_seconds(0.020, **HISTORY) == pytest.approx(0.030)


def test_the_absolute_margin_is_at_most_half_the_benchmark():
    # A benchmark of 0.3 ms (a scroll frame) may not grow by 1 ms, three
    # times itself, unnoticed: half of it is the most the margin hides.
    assert pm.limit_seconds(0.0003, **SUITE) == pytest.approx(0.00045)
    assert pm.limit_seconds(0.004, **HISTORY) == pytest.approx(0.006)


def test_a_long_benchmark_is_judged_by_the_tolerance():
    assert pm.limit_seconds(1.0, **SUITE) == pytest.approx(1.05)
    assert pm.limit_seconds(1.0, **HISTORY) == pytest.approx(1.10)


def test_a_noisy_benchmark_gets_three_times_its_spread():
    # The 99th percentile read of a 1 GB indexing: 232 µs with an IQR of
    # 529 µs on one runner, 515 µs on another (Performance run 36962491329,
    # Benchmarks run 36938651506).
    limit = pm.limit_seconds(232e-6, spread_seconds=529e-6, **HISTORY)
    assert limit == pytest.approx(232e-6 + 3 * 529e-6)
    assert 515e-6 < limit


def test_below_a_microsecond_nothing_is_resolved():
    # The reports kept six decimals of a second until #705: a read of 0.1 µs
    # was recorded as 0, and 1 µs on the next run is no regression.
    assert pm.limit_seconds(0.0, **SUITE) == pytest.approx(1e-6)
    assert pm.limit_seconds(0.0, **HISTORY) == pytest.approx(1e-6)


def test_a_margin_set_for_the_benchmark_replaces_the_absolute_margin():
    assert pm.limit_seconds(0.002, min_delta_seconds=0.0, **SUITE) == pytest.approx(0.0021)
    assert pm.limit_seconds(10e-6, min_delta_seconds=0.0005, **SUITE) == pytest.approx(510e-6)


def test_a_margin_set_for_the_benchmark_does_not_switch_off_tolerance_or_spread():
    assert pm.limit_seconds(1.0, min_delta_seconds=0.0, **HISTORY) == pytest.approx(1.10)
    assert pm.limit_seconds(
        1e-3, min_delta_seconds=0.0, spread_seconds=1e-3, **SUITE
    ) == pytest.approx(4e-3)
