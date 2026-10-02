"""Tests for perf_changepoint.py (#677): a regression is a change point in a
benchmark's series of nightly runs, not one run against a threshold."""

from __future__ import annotations

import pytest

import perf_changepoint

# A rule small enough to read the series of a test at a glance.
RULE = perf_changepoint.Rule(window=6, min_history=4, persistence=2,
                             tolerance_percent=10.0, min_delta_cap=0.0)


def test_a_flat_series_is_ok():
    d = perf_changepoint.detect([1.0] * 8, RULE)
    assert d.status == "ok"
    assert d.reference == 1.0
    assert d.limit == pytest.approx(1.1)
    assert d.history_count == 6
    assert d.change_index is None


def test_a_shift_that_persists_is_a_regression_at_its_first_run():
    #             0    1    2    3    4    5    6    7
    d = perf_changepoint.detect([1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 1.5, 1.5], RULE)
    assert d.status == "regression"
    assert d.change_index == 5
    assert d.streak == 3
    # The reference is the level before the change, not a median that
    # already holds part of it.
    assert d.reference == 1.0


def test_one_run_above_the_limit_is_pending_until_it_persists():
    d = perf_changepoint.detect([1.0] * 6 + [1.5], RULE)
    assert d.status == "pending"
    assert d.change_index is None


def test_one_odd_run_in_the_past_does_not_count():
    d = perf_changepoint.detect([1.0, 1.0, 1.0, 1.0, 1.5, 1.0, 1.5], RULE)
    assert d.status == "pending"


def test_up_to_the_tolerance_is_ok():
    assert perf_changepoint.detect([1.0] * 6 + [1.09, 1.09], RULE).status == "ok"
    assert perf_changepoint.detect([1.0] * 6 + [1.11, 1.11], RULE).status == "regression"


def test_a_series_that_scatters_gets_room_for_its_spread():
    # Runners of two CPU models: the medians alternate by 30 %; the limit
    # leaves three interquartile ranges of the reference runs.
    scatter = [1.0, 1.3, 1.0, 1.3, 1.0, 1.3]
    assert perf_changepoint.detect(scatter + [1.3, 1.3], RULE).status == "ok"
    assert perf_changepoint.detect(scatter + [3.0, 3.0], RULE).status == "regression"


def test_the_reference_is_the_window_before_the_change():
    # The first 10.0 is outside the window of 6 runs before the change.
    series = [10.0] + [1.0] * 6 + [2.0, 2.0]
    d = perf_changepoint.detect(series, RULE)
    assert d.status == "regression"
    assert d.change_index == 7
    assert d.reference == 1.0
    assert d.history_count == 6


def test_a_long_regression_stays_one_change_point():
    # The median of the last runs would have caught up with it; the change
    # point does not move.
    series = [1.0] * 6 + [2.0] * 10
    d = perf_changepoint.detect(series, RULE)
    assert d.status == "regression"
    assert d.change_index == 6
    assert d.streak == 10
    assert d.reference == 1.0


def test_a_regression_that_was_fixed_is_ok_again():
    assert perf_changepoint.detect([1.0] * 6 + [2.0, 2.0, 1.0], RULE).status == "ok"


def test_too_little_history_reports_only():
    d = perf_changepoint.detect([1.0, 1.0, 1.0, 5.0], RULE)
    assert d.status == "warming-up"
    assert d.above_limit
    assert d.history_count == 3


def test_the_change_point_needs_the_minimal_history_before_it():
    # Five runs before the change are enough for a reference of four.
    d = perf_changepoint.detect([1.0] * 4 + [2.0, 2.0], RULE)
    assert d.status == "regression"
    assert d.change_index == 4


def test_a_single_run_is_new():
    d = perf_changepoint.detect([1.0], RULE)
    assert d.status == "new"
    assert d.reference is None


def test_much_faster_is_reported_as_faster():
    assert perf_changepoint.detect([1.0] * 6 + [0.5], RULE).status == "faster"


def test_persistence_of_one_flags_the_first_run():
    counts = perf_changepoint.Rule(window=6, min_history=3, persistence=1,
                                   tolerance_percent=2.0, min_delta_cap=0.0)
    d = perf_changepoint.detect([1000.0] * 3 + [1030.0], counts)
    assert d.status == "regression"
    assert d.change_index == 3


def test_the_tolerance_and_the_absolute_margin_can_be_set_per_series():
    assert perf_changepoint.detect([1.0] * 6 + [1.15, 1.15], RULE,
                                   tolerance_percent=20.0).status == "ok"
    # An absolute margin of 0.5 keeps +40 % inside.
    assert perf_changepoint.detect([1.0] * 6 + [1.4, 1.4], RULE, min_delta=0.5).status == "ok"


def test_a_read_of_microseconds_is_not_inside_a_margin_of_milliseconds():
    # #705: 0.1 µs to 60 µs; the margin scales with the benchmark.
    wall = perf_changepoint.Rule(window=6, min_history=4, persistence=2,
                                 tolerance_percent=10.0, min_delta_cap=0.010)
    assert perf_changepoint.detect([0.1e-6] * 6 + [60e-6, 60e-6], wall).status == "regression"
    # 20 ms -> 28 ms is +40 % but inside the cap of 10 ms.
    assert perf_changepoint.detect([0.020] * 6 + [0.028, 0.028], wall).status == "ok"


def test_iqr():
    assert perf_changepoint.iqr([1.0]) == 0.0
    assert perf_changepoint.iqr([1.0, 1.0, 1.0, 2.0]) == pytest.approx(0.25)


def test_the_spread_within_the_reference_runs_gives_room_too():
    # ADR 0018: a benchmark whose runs scatter (the IQR of its runs, kept as
    # iqr_seconds) gets three times that, even when its medians agree.
    values = [232e-6] * 6 + [515e-6, 515e-6]
    wall = perf_changepoint.Rule(window=6, min_history=4, persistence=2,
                                 tolerance_percent=10.0, min_delta_cap=0.010)
    assert perf_changepoint.detect(values, wall).status == "regression"
    spreads = [529e-6] * 6 + [None, None]
    assert perf_changepoint.detect(values, wall, spreads=spreads).status == "ok"
