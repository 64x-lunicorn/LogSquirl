/*
 * Copyright (C) 2026 LogSquirl Contributors
 *
 * This file is part of LogSquirl.
 *
 * LogSquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LogSquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LogSquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch.hpp>

#include "logtablehighlightdelegate.h"

#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTableView>

namespace {

using LineTypeFlags = AbstractLogData::LineTypeFlags;

HighlighterSet setWithHighlighter( const QString& pattern, bool highlightOnlyMatch,
                                   const QColor& foreColor, const QColor& backColor )
{
    auto set = HighlighterSet::createNewSet( "test" );
    set.addHighlighter( Highlighter{ pattern, false, highlightOnlyMatch, foreColor, backColor } );
    return set;
}

LineDecorator::Context emptyDecoratorContext()
{
    return LineDecorator::Context{ HighlighterSet{},   std::nullopt,         {},
                                   QuickFindMatcher{}, QColor{ Qt::yellow }, SearchLimits{} };
}

} // namespace

// ── TableCellSelection tests ───────────────────────────────────────────────

// Minimal reproduction of CrawlerWidget::TableCellSelection for unit testing.
// The struct is private to CrawlerWidget, so we duplicate it here to test the
// logic independently. This duplicate tests a copy of production code, not
// the production code itself; folding it into the real thing belongs to the
// separate Crawler Widget deepening.
namespace {

struct TableCellSelection {
    bool active = false;
    int row = -1;
    int column = -1;
    int startChar = 0;
    int endChar = 0;

    void clear()
    {
        active = false;
        row = -1;
        column = -1;
        startChar = 0;
        endChar = 0;
    }

    QString selectedText( const QString& cellText ) const
    {
        if ( !active || startChar == endChar ) {
            return {};
        }
        const int lo = std::min( startChar, endChar );
        const int hi
            = std::min( std::max( startChar, endChar ), static_cast<int>( cellText.size() ) );
        return cellText.mid( lo, hi - lo );
    }
};

} // namespace

SCENARIO( "TableCellSelection::clear resets all fields",
          "[logtablehighlightdelegate][tablecellselection]" )
{
    GIVEN( "An active selection" )
    {
        TableCellSelection sel;
        sel.active = true;
        sel.row = 5;
        sel.column = 2;
        sel.startChar = 3;
        sel.endChar = 10;

        WHEN( "clear() is called" )
        {
            sel.clear();

            THEN( "All fields are reset to defaults" )
            {
                REQUIRE_FALSE( sel.active );
                REQUIRE( sel.row == -1 );
                REQUIRE( sel.column == -1 );
                REQUIRE( sel.startChar == 0 );
                REQUIRE( sel.endChar == 0 );
            }
        }
    }
}

SCENARIO( "TableCellSelection::selectedText returns the correct substring",
          "[logtablehighlightdelegate][tablecellselection]" )
{
    const QString text = "Hello, World!";

    GIVEN( "A normal forward selection (start < end)" )
    {
        TableCellSelection sel;
        sel.active = true;
        sel.row = 0;
        sel.column = 0;
        sel.startChar = 7;
        sel.endChar = 12;

        THEN( "The correct substring is returned" )
        {
            REQUIRE( sel.selectedText( text ) == "World" );
        }
    }

    GIVEN( "A reversed selection (start > end)" )
    {
        TableCellSelection sel;
        sel.active = true;
        sel.row = 0;
        sel.column = 0;
        sel.startChar = 12;
        sel.endChar = 7;

        THEN( "The correct substring is still returned (normalised)" )
        {
            REQUIRE( sel.selectedText( text ) == "World" );
        }
    }

    GIVEN( "A zero-width selection (start == end)" )
    {
        TableCellSelection sel;
        sel.active = true;
        sel.row = 0;
        sel.column = 0;
        sel.startChar = 5;
        sel.endChar = 5;

        THEN( "An empty string is returned" )
        {
            REQUIRE( sel.selectedText( text ).isEmpty() );
        }
    }

    GIVEN( "An inactive selection" )
    {
        TableCellSelection sel;
        sel.active = false;
        sel.startChar = 0;
        sel.endChar = 5;

        THEN( "An empty string is returned" )
        {
            REQUIRE( sel.selectedText( text ).isEmpty() );
        }
    }

    GIVEN( "A selection that extends past the text length" )
    {
        TableCellSelection sel;
        sel.active = true;
        sel.row = 0;
        sel.column = 0;
        sel.startChar = 7;
        sel.endChar = 100;

        THEN( "The result is clamped to the text length" )
        {
            REQUIRE( sel.selectedText( text ) == "World!" );
        }
    }

    GIVEN( "A selection of the entire text" )
    {
        TableCellSelection sel;
        sel.active = true;
        sel.row = 0;
        sel.column = 0;
        sel.startChar = 0;
        sel.endChar = static_cast<int>( text.size() );

        THEN( "The full text is returned" )
        {
            REQUIRE( sel.selectedText( text ) == text );
        }
    }

    GIVEN( "An empty cell text" )
    {
        TableCellSelection sel;
        sel.active = true;
        sel.row = 0;
        sel.column = 0;
        sel.startChar = 0;
        sel.endChar = 5;

        THEN( "An empty string is returned" )
        {
            REQUIRE( sel.selectedText( QString{} ).isEmpty() );
        }
    }
}

namespace {

// Helper to create a model, delegate, and paint into an off-screen pixmap.
struct PaintFixture {
    QStandardItemModel model;
    LogTableHighlightDelegate delegate;
    QPixmap pixmap{ 400, 30 };
    QPainter painter;
    QStyleOptionViewItem option;

    PaintFixture()
    {
        model.setColumnCount( 3 );
        model.setRowCount( 3 );
        model.setData( model.index( 0, 0 ), "2026-05-07 12:00:00" );
        model.setData( model.index( 0, 1 ), "INFO" );
        model.setData( model.index( 0, 2 ), "Hello World from LogSquirl" );
        model.setData( model.index( 1, 0 ), "2026-05-07 12:00:01" );
        model.setData( model.index( 1, 1 ), "ERROR" );
        model.setData( model.index( 1, 2 ), "Something went wrong" );
        model.setData( model.index( 2, 0 ), "" );
        model.setData( model.index( 2, 1 ), "" );
        model.setData( model.index( 2, 2 ), "" );

        pixmap.fill( Qt::white );
        painter.begin( &pixmap );

        option.rect = QRect( 0, 0, 400, 30 );
        option.font = QFont( "Monospace", 10 );
        option.fontMetrics = QFontMetrics( option.font );
        option.palette = QApplication::palette();
        option.state = QStyle::State_Enabled;
    }

    ~PaintFixture()
    {
        painter.end();
    }
};

} // namespace

// ── sizeHint tests ─────────────────────────────────────────────────────────

SCENARIO( "sizeHint returns positive width for non-empty text",
          "[logtablehighlightdelegate][sizehint]" )
{
    PaintFixture f;

    GIVEN( "A cell with text" )
    {
        const auto index = f.model.index( 0, 2 );

        WHEN( "sizeHint is queried" )
        {
            const auto hint = f.delegate.sizeHint( f.option, index );

            THEN( "Width is positive and accounts for text + padding" )
            {
                REQUIRE( hint.width() > 8 ); // at least padding
                const auto fm = f.option.fontMetrics;
                const auto expected = fm.horizontalAdvance( "Hello World from LogSquirl" ) + 8;
                REQUIRE( hint.width() == expected );
            }
        }
    }
}

SCENARIO( "sizeHint for empty text falls back to base class",
          "[logtablehighlightdelegate][sizehint]" )
{
    PaintFixture f;

    GIVEN( "A cell with empty text" )
    {
        const auto index = f.model.index( 2, 0 );

        WHEN( "sizeHint is queried" )
        {
            const auto hint = f.delegate.sizeHint( f.option, index );

            THEN( "Width comes from the base class (no custom calculation)" )
            {
                const auto baseHint = QStyledItemDelegate{}.sizeHint( f.option, index );
                REQUIRE( hint.width() == baseHint.width() );
            }
        }
    }
}

// ── decorationFor composition tests (issue #81) ─────────────────────────────
//
// The Table View obtains its Decoration from the Line Decorator instead of
// its own copy of the colour rule: the row-level Line Verdict is decided
// from the raw Log Line -- a whole-line Highlighter and the Search Limits
// are facts about the whole line, not about one field of it -- while the
// Decoration itself is composed straight from the cell's own text (a
// word-only Highlighter, main search, Color Labels and QuickFind are all
// matched again directly against that text, in its own coordinate space).

namespace {

LineVerdict rowVerdictFor( const LineDecorator::Context& context, LineNumber lineNumber,
                           const QString& rawLine, AbstractLogData::LineType lineType )
{
    return LineDecorator{ context }.verdictFor( LogLine{ lineNumber, rawLine }, lineType );
}

} // namespace

SCENARIO( "A whole-line Highlighter colours the whole row, not just the "
          "matching cell",
          "[logtablehighlightdelegate][decorationfor]" )
{
    GIVEN( "a whole-line Highlighter for lines containing ERROR" )
    {
        auto context = emptyDecoratorContext();
        context.highlighterSet
            = setWithHighlighter( "ERROR", false, QColor{ Qt::white }, QColor{ Qt::red } );

        const QString rawLine = "2026-05-07 an ERROR occurred in the pipeline";

        WHEN( "deciding the row-level verdict from the raw line" )
        {
            const auto rowVerdict = rowVerdictFor( context, 0_lnum, rawLine, LineTypeFlags::Plain );

            THEN( "the whole-line colour is decided for the row, independently of any one "
                  "cell's own text" )
            {
                REQUIRE( rowVerdict.wholeLineHighlight().has_value() );
                REQUIRE( rowVerdict.wholeLineHighlight()->foreColor == QColor{ Qt::white } );
                REQUIRE( rowVerdict.wholeLineHighlight()->backColor == QColor{ Qt::red } );
            }

            AND_WHEN( "decorating a cell whose own text does not contain the pattern at all" )
            {
                const auto decoration = LogTableHighlightDelegate::decorationFor(
                    context, rowVerdict, 0_lnum, LineTypeFlags::Plain, "unrelated field text" );

                THEN( "no bogus span leaks in from the raw line's own coordinate space -- the "
                      "whole row's colour is applied by paint() from the row verdict above, "
                      "not by a span here" )
                {
                    REQUIRE( decoration.spans().empty() );
                }
            }
        }
    }
}

SCENARIO( "A word-only Highlighter matches the cell's own text, not the raw "
          "line's coordinate space",
          "[logtablehighlightdelegate][decorationfor]" )
{
    GIVEN( "a word-only Highlighter for ERROR" )
    {
        auto context = emptyDecoratorContext();
        context.highlighterSet
            = setWithHighlighter( "ERROR", true, QColor{ Qt::white }, QColor{ Qt::red } );

        // The word is much further into the raw line than into the cell's
        // own (shorter, differently-offset) text.
        const QString rawLine = "2026-05-07 12:00:00 an ERROR occurred";
        const QString cellText = "ERROR: pipeline failed";
        const auto rowVerdict = rowVerdictFor( context, 0_lnum, rawLine, LineTypeFlags::Plain );

        WHEN( "decorating the cell" )
        {
            const auto decoration = LogTableHighlightDelegate::decorationFor(
                context, rowVerdict, 0_lnum, LineTypeFlags::Plain, cellText );

            THEN( "the match is positioned within the cell's own text, not the raw line's" )
            {
                REQUIRE( decoration.spans().size() == 1 );
                const auto& span = decoration.spans().front();
                REQUIRE( span.startColumn() == LineColumn{ cellText.indexOf( "ERROR" ) } );
                REQUIRE( span.size() == LineLength{ 5 } );
                REQUIRE( span.foreColor() == QColor{ Qt::white } );
                REQUIRE( span.backColor() == QColor{ Qt::red } );
            }
        }

        WHEN( "decorating a cell whose own text does not contain the word" )
        {
            const auto decoration = LogTableHighlightDelegate::decorationFor(
                context, rowVerdict, 0_lnum, LineTypeFlags::Plain, "unrelated field" );

            THEN( "no span is produced for this cell" )
            {
                REQUIRE( decoration.spans().empty() );
            }
        }
    }
}

SCENARIO( "A row outside the Search Limits is subdued in the Table View, "
          "as in the text view",
          "[logtablehighlightdelegate][decorationfor]" )
{
    GIVEN( "a whole-line Highlighter and Search Limits restricted to lines 5-10" )
    {
        auto context = emptyDecoratorContext();
        context.highlighterSet
            = setWithHighlighter( "ERROR", false, QColor{ Qt::white }, QColor{ Qt::red } );
        context.searchLimits = SearchLimits{ 5_lnum, 10_lnum };

        const QString rawLine = "an ERROR occurred";
        const QString cellText = "an ERROR occurred";

        WHEN( "decorating a row before the limits" )
        {
            const auto rowVerdict = rowVerdictFor( context, 0_lnum, rawLine, LineTypeFlags::Plain );

            THEN( "the row verdict itself says so" )
            {
                REQUIRE( rowVerdict.isOutsideSearchLimits() );
                REQUIRE_FALSE( rowVerdict.wholeLineHighlight().has_value() );
            }

            AND_THEN( "the Highlighter colour is suppressed in the cell's Decoration too" )
            {
                const auto decoration = LogTableHighlightDelegate::decorationFor(
                    context, rowVerdict, 0_lnum, LineTypeFlags::Plain, cellText );
                REQUIRE( decoration.spans().empty() );
            }
        }

        WHEN( "decorating a row inside the limits" )
        {
            const auto rowVerdict = rowVerdictFor( context, 7_lnum, rawLine, LineTypeFlags::Plain );
            const auto decoration = LogTableHighlightDelegate::decorationFor(
                context, rowVerdict, 7_lnum, LineTypeFlags::Plain, cellText );

            THEN( "the Highlighter colour still applies" )
            {
                REQUIRE_FALSE( rowVerdict.isOutsideSearchLimits() );
                REQUIRE_FALSE( decoration.spans().empty() );
            }
        }
    }
}

SCENARIO( "Main search matches are highlighted in the Table View",
          "[logtablehighlightdelegate][decorationfor]" )
{
    GIVEN( "a main search pattern for the word 'field'" )
    {
        auto context = emptyDecoratorContext();
        context.mainSearch
            = Highlighter{ "field", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } };

        const QString rawLine = "2026-05-07 the field name is set";
        const QString cellText = "the field name";
        const auto rowVerdict = rowVerdictFor( context, 0_lnum, rawLine, LineTypeFlags::Plain );

        WHEN( "decorating the cell containing the matched word" )
        {
            const auto decoration = LogTableHighlightDelegate::decorationFor(
                context, rowVerdict, 0_lnum, LineTypeFlags::Plain, cellText );

            THEN( "the matched word carries the main search colour" )
            {
                REQUIRE( decoration.spans().size() == 1 );
                const auto& span = decoration.spans().front();
                REQUIRE( span.startColumn() == LineColumn{ cellText.indexOf( "field" ) } );
                REQUIRE( span.size() == LineLength{ 5 } );
                REQUIRE( span.backColor() == QColor{ Qt::yellow } );
            }
        }
    }
}

SCENARIO( "A partially selected row still shows Highlighter colour outside "
          "the selection",
          "[logtablehighlightdelegate][decorationfor]" )
{
    GIVEN( "a whole-line Highlighter for lines containing hello" )
    {
        auto context = emptyDecoratorContext();
        context.highlighterSet
            = setWithHighlighter( "hello", false, QColor{ Qt::white }, QColor{ Qt::red } );

        const QString text = "hello world";
        const auto rowVerdict = rowVerdictFor( context, 0_lnum, text, LineTypeFlags::Plain );
        const HighlightedMatch selection{ 6_lcol, LineLength{ 5 }, QColor{ Qt::white },
                                          QColor{ Qt::blue } };

        WHEN( "decorating the cell with a selection over part of it" )
        {
            const auto decoration = LogTableHighlightDelegate::decorationFor(
                context, rowVerdict, 0_lnum, LineTypeFlags::Plain, text, selection );

            THEN( "the selection wins where it overlaps" )
            {
                bool foundSelection = false;
                for ( const auto& span : decoration.spans() ) {
                    if ( span.startColumn() == 6_lcol ) {
                        REQUIRE( span.backColor() == QColor{ Qt::blue } );
                        foundSelection = true;
                    }
                }
                REQUIRE( foundSelection );

                AND_THEN( "the untouched prefix keeps the whole-line Highlighter's colour" )
                {
                    const auto& first = decoration.spans().front();
                    REQUIRE( first.startColumn() == 0_lcol );
                    REQUIRE( first.backColor() == QColor{ Qt::red } );
                }
            }
        }
    }
}

SCENARIO( "A fully selected row overrides Highlighter colour everywhere",
          "[logtablehighlightdelegate][decorationfor]" )
{
    GIVEN( "a whole-line Highlighter for lines containing hello" )
    {
        auto context = emptyDecoratorContext();
        context.highlighterSet
            = setWithHighlighter( "hello", false, QColor{ Qt::white }, QColor{ Qt::red } );

        const QString text = "hello world";
        const auto rowVerdict = rowVerdictFor( context, 0_lnum, text, LineTypeFlags::Plain );
        const HighlightedMatch selection{ 0_lcol, LineLength{ text.size() }, QColor{ Qt::white },
                                          QColor{ Qt::blue } };

        WHEN( "decorating the cell with a selection covering the whole cell text" )
        {
            const auto decoration = LogTableHighlightDelegate::decorationFor(
                context, rowVerdict, 0_lnum, LineTypeFlags::Plain, text, selection );

            THEN( "the selection colour covers the whole cell, with nothing left showing "
                  "the Highlighter's colour" )
            {
                for ( const auto& span : decoration.spans() ) {
                    REQUIRE( span.backColor() == QColor{ Qt::blue } );
                }
            }
        }
    }
}

// ── rowColorsFor tests (issue #82) ──────────────────────────────────────────
//
// Marking a Log Line already wired the setter, the LineType read used by
// AbstractLogView's gutter bullet, and the repaint -- only the Table View's
// own read of that LineType, to turn it into a row background, was ever
// missing. rowColorsFor() is that read, isolated from live singletons so it
// can be tested directly.

SCENARIO( "rowColorsFor renders a Match line's row background in the "
          "text view's bullet colour",
          "[logtablehighlightdelegate][rowcolors]" )
{
    GIVEN( "a Line Verdict for a Match line, with no whole-line Highlighter" )
    {
        const LineVerdict verdict{ std::nullopt, LineTypeFlags::Match, false };

        WHEN( "deciding the row colours" )
        {
            const auto colors = LogTableHighlightDelegate::rowColorsFor(
                verdict, QColor{ Qt::black }, QColor{ Qt::white }, QColor{ Qt::gray } );

            THEN( "the row background is the text view's match bullet colour (red)" )
            {
                REQUIRE( colors.backColor == QColor{ Qt::red } );
            }
        }
    }
}

SCENARIO( "rowColorsFor renders a Mark line's row background in the "
          "text view's bullet colour",
          "[logtablehighlightdelegate][rowcolors]" )
{
    GIVEN( "a Line Verdict for a Mark-only line" )
    {
        const LineVerdict verdict{ std::nullopt, LineTypeFlags::Mark, false };

        WHEN( "deciding the row colours" )
        {
            const auto colors = LogTableHighlightDelegate::rowColorsFor(
                verdict, QColor{ Qt::black }, QColor{ Qt::white }, QColor{ Qt::gray } );

            THEN( "the row background is the text view's mark bullet colour (dodgerblue)" )
            {
                REQUIRE( colors.backColor == QColor{ "dodgerblue" } );
            }
        }
    }
}

SCENARIO( "rowColorsFor gives a Mark+Match line its own distinct colour, "
          "as the text view's bullet does",
          "[logtablehighlightdelegate][rowcolors]" )
{
    GIVEN( "a Line Verdict for a line that is both Mark and Match" )
    {
        const LineVerdict verdict{ std::nullopt, LineTypeFlags::Mark | LineTypeFlags::Match,
                                   false };

        WHEN( "deciding the row colours" )
        {
            const auto colors = LogTableHighlightDelegate::rowColorsFor(
                verdict, QColor{ Qt::black }, QColor{ Qt::white }, QColor{ Qt::gray } );

            THEN( "the row background is the text view's marked-match bullet colour (violet), "
                  "not plain match or plain mark" )
            {
                REQUIRE( colors.backColor == QColor{ "violet" } );
            }
        }
    }
}

SCENARIO( "rowColorsFor leaves a Plain line's row background untouched",
          "[logtablehighlightdelegate][rowcolors]" )
{
    GIVEN( "a Line Verdict for a Plain line" )
    {
        const LineVerdict verdict{ std::nullopt, LineTypeFlags::Plain, false };

        WHEN( "deciding the row colours" )
        {
            const auto colors = LogTableHighlightDelegate::rowColorsFor(
                verdict, QColor{ Qt::black }, QColor{ Qt::white }, QColor{ Qt::gray } );

            THEN( "the default background is unchanged" )
            {
                REQUIRE( colors.backColor == QColor{ Qt::white } );
                REQUIRE( colors.foreColor == QColor{ Qt::black } );
            }
        }
    }
}

SCENARIO( "rowColorsFor dims a Context Line's foreground, as the text view does",
          "[logtablehighlightdelegate][rowcolors]" )
{
    GIVEN( "a Line Verdict for a Context Line" )
    {
        const LineVerdict verdict{ std::nullopt, LineTypeFlags::Context, false };

        WHEN( "deciding the row colours" )
        {
            const auto colors = LogTableHighlightDelegate::rowColorsFor(
                verdict, QColor{ Qt::black }, QColor{ Qt::white }, QColor{ Qt::gray } );

            THEN( "the foreground is subdued (reduced alpha), matching the text view's dimming" )
            {
                REQUIRE( colors.foreColor.alpha() == 128 );
            }

            AND_THEN( "the background is untouched -- the text view dims foreground only" )
            {
                REQUIRE( colors.backColor == QColor{ Qt::white } );
            }
        }
    }
}

SCENARIO( "rowColorsFor gives Search Limits the highest precedence, "
          "suppressing Mark/Match colour outside the limits",
          "[logtablehighlightdelegate][rowcolors]" )
{
    GIVEN( "a Line Verdict for a Match line that is outside the Search Limits" )
    {
        const LineVerdict verdict{ std::nullopt, LineTypeFlags::Match,
                                   /* isOutsideSearchLimits = */ true };

        WHEN( "deciding the row colours" )
        {
            const auto colors = LogTableHighlightDelegate::rowColorsFor(
                verdict, QColor{ Qt::black }, QColor{ Qt::white }, QColor{ Qt::gray } );

            THEN( "the disabled foreground wins and the match colour does not show" )
            {
                REQUIRE( colors.foreColor == QColor{ Qt::gray } );
                REQUIRE( colors.backColor == QColor{ Qt::white } );
            }
        }
    }
}

SCENARIO( "rowColorsFor gives a whole-line Highlighter precedence over "
          "Mark/Match row colour",
          "[logtablehighlightdelegate][rowcolors]" )
{
    GIVEN( "a Line Verdict for a Match line that also carries a whole-line Highlighter" )
    {
        const HighlightColor wholeLine{ QColor{ Qt::white }, QColor{ Qt::green } };
        const LineVerdict verdict{ wholeLine, LineTypeFlags::Match, false };

        WHEN( "deciding the row colours" )
        {
            const auto colors = LogTableHighlightDelegate::rowColorsFor(
                verdict, QColor{ Qt::black }, QColor{ Qt::white }, QColor{ Qt::gray } );

            THEN( "the Highlighter's own colour wins over the match colour" )
            {
                REQUIRE( colors.backColor == QColor{ Qt::green } );
                REQUIRE( colors.foreColor == QColor{ Qt::white } );
            }
        }
    }
}

// A regression test for the bug itself: marking a line (via LogFilteredData,
// the same path CrawlerWidget::markLinesFromMain drives) must change what
// rowColorsFor() -- and therefore paint() -- produces for that line. Before
// this fix, the row-level LineType was read into the Line Verdict but never
// consulted for colour, so this would fail: the "before" and "after" colours
// were identical no matter what the LineType said.
SCENARIO( "Marking a line changes the Table View's row background",
          "[logtablehighlightdelegate][rowcolors][regression]" )
{
    GIVEN( "an unmarked Plain line" )
    {
        const LineVerdict before{ std::nullopt, LineTypeFlags::Plain, false };
        const auto beforeColors = LogTableHighlightDelegate::rowColorsFor(
            before, QColor{ Qt::black }, QColor{ Qt::white }, QColor{ Qt::gray } );

        WHEN( "the line is marked (LineType now carries the Mark flag)" )
        {
            const LineVerdict after{ std::nullopt, LineTypeFlags::Mark, false };
            const auto afterColors = LogTableHighlightDelegate::rowColorsFor(
                after, QColor{ Qt::black }, QColor{ Qt::white }, QColor{ Qt::gray } );

            THEN( "the row background actually changes" )
            {
                REQUIRE( beforeColors.backColor != afterColors.backColor );
                REQUIRE( afterColors.backColor == QColor{ "dodgerblue" } );
            }
        }
    }
}
