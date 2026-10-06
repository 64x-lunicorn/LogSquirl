"""Tests for check-shown-widget-wait.py (#754), whose docstring holds the rule.
No build, no Qt."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "check_shown_widget_wait", Path(__file__).with_name("check-shown-widget-wait.py"))
sw = importlib.util.module_from_spec(_SPEC)
sys.modules["check_shown_widget_wait"] = sw
_SPEC.loader.exec_module(sw)


# -- which show() waits ------------------------------------------------------

def test_a_show_followed_by_nothing_that_waits_is_reported_with_its_line():
    source = """\
TEST_CASE( "paints" )
{
    view.show();
    QCoreApplication::processEvents();
}
"""
    assert sw.unwaited_shows(source) == [3]


def test_a_show_followed_by_a_wait_for_exposure_passes():
    source = """\
    view.show();
    REQUIRE( QTest::qWaitForWindowExposed( &view ) );
"""
    assert sw.unwaited_shows(source) == []


def test_a_show_activated_and_waited_for_until_active_passes():
    # An active window is an exposed one.
    source = """\
    mainWindow->show();
    mainWindow->activateWindow();
    REQUIRE( QTest::qWaitForWindowActive( mainWindow.get(), 5000 ) );
"""
    assert sw.unwaited_shows(source) == []


def test_comments_and_strings_are_not_code():
    source = """\
    // The show()/repaint() path exercises that code.
    /* view.show();
       nothing */
    const auto name = "show()";
    view.show();
    // Exposed before anything is measured.

    REQUIRE( QTest::qWaitForWindowExposed( &view ) );
    view.show(); /* a comment, then the wait */ REQUIRE( QTest::qWaitForWindowExposed( &view ) );
"""
    assert sw.unwaited_shows(source) == []


def test_other_calls_named_like_show_are_not_shows():
    source = """\
    combo->showPopup();
    view.showWide();
    reshow();
    dialog->setVisible( true );
"""
    assert sw.unwaited_shows(source) == []


def test_a_wait_inside_a_comment_does_not_count():
    source = """\
    view.show();
    // REQUIRE( QTest::qWaitForWindowExposed( &view ) );
    view.repaint();
"""
    assert sw.unwaited_shows(source) == [1]


# -- what counts as a wait ----------------------------------------------------

def test_a_wait_inside_a_lambda_that_is_never_called_does_not_count():
    source = """\
    window->show();
    const auto exposed = [ & ] { return QTest::qWaitForWindowExposed( window.get() ); };
"""
    assert sw.unwaited_shows(source) == [1]


def test_a_wait_that_never_runs_does_not_count():
    source = """\
    view.show();
    if ( false ) REQUIRE( QTest::qWaitForWindowExposed( &view ) );
"""
    assert sw.unwaited_shows(source) == [1]


def test_a_wait_whose_result_is_dropped_does_not_count():
    source = """\
    view.show();
    QTest::qWaitForWindowExposed( &view );
"""
    assert sw.unwaited_shows(source) == [1]


def test_a_wait_for_another_widget_does_not_count():
    source = """\
    view.show();
    REQUIRE( QTest::qWaitForWindowExposed( &other ) );
"""
    assert sw.unwaited_shows(source) == [1]


def test_a_wait_assigned_to_a_variable_that_is_never_checked_does_not_count():
    source = """\
    view.show();
    const bool exposed = QTest::qWaitForWindowExposed( &view );
    view.repaint();
"""
    assert sw.unwaited_shows(source) == [1]


def test_a_wait_checked_with_check_passes():
    source = """\
    view.show();
    CHECK( QTest::qWaitForWindowFocused( &view, 2000 ) );
"""
    assert sw.unwaited_shows(source) == []


def test_a_wait_assigned_to_a_variable_checked_later_passes():
    # As in a callback that cannot REQUIRE itself.
    source = """\
    bool openedExposed = false;
    connect( this, &Opener::opened, [ & ] {
        opened->show();
        openedExposed = QTest::qWaitForWindowExposed( opened.get() );
    } );
    open();
    REQUIRE( openedExposed );
"""
    assert sw.unwaited_shows(source) == []


def test_a_wait_spread_over_lines_passes():
    source = """\
    mainWindow->show();
    REQUIRE( QTest::qWaitForWindowActive( mainWindow.get(),
                                          5000 ) );
"""
    assert sw.unwaited_shows(source) == []


def test_the_shown_object_and_the_waited_one_are_compared_however_they_are_spelled():
    source = """\
    view.show();
    REQUIRE( QTest::qWaitForWindowExposed( &view ) );
    window->show();
    REQUIRE( QTest::qWaitForWindowExposed( window.get() ) );
    raw->show();
    REQUIRE( QTest::qWaitForWindowExposed( raw ) );
    owned->show();
    showUntilExposed( *owned );
    show();
    REQUIRE( QTest::qWaitForWindowExposed( this ) );
"""
    assert sw.unwaited_shows(source) == []


def test_show_until_exposed_of_another_widget_does_not_count():
    source = """\
    view.show();
    showUntilExposed( other );
"""
    assert sw.unwaited_shows(source) == [1]


# -- a deferred wait ------------------------------------------------------------

def test_a_show_marked_deferred_passes_when_a_checked_wait_on_it_follows_later():
    source = """\
    window->show(); // shown-widget-wait: deferred, the file is opened before the window is exposed
    window->loadInitialFile( path, false );
    REQUIRE( QTest::qWaitForWindowExposed( window.get() ) );
"""
    assert sw.unwaited_shows(source) == []


def test_a_show_marked_deferred_fails_when_no_checked_wait_on_it_follows():
    source = """\
    REQUIRE( QTest::qWaitForWindowExposed( window.get() ) );
    window->show(); // shown-widget-wait: deferred, the file is opened before the window is exposed
    window->loadInitialFile( path, false );
    REQUIRE( QTest::qWaitForWindowExposed( other.get() ) );
    QTest::qWaitForWindowExposed( window.get() );
"""
    assert sw.unwaited_shows(source) == [2]


def test_a_deferred_marker_without_a_reason_does_not_count():
    source = """\
    window->show(); // shown-widget-wait: deferred,
    window->loadInitialFile( path, false );
    REQUIRE( QTest::qWaitForWindowExposed( window.get() ) );
"""
    assert sw.unwaited_shows(source) == [1]


def test_a_deferred_marker_inside_a_string_does_not_count():
    source = """\
    window->show(); log( "// shown-widget-wait: deferred, because" );
    window->loadInitialFile( path, false );
    REQUIRE( QTest::qWaitForWindowExposed( window.get() ) );
"""
    assert sw.unwaited_shows(source) == [1]


# -- the tests of a tree ------------------------------------------------------

UNWAITED = "    view.show();\n    view.repaint();\n"
WAITED = "    view.show();\n    REQUIRE( QTest::qWaitForWindowExposed( &view ) );\n"


def tree(tmp_path: Path, files: dict[str, str]) -> Path:
    for name, text in files.items():
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return tmp_path


def test_a_test_that_does_not_wait_fails_with_its_file_and_line(tmp_path):
    root = tree(tmp_path, {"tests/ui/new_test.cpp": "#include <QTest>\n" + UNWAITED,
                           "tests/ui/good_test.cpp": WAITED})
    assert sw.problems(root) == [
        "::error file=tests/ui/new_test.cpp,line=2::tests/ui/new_test.cpp:2: show() is not followed by "
        "a wait until the widget is exposed; use showUntilExposed() from tests/helpers/shown_widget.h"]


def test_every_test_is_held_to_the_wait_wherever_it_lives(tmp_path):
    root = tree(tmp_path, {"tests/unit/a_test.cpp": UNWAITED, "tests/benchmarks/b_benchmark.cpp": UNWAITED,
                           "tests/helpers/c.h": WAITED + UNWAITED})
    assert [problem.split("::")[1] for problem in sw.problems(root)] == [
        "error file=tests/benchmarks/b_benchmark.cpp,line=1",
        "error file=tests/helpers/c.h,line=3",
        "error file=tests/unit/a_test.cpp,line=1"]


def test_only_test_sources_are_checked(tmp_path):
    root = tree(tmp_path, {"src/ui/widget.cpp": UNWAITED, "tests/e2e/notes.txt": UNWAITED})
    assert sw.problems(root) == []


def test_the_check_fails_on_a_test_that_does_not_wait_and_passes_once_it_waits(tmp_path, capsys):
    root = tree(tmp_path, {"tests/ui/old_test.cpp": UNWAITED})
    assert sw.main(["--root", str(root)]) == 1
    assert "tests/ui/old_test.cpp:1: show() is not followed" in capsys.readouterr().out

    (root / "tests/ui/old_test.cpp").write_text(WAITED, encoding="utf-8")
    assert sw.main(["--root", str(root)]) == 0
    assert capsys.readouterr().out == "Every show() under tests/ waits until its widget is exposed.\n"


def test_an_unclosed_quote_keeps_the_line_numbers():
    source = """\
#error it's not closed
    view.show();
    view.repaint();
"""
    assert sw.unwaited_shows(source) == [2]


def test_a_digit_separator_does_not_open_a_character_literal():
    source = """\
    resize( 1'000, 600 ); view.show();
    view.repaint();
"""
    assert sw.unwaited_shows(source) == [1]
