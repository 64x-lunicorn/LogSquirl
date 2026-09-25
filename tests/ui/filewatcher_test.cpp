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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

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

QString writeFile( const QTemporaryDir& dir, const QString& content,
                   const QString& fileName = QStringLiteral( "watched.log" ) )
{
    const auto path = dir.filePath( fileName );
    QFile file{ path };
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Append ) );
    file.write( content.toLatin1() );
    file.close();
    return path;
}

// Requires a change to the file named fileName to be reported within the
// window native watching gets -- except on macOS, where an environment that
// cannot register a native watch only warns (see below). Matched by name, not
// by count: a change to some other file in the same directory may still be on
// its way.
void requireNativeChangeReported( SafeQSignalSpy& changedSpy, const QString& fileName )
{
    const auto changeReported = [ &changedSpy, &fileName ] {
        return std::any_of( changedSpy.cbegin(), changedSpy.cend(),
                            [ &fileName ]( const QList<QVariant>& arguments ) {
                                return QFileInfo( arguments.at( 0 ).toString() ).fileName()
                                       == fileName;
                            } );
    };

    // Native events arrive from the OS's own filesystem notification
    // service (efsw) rather than a Qt timer, so delivery can take longer
    // than a poll tick, especially under a container filesystem -- give it
    // a generous window rather than the short one polling gets.
    const bool reported = waitUiState( changeReported, 10000 );

#ifdef Q_OS_MAC
    // FSEvents -- efsw's native backend on this platform -- fails to
    // register a watch at all in some sandboxed CI and local dev
    // environments (observed error -111, not specific to a Log File or this
    // test), independently of this codebase: it is why the shipped default
    // already pairs native watching with polling on macOS (see
    // qtests_main.cpp / Configuration's platform defaults) rather than
    // relying on native watching alone. Assert when the platform delivers,
    // but do not fail the build over an environment that cannot register
    // the watch.
    if ( !reported ) {
        WARN( "Native watch event not observed -- FSEvents unavailable in this "
              "environment (see EfswFileWatcher::addFile's \"failed to add watch\" "
              "log); native watching also runs behind polling in the shipped "
              "defaults on this platform." );
    }
#else
    REQUIRE( reported );
#endif
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

    GIVEN( "a Policy that watches natively" )
    {
        const auto path = writeFile( tempDir, "first line\n" );

        FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{
            .nativeWatchEnabled = true, .pollingEnabled = false, .pollIntervalMs = 100 } );

        SafeQSignalSpy changedSpy( &FileWatcher::getFileWatcher(),
                                   SIGNAL( fileChanged( QString ) ) );
        WatchedFile watched{ path };

        WHEN( "the file grows" )
        {
            writeFile( tempDir, "second line\n" );

            THEN( "the change is reported" )
            {
                requireNativeChangeReported( changedSpy, QFileInfo( path ).fileName() );
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

// #322: polling stats every watched file off the UI thread and outside the
// watcher's lock, so adding or removing a watch from the UI thread must
// stay safe while a poll tick is in flight -- neither thread should be able
// to deadlock the other, and a change must still be reported once the file
// that changed settles down as a steadily watched file.
SCENARIO( "File watching survives files being added and removed while polling runs", "[filewatch]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    GIVEN( "a Policy that polls at a short interval, and many files churning in and out" )
    {
        FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{
            .nativeWatchEnabled = false, .pollingEnabled = true, .pollIntervalMs = 10 } );

        WHEN( "files are added and removed on the UI thread while polling keeps ticking" )
        {
            // Churn many short-lived watches across several poll ticks: if
            // checkWatches() held the watcher mutex across a QFileInfo stat,
            // this loop -- running entirely on the UI thread, same as the
            // watcher's other calls -- would stall for as long as the
            // watcher was busy stat-ing.
            QElapsedTimer churnTimer;
            churnTimer.start();

            int round = 0;
            while ( churnTimer.elapsed() < 500 ) {
                const auto fileName = QStringLiteral( "churn_%1.log" ).arg( round++ );
                const auto path = writeFile( tempDir, "first line\n", fileName );

                FileWatcher::getFileWatcher().addFile( path );
                FileWatcher::getFileWatcher().removeFile( path );

                QCoreApplication::processEvents();
            }

            THEN( "the UI thread was never blocked long enough to fail this churn, and a "
                  "file watched afterwards still has its change reported" )
            {
                const auto path = writeFile( tempDir, "first line\n" );

                SafeQSignalSpy changedSpy( &FileWatcher::getFileWatcher(),
                                           SIGNAL( fileChanged( QString ) ) );
                WatchedFile watched{ path };

                writeFile( tempDir, "second line\n" );

                REQUIRE( waitUiState( [ &changedSpy ] { return changedSpy.count() >= 1; } ) );
            }
        }
    }

    FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{} );
}

// #322: the polling tick itself must not run on the thread that owns the UI.
SCENARIO( "Polling does not run on the UI thread", "[filewatch]" )
{
    GIVEN( "a Policy that polls" )
    {
        FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{
            .nativeWatchEnabled = false, .pollingEnabled = true, .pollIntervalMs = 100 } );

        THEN( "the thread polling runs on is a real thread, and it is not the UI thread's" )
        {
            auto* pollThread = FileWatcher::getFileWatcher().pollThreadForTesting();

            REQUIRE( pollThread != nullptr );
            REQUIRE( pollThread != QCoreApplication::instance()->thread() );
            REQUIRE( pollThread != QThread::currentThread() );
        }

        FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{} );
    }
}

// The watcher is never destroyed, but its poll thread runs an event loop,
// and Qt's application object must not be destroyed while one does: under a
// Qt built with ThreadSanitizer, every FileWatcher itest reported the poll
// thread dispatching an event while main() tore QApplication down (#510).
SCENARIO( "The poll thread ends before the application does", "[filewatch]" )
{
    GIVEN( "a watcher polling a file" )
    {
        QTemporaryDir tempDir;
        const auto fileName = writeFile( tempDir, "one\n" );

        auto& watcher = FileWatcher::getFileWatcher();
        watcher.setWatchPolicy( WatchPolicy{
            .nativeWatchEnabled = false, .pollingEnabled = true, .pollIntervalMs = 50 } );
        watcher.addFile( fileName );

        WHEN( "polling is stopped, twice over" )
        {
            watcher.stopPolling();
            watcher.stopPolling();

            THEN( "the poll thread has finished, and the watcher still takes a Policy and files" )
            {
                REQUIRE( watcher.pollThreadForTesting()->isFinished() );

                watcher.setWatchPolicy( WatchPolicy{} );
                watcher.removeFile( fileName );
                QCoreApplication::processEvents();
            }
        }
    }
}

// #116: every Log File closed removes the native watch on its directory once
// no other watched file is left there, and the next one opened re-creates it
// -- while the directory is still changing (the closed Log File's temporary
// file being deleted, the next one being written). efsw 1.4.1 freed a Windows
// directory watch while its cancelled read could still complete into it, so
// this churn corrupted the heap and crashed the test binary somewhere
// unrelated, long after the fact.
SCENARIO( "File watching survives its directory watch being torn down and re-created",
          "[filewatch]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    // WatchedFile hands the watcher back watching nothing, so every watch
    // below starts by handing it this Policy again.
    const auto watchNatively = [] {
        FileWatcher::getFileWatcher().setWatchPolicy( WatchPolicy{
            .nativeWatchEnabled = true, .pollingEnabled = false, .pollIntervalMs = 100 } );
    };

    GIVEN( "a Policy that watches natively" )
    {
        WHEN( "files in one directory are watched, changed and unwatched many times over" )
        {
            for ( int round = 0; round < 200; ++round ) {
                const auto fileName = QStringLiteral( "churn_%1.log" ).arg( round );
                const auto path = writeFile( tempDir, "first line\n", fileName );
                {
                    watchNatively();
                    WatchedFile watched{ path };
                    writeFile( tempDir, "second line\n", fileName );
                }
                QFile::remove( path );
            }

            const auto path = writeFile( tempDir, "first line\n" );
            SafeQSignalSpy changedSpy( &FileWatcher::getFileWatcher(),
                                       SIGNAL( fileChanged( QString ) ) );
            watchNatively();
            WatchedFile watched{ path };

            writeFile( tempDir, "second line\n" );

            THEN( "a change to a file watched afterwards is still reported" )
            {
                requireNativeChangeReported( changedSpy, QFileInfo( path ).fileName() );
            }
        }
    }
}
