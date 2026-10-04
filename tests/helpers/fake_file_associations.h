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

#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

#include "fileassociations.h"

// File associations that stand in for the machine (#720): the states are
// what the test sets, apply() changes them the way a platform without a
// confirmation would and records what it was asked. A type it has no state
// for is not registered.
class FakeFileAssociations : public FileAssociations {
public:
    bool available = true;
    QString reason;
    QString note;
    FileAssociationStates current;
    // The ids of the types apply() fails for, and the error it gives then.
    QStringList failing;
    QString failure = QStringLiteral( "The system refused." );
    // Each call of apply(): the ids to make default, the ids to give back.
    std::vector<std::pair<QStringList, QStringList>> applied;
    // The entry "Open with LogSquirl" of every file's context menu: whether
    // the platform has one, whether it is there, and each call to show or
    // hide it.
    bool offersEntry = false;
    bool entry = false;
    std::vector<bool> entrySet;

    bool isAvailable() const override
    {
        return available;
    }

    QString unavailableReason() const override
    {
        return available ? QString{} : reason;
    }

    QString applyNote() const override
    {
        return note;
    }

    FileAssociationState state( const FileType& type ) const override
    {
        const auto it = current.find( type.id );
        return it == current.end() ? FileAssociationState::NotRegistered : it->second;
    }

    FileAssociationResult apply( const std::vector<FileType>& makeDefault,
                                 const std::vector<FileType>& release ) override
    {
        FileAssociationResult result;
        std::pair<QStringList, QStringList> call;
        for ( const auto& type : makeDefault ) {
            call.first << type.id;
            if ( failing.contains( type.id ) ) {
                result.failed << type.id;
                continue;
            }
            current[ type.id ] = FileAssociationState::Default;
        }
        for ( const auto& type : release ) {
            call.second << type.id;
            if ( failing.contains( type.id ) ) {
                result.failed << type.id;
                continue;
            }
            if ( state( type ) == FileAssociationState::Default ) {
                current[ type.id ] = FileAssociationState::Registered;
            }
        }
        if ( !result.failed.isEmpty() ) {
            result.error = failure;
        }
        applied.push_back( call );
        return result;
    }

    bool offersContextMenuEntry() const override
    {
        return offersEntry;
    }

    bool hasContextMenuEntry() const override
    {
        return entry;
    }

    FileAssociationResult setContextMenuEntry( bool shown ) override
    {
        entrySet.push_back( shown );
        entry = shown;
        return {};
    }

    // Another application takes the type over, or the user confirms it
    // outside LogSquirl.
    void change( const QString& id, FileAssociationState to )
    {
        current[ id ] = to;
        Q_EMIT statesChanged();
    }
};
