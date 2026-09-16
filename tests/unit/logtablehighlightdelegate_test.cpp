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
#include "painting_test_font.h"

#include <QApplication>
#include <QFontMetrics>
#include <QImage>
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
    GIVEN( "a whole-line Highlighter and Search Limits from line 5 up to, not including, line 10" )
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

// The Search Limits as the Presentations hold them: half-open, from the first
// Log Line searched up to the Log Line after the last one. The Text View's
// painting test subdues the same Log Lines for the same limits (#232).
namespace {

const QColor SubduedTextColor{ 150, 150, 150 };

// Whether paint() draws the Row of the given Log Line subdued, with the
// Search Limits handed to the delegate the way LogTableView hands them.
bool isRowSubdued( LineNumber logLine, LineNumber searchStart, LineNumber searchEnd )
{
    const auto font = paintingtestfont::requirePaintingTestFont();

    // One Row per Log Line, so the Row painted is the Log Line's number.
    const auto row = static_cast<int>( logLine.get() );
    QStandardItemModel model( row + 1, 1 );
    model.setData( model.index( row, 0 ), "MMMMMMMM" );

    LogTableHighlightDelegate delegate;
    delegate.setSearchLimits( searchStart, searchEnd );

    const QRect cellRect( 0, 0, 120, 24 );
    QImage image( cellRect.size(), QImage::Format_ARGB32 );
    image.fill( Qt::white );

    QStyleOptionViewItem option;
    option.rect = cellRect;
    option.font = font;
    option.state = QStyle::State_Enabled;
    option.palette.setColor( QPalette::Base, Qt::white );
    option.palette.setColor( QPalette::Text, Qt::black );
    option.palette.setColor( QPalette::Disabled, QPalette::Text, SubduedTextColor );

    QPainter painter( &image );
    painter.setFont( font );
    delegate.paint( &painter, option, model.index( row, 0 ) );
    painter.end();

    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            if ( image.pixel( x, y ) == SubduedTextColor.rgb() ) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

SCENARIO( "The Table View subdues exactly the Log Lines outside the Search Limits",
          "[logtablehighlightdelegate][searchlimits]" )
{
    GIVEN( "Search Limits from Log Line 8 up to, not including, Log Line 12" )
    {
        const auto searchStart = 8_lnum;
        const auto searchEnd = 12_lnum;

        THEN( "the Log Line before the first one searched is subdued" )
        {
            REQUIRE( isRowSubdued( 7_lnum, searchStart, searchEnd ) );
        }

        THEN( "the first Log Line searched is not subdued" )
        {
            REQUIRE_FALSE( isRowSubdued( 8_lnum, searchStart, searchEnd ) );
        }

        THEN( "the last Log Line searched is not subdued" )
        {
            REQUIRE_FALSE( isRowSubdued( 11_lnum, searchStart, searchEnd ) );
        }

        THEN( "the Log Line directly after the end is subdued" )
        {
            REQUIRE( isRowSubdued( 12_lnum, searchStart, searchEnd ) );
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

// ── Cell hit test (#134) ────────────────────────────────────────────────────
//
// A click in a cell has to resolve to the character drawn under it. The hit
// test and paint() each apply the cell's horizontal padding, and only a test
// keeps them agreeing. So the expectation below is not computed from the
// padding constant; it is read back from what paint() actually drew. A
// one-character portion selection makes paint() fill exactly that
// character's cell in the highlight color, and the hit test is asked about
// the pixels either side of that cell's midpoint: a click on the left half
// of a character is a caret before it, on the right half a caret after it.
// If the two paddings differ by even one pixel, one of those probes lands on
// the wrong side of the midpoint.

namespace {

const QColor HitTestHighlightColor{ 0, 200, 0 };

struct PaintedCharacter {
    int left = 0;
    int width = 0;
    QFontMetrics fontMetrics;
};

// Paints cellText into cellRect with the given character portion-selected,
// and returns the horizontal extent paint() filled for that character along
// the top pixel row of the cell, where no glyph reaches.
std::optional<PaintedCharacter> paintCharacter( const QString& cellText, int character,
                                                const QRect& cellRect, const QFont& font )
{
    QStandardItemModel model( 1, 1 );
    model.setData( model.index( 0, 0 ), cellText );

    LogTableHighlightDelegate delegate;
    delegate.setPortionSelection( 0, 0, character, character + 1 );

    QImage image( cellRect.right() + 1, cellRect.bottom() + 1, QImage::Format_ARGB32 );
    image.fill( Qt::white );

    QStyleOptionViewItem option;
    option.rect = cellRect;
    option.font = font;
    option.state = QStyle::State_Enabled;
    option.palette.setColor( QPalette::Base, Qt::white );
    option.palette.setColor( QPalette::Text, Qt::black );
    option.palette.setColor( QPalette::Highlight, HitTestHighlightColor );
    option.palette.setColor( QPalette::HighlightedText, Qt::black );

    QPainter painter( &image );
    painter.setFont( font );
    delegate.paint( &painter, option, model.index( 0, 0 ) );
    const auto fontMetrics = painter.fontMetrics();
    painter.end();

    const auto highlight = HitTestHighlightColor.rgb();
    int left = -1;
    int width = 0;
    for ( int x = cellRect.left(); x <= cellRect.right(); ++x ) {
        if ( image.pixel( x, cellRect.top() ) == highlight ) {
            if ( left < 0 ) {
                left = x;
            }
            ++width;
        }
    }

    if ( left < 0 ) {
        return std::nullopt;
    }
    return PaintedCharacter{ left, width, fontMetrics };
}

void requireClicksResolveToPaintedCharacter( const QString& cellText, int character )
{
    // A cell that does not start at x = 0, so the hit test has to honour the
    // cell's own left edge as well as the padding.
    const QRect cellRect( 37, 0, 300, 24 );
    auto font = QApplication::font();
    font.setPixelSize( 16 );

    const auto painted = paintCharacter( cellText, character, cellRect, font );
    REQUIRE( painted.has_value() );

    // Where the character starts is read from the image: that is the edge
    // both paddings decide. How wide it is comes from the same prefix
    // advances the hit test uses -- with a proportional font, the advance
    // of a character on its own and the difference of the advances of the
    // prefixes either side of it can round one pixel apart.
    const auto& fm = painted->fontMetrics;
    const int width = fm.horizontalAdvance( cellText.left( character + 1 ) )
                      - fm.horizontalAdvance( cellText.left( character ) );
    // Narrower than two pixels, a character has no left and right half to
    // tell apart.
    REQUIRE( width >= 2 );

    const auto hit = [ & ]( int pixelX ) {
        return LogTableHighlightDelegate::charIndexAtX( cellText, fm, cellRect.left(), pixelX );
    };
    const int firstRightHalfPx = painted->left + ( width + 1 ) / 2;
    const int lastPx = painted->left + width - 1;

    REQUIRE( hit( painted->left ) == character );
    REQUIRE( hit( firstRightHalfPx - 1 ) == character );
    REQUIRE( hit( firstRightHalfPx ) == character + 1 );
    REQUIRE( hit( lastPx ) == character + 1 );
}

} // namespace

SCENARIO( "A click in a Table View cell resolves to the character painted under it",
          "[logtablehighlightdelegate][hittest]" )
{
    const QString cellText = "abcdefghij";

    GIVEN( "the first character of a cell" )
    {
        THEN( "its left half is before it and its right half after it" )
        {
            requireClicksResolveToPaintedCharacter( cellText, 0 );
        }
    }

    GIVEN( "a character in the middle of a cell" )
    {
        THEN( "its left half is before it and its right half after it" )
        {
            requireClicksResolveToPaintedCharacter( cellText, 5 );
        }
    }

    GIVEN( "the last character of a cell" )
    {
        THEN( "its left half is before it and its right half after it" )
        {
            requireClicksResolveToPaintedCharacter( cellText, 9 );
        }
    }
}

SCENARIO( "A click in a Table View cell away from its text resolves to the nearest end",
          "[logtablehighlightdelegate][hittest]" )
{
    const QFontMetrics fontMetrics( QApplication::font() );
    const int cellLeft = 37;

    GIVEN( "an empty cell" )
    {
        THEN( "any click is the first position" )
        {
            REQUIRE( LogTableHighlightDelegate::charIndexAtX( QString{}, fontMetrics, cellLeft,
                                                              cellLeft + 50 )
                     == 0 );
        }
    }

    GIVEN( "a cell with text" )
    {
        const QString cellText = "abcdefghij";

        THEN( "a click in the padding left of the text is the first position" )
        {
            REQUIRE( LogTableHighlightDelegate::charIndexAtX( cellText, fontMetrics, cellLeft,
                                                              cellLeft + 1 )
                     == 0 );
        }

        THEN( "a click left of the cell altogether is the first position" )
        {
            REQUIRE( LogTableHighlightDelegate::charIndexAtX( cellText, fontMetrics, cellLeft, 0 )
                     == 0 );
        }

        THEN( "a click past the end of the text is the position after the last character" )
        {
            REQUIRE( LogTableHighlightDelegate::charIndexAtX( cellText, fontMetrics, cellLeft,
                                                              cellLeft + 10000 )
                     == static_cast<int>( cellText.size() ) );
        }
    }
}
