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

#include <catch2/catch_test_macros.hpp>

#include "rowmapping.h"
#include "tableviewselection.h"

#include <QStandardItemModel>

namespace {

// A Table View of three Rows with two columns each.
void fillModel( QStandardItemModel& model )
{
    model.setColumnCount( 2 );
    model.setRowCount( 3 );
    model.setData( model.index( 0, 0 ), "12:00:00" );
    model.setData( model.index( 0, 1 ), "Hello, World!" );
    model.setData( model.index( 1, 0 ), "12:00:01" );
    model.setData( model.index( 1, 1 ), "Something went wrong" );
    model.setData( model.index( 2, 0 ), "12:00:02" );
    model.setData( model.index( 2, 1 ), "All good" );
}

} // namespace

SCENARIO( "An empty Table View selection selects nothing", "[tableviewselection]" )
{
    QStandardItemModel model;
    fillModel( model );

    GIVEN( "a selection with neither Rows nor an in-cell selection" )
    {
        const TableViewSelection selection;

        THEN( "no Log Line is selected" )
        {
            REQUIRE( selection.selectedLogLines( OneRowPerLogLine{} ).empty() );
        }

        THEN( "no text is selected" )
        {
            REQUIRE( selection.selectedText( model ).isEmpty() );
            REQUIRE_FALSE( selection.hasInCellSelection() );
        }
    }
}

SCENARIO( "Several selected Rows select their Log Lines and their whole text",
          "[tableviewselection]" )
{
    QStandardItemModel model;
    fillModel( model );

    GIVEN( "the first and the last Row selected" )
    {
        TableViewSelection selection;
        selection.setRows( { 0, 2 } );

        THEN( "both Log Lines are selected, in order" )
        {
            REQUIRE( selection.selectedLogLines( OneRowPerLogLine{} )
                     == logsquirl::vector<LineNumber>{ 0_lnum, 2_lnum } );
        }

        THEN( "the text is each Row's cells separated by tabs, one Row per line" )
        {
            REQUIRE( selection.selectedText( model )
                     == "12:00:00\tHello, World!\n12:00:02\tAll good" );
        }
    }
}

SCENARIO( "An in-cell selection wins over whole Rows", "[tableviewselection]" )
{
    QStandardItemModel model;
    fillModel( model );

    GIVEN( "two Rows selected and characters selected inside a cell of one of them" )
    {
        TableViewSelection selection;
        selection.setRows( { 0, 1 } );
        selection.selectInCell( 0, 1, 7, 12 );

        THEN( "the selected text is the characters, not the Rows" )
        {
            REQUIRE( selection.hasInCellSelection() );
            REQUIRE( selection.selectedText( model ) == "World" );
        }

        THEN( "the selected Log Lines are still the Rows'" )
        {
            REQUIRE( selection.selectedLogLines( OneRowPerLogLine{} )
                     == logsquirl::vector<LineNumber>{ 0_lnum, 1_lnum } );
        }

        AND_WHEN( "the in-cell selection is cleared" )
        {
            selection.clearInCell();

            THEN( "the Rows' text is selected again" )
            {
                REQUIRE_FALSE( selection.hasInCellSelection() );
                REQUIRE( selection.selectedText( model )
                         == "12:00:00\tHello, World!\n12:00:01\tSomething went wrong" );
            }
        }
    }

    GIVEN( "a Row selected and a caret without any character inside one of its cells" )
    {
        TableViewSelection selection;
        selection.setRows( { 1 } );
        selection.startInCell( 1, 1, 4 );

        THEN( "there is no in-cell selection to win, so the Row's text is selected" )
        {
            REQUIRE_FALSE( selection.hasInCellSelection() );
            REQUIRE( selection.selectedText( model ) == "12:00:01\tSomething went wrong" );
        }
    }
}

SCENARIO( "An in-cell selection selects the characters between its ends", "[tableviewselection]" )
{
    QStandardItemModel model;
    fillModel( model );
    TableViewSelection selection;

    GIVEN( "a selection dragged backwards" )
    {
        selection.selectInCell( 0, 1, 12, 7 );

        THEN( "the same characters are selected as dragged forwards" )
        {
            REQUIRE( selection.selectedText( model ) == "World" );
        }
    }

    GIVEN( "a selection reaching past the end of the cell's text" )
    {
        selection.selectInCell( 0, 1, 7, 100 );

        THEN( "it stops at the end of the text" )
        {
            REQUIRE( selection.selectedText( model ) == "World!" );
        }
    }

    GIVEN( "a selection started in a cell" )
    {
        selection.startInCell( 0, 1, 0 );

        WHEN( "it is extended within the same cell" )
        {
            const bool extended = selection.extendInCell( 0, 1, 5 );

            THEN( "the characters up to the new end are selected" )
            {
                REQUIRE( extended );
                REQUIRE( selection.selectedText( model ) == "Hello" );
            }
        }

        WHEN( "it is extended into another cell" )
        {
            const bool extended = selection.extendInCell( 1, 1, 5 );

            THEN( "the selection is left as it was" )
            {
                REQUIRE_FALSE( extended );
                REQUIRE_FALSE( selection.hasInCellSelection() );
                REQUIRE( selection.inCell()->row == 0 );
            }
        }
    }
}
