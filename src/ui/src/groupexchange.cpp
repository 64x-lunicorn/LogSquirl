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

#include "groupexchange.h"

#include <QFileInfo>
#include <QSettings>

#include <utility>

#include "uuid.h"

namespace logsquirl::groupexchange {

namespace {

QString& lastExportFolder()
{
    static QString folder;
    return folder;
}

template <typename Collection>
bool writeCollection( const QString& file, const Collection& collection )
{
    QSettings settings{ file, QSettings::IniFormat };
    // Start from an empty file: an existing one may hold other groups.
    settings.clear();
    collection.saveToStorage( settings );
    settings.sync();
    return settings.status() == QSettings::NoError;
}

template <typename Group>
QStringList namesOf( const QList<Group>& groups, qsizetype except = -1 )
{
    QStringList names;
    for ( qsizetype i = 0; i < groups.size(); ++i ) {
        if ( i != except ) {
            names.append( groups[ i ].name() );
        }
    }
    return names;
}

template <typename Group>
qsizetype indexOfId( const QList<Group>& groups, const QString& id )
{
    for ( qsizetype i = 0; i < groups.size(); ++i ) {
        if ( groups[ i ].id() == id ) {
            return i;
        }
    }
    return -1;
}

template <typename Group>
qsizetype indexOfName( const QList<Group>& groups, const QString& name )
{
    for ( qsizetype i = 0; i < groups.size(); ++i ) {
        if ( groups[ i ].name() == name ) {
            return i;
        }
    }
    return -1;
}

template <typename Group>
ImportResult mergeImpl( QList<Group>& groups, const QList<Group>& imported, ImportSession& session )
{
    ImportResult result;
    for ( Group group : imported ) {
        // Whatever the Default Filter Group's id stands for on the sender's
        // side, on the recipient's it is a group of its own.
        if ( group.id() == defaultFilterSetId() ) {
            group = group.withId( generateIdFromUuid() );
        }

        auto existing = indexOfId( groups, group.id() );
        const auto kind = existing >= 0 ? ConflictKind::SameId : ConflictKind::SameName;
        if ( existing < 0 ) {
            existing = indexOfName( groups, group.name() );
        }
        if ( existing < 0 ) {
            groups.append( group );
            ++result.added;
            continue;
        }

        const bool replaceAllowed = groups[ existing ].id() != defaultFilterSetId();
        auto answer = session.decide( { kind, group.name(), groups[ existing ].name(),
                                        preselectedAnswer( kind ), replaceAllowed } );
        if ( answer == ConflictAnswer::Replace && !replaceAllowed ) {
            answer = ConflictAnswer::KeepBoth;
        }

        switch ( answer ) {
        case ConflictAnswer::Replace: {
            const auto name = firstFreeName( group.name(), namesOf( groups, existing ) );
            group = group.withId( groups[ existing ].id() );
            group.setName( name );
            groups[ existing ] = group;
            ++result.replaced;
            break;
        }
        case ConflictAnswer::KeepBoth: {
            const auto name = firstFreeName( group.name(), namesOf( groups ) );
            group = group.withId( generateIdFromUuid() );
            group.setName( name );
            groups.append( group );
            ++result.added;
            break;
        }
        case ConflictAnswer::Skip:
            ++result.skipped;
            break;
        }
    }
    return result;
}

// Whether the file can be read as settings at all.
bool isReadableSettings( const QString& file, const QSettings& settings )
{
    const QFileInfo info( file );
    return info.isFile() && info.isReadable() && settings.status() == QSettings::NoError;
}

template <typename Group>
ImportResult importImpl( const ReadGroups<Group>& read, QList<Group>& groups,
                         ImportSession& session )
{
    if ( read.error != ReadError::None ) {
        ImportResult result;
        result.error = read.error;
        return result;
    }
    return mergeImpl( groups, read.groups, session );
}

} // namespace

ConflictAnswer preselectedAnswer( ConflictKind kind )
{
    return kind == ConflictKind::SameId ? ConflictAnswer::Replace : ConflictAnswer::KeepBoth;
}

QString firstFreeName( const QString& name, const QStringList& takenNames )
{
    if ( !takenNames.contains( name ) ) {
        return name;
    }
    for ( int n = 2;; ++n ) {
        const auto candidate = QStringLiteral( "%1 (%2)" ).arg( name ).arg( n );
        if ( !takenNames.contains( candidate ) ) {
            return candidate;
        }
    }
}

ImportSession::ImportSession( ConflictResolver resolver )
    : resolver_( std::move( resolver ) )
{
}

ConflictAnswer ImportSession::decide( const ConflictQuestion& question )
{
    if ( rememberedAnswer_ ) {
        return *rememberedAnswer_;
    }
    const auto decision = resolver_( question );
    if ( decision.applyToAll ) {
        rememberedAnswer_ = decision.answer;
    }
    return decision.answer;
}

ReadGroups<PredefinedFilterSet> readFilterGroups( const QString& file )
{
    ReadGroups<PredefinedFilterSet> read;
    QSettings settings{ file, QSettings::IniFormat };
    if ( !isReadableSettings( file, settings ) ) {
        read.error = ReadError::Unreadable;
        return read;
    }

    PredefinedFiltersCollection collection;
    collection.retrieveFromStorage( settings );
    for ( const auto& set : collection.filterSets() ) {
        // The collection adds an empty Default group to a file that has none.
        const bool absentDefault = set.id() == defaultFilterSetId() && set.filters().isEmpty();
        if ( !set.id().isEmpty() && !absentDefault ) {
            read.groups.append( set );
        }
    }
    read.error = read.groups.isEmpty() ? ReadError::NoGroups : ReadError::None;
    return read;
}

ReadGroups<HighlighterSet> readHighlighterGroups( const QString& file )
{
    ReadGroups<HighlighterSet> read;
    QSettings settings{ file, QSettings::IniFormat };
    if ( !isReadableSettings( file, settings ) ) {
        read.error = ReadError::Unreadable;
        return read;
    }

    HighlighterSetCollection collection;
    collection.retrieveFromStorage( settings );
    read.groups = collection.highlighterSets();
    read.error = read.groups.isEmpty() ? ReadError::NoGroups : ReadError::None;
    return read;
}

ImportResult mergeGroups( QList<PredefinedFilterSet>& groups,
                          const QList<PredefinedFilterSet>& imported, ImportSession& session )
{
    return mergeImpl( groups, imported, session );
}

ImportResult mergeGroups( QList<HighlighterSet>& groups, const QList<HighlighterSet>& imported,
                          ImportSession& session )
{
    return mergeImpl( groups, imported, session );
}

ImportResult importFile( const QString& file, QList<PredefinedFilterSet>& groups,
                         ImportSession& session )
{
    return importImpl( readFilterGroups( file ), groups, session );
}

ImportResult importFile( const QString& file, QList<HighlighterSet>& groups,
                         ImportSession& session )
{
    return importImpl( readHighlighterGroups( file ), groups, session );
}

QString suggestedFileName( const QString& groupName, GroupKind kind )
{
    QString name = groupName;
    for ( QChar& character : name ) {
        if ( QStringLiteral( "/\\:*?\"<>|" ).contains( character ) ) {
            character = QLatin1Char( '_' );
        }
    }
    return name
           + ( kind == GroupKind::Filter ? QStringLiteral( "_filter.conf" )
                                         : QStringLiteral( "_highlighter.conf" ) );
}

bool writeGroup( const QString& file, const PredefinedFilterSet& group )
{
    PredefinedFiltersCollection collection;
    collection.setFilterSets( { group } );
    return writeCollection( file, collection );
}

bool writeGroup( const QString& file, const HighlighterSet& group )
{
    // A fresh collection has no Color Labels and no active sets.
    HighlighterSetCollection collection;
    collection.setHighlighterSets( { group } );
    return writeCollection( file, collection );
}

QString exportFolder()
{
    return lastExportFolder();
}

void rememberExportFolder( const QString& file )
{
    lastExportFolder() = QFileInfo( file ).absolutePath();
}

QString withConfSuffix( const QString& file )
{
    return file.endsWith( QStringLiteral( ".conf" ) ) ? file : file + QStringLiteral( ".conf" );
}

} // namespace logsquirl::groupexchange
