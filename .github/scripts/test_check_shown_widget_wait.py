"""Tests for check-shown-widget-wait.py (#754): which show() of a test waits
until its widget is exposed, and how the allowlist of files that do not yet
wait only shrinks. No build, no Qt."""

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


# -- the tests of a tree, and the allowlist -----------------------------------

UNWAITED = "    view.show();\n    view.repaint();\n"
WAITED = "    view.show();\n    REQUIRE( QTest::qWaitForWindowExposed( &view ) );\n"


def tree(tmp_path: Path, files: dict[str, str]) -> Path:
    for name, text in files.items():
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return tmp_path


def test_a_new_test_that_does_not_wait_fails_with_its_file_and_line(tmp_path):
    root = tree(tmp_path, {"tests/ui/new_test.cpp": "#include <QTest>\n" + UNWAITED,
                           "tests/ui/good_test.cpp": WAITED})
    assert sw.problems(root, allowlist=[]) == [
        "::error file=tests/ui/new_test.cpp,line=2::tests/ui/new_test.cpp:2: show() is not followed by "
        "a wait until the widget is exposed; use showUntilExposed() from tests/helpers/shown_widget.h"]


def test_only_test_sources_are_checked(tmp_path):
    root = tree(tmp_path, {"src/ui/widget.cpp": UNWAITED, "tests/e2e/notes.txt": UNWAITED})
    assert sw.problems(root, allowlist=[]) == []


def test_a_file_on_the_allowlist_may_still_not_wait(tmp_path):
    root = tree(tmp_path, {"tests/unit/old_test.cpp": UNWAITED})
    assert sw.problems(root, allowlist=["tests/unit/old_test.cpp"]) == []


def test_a_file_on_the_allowlist_that_waits_everywhere_fails_until_its_entry_is_removed(tmp_path):
    root = tree(tmp_path, {"tests/unit/fixed_test.cpp": WAITED})
    assert sw.problems(root, allowlist=["tests/unit/fixed_test.cpp"]) == [
        f"::error file={sw.ALLOWLIST}::tests/unit/fixed_test.cpp now waits after every show(); "
        f"remove its entry from {sw.ALLOWLIST}"]


def test_a_file_on_the_allowlist_that_is_gone_fails_until_its_entry_is_removed(tmp_path):
    root = tree(tmp_path, {})
    assert sw.problems(root, allowlist=["tests/unit/gone_test.cpp"]) == [
        f"::error file={sw.ALLOWLIST}::tests/unit/gone_test.cpp now waits after every show(); "
        f"remove its entry from {sw.ALLOWLIST}"]


def test_an_entry_the_base_allowlist_does_not_have_is_rejected(tmp_path):
    root = tree(tmp_path, {"tests/unit/old_test.cpp": UNWAITED, "tests/ui/new_test.cpp": UNWAITED})
    assert sw.problems(root, allowlist=["tests/unit/old_test.cpp", "tests/ui/new_test.cpp"],
                       base=["tests/unit/old_test.cpp"]) == [
        f"::error file={sw.ALLOWLIST}::tests/ui/new_test.cpp was added to {sw.ALLOWLIST}; an entry "
        "may only be removed, so make the test wait instead"]


def test_the_allowlist_lists_one_path_per_line_with_comments():
    text = "# Files that do not wait yet (#754).\n\ntests/unit/a_test.cpp\n  tests/ui/b_test.cpp  # UI\n"
    assert sw.read_allowlist(text) == ["tests/unit/a_test.cpp", "tests/ui/b_test.cpp"]


def test_the_check_fails_on_a_test_that_does_not_wait_and_passes_once_it_is_listed(tmp_path, capsys):
    root = tree(tmp_path, {"tests/ui/old_test.cpp": UNWAITED, sw.ALLOWLIST: "# None yet.\n"})
    assert sw.main(["--root", str(root)]) == 1
    assert "tests/ui/old_test.cpp:1: show() is not followed" in capsys.readouterr().out

    (root / sw.ALLOWLIST).write_text("tests/ui/old_test.cpp\n", encoding="utf-8")
    assert sw.main(["--root", str(root)]) == 0


def test_the_check_reads_the_base_allowlist_when_it_is_given(tmp_path, capsys):
    root = tree(tmp_path, {"tests/ui/old_test.cpp": UNWAITED, sw.ALLOWLIST: "tests/ui/old_test.cpp\n",
                           "base.allowlist": "# Empty.\n"})
    assert sw.main(["--root", str(root), "--base-allowlist", str(root / "base.allowlist")]) == 1
    assert "an entry may only be removed" in capsys.readouterr().out
    # Before the allowlist existed there is no base to compare with.
    assert sw.main(["--root", str(root), "--base-allowlist", str(root / "missing.allowlist")]) == 0


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
