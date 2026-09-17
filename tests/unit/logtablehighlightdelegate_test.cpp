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

#include <map>
#include <vector>

namespace {

using LineTypeFlags = AbstractLogData::LineTypeFlags;

const LinePalette TestPalette{ QColor{ 10, 10, 10 }, QColor{ 250, 250, 250 },
                               QColor{ 128, 128, 128 }, QColor{ 240, 240, 200 },
                               QColor{ 30, 60, 200 } };

HighlighterSet setWithHighlighter( const QString& pattern, bool highlightOnlyMatch,
                                   const QColor& foreColor, const QColor& backColor )
{
    auto set = HighlighterSet::createNewSet( "test" );
    set.addHighlighter( Highlighter{ pattern, false, highlightOnlyMatch, foreColor, backColor } );
    return set;
}

LineDecorator::Context emptyDecoratorContext()
{
    return LineDecorator::Context{
        HighlighterSet{},     std::nullopt,   {},          QuickFindMatcher{},
        QColor{ Qt::yellow }, SearchLimits{}, TestPalette, LineStatusDisplay::AsBackground
    };
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
                      "cell's text carries the whole row's colour from the row verdict above" )
                {
                    REQUIRE( decoration.spans().size() == 1 );
                    REQUIRE( decoration.spans().front().backColor() == QColor{ Qt::red } );
                    REQUIRE( decoration.lineColors().backColor == QColor{ Qt::red } );
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
                REQUIRE( decoration.spans().size() == 2 );
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

            THEN( "the cell's text is all in the row's own colours" )
            {
                REQUIRE( decoration.spans().size() == 1 );
                REQUIRE( decoration.spans().front().backColor() == TestPalette.base );
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
                REQUIRE( decoration.spans().size() == 1 );
                REQUIRE( decoration.spans().front().foreColor() == TestPalette.subduedText );
                REQUIRE( decoration.spans().front().backColor() == TestPalette.base );
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
                REQUIRE( decoration.spans().front().backColor() == QColor{ Qt::red } );
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
                REQUIRE( decoration.spans().size() == 3 );
                const auto& span = decoration.spans()[ 1 ];
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

// ── A Row's own colours come from the Line Decorator (#82, #241) ────────────
//
// The Table View has no gutter, so a Mark or a Match colours the Row's
// background; which colour a Row gets is the Line Decorator's decision, tested
// in linedecorator_test.cpp. These see it painted.

namespace {

const QColor PaintedSelectionColor{ 0, 0, 200 };
const QColor PaintedQuickFindColor{ 0, 220, 220 };

// Paints one cell of the given text into an image, as LogTableView would with
// a QuickFind pattern for "World", the Row selected as a whole or not.
QImage paintRow( const QString& cellText, bool selectedAsWhole )
{
    const auto font = paintingtestfont::requirePaintingTestFont();

    QStandardItemModel model( 1, 1 );
    model.setData( model.index( 0, 0 ), cellText );

    LogTableHighlightDelegate delegate;
    auto quickFindPattern = std::make_shared<QuickFindPattern>();
    quickFindPattern->changeSearchPattern( "World", false, false );
    delegate.setQuickFindPattern( quickFindPattern );
    DecorationPolicy policy;
    policy.quickFindBackColor = PaintedQuickFindColor;
    delegate.setDecorationPolicy( policy );

    const QRect cellRect( 0, 0, 240, 24 );
    QImage image( cellRect.size(), QImage::Format_ARGB32 );
    image.fill( Qt::white );

    QStyleOptionViewItem option;
    option.rect = cellRect;
    option.font = font;
    option.state = QStyle::State_Enabled;
    if ( selectedAsWhole ) {
        option.state |= QStyle::State_Selected;
    }
    option.palette.setColor( QPalette::Base, Qt::white );
    option.palette.setColor( QPalette::Text, Qt::black );
    option.palette.setColor( QPalette::Highlight, PaintedSelectionColor );
    option.palette.setColor( QPalette::HighlightedText, Qt::white );

    QPainter painter( &image );
    painter.setFont( font );
    delegate.paint( &painter, option, model.index( 0, 0 ) );
    painter.end();
    return image;
}

bool containsColor( const QImage& image, const QColor& color )
{
    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            if ( image.pixel( x, y ) == color.rgb() ) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

SCENARIO( "A Row selected as a whole keeps its QuickFind match visible",
          "[logtablehighlightdelegate][selectedaswhole]" )
{
    GIVEN( "a Row whose text QuickFind matches" )
    {
        WHEN( "the Row is selected as a whole" )
        {
            const auto painted = paintRow( "Hello World", true );

            THEN( "the Row shows the selection colour" )
            {
                REQUIRE( containsColor( painted, PaintedSelectionColor ) );
            }

            THEN( "the QuickFind match is painted on top of it" )
            {
                REQUIRE( containsColor( painted, PaintedQuickFindColor ) );
            }
        }

        WHEN( "the Row is not selected" )
        {
            const auto painted = paintRow( "Hello World", false );

            THEN( "the QuickFind match is painted, and no selection colour" )
            {
                REQUIRE( containsColor( painted, PaintedQuickFindColor ) );
                REQUIRE_FALSE( containsColor( painted, PaintedSelectionColor ) );
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

// ── One paint pass decides each Row once (#294) ────────────────────────────
//
// QTableView paints cell by cell. Within one paint pass the Line Decorator's
// Context is built once and each Row's Line Verdict is decided once, from its
// raw Log Line, however many cells the Row has -- and the table looks exactly
// as it does when every cell is painted on its own.

namespace {

// The Highlighter Sets and which of them are active, restored when this
// object goes: nothing a test activates leaks into the tests that run next.
class PinnedHighlighterSets {
public:
    PinnedHighlighterSets()
        : sets_( HighlighterSetCollection::get().highlighterSets() )
        , activeSetIds_( HighlighterSetCollection::get().activeSetIds() )
    {
    }

    ~PinnedHighlighterSets()
    {
        auto& collection = HighlighterSetCollection::get();
        collection.setHighlighterSets( sets_ );
        collection.deactivateAll();
        for ( const auto& setId : activeSetIds_ ) {
            collection.activateSet( setId );
        }
    }

    PinnedHighlighterSets( const PinnedHighlighterSets& ) = delete;
    PinnedHighlighterSets& operator=( const PinnedHighlighterSets& ) = delete;

private:
    QList<HighlighterSet> sets_;
    QStringList activeSetIds_;
};

// Activates a Highlighter Set with a whole-line Highlighter for ERROR and a
// word-only one for host.
void activateTableHighlighters()
{
    auto set = HighlighterSet::createNewSet( "logtablehighlightdelegate_test" );
    set.addHighlighter(
        Highlighter{ "ERROR", false, false, QColor{ Qt::white }, QColor{ 200, 0, 0 } } );
    set.addHighlighter(
        Highlighter{ "host", false, true, QColor{ Qt::black }, QColor{ 0, 200, 0 } } );
    auto& collection = HighlighterSetCollection::get();
    collection.deactivateAll();
    auto sets = collection.highlighterSets();
    sets.append( set );
    collection.setHighlighterSets( sets );
    collection.activateSet( set.id() );
}

// A model that counts, per Row, how often a Row's raw Log Line is read: the
// Line Verdict is decided from it, so a Row whose raw Log Line is read once
// had the Highlighter Set matched against its Log Line once.
class RawLineCountingModel : public QStandardItemModel {
public:
    using QStandardItemModel::QStandardItemModel;

    QVariant data( const QModelIndex& index, int role ) const override
    {
        if ( role == LogFormatTableModel::RawLineRole ) {
            ++rawLineReads[ index.row() ];
        }
        return QStandardItemModel::data( index, role );
    }

    mutable std::map<int, int> rawLineReads;
};

constexpr int TableRows = 4;
constexpr int TableColumns = 3;
constexpr int CellWidth = 160;
constexpr int CellHeight = 22;

void fillTable( QStandardItemModel& model )
{
    const QStringList hosts = { "host1", "host2", "host3", "host4" };
    for ( int row = 0; row < TableRows; ++row ) {
        const QString level = row % 2 == 1 ? "ERROR" : "INFO";
        const QString body = QString( "message %1 from host" ).arg( row );
        model.setData( model.index( row, 0 ), hosts[ row ] );
        model.setData( model.index( row, 1 ), level );
        model.setData( model.index( row, 2 ), body );
        for ( int column = 0; column < TableColumns; ++column ) {
            model.setData( model.index( row, column ), hosts[ row ] + " " + level + " " + body,
                           LogFormatTableModel::RawLineRole );
        }
    }
}

QRect cellRect( int row, int column )
{
    return QRect( column * CellWidth, row * CellHeight, CellWidth, CellHeight );
}

// The option QTableView hands the delegate for a cell: odd Rows alternate,
// Row 3 is selected as a whole.
QStyleOptionViewItem cellOption( const QFont& font, int row, int column )
{
    QStyleOptionViewItem option;
    option.rect = cellRect( row, column );
    option.font = font;
    option.state = QStyle::State_Enabled;
    if ( row == 3 ) {
        option.state |= QStyle::State_Selected;
    }
    if ( row % 2 == 1 ) {
        option.features |= QStyleOptionViewItem::Alternate;
    }
    option.palette.setColor( QPalette::Base, Qt::white );
    option.palette.setColor( QPalette::Text, Qt::black );
    option.palette.setColor( QPalette::Highlight, QColor{ 0, 0, 200 } );
    option.palette.setColor( QPalette::HighlightedText, Qt::white );
    return option;
}

// The delegate as LogTableView sets it up: Row 2 under the mouse cursor and
// characters selected in the last cell of Row 0.
void setUpDelegate( LogTableHighlightDelegate& delegate )
{
    delegate.setHoverRow( 2 );
    delegate.setPortionSelection( 0, 2, 2, 9 );
}

QImage emptyTableImage()
{
    QImage image( TableColumns * CellWidth, TableRows * CellHeight, QImage::Format_ARGB32 );
    image.fill( Qt::white );
    return image;
}

} // namespace

SCENARIO( "A paint pass paints the Table View exactly as painting each cell on its own does",
          "[logtablehighlightdelegate][paintpass]" )
{
    const auto font = paintingtestfont::requirePaintingTestFont();
    const PinnedHighlighterSets pinnedSets;
    activateTableHighlighters();

    GIVEN( "Rows with a whole-line and a word-only Highlighter, alternating, hovered, selected "
           "as a whole and with characters selected" )
    {
        QStandardItemModel model( TableRows, TableColumns );
        fillTable( model );

        // Each cell by a delegate of its own, so nothing one cell decided can
        // reach another.
        auto eachCellOnItsOwn = emptyTableImage();
        {
            QPainter painter( &eachCellOnItsOwn );
            painter.setFont( font );
            for ( int row = 0; row < TableRows; ++row ) {
                for ( int column = 0; column < TableColumns; ++column ) {
                    LogTableHighlightDelegate delegate;
                    setUpDelegate( delegate );
                    delegate.paint( &painter, cellOption( font, row, column ),
                                    model.index( row, column ) );
                }
            }
        }

        WHEN( "every cell is painted in one paint pass" )
        {
            LogTableHighlightDelegate delegate;
            setUpDelegate( delegate );
            auto inOnePass = emptyTableImage();
            {
                QPainter painter( &inOnePass );
                painter.setFont( font );
                const auto pass = delegate.paintPass();
                for ( int row = 0; row < TableRows; ++row ) {
                    for ( int column = 0; column < TableColumns; ++column ) {
                        delegate.paint( &painter, cellOption( font, row, column ),
                                        model.index( row, column ) );
                    }
                }
            }

            THEN( "the table looks the same" )
            {
                REQUIRE( inOnePass == eachCellOnItsOwn );
            }
        }
    }
}

SCENARIO( "A paint pass decides each Row's Line Verdict once, not once per cell",
          "[logtablehighlightdelegate][paintpass]" )
{
    const PinnedHighlighterSets pinnedSets;
    activateTableHighlighters();

    GIVEN( "a table of several Rows and columns" )
    {
        RawLineCountingModel model( TableRows, TableColumns );
        fillTable( model );
        model.rawLineReads.clear();

        LogTableHighlightDelegate delegate;
        auto image = emptyTableImage();
        QPainter painter( &image );

        WHEN( "every cell is painted in one paint pass, some of them twice, as when a paint "
              "event's region has overlapping parts" )
        {
            {
                const auto pass = delegate.paintPass();
                for ( int time = 0; time < 2; ++time ) {
                    for ( int row = 0; row < TableRows; ++row ) {
                        for ( int column = 0; column < TableColumns; ++column ) {
                            delegate.paint( &painter,
                                            cellOption( QApplication::font(), row, column ),
                                            model.index( row, column ) );
                        }
                    }
                }
            }

            THEN( "each Row's raw Log Line is read once" )
            {
                REQUIRE( model.rawLineReads
                         == std::map<int, int>{ { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 } } );
            }

            AND_WHEN( "the cells are painted in the next paint pass" )
            {
                model.rawLineReads.clear();
                {
                    const auto pass = delegate.paintPass();
                    delegate.paint( &painter, cellOption( QApplication::font(), 1, 0 ),
                                    model.index( 1, 0 ) );
                    delegate.paint( &painter, cellOption( QApplication::font(), 1, 1 ),
                                    model.index( 1, 1 ) );
                }

                THEN( "the Row is decided afresh, once" )
                {
                    REQUIRE( model.rawLineReads == std::map<int, int>{ { 1, 1 } } );
                }
            }
        }
    }
}

// ── The hit test measures no more than it needs (#294) ──────────────────────

SCENARIO( "A click in a long Table View cell resolves to the caret position nearest to it",
          "[logtablehighlightdelegate][hittest]" )
{
    auto font = QApplication::font();
    font.setPixelSize( 13 );
    const QFontMetrics fm( font );
    const int cellLeft = 11;

    GIVEN( "a long cell of characters of different widths" )
    {
        QString cellText;
        for ( int i = 0; i < 12; ++i ) {
            cellText += "iWm.ll wide MMM narrow iii | ";
        }
        const int textLength = static_cast<int>( cellText.size() );

        // The advance of each prefix, measured once here.
        std::vector<int> prefixAdvance;
        for ( int i = 0; i <= textLength; ++i ) {
            prefixAdvance.push_back( fm.horizontalAdvance( cellText.left( i ) ) );
        }

        THEN( "every pixel resolves to the caret whose prefix advance is nearest, the left one "
              "on a tie" )
        {
            const int textLeft = cellLeft + LogTableHighlightDelegate::HorizontalTextPadding;
            for ( int x = 0; x <= textLeft + prefixAdvance.back() + 3; ++x ) {
                const int relativeX = x - textLeft;
                int expected = textLength;
                if ( relativeX <= 0 ) {
                    expected = 0;
                }
                else {
                    for ( int i = 1; i <= textLength; ++i ) {
                        const int right = prefixAdvance[ static_cast<size_t>( i ) ];
                        if ( relativeX < right ) {
                            const int left = prefixAdvance[ static_cast<size_t>( i - 1 ) ];
                            expected = relativeX - left < right - relativeX ? i - 1 : i;
                            break;
                        }
                    }
                }
                INFO( "x = " << x );
                REQUIRE( LogTableHighlightDelegate::charIndexAtX( cellText, fm, cellLeft, x )
                         == expected );
            }
        }
    }
}
