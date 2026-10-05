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

#include "configuration.h"
#include "filetypes.h"

void FileAssociationChoice::apply( const QStringList& checkedIds )
{
    ask = false;
    chosen = checkedIds;
    dismissed.clear();
    QStringList stillConfirmed;
    for ( const auto& id : confirmed ) {
        if ( checkedIds.contains( id ) ) {
            stillConfirmed << id;
        }
    }
    confirmed = stillConfirmed;
}

bool FileAssociationChoice::confirm( const FileAssociationStates& states )
{
    if ( !chosen ) {
        return false;
    }
    auto changed = false;
    for ( const auto& id : *chosen ) {
        if ( isDefault( states, id ) && !confirmed.contains( id ) ) {
            confirmed << id;
            changed = true;
        }
    }
    return changed;
}

FileAssociationChoice FileAssociationChoice::of( const Configuration& config )
{
    FileAssociationChoice choice;
    choice.ask = config.askForFileAssociations();
    choice.chosen = config.chosenFileAssociations();
    choice.dismissed = config.dismissedFileAssociations();
    choice.confirmed = config.confirmedFileAssociations();
    return choice;
}

void FileAssociationChoice::keepIn( Configuration& config ) const
{
    config.setAskForFileAssociations( ask );
    config.setChosenFileAssociations( chosen );
    config.setDismissedFileAssociations( dismissed );
    config.setConfirmedFileAssociations( confirmed );
    config.save();
}

void FileAssociationChoice::confirmIn( Configuration& config, const FileAssociationStates& states )
{
    auto choice = of( config );
    if ( choice.confirm( states ) ) {
        choice.keepIn( config );
    }
}

FileAssociationsAtStart FileAssociationsAtStart::of( const FileAssociations& fileAssociations,
                                                     FileAssociationChoice& choice, bool mayAsk )
{
    FileAssociationsAtStart atStart;
    if ( !fileAssociations.isAvailable() ) {
        return atStart;
    }

    const auto states = fileAssociations.states();

    // Before the user chose, what LogSquirl opens is what the installer, or
    // whoever made it the default, chose.
    if ( !choice.chosen ) {
        QStringList defaults;
        for ( const auto& type : FileTypes::choices() ) {
            if ( isDefault( states, type.id ) ) {
                defaults << type.id;
            }
        }
        if ( !defaults.isEmpty() ) {
            choice.chosen = defaults;
            atStart.choiceChanged = true;
        }
    }
    if ( choice.confirm( states ) ) {
        atStart.choiceChanged = true;
    }

    if ( mayAsk && choice.ask ) {
        auto suggestedAreDefault = true;
        for ( const auto& type : FileTypes::choices() ) {
            if ( type.checkedByDefault || isChosen( stateOf( states, type.id ) ) ) {
                atStart.checks << type.id;
            }
            suggestedAreDefault
                = suggestedAreDefault && ( !type.checkedByDefault || isDefault( states, type.id ) );
        }
        // Nothing to ask when everything it would suggest is LogSquirl's.
        atStart.ask = !suggestedAreDefault;
        if ( atStart.ask ) {
            return atStart;
        }
        atStart.checks.clear();
    }

    if ( !choice.chosen ) {
        return atStart;
    }
    // A moved portable LogSquirl: every chosen type points at where it was,
    // whatever the system says of it. Otherwise a type is lost once LogSquirl
    // no longer opens it, having opened it since it was chosen.
    const auto oldLocation = fileAssociations.movedFrom();
    for ( const auto& type : FileTypes::choices() ) {
        if ( !choice.chosen->contains( type.id ) || choice.dismissed.contains( type.id ) ) {
            continue;
        }
        if ( !oldLocation.isEmpty()
             || ( choice.confirmed.contains( type.id ) && !isDefault( states, type.id ) ) ) {
            atStart.lost << type.id;
        }
    }
    if ( !atStart.lost.isEmpty() ) {
        atStart.movedFrom = oldLocation;
    }
    return atStart;
}
