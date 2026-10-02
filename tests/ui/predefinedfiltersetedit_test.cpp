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

#include <QCheckBox>
#include <QSignalSpy>

#include "predefinedfilters.h"
#include "predefinedfiltersetedit.h"

SCENARIO( "Checking a Predefined Filter's Regex box is kept", "[ui][predefinedfilters]" )
{
    auto group = PredefinedFilterSet::createNewSet( QStringLiteral( "Group" ) );
    group.addFilter( { QStringLiteral( "Errors" ), QStringLiteral( "err.r" ), false } );

    PredefinedFilterSetEdit edit;
    edit.setFilterSet( group );
    edit.show();
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
