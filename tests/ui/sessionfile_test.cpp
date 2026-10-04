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

// A Session File: one window's Session saved to a file and read again, here
// or, moved with its logs, on another machine (#576).

#include "recording_views.h"
#include "test_policies.h"

#include "logformatcatalog.h"
#include "session.h"
#include "sessionfile.h"
#include "sessioninfo.h"
#include "settingspolicies.h"
#include "tabgroupinfo.h"
#include "tabnamemapping.h"
#include "viewstatecodec.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

// Writes a file of one line at `path`, making its folder, and returns the
// path.
QString writeLogFile( const QString& path )
{
    QDir().mkpath( QFileInfo( path ).absolutePath() );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "a Log Line\n" );
    return QDir::cleanPath( QFileInfo( path ).absoluteFilePath() );
}

QString viewContextWith( LineNumber::UnderlyingType scrollPosition, bool followFile )
{
    ViewState state;
    state.sizes = { 400, 100 };
    state.followFile = followFile;
    state.ignoreCase = true;
    state.marks = { 3, 17 };
    state.scrollPosition = scrollPosition;
    return encodeViewState( state );
}

ViewState decoded( const QString& viewContext )
{
    return decodeViewState( viewContext, QuickFindPolicy{} );
}

QJsonObject asJson( const QByteArray& text )
{
    return QJsonDocument::fromJson( text ).object();
}

// Leaves the stored tab names and groups as they were once it goes, whatever
// the test stored in between.
class StoredTabLabels {
public:
    StoredTabLabels()
        : names_( TabNameMapping::getSynced() )
        , groups_( TabGroupInfo::getSynced() )
    {
    }

    ~StoredTabLabels()
    {
        names_.save();
        TabNameMapping::getSynced();
        groups_.save();
        TabGroupInfo::getSynced();
    }

    StoredTabLabels( const StoredTabLabels& ) = delete;
    StoredTabLabels& operator=( const StoredTabLabels& ) = delete;
    StoredTabLabels( StoredTabLabels&& ) = delete;
    StoredTabLabels& operator=( StoredTabLabels&& ) = delete;

private:
    const TabNameMapping names_;
    const TabGroupInfo groups_;
};

class TextViewContext final : public ViewContextInterface {
public:
    explicit TextViewContext( QString text )
        : text_( std::move( text ) )
    {
    }

    QString toString() const override
    {
        return text_;
    }

private:
    QString text_;
};

} // namespace

SCENARIO( "A Session File reads back the snapshot it was written from", "[ui][session][file]" )
{
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto appLog = writeLogFile( folder.filePath( "logs/app.log" ) );
    const auto dbLog = writeLogFile( folder.filePath( "db.log" ) );
    const auto archive = writeLogFile( folder.filePath( "logs/old.zip" ) );
    const ArchiveMember member{ archive, { "a/old.log.gz", QString{} } };

    WindowSnapshot window;
    window.files = { { appLog, viewContextWith( 1200, true ) },
                     { "/tmp/old.log.AbCdEf", viewContextWith( 7, false ), member },
                     { dbLog, viewContextWith( 0, false ) } };
    window.currentFile = 1;
    window.tabs = { { "App", "Backend" }, { QString{}, "Archive" }, { "DB", "Backend" } };
    window.groups = { { "Backend", QColor( "#3a7bd5" ) }, { "Archive", QColor( "#aa0000" ) } };

    const auto text = writeSessionFile( window, QDir( folder.path() ) );

    THEN( "the file says what it is, and names each Log File by both of its paths" )
    {
        const auto root = asJson( text );
        REQUIRE( root.value( "format" ).toString() == "logsquirl-session" );
        REQUIRE( root.value( "version" ).toInt() == 1 );
        REQUIRE( root.value( "currentFile" ).toInt() == 1 );
        const auto files = root.value( "files" ).toArray();
        REQUIRE( files.size() == 3 );
        REQUIRE( files[ 0 ].toObject().value( "path" ).toString() == appLog );
        REQUIRE( files[ 0 ].toObject().value( "relativePath" ).toString() == "logs/app.log" );
        REQUIRE( files[ 0 ].toObject().value( "viewState" ).isObject() );
        REQUIRE( files[ 0 ].toObject().value( "tabName" ).toString() == "App" );
        REQUIRE( files[ 0 ].toObject().value( "group" ).toString() == "Backend" );

        const auto archived = files[ 1 ].toObject();
        REQUIRE_FALSE( archived.contains( "tabName" ) );
        const auto archiveEntry = archived.value( "archive" ).toObject();
        REQUIRE( archiveEntry.value( "path" ).toString() == archive );
        REQUIRE( archiveEntry.value( "relativePath" ).toString() == "logs/old.zip" );
        REQUIRE( archiveEntry.value( "member" ).toString() == "a/old.log.gz" );

        REQUIRE( root.value( "groups" ).toArray().size() == 2 );
        REQUIRE( root.value( "groups" ).toArray()[ 0 ].toObject().value( "color" ).toString()
                 == "#3a7bd5" );
    }

    WHEN( "it is read again" )
    {
        const auto read = readSessionFile( text, QDir( folder.path() ) );
        REQUIRE( read.has_value() );

        THEN( "it gives the same Log Files in the same order, the same tab in front, the same "
              "view states, tab names and groups, and leaves none out" )
        {
            REQUIRE( read->leftOut.isEmpty() );
            const auto& reread = read->window;
            REQUIRE( reread.files.size() == 3 );
            REQUIRE( reread.files[ 0 ] == window.files[ 0 ] );
            REQUIRE( reread.files[ 1 ].archiveMember == member );
            REQUIRE( decoded( reread.files[ 1 ].viewContext )
                     == decoded( window.files[ 1 ].viewContext ) );
            REQUIRE( reread.files[ 2 ] == window.files[ 2 ] );
            REQUIRE( reread.currentFile == window.currentFile );
            REQUIRE( reread.tabs == window.tabs );
            REQUIRE( reread.groups == window.groups );
        }
    }
}

SCENARIO( "A Session File keeps every Kept Search of a tab", "[ui][session][file]" )
{
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto appLog = writeLogFile( folder.filePath( "app.log" ) );

    ViewState state;
    state.sizes = { 400, 100 };
    state.searches = { KeptSearchState{ .pattern = "ERROR", .ignoreCase = true },
                       KeptSearchState{ .pattern = "took (\\d+) ms", .useRegexp = true },
                       KeptSearchState{ .pattern = "retry", .inverseRegexp = true } };
    state.currentSearch = 2;

    WindowSnapshot window;
    window.files = { { appLog, encodeViewState( state ) } };
    window.currentFile = 0;

    WHEN( "it is written and read again" )
    {
        const auto read = readSessionFile( writeSessionFile( window, QDir( folder.path() ) ),
                                           QDir( folder.path() ) );
        REQUIRE( read.has_value() );
        REQUIRE( read->window.files.size() == 1 );

        THEN( "the tab has the same Searches in the same order, the same one current" )
        {
            const auto reread = decoded( read->window.files[ 0 ].viewContext );
            REQUIRE( reread.searches == state.searches );
            REQUIRE( reread.currentSearch == 2 );
            REQUIRE( reread == decoded( window.files[ 0 ].viewContext ) );
        }
    }
}

SCENARIO( "A folder of logs and its Session File opens once moved", "[ui][session][file]" )
{
    QTemporaryDir parent;
    REQUIRE( parent.isValid() );
    const auto original = parent.filePath( "incident" );
    const auto appLog = writeLogFile( original + "/logs/app.log" );
    const auto archive = writeLogFile( original + "/logs/old.log.gz" );

    WindowSnapshot window;
    window.files = { { appLog, viewContextWith( 5, false ) },
                     { "/tmp/old.log.XyZ", viewContextWith( 9, false ),
                       ArchiveMember{ archive, { QString{} } } } };
    window.currentFile = 0;
    const auto text = writeSessionFile( window, QDir( original ) );

    GIVEN( "the folder is moved where the saved absolute paths no longer are" )
    {
        const auto moved = parent.filePath( "elsewhere" );
        REQUIRE( QDir().rename( original, moved ) );

        WHEN( "the Session File is read in its new place" )
        {
            const auto read = readSessionFile( text, QDir( moved ) );
            REQUIRE( read.has_value() );

            THEN( "each Log File, and the archive, is found by its relative path" )
            {
                REQUIRE( read->leftOut.isEmpty() );
                REQUIRE( read->window.files.size() == 2 );
                REQUIRE( read->window.files[ 0 ].fileName
                         == QDir::cleanPath( moved + "/logs/app.log" ) );
                REQUIRE( read->window.files[ 1 ].archiveMember.archive
                         == QDir::cleanPath( moved + "/logs/old.log.gz" ) );
                REQUIRE( decoded( read->window.files[ 0 ].viewContext ).scrollPosition == 5 );
            }
        }
    }
}

SCENARIO( "A Session File leaves out a Log File that is missing", "[ui][session][file]" )
{
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto first = writeLogFile( folder.filePath( "first.log" ) );
    const auto gone = writeLogFile( folder.filePath( "gone.log" ) );
    const auto last = writeLogFile( folder.filePath( "last.log" ) );

    WindowSnapshot window;
    window.files = { { first, QString{} }, { gone, QString{} }, { last, QString{} } };
    window.currentFile = 1;
    window.tabs = { { QString{}, QString{} }, { "Gone", "Lonely" }, { QString{}, QString{} } };
    window.groups = { { "Lonely", QColor( Qt::red ) } };
    const auto text = writeSessionFile( window, QDir( folder.path() ) );
    REQUIRE( QFile::remove( gone ) );

    const auto read = readSessionFile( text, QDir( folder.path() ) );
    REQUIRE( read.has_value() );

    THEN( "the others are read, the one before it in front, and it is named" )
    {
        REQUIRE( read->window.files.size() == 2 );
        REQUIRE( read->window.files[ 0 ].fileName == first );
        REQUIRE( read->window.files[ 1 ].fileName == last );
        REQUIRE( read->window.currentFile == 0 );
        REQUIRE( read->window.tabs.size() == 2 );
        REQUIRE( read->window.groups.empty() );
        REQUIRE( read->leftOut == QStringList{ QDir::toNativeSeparators( gone ) } );
    }
}

SCENARIO( "A file that is not a Session File, or is from a newer LogSquirl, is not read",
          "[ui][session][file]" )
{
    const QDir folder = QDir::temp();

    const auto errorOf = [ &folder ]( const QByteArray& text ) {
        const auto read = readSessionFile( text, folder );
        REQUIRE_FALSE( read.has_value() );
        return read.error();
    };

    REQUIRE( errorOf( "not JSON at all" ) == SessionFileError::NotASessionFile );
    REQUIRE( errorOf( R"({"version":1,"files":[]})" ) == SessionFileError::NotASessionFile );
    REQUIRE( errorOf( R"({"format":"chipmunk","version":1})" )
             == SessionFileError::NotASessionFile );
    REQUIRE( errorOf( R"({"format":"logsquirl-session","version":2,"files":[]})" )
             == SessionFileError::NewerVersion );
    REQUIRE_FALSE( sessionFileErrorText( SessionFileError::NotASessionFile ).isEmpty() );
    REQUIRE_FALSE( sessionFileErrorText( SessionFileError::NewerVersion ).isEmpty() );

    AND_THEN( "keys it does not know are ignored" )
    {
        const auto read = readSessionFile(
            R"({"format":"logsquirl-session","version":1,"files":[],"comment":"later"})", folder );
        REQUIRE( read.has_value() );
        REQUIRE( read->window.files.empty() );
    }
}

// Saving the automatic Session and saving a Session File take the same
// snapshot of the window, which leaves a Transient Log File out (#570).
SCENARIO( "A Session File holds what the automatic Session saves, and no Transient Log File",
          "[ui][session][file]" )
{
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto ordinaryPath = writeLogFile( folder.filePath( "ordinary.log" ) );
    const auto transientPath = writeLogFile( folder.filePath( "stdin.spool" ) );
    const auto windowId = QStringLiteral( "sessionfile_test_window_576" );

    const auto appSession
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    std::vector<RecordingViews*> built;
    {
        WindowSession window{ appSession, windowId, 0 };
        const auto* transient = window.open( transientPath, RecordingViews::factory( built ),
                                             LogFileOrigin::transient() );
        const auto* ordinary = window.open( ordinaryPath, RecordingViews::factory( built ) );
        std::vector<SaveFileInfo> tabs;
        tabs.emplace_back( transient, std::make_shared<const TextViewContext>( "{}" ) );
        tabs.emplace_back( ordinary,
                           std::make_shared<const TextViewContext>( viewContextWith( 42, true ) ) );

        const auto snapshot = window.snapshot( tabs, ordinary );
        window.save( tabs, ordinary, QByteArray{}, 0 );

        THEN( "the automatic Session saves exactly the snapshot" )
        {
            REQUIRE( SessionInfo::get().openFiles( windowId ) == snapshot.files );
            REQUIRE( SessionInfo::get().currentFile( windowId ) == snapshot.currentFile );
            REQUIRE( window.storedSnapshot() == snapshot );
        }

        AND_THEN( "the Session File holds the Ordinary Log File alone, in front" )
        {
            const auto root = asJson( writeSessionFile( snapshot, QDir( folder.path() ) ) );
            const auto files = root.value( "files" ).toArray();
            REQUIRE( files.size() == 1 );
            REQUIRE( files[ 0 ].toObject().value( "path" ).toString() == ordinaryPath );
            REQUIRE( root.value( "currentFile" ).toInt() == 0 );
        }

        AND_THEN( "a Log File open in the application is known as open" )
        {
            REQUIRE( window.isOpen( SessionInfo::OpenFile{ ordinaryPath, QString{} } ) );
            REQUIRE_FALSE( window.isOpen(
                SessionInfo::OpenFile{ folder.filePath( "other.log" ), QString{} } ) );
            REQUIRE_FALSE( window.isOpen( SessionInfo::OpenFile{
                ordinaryPath, QString{}, ArchiveMember{ folder.filePath( "a.zip" ), { "x" } } } ) );
        }

        for ( const auto& tab : tabs ) {
            window.close( std::get<0>( tab ) );
        }
    }
    for ( auto* views : built ) {
        delete views;
    }

    auto& stored = SessionInfo::getSynced();
    stored.remove( windowId );
    stored.save();
}

SCENARIO( "A Session File's tab names and groups merge into the stored ones",
          "[ui][session][file]" )
{
    const StoredTabLabels keep;
    const auto first = QStringLiteral( "/sessionfile_test/first.log" );
    const auto second = QStringLiteral( "/sessionfile_test/second.log" );
    const ArchiveMember member{ "/sessionfile_test/old.zip", { "old.log" } };

    GIVEN( "a stored group named as one of the file's, in another color, holding the first Log "
           "File" )
    {
        auto& groups = TabGroupInfo::getSynced();
        const auto existing = groups.addGroup( "Backend", QColor( Qt::green ) );
        const auto other = groups.addGroup( "Elsewhere", QColor( Qt::yellow ) );
        groups.addTabToGroup( other, second );
        groups.save();

        WindowSnapshot window;
        window.files = { { first, QString{} },
                         { second, QString{} },
                         { "/tmp/old.log.Xy", QString{}, member } };
        window.tabs = { { "First", "Backend" }, { QString{}, "Frontend" }, { "Old", QString{} } };
        window.groups = { { "Backend", QColor( Qt::red ) }, { "Frontend", QColor( "#123456" ) } };

        WHEN( "its tab names and groups are applied" )
        {
            applyTabLabels( window );
            const auto& stored = TabGroupInfo::get();

            THEN( "the tab names are stored, a decompressed Log File's by its archive" )
            {
                REQUIRE( TabNameMapping::get().tabName( first ) == "First" );
                REQUIRE( TabNameMapping::get().tabName( member.key() ) == "Old" );
            }

            AND_THEN( "the group with the same name is reused and keeps its color" )
            {
                const auto group = stored.groupForTab( first );
                REQUIRE( group.has_value() );
                REQUIRE( group->id == existing );
                REQUIRE( group->color == QColor( Qt::green ) );
            }

            AND_THEN( "any other is created with its color, and a Log File in another group is "
                      "moved to it" )
            {
                const auto group = stored.groupForTab( second );
                REQUIRE( group.has_value() );
                REQUIRE( group->name == "Frontend" );
                REQUIRE( group->color == QColor( "#123456" ) );
            }

            AND_THEN( "taking them back gives what was applied" )
            {
                auto taken = window;
                takeTabLabels( taken );
                REQUIRE( taken.tabs == window.tabs );
                REQUIRE( taken.groups.size() == 2 );
                REQUIRE( taken.groups[ 0 ] == WindowSnapshot::Group{ "Backend", Qt::green } );
                REQUIRE( taken.groups[ 1 ] == window.groups[ 1 ] );
            }
        }
    }
}
