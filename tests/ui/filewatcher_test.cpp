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

#include <catch2/catch.hpp>

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "filewatcher.h"
#include "test_utils.h"

// File watching reads no setting of its own: what it does is decided by the
// Watch Policy it was handed (#93). These tests hand it two opposite
// Policies and check that the watcher followed the one it was given.

namespace {

// The watcher is a process-wide singleton shared with every other test in
// this binary, so each test leaves it watching nothing.
class WatchedFile {
public:
    explicit WatchedFile( const QString& path )
        : path_( path )
    {
        FileWatcher::getFileWatcher().addFile( path_ );
    }

    ~WatchedFile()
    {
        FileWatcher::getFileWatcher().removeFile( path_ );
        FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{} );
    }

    WatchedFile( const WatchedFile& ) = delete;
    WatchedFile& operator=( const WatchedFile& ) = delete;

private:
    QString path_;
};

QString writeFile( const QTemporaryDir& dir, const QString& content )
{
    const auto path = dir.filePath( "watched.log" );
    QFile file{ path };
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Append ) );
    file.write( content.toLatin1() );
    file.close();
    return path;
}

} // namespace

SCENARIO( "File watching follows the Watch Policy it was handed", "[filewatch]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    GIVEN( "a Policy that polls at a short interval" )
    {
        const auto path = writeFile( tempDir, "first line\n" );

        FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{
            .nativeWatchEnabled = false, .pollingEnabled = true, .pollIntervalMs = 100 } );

        SafeQSignalSpy changedSpy( &FileWatcher::getFileWatcher(),
                                   SIGNAL( fileChanged( QString ) ) );
        WatchedFile watched{ path };

        WHEN( "the file grows" )
        {
            writeFile( tempDir, "second line\n" );

            THEN( "the change is reported" )
            {
                REQUIRE( waitUiState( [ &changedSpy ] { return changedSpy.count() >= 1; } ) );
            }
        }
    }

    GIVEN( "a Policy with neither native watching nor polling" )
    {
        const auto path = writeFile( tempDir, "first line\n" );

        FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{
            .nativeWatchEnabled = false, .pollingEnabled = false, .pollIntervalMs = 100 } );

        SafeQSignalSpy changedSpy( &FileWatcher::getFileWatcher(),
                                   SIGNAL( fileChanged( QString ) ) );
        WatchedFile watched{ path };

        WHEN( "the file grows" )
        {
            writeFile( tempDir, "second line\n" );

            THEN( "nothing is reported" )
            {
                REQUIRE_FALSE(
                    waitUiState( [ &changedSpy ] { return changedSpy.count() >= 1; }, 1000 ) );
            }
        }
    }

    GIVEN( "a watcher already polling a file" )
    {
        const auto path = writeFile( tempDir, "first line\n" );

        FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{
            .nativeWatchEnabled = false, .pollingEnabled = true, .pollIntervalMs = 100 } );

        WatchedFile watched{ path };

        WHEN( "a Policy that watches nothing arrives" )
        {
            FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{
                .nativeWatchEnabled = false, .pollingEnabled = false, .pollIntervalMs = 100 } );

            SafeQSignalSpy changedSpy( &FileWatcher::getFileWatcher(),
                                       SIGNAL( fileChanged( QString ) ) );
            writeFile( tempDir, "second line\n" );

            THEN( "the running watch stops reporting, without the file being re-added" )
            {
                REQUIRE_FALSE(
                    waitUiState( [ &changedSpy ] { return changedSpy.count() >= 1; }, 1000 ) );
            }
        }
    }
}
