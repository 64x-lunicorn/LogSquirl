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

// The dialog of Merge…: which Log Files take part, in which order, and whether
// duplicate lines are dropped (#571).

#include "mergedialog.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>

#include <catch2/catch_test_macros.hpp>

namespace {

QPushButton* buttonWithText( const MergeDialog& dialog, const char* text )
{
    const auto translated = QApplication::translate( "MergeDialog", text );
    for ( auto* button : dialog.findChildren<QPushButton*>() ) {
        if ( button->text() == translated ) {
            return button;
        }
    }
    return nullptr;
}

struct DialogParts {
    explicit DialogParts( const MergeDialog& dialog )
        : list( dialog.findChild<QListWidget*>() )
        , dropDuplicates( dialog.findChild<QCheckBox*>() )
        , merge( buttonWithText( dialog, "Merge" ) )
        , moveUp( buttonWithText( dialog, "Move Up" ) )
        , moveDown( buttonWithText( dialog, "Move Down" ) )
    {
        REQUIRE( list != nullptr );
        REQUIRE( dropDuplicates != nullptr );
        REQUIRE( merge != nullptr );
        REQUIRE( moveUp != nullptr );
        REQUIRE( moveDown != nullptr );
    }

    QStringList names() const
    {
        QStringList shown;
        for ( int row = 0; row < list->count(); ++row ) {
            shown.append( list->item( row )->text() );
        }
        return shown;
    }

    void uncheck( int row ) const
    {
        list->item( row )->setCheckState( Qt::Unchecked );
    }

    QListWidget* list;
    QCheckBox* dropDuplicates;
    QPushButton* merge;
    QPushButton* moveUp;
    QPushButton* moveDown;
};

std::vector<MergeCandidate> threeCandidates()
{
    return {
        { QStringLiteral( "/logs/a.log" ), QStringLiteral( "a.log" ),
          QStringLiteral( "/logs/a.log" ) },
        { QStringLiteral( "/tmp/stdin_1" ), QStringLiteral( "Standard input" ),
          QStringLiteral( "Standard input, read while logsquirl runs" ) },
        { QStringLiteral( "/logs/c.log" ), QStringLiteral( "Renamed c" ),
          QStringLiteral( "/logs/c.log" ) },
    };
}

} // namespace

SCENARIO( "The Merge dialog picks and orders the Log Files to merge", "[ui][merge]" )
{
    GIVEN( "the dialog opened for three Log Files, one of them transient" )
    {
        MergeDialog dialog( threeCandidates() );
        const DialogParts parts( dialog );

        THEN( "it lists them in tab order, by tab name and with their tooltips, all checked" )
        {
            REQUIRE( parts.names() == QStringList{ "a.log", "Standard input", "Renamed c" } );
            REQUIRE( parts.list->item( 1 )->toolTip()
                     == "Standard input, read while logsquirl runs" );
            REQUIRE( parts.list->item( 2 )->toolTip() == "/logs/c.log" );
            for ( int row = 0; row < parts.list->count(); ++row ) {
                REQUIRE( parts.list->item( row )->checkState() == Qt::Checked );
            }
            REQUIRE( dialog.checkedPaths()
                     == QStringList{ "/logs/a.log", "/tmp/stdin_1", "/logs/c.log" } );
        }

        THEN( "duplicate lines are kept, and Merge is the enabled default button" )
        {
            REQUIRE_FALSE( parts.dropDuplicates->isChecked() );
            REQUIRE_FALSE( parts.dropDuplicates->toolTip().isEmpty() );
            REQUIRE_FALSE( dialog.dropsDuplicates() );
            REQUIRE( parts.merge->isEnabled() );
            REQUIRE( parts.merge->isDefault() );
        }

        THEN( "the rows can be dragged within the list" )
        {
            REQUIRE( parts.list->dragDropMode() == QAbstractItemView::InternalMove );
            REQUIRE( parts.list->dragEnabled() );
        }

        WHEN( "one file is unchecked" )
        {
            parts.uncheck( 1 );

            THEN( "it is left out, and Merge stays enabled for the other two" )
            {
                REQUIRE( dialog.checkedPaths() == QStringList{ "/logs/a.log", "/logs/c.log" } );
                REQUIRE( parts.merge->isEnabled() );
            }

            AND_WHEN( "a second one is unchecked" )
            {
                parts.uncheck( 0 );

                THEN( "Merge is disabled: one file is no merge" )
                {
                    REQUIRE_FALSE( parts.merge->isEnabled() );

                    AND_WHEN( "one of them is checked again" )
                    {
                        parts.list->item( 1 )->setCheckState( Qt::Checked );

                        THEN( "Merge is enabled again" )
                        {
                            REQUIRE( parts.merge->isEnabled() );
                        }
                    }
                }
            }
        }

        WHEN( "the last file is moved up twice with the button" )
        {
            parts.list->setCurrentRow( 2 );
            parts.moveUp->click();
            parts.moveUp->click();

            THEN( "it comes first, stays selected, and can go no higher" )
            {
                REQUIRE( parts.names() == QStringList{ "Renamed c", "a.log", "Standard input" } );
                REQUIRE( dialog.checkedPaths()
                         == QStringList{ "/logs/c.log", "/logs/a.log", "/tmp/stdin_1" } );
                REQUIRE( parts.list->currentRow() == 0 );
                REQUIRE_FALSE( parts.moveUp->isEnabled() );
                REQUIRE( parts.moveDown->isEnabled() );
            }
        }

        WHEN( "the first file is moved down with the button" )
        {
            parts.list->setCurrentRow( 0 );
            parts.moveDown->click();

            THEN( "it comes second, and keeps its check state" )
            {
                REQUIRE( parts.names() == QStringList{ "Standard input", "a.log", "Renamed c" } );
                REQUIRE( parts.list->item( 1 )->checkState() == Qt::Checked );
                REQUIRE( parts.list->currentRow() == 1 );
            }
        }

        WHEN( "an unchecked file is moved down with the button" )
        {
            parts.uncheck( 0 );
            parts.list->setCurrentRow( 0 );
            parts.moveDown->click();

            THEN( "it stays unchecked and left out" )
            {
                REQUIRE( parts.list->item( 1 )->checkState() == Qt::Unchecked );
                REQUIRE( dialog.checkedPaths() == QStringList{ "/tmp/stdin_1", "/logs/c.log" } );
            }
        }

        WHEN( "a row is moved the way a drag within the list moves it" )
        {
            // A drag within the list ends in a move of the model's rows.
            REQUIRE( parts.list->model()->moveRow( {}, 0, {}, 3 ) );

            THEN( "the order to merge in follows the list" )
            {
                REQUIRE( parts.names() == QStringList{ "Standard input", "Renamed c", "a.log" } );
                REQUIRE( dialog.checkedPaths()
                         == QStringList{ "/tmp/stdin_1", "/logs/c.log", "/logs/a.log" } );
            }
        }

        WHEN( "Drop duplicate lines is checked" )
        {
            parts.dropDuplicates->setChecked( true );

            THEN( "the dialog asks for dedup" )
            {
                REQUIRE( dialog.dropsDuplicates() );
            }
        }

        WHEN( "no row is selected" )
        {
            parts.list->setCurrentRow( -1 );

            THEN( "neither move button is enabled" )
            {
                REQUIRE_FALSE( parts.moveUp->isEnabled() );
                REQUIRE_FALSE( parts.moveDown->isEnabled() );
            }
        }
    }
}
