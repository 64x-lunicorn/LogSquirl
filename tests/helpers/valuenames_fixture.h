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

#pragma once

// The Value Names the view tests name Log Lines with (#647): the example of
// the ticket, set in the Value Names Collection for as long as a
// ScopedValueNames lives.

#include <utility>

#include <QList>

#include "naminggroup.h"
#include "valuenamescollection.h"

namespace valuenamesfixture {

using logsquirl::valuenames::GroupTable;
using logsquirl::valuenames::NameRow;
using logsquirl::valuenames::NameTable;
using logsquirl::valuenames::NamingGroup;
using logsquirl::valuenames::NamingRule;

// "BAP << ECU 0x15 0x14" shows "BAP << ECU Beispiel(0x15) Sample(0x14)",
// and "id=7" shows "id=seven(7)"; nothing else is named.
inline NamingGroup exampleGroup()
{
    auto group = NamingGroup::createNewGroup( QStringLiteral( "BAP" ) );

    NamingRule ecu;
    ecu.name = QStringLiteral( "BAP ECU" );
    ecu.pattern = QStringLiteral( "BAP << ECU (0x[0-9A-F]{2}) (0x[0-9A-F]{2})" );
    ecu.groupTables = { GroupTable{ QStringLiteral( "1" ), QStringLiteral( "ECU" ) },
                        GroupTable{ QStringLiteral( "2" ), QStringLiteral( "Function" ) } };

    NamingRule id;
    id.name = QStringLiteral( "Id" );
    id.pattern = QStringLiteral( "id=(\\d+)" );
    id.groupTables = { GroupTable{ QStringLiteral( "1" ), QStringLiteral( "Ids" ) } };

    group.setRules( { ecu, id } );
    group.setTables(
        { NameTable{ QStringLiteral( "ECU" ),
                     { NameRow{ QStringLiteral( "0x0*15" ), QStringLiteral( "Beispiel" ) } } },
          NameTable{ QStringLiteral( "Function" ),
                     { NameRow{ QStringLiteral( "0x14" ), QStringLiteral( "Sample" ) } } },
          NameTable{ QStringLiteral( "Ids" ),
                     { NameRow{ QStringLiteral( "7" ), QStringLiteral( "seven" ) } } } } );
    return group;
}

// The groups of the Value Names Collection, replaced for as long as this
// lives and restored when it goes.
class ScopedValueNames {
public:
    explicit ScopedValueNames( QList<NamingGroup> groups = { exampleGroup() } )
        : before_( ValueNamesCollection::get().ownGroups() )
    {
        ValueNamesCollection::get().setGroups( std::move( groups ) );
    }

    ~ScopedValueNames()
    {
        ValueNamesCollection::get().setGroups( before_ );
    }

    ScopedValueNames( const ScopedValueNames& ) = delete;
    ScopedValueNames& operator=( const ScopedValueNames& ) = delete;

private:
    QList<NamingGroup> before_;
};

} // namespace valuenamesfixture
