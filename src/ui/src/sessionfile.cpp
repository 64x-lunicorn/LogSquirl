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

#include "sessionfile.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "containers.h"
#include "settingspolicies.h"
#include "tabgroupinfo.h"
#include "tabnamemapping.h"
#include "viewstatecodec.h"

namespace {

constexpr auto FormatName = "logsquirl-session";

// The view state saved for a Log File, as the JSON object it is written as.
QJsonObject viewStateObject( const QString& viewContext )
{
    const auto document = QJsonDocument::fromJson( viewContext.toUtf8() );
    if ( document.isObject() ) {
        return document.object();
    }
    // A view state in the legacy format glogg wrote: in the current one.
    const auto current = encodeViewState( decodeViewState( viewContext, QuickFindPolicy{} ) );
    return QJsonDocument::fromJson( current.toUtf8() ).object();
}

// The file at `path`, else at `relativePath` in `folder`; empty when it is at
// neither.
QString resolve( const QString& path, const QString& relativePath, const QDir& folder )
{
    if ( !path.isEmpty() && QFileInfo( path ).isFile() ) {
        return QDir::cleanPath( QFileInfo( path ).absoluteFilePath() );
    }
    if ( !relativePath.isEmpty() ) {
        const auto moved = QDir::cleanPath( folder.absoluteFilePath( relativePath ) );
        if ( QFileInfo( moved ).isFile() ) {
            return moved;
        }
    }
    return {};
}

// Keeps only the groups some tab is in.
void dropUnusedGroups( WindowSnapshot& window )
{
    std::erase_if( window.groups, [ &window ]( const WindowSnapshot::Group& group ) {
        return std::ranges::none_of( window.tabs, [ &group ]( const WindowSnapshot::Tab& tab ) {
            return tab.group == group.name;
        } );
    } );
}

} // namespace

QString sessionFileErrorText( SessionFileError error )
{
    switch ( error ) {
    case SessionFileError::NotASessionFile:
        return QCoreApplication::translate( "SessionFile",
                                            "This is not a LogSquirl session file." );
    case SessionFileError::NewerVersion:
        return QCoreApplication::translate( "SessionFile",
                                            "This session file was saved by a newer LogSquirl." );
    }
    return {};
}

QString tabLabelKey( const SessionInfo::OpenFile& file )
{
    return file.archiveMember.isEmpty() ? file.fileName : file.archiveMember.key();
}

QByteArray writeSessionFile( const WindowSnapshot& window, const QDir& folder )
{
    QJsonArray files;
    for ( size_t i = 0; i < window.files.size(); ++i ) {
        const auto& file = window.files[ i ];
        QJsonObject entry;

        if ( file.archiveMember.isEmpty() ) {
            const auto path = QDir::cleanPath( QFileInfo( file.fileName ).absoluteFilePath() );
            entry[ "path" ] = path;
            entry[ "relativePath" ] = folder.relativeFilePath( path );
        }
        else {
            // The temporary file it was read from is gone: it is named by
            // its archive and member, as its tab name is stored.
            const auto& member = file.archiveMember;
            const auto archivePath
                = QDir::cleanPath( QFileInfo( member.archive ).absoluteFilePath() );
            const auto archiveRelativePath = folder.relativeFilePath( archivePath );
            const auto inside = member.key().mid( member.archive.size() );
            entry[ "path" ] = archivePath + inside;
            entry[ "relativePath" ] = archiveRelativePath + inside;

            QJsonObject archive;
            archive[ "path" ] = archivePath;
            archive[ "relativePath" ] = archiveRelativePath;
            archive[ "member" ] = member.members.value( 0 );
            if ( member.members.size() > 1 ) {
                archive[ "members" ] = QJsonArray::fromStringList( member.members );
            }
            entry[ "archive" ] = archive;
        }

        entry[ "viewState" ] = viewStateObject( file.viewContext );

        if ( i < window.tabs.size() ) {
            const auto& tab = window.tabs[ i ];
            if ( !tab.name.isEmpty() ) {
                entry[ "tabName" ] = tab.name;
            }
            if ( !tab.group.isEmpty() ) {
                entry[ "group" ] = tab.group;
            }
        }

        files.append( entry );
    }

    QJsonArray groups;
    for ( const auto& group : window.groups ) {
        groups.append( QJsonObject{ { "name", group.name }, { "color", group.color.name() } } );
    }

    QJsonObject root;
    root[ "format" ] = FormatName;
    root[ "version" ] = SessionFileVersion;
    root[ "currentFile" ] = window.currentFile;
    root[ "files" ] = files;
    root[ "groups" ] = groups;

    return QJsonDocument( root ).toJson( QJsonDocument::Indented );
}

void SessionFileRead::leaveOut(
    const std::function<bool( const SessionInfo::OpenFile& )>& isLeftOut )
{
    WindowSnapshot kept;
    kept.groups = window.groups;
    const auto hasTabs = window.tabs.size() == window.files.size();
    for ( size_t i = 0; i < window.files.size(); ++i ) {
        const auto& file = window.files[ i ];
        if ( isLeftOut( file ) ) {
            leftOut.append( QDir::toNativeSeparators( tabLabelKey( file ) ) );
            continue;
        }
        if ( static_cast<int>( i ) <= window.currentFile ) {
            // The tab in front, or the last one left before it.
            kept.currentFile = logsquirl::isize( kept.files );
        }
        kept.files.push_back( file );
        if ( hasTabs ) {
            kept.tabs.push_back( window.tabs[ i ] );
        }
    }
    if ( window.currentFile >= 0 && kept.currentFile < 0 && !kept.files.empty() ) {
        // None was left up to the tab in front: the first one after it.
        kept.currentFile = 0;
    }
    dropUnusedGroups( kept );
    window = std::move( kept );
}

std::expected<SessionFileRead, SessionFileError> readSessionFile( const QByteArray& text,
                                                                  const QDir& folder )
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( text, &parseError );
    if ( parseError.error != QJsonParseError::NoError || !document.isObject() ) {
        return std::unexpected( SessionFileError::NotASessionFile );
    }
    const auto root = document.object();
    if ( root.value( "format" ).toString() != QLatin1String( FormatName ) ) {
        return std::unexpected( SessionFileError::NotASessionFile );
    }
    if ( root.value( "version" ).toInt( SessionFileVersion ) > SessionFileVersion ) {
        return std::unexpected( SessionFileError::NewerVersion );
    }

    SessionFileRead read;
    auto& window = read.window;
    window.currentFile = root.value( "currentFile" ).toInt( -1 );

    // Whether each Log File was found, parallel to window.files.
    std::vector<bool> found;
    for ( const auto& value : root.value( "files" ).toArray() ) {
        const auto entry = value.toObject();
        const auto path = entry.value( "path" ).toString();
        const auto relativePath = entry.value( "relativePath" ).toString();

        ArchiveMember member;
        QString fileName;
        if ( entry.contains( "archive" ) ) {
            const auto archive = entry.value( "archive" ).toObject();
            const auto archivePath = archive.value( "path" ).toString();
            const auto resolved
                = resolve( archivePath, archive.value( "relativePath" ).toString(), folder );
            member.archive = resolved.isEmpty() ? archivePath : resolved;
            if ( archive.contains( "members" ) ) {
                for ( const auto& level : archive.value( "members" ).toArray() ) {
                    member.members.append( level.toString() );
                }
            }
            else {
                member.members.append( archive.value( "member" ).toString() );
            }
            fileName = member.key();
            found.push_back( !resolved.isEmpty() );
        }
        else {
            const auto resolved = resolve( path, relativePath, folder );
            fileName = resolved.isEmpty() ? path : resolved;
            found.push_back( !resolved.isEmpty() );
        }

        const auto viewState = entry.value( "viewState" );
        const auto viewContext
            = viewState.isObject()
                  ? QString::fromUtf8(
                        QJsonDocument( viewState.toObject() ).toJson( QJsonDocument::Compact ) )
                  : QString{};

        window.files.emplace_back( fileName, viewContext, member );
        window.tabs.push_back( WindowSnapshot::Tab{ entry.value( "tabName" ).toString(),
                                                    entry.value( "group" ).toString() } );
    }

    for ( const auto& value : root.value( "groups" ).toArray() ) {
        const auto group = value.toObject();
        const auto name = group.value( "name" ).toString();
        if ( name.isEmpty() || std::ranges::any_of( window.groups, [ &name ]( const auto& known ) {
                 return known.name == name;
             } ) ) {
            continue;
        }
        window.groups.push_back(
            WindowSnapshot::Group{ name, QColor( group.value( "color" ).toString() ) } );
    }

    // What is found at neither of its paths is left out.
    size_t next = 0;
    read.leaveOut( [ &found, &next ]( const SessionInfo::OpenFile& ) { return !found[ next++ ]; } );

    return read;
}

void takeTabLabels( WindowSnapshot& window )
{
    window.tabs.clear();
    window.groups.clear();

    const auto& names = TabNameMapping::get();
    const auto& groups = TabGroupInfo::get();
    for ( const auto& file : window.files ) {
        const auto key = tabLabelKey( file );
        WindowSnapshot::Tab tab;
        tab.name = names.tabName( key );
        if ( const auto group = groups.groupForTab( key ) ) {
            tab.group = group->name;
            if ( std::ranges::none_of( window.groups, [ &group ]( const auto& known ) {
                     return known.name == group->name;
                 } ) ) {
                window.groups.push_back( WindowSnapshot::Group{ group->name, group->color } );
            }
        }
        window.tabs.push_back( std::move( tab ) );
    }
}

void applyTabLabels( const WindowSnapshot& window,
                     const std::function<bool( const SessionInfo::OpenFile& )>& isOpened )
{
    if ( window.tabs.size() != window.files.size() ) {
        return;
    }
    const auto applies
        = [ &window, &isOpened ]( size_t i ) { return !isOpened || isOpened( window.files[ i ] ); };

    auto& names = TabNameMapping::getSynced();
    for ( size_t i = 0; i < window.files.size(); ++i ) {
        if ( applies( i ) && !window.tabs[ i ].name.isEmpty() ) {
            names.setTabName( tabLabelKey( window.files[ i ] ), window.tabs[ i ].name );
        }
    }
    names.save();

    auto& groups = TabGroupInfo::getSynced();
    for ( size_t i = 0; i < window.files.size(); ++i ) {
        const auto& name = window.tabs[ i ].group;
        if ( !applies( i ) || name.isEmpty() ) {
            continue;
        }
        const auto& stored = groups.groups();
        auto groupId = QString{};
        if ( const auto sameName = std::ranges::find( stored, name, &TabGroupInfo::TabGroup::name );
             sameName != stored.end() ) {
            // It keeps its color.
            groupId = sameName->id;
        }
        else {
            const auto saved
                = std::ranges::find( window.groups, name, &WindowSnapshot::Group::name );
            const auto color = saved != window.groups.end() && saved->color.isValid()
                                   ? saved->color
                                   : QColor( Qt::blue );
            groupId = groups.addGroup( name, color );
        }
        groups.addTabToGroup( groupId, tabLabelKey( window.files[ i ] ) );
    }
    groups.save();
}
