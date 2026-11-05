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

// The editor of a Predefined Filter group keeps what is changed in its table.

#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QRegularExpression>
#include <QSignalSpy>

#include "predefinedfilters.h"
#include "predefinedfiltersdialog.h"
#include "predefinedfiltersetedit.h"
#include "shown_widget.h"

SCENARIO( "Checking a Predefined Filter's Regex box is kept", "[ui][predefinedfilters]" )
{
    auto group = PredefinedFilterSet::createNewSet( QStringLiteral( "Group" ) );
    group.addFilter( { QStringLiteral( "Errors" ), QStringLiteral( "err.r" ), false } );

    PredefinedFilterSetEdit edit;
    edit.setFilterSet( group );
    showUntilExposed( edit );
    QSignalSpy changed( &edit, &PredefinedFilterSetEdit::changed );

    WHEN( "the Regex box of a filter is checked, and nothing else is changed" )
    {
        auto* regex = edit.filtersTableWidget->cellWidget( 0, 2 )->findChild<QCheckBox*>();
        REQUIRE( regex != nullptr );
        regex->click();

        THEN( "the group the editor hands back reads the filter as a regular expression" )
        {
            CHECK( edit.filterSet().filters().front().useRegex );
            CHECK( changed.size() == 1 );
        }
    }
}

// The glossary calls a group of Predefined Filters a Filter Group, never a
// filter set; so does the dialog that edits them (#891).
SCENARIO( "The Predefined Filters dialog calls a group a Filter Group", "[ui][predefinedfilters]" )
{
    GIVEN( "the Predefined Filters dialog" )
    {
        PredefinedFiltersDialog dialog( QString{} );

        THEN( "the editor's name field is labeled as the Filter Group's" )
        {
            REQUIRE( dialog.findChild<PredefinedFilterSetEdit*>() != nullptr );
            const auto* label = dialog.findChild<PredefinedFilterSetEdit*>()->nameLabel;
            CHECK( label->text() == QStringLiteral( "Filter Group name:" ) );
        }

        THEN( "no title, label, button or tooltip of it speaks of a set" )
        {
            static const QRegularExpression set( QStringLiteral( R"(\bsets?\b)" ),
                                                 QRegularExpression::CaseInsensitiveOption );
            QStringList texts{ dialog.windowTitle() };
            for ( const auto* widget : dialog.findChildren<QWidget*>() ) {
                texts << widget->windowTitle() << widget->toolTip();
                if ( const auto* label = qobject_cast<const QLabel*>( widget ) ) {
                    texts << label->text();
                }
                if ( const auto* box = qobject_cast<const QGroupBox*>( widget ) ) {
                    texts << box->title();
                }
                if ( const auto* button = qobject_cast<const QAbstractButton*>( widget ) ) {
                    texts << button->text();
                }
            }
            for ( const auto& text : texts ) {
                INFO( text.toStdString() );
                CHECK_FALSE( set.match( text ).hasMatch() );
            }
        }
    }
}
