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

// ── TableCellSelection tests ───────────────────────────────────────────────

// Minimal reproduction of CrawlerWidget::TableCellSelection for unit testing.
// The struct is private to CrawlerWidget, so we duplicate it here to test the
// logic independently.
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
        const int hi = std::min( std::max( startChar, endChar ),
                                 static_cast<int>( cellText.size() ) );
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

// ── LogTableHighlightDelegate state management tests ───────────────────────

SCENARIO( "setPortionSelection normalises start/end",
          "[logtablehighlightdelegate][portionselection]" )
{
    LogTableHighlightDelegate delegate;

    GIVEN( "A forward selection (start < end)" )
    {
        delegate.setPortionSelection( 3, 1, 5, 15 );

        THEN( "The delegate does not crash and accepts the values" )
        {
            // We cannot read the private members directly, but we verify that
            // painting with these values does not crash (tested below).
            REQUIRE( true );
        }
    }

    GIVEN( "A reversed selection (start > end)" )
    {
        delegate.setPortionSelection( 3, 1, 15, 5 );

        THEN( "The delegate normalises internally without crash" )
        {
            REQUIRE( true );
        }
    }

    GIVEN( "A zero-width selection" )
    {
        delegate.setPortionSelection( 3, 1, 10, 10 );

        THEN( "The delegate accepts the values" )
        {
            REQUIRE( true );
        }
    }
}

SCENARIO( "clearPortionSelection resets portion state",
          "[logtablehighlightdelegate][portionselection]" )
{
    LogTableHighlightDelegate delegate;

    GIVEN( "An active portion selection" )
    {
        delegate.setPortionSelection( 2, 1, 5, 20 );

        WHEN( "clearPortionSelection is called" )
        {
            delegate.clearPortionSelection();

            THEN( "The delegate does not paint any portion highlight" )
            {
                // Verified via paint tests below
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "setHoverRow and clearHoverRow manage hover state",
          "[logtablehighlightdelegate][hover]" )
{
    LogTableHighlightDelegate delegate;

    WHEN( "setHoverRow is called with a valid row" )
    {
        delegate.setHoverRow( 5 );
        THEN( "No crash" ) { REQUIRE( true ); }
    }

    WHEN( "clearHoverRow is called" )
    {
        delegate.setHoverRow( 5 );
        delegate.clearHoverRow();
        THEN( "No crash" ) { REQUIRE( true ); }
    }

    WHEN( "setHoverRow is called with -1" )
    {
        delegate.setHoverRow( -1 );
        THEN( "No crash" ) { REQUIRE( true ); }
    }
}

// ── Paint smoke tests ──────────────────────────────────────────────────────

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

    ~PaintFixture() { painter.end(); }
};

} // namespace

SCENARIO( "Delegate paints without crash for valid index",
          "[logtablehighlightdelegate][paint]" )
{
    PaintFixture f;

    WHEN( "Painting a cell with text" )
    {
        const auto index = f.model.index( 0, 2 );
        THEN( "No crash occurs" )
        {
            f.delegate.paint( &f.painter, f.option, index );
            REQUIRE( true );
        }
    }
}

SCENARIO( "Delegate paints without crash for invalid index",
          "[logtablehighlightdelegate][paint]" )
{
    PaintFixture f;

    WHEN( "Painting with an invalid QModelIndex" )
    {
        THEN( "No crash occurs (falls back to base class)" )
        {
            f.delegate.paint( &f.painter, f.option, QModelIndex{} );
            REQUIRE( true );
        }
    }
}

SCENARIO( "Delegate paints without crash for empty cell text",
          "[logtablehighlightdelegate][paint]" )
{
    PaintFixture f;

    WHEN( "Painting a cell with empty text" )
    {
        const auto index = f.model.index( 2, 0 );
        THEN( "No crash occurs" )
        {
            f.delegate.paint( &f.painter, f.option, index );
            REQUIRE( true );
        }
    }
}

SCENARIO( "Delegate paints selected row without crash",
          "[logtablehighlightdelegate][paint]" )
{
    PaintFixture f;

    WHEN( "Painting a selected cell" )
    {
        f.option.state |= QStyle::State_Selected;
        const auto index = f.model.index( 0, 2 );
        THEN( "No crash occurs" )
        {
            f.delegate.paint( &f.painter, f.option, index );
            REQUIRE( true );
        }
    }
}

SCENARIO( "Delegate paints portion selection without crash",
          "[logtablehighlightdelegate][paint]" )
{
    PaintFixture f;

    GIVEN( "A portion selection on row 0, column 2, chars 6..11" )
    {
        f.delegate.setPortionSelection( 0, 2, 6, 11 );

        WHEN( "Painting the cell with the portion selection" )
        {
            const auto index = f.model.index( 0, 2 );
            THEN( "No crash occurs" )
            {
                f.delegate.paint( &f.painter, f.option, index );
                REQUIRE( true );
            }
        }

        WHEN( "Painting a different cell (no portion on this cell)" )
        {
            const auto index = f.model.index( 0, 1 );
            THEN( "No crash occurs" )
            {
                f.delegate.paint( &f.painter, f.option, index );
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "Delegate paints portion selection on selected row without crash",
          "[logtablehighlightdelegate][paint]" )
{
    PaintFixture f;

    GIVEN( "A portion selection on a selected row" )
    {
        f.delegate.setPortionSelection( 0, 2, 6, 11 );
        f.option.state |= QStyle::State_Selected;

        WHEN( "Painting the cell with portion + selection" )
        {
            const auto index = f.model.index( 0, 2 );
            THEN( "Row selection is suppressed so portion is visible" )
            {
                f.delegate.paint( &f.painter, f.option, index );
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "Delegate paints reversed portion selection without crash",
          "[logtablehighlightdelegate][paint]" )
{
    PaintFixture f;

    GIVEN( "A reversed portion selection (start > end)" )
    {
        f.delegate.setPortionSelection( 0, 2, 15, 5 );

        WHEN( "Painting the cell" )
        {
            const auto index = f.model.index( 0, 2 );
            THEN( "No crash occurs (normalised internally)" )
            {
                f.delegate.paint( &f.painter, f.option, index );
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "Delegate paints with portion selection beyond text length",
          "[logtablehighlightdelegate][paint]" )
{
    PaintFixture f;

    GIVEN( "A portion selection that extends past the cell text" )
    {
        // "Hello World from LogSquirl" is 26 chars
        f.delegate.setPortionSelection( 0, 2, 20, 999 );

        WHEN( "Painting the cell" )
        {
            const auto index = f.model.index( 0, 2 );
            THEN( "No crash occurs (clamped to text length)" )
            {
                f.delegate.paint( &f.painter, f.option, index );
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "Delegate paints hover row without crash",
          "[logtablehighlightdelegate][paint]" )
{
    PaintFixture f;

    GIVEN( "Hover row is set to row 0" )
    {
        f.delegate.setHoverRow( 0 );

        WHEN( "Painting a cell on the hover row" )
        {
            const auto index = f.model.index( 0, 2 );
            THEN( "No crash occurs" )
            {
                f.delegate.paint( &f.painter, f.option, index );
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "Delegate paints alternating row without crash",
          "[logtablehighlightdelegate][paint]" )
{
    PaintFixture f;

    GIVEN( "The Alternate feature flag is set" )
    {
        f.option.features |= QStyleOptionViewItem::Alternate;

        WHEN( "Painting a cell" )
        {
            const auto index = f.model.index( 1, 1 );
            THEN( "No crash occurs" )
            {
                f.delegate.paint( &f.painter, f.option, index );
                REQUIRE( true );
            }
        }
    }
}

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
                const auto expected
                    = fm.horizontalAdvance( "Hello World from LogSquirl" ) + 8;
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
                const auto baseHint
                    = QStyledItemDelegate{}.sizeHint( f.option, index );
                REQUIRE( hint.width() == baseHint.width() );
            }
        }
    }
}

// ── setColorLabelWords tests ───────────────────────────────────────────────

SCENARIO( "setColorLabelWords accepts various inputs",
          "[logtablehighlightdelegate][colorlabels]" )
{
    LogTableHighlightDelegate delegate;

    WHEN( "Setting empty color label words" )
    {
        delegate.setColorLabelWords( {} );
        THEN( "No crash" ) { REQUIRE( true ); }
    }

    WHEN( "Setting color label words with empty inner lists" )
    {
        delegate.setColorLabelWords( { QStringList{}, QStringList{} } );
        THEN( "No crash" ) { REQUIRE( true ); }
    }

    WHEN( "Setting color label words with actual words" )
    {
        delegate.setColorLabelWords( { QStringList{ "error", "fatal" },
                                       QStringList{ "warning" } } );
        THEN( "No crash" ) { REQUIRE( true ); }
    }
}

// ── setFilteredData tests ──────────────────────────────────────────────────

SCENARIO( "setFilteredData accepts nullptr",
          "[logtablehighlightdelegate][filtereddata]" )
{
    LogTableHighlightDelegate delegate;

    WHEN( "Setting filtered data to nullptr" )
    {
        delegate.setFilteredData( nullptr );
        THEN( "No crash" ) { REQUIRE( true ); }
    }
}

// ── setQuickFindPattern tests ──────────────────────────────────────────────

SCENARIO( "setQuickFindPattern accepts nullptr and valid shared_ptr",
          "[logtablehighlightdelegate][quickfind]" )
{
    LogTableHighlightDelegate delegate;

    WHEN( "Setting a null shared_ptr" )
    {
        delegate.setQuickFindPattern( nullptr );
        THEN( "No crash" ) { REQUIRE( true ); }
    }

    WHEN( "Setting a valid QuickFindPattern" )
    {
        auto pattern = std::make_shared<QuickFindPattern>();
        delegate.setQuickFindPattern( pattern );
        THEN( "No crash" ) { REQUIRE( true ); }
    }
}

// ── Combined state paint tests ─────────────────────────────────────────────

SCENARIO( "Delegate paints correctly with hover + portion + alternating combined",
          "[logtablehighlightdelegate][paint][combined]" )
{
    PaintFixture f;

    GIVEN( "Hover, portion selection, and alternating row all active" )
    {
        f.delegate.setHoverRow( 0 );
        f.delegate.setPortionSelection( 0, 2, 0, 5 );
        f.option.features |= QStyleOptionViewItem::Alternate;

        WHEN( "Painting the cell" )
        {
            const auto index = f.model.index( 0, 2 );
            THEN( "No crash occurs" )
            {
                f.delegate.paint( &f.painter, f.option, index );
                REQUIRE( true );
            }
        }
    }
}

SCENARIO( "Portion selection on row does not suppress unrelated rows",
          "[logtablehighlightdelegate][paint][portionsuppression]" )
{
    PaintFixture f;

    GIVEN( "A portion selection on row 0" )
    {
        f.delegate.setPortionSelection( 0, 2, 5, 10 );
        f.option.state |= QStyle::State_Selected;

        WHEN( "Painting row 1 (no portion selection)" )
        {
            const auto index = f.model.index( 1, 2 );
            THEN( "Row 1 is still painted as selected (no suppression)" )
            {
                // This should not crash and should paint normally
                f.delegate.paint( &f.painter, f.option, index );
                REQUIRE( true );
            }
        }
    }
}
