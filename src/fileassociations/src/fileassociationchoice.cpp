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

#include "fileassociationchoice.h"

#include "filetypes.h"

void FileAssociationChoice::apply( const QStringList& checkedIds )
{
    ask = false;
    chosen = checkedIds;
}

FileAssociationsAtStart FileAssociationsAtStart::of( const FileAssociations& fileAssociations,
                                                     const FileAssociationChoice& choice,
                                                     bool mayAsk )
{
    FileAssociationsAtStart atStart;
    if ( !fileAssociations.isAvailable() ) {
        return atStart;
    }

    const auto states = fileAssociations.states();
    const auto isDefault = [ &states ]( const QString& id ) {
        const auto it = states.find( id );
        return it != states.end() && it->second == FileAssociationState::Default;
    };

    if ( mayAsk && choice.ask ) {
        auto suggestedAreDefault = true;
        for ( const auto& type : FileTypes::choices() ) {
            if ( type.checkedByDefault || isDefault( type.id ) ) {
                atStart.checks << type.id;
            }
            suggestedAreDefault
                = suggestedAreDefault && ( !type.checkedByDefault || isDefault( type.id ) );
        }
        // Nothing to ask when everything it would suggest is LogSquirl's.
        atStart.ask = !suggestedAreDefault;
        if ( !atStart.ask ) {
            atStart.checks.clear();
        }
    }
    return atStart;
}
