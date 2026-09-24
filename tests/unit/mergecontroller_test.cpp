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

#include "mergecontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimer>

namespace {

// Writes a temporary file with the given lines and returns the file path.
QString writeTestFile( const QTemporaryDir& dir, const QString& name, const QStringList& lines )
{
    const auto path = QDir( dir.path() ).filePath( name );
    QFile f( path );
    (void)f.open( QIODevice::WriteOnly | QIODevice::Text );
    QTextStream out( &f );
    for ( const auto& line : lines ) {
        out << line << '\n';
    }
    f.close();
    return path;
}

// Reads all lines from a file (strips trailing empty line from the trailing newline).
QStringList readAllLines( const QString& path )
{
    QFile f( path );
    (void)f.open( QIODevice::ReadOnly | QIODevice::Text );
    QTextStream in( &f );
    QStringList result;
    while ( !in.atEnd() ) {
        result.append( in.readLine() );
    }
    return result;
}

// Runs the event loop until the controller reports an update or `timeoutMs` passes.
bool waitForUpdate( MergeController& controller, int timeoutMs = 5000 )
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot( true );
    bool updated = false;
    QObject::connect( &controller, &MergeController::mergedFileUpdated, &loop, [ & ] {
        updated = true;
        loop.quit();
    } );
    QObject::connect( &timeout, &QTimer::timeout, &loop, &QEventLoop::quit );
    timeout.start( timeoutMs );
    loop.exec();
    return updated;
}

void appendToFile( const QString& path, const QString& text )
{
    QFile f( path );
    (void)f.open( QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text );
    QTextStream out( &f );
    out << text << '\n';
}

} // namespace

SCENARIO( "MergeController concatenates files", "[mergecontroller]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );

    GIVEN( "Two source files with different content" )
    {
        const auto pathA = writeTestFile( tmpDir, "a.log", { "alpha", "beta" } );
        const auto pathB = writeTestFile( tmpDir, "b.log", { "gamma", "delta" } );

        WHEN( "Merging without dedup" )
        {
            MergeController controller;
            const auto merged = controller.merge( { pathA, pathB }, false );

            THEN( "The merged file contains all lines in order" )
            {
                const auto lines = readAllLines( merged );
                REQUIRE( lines.size() == 4 );
                REQUIRE( lines[ 0 ] == "alpha" );
                REQUIRE( lines[ 1 ] == "beta" );
                REQUIRE( lines[ 2 ] == "gamma" );
                REQUIRE( lines[ 3 ] == "delta" );
            }
        }
    }
}

SCENARIO( "MergeController deduplicates lines", "[mergecontroller]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );

    GIVEN( "Two files with overlapping lines" )
    {
        const auto pathA = writeTestFile( tmpDir, "a.log", { "alpha", "beta", "gamma" } );
        const auto pathB = writeTestFile( tmpDir, "b.log", { "beta", "delta", "alpha" } );

        WHEN( "Merging with dedup enabled" )
        {
            MergeController controller;
            const auto merged = controller.merge( { pathA, pathB }, true );

            THEN( "Duplicate lines are removed, first occurrence kept" )
            {
                const auto lines = readAllLines( merged );
                REQUIRE( lines.size() == 4 );
                REQUIRE( lines[ 0 ] == "alpha" );
                REQUIRE( lines[ 1 ] == "beta" );
                REQUIRE( lines[ 2 ] == "gamma" );
                REQUIRE( lines[ 3 ] == "delta" );
            }
        }
    }
}

SCENARIO( "MergeController handles empty file list", "[mergecontroller]" )
{
    GIVEN( "An empty source list" )
    {
        WHEN( "Merging" )
        {
            MergeController controller;
            const auto merged = controller.merge( {}, false );

            THEN( "The merged file is empty" )
            {
                const auto lines = readAllLines( merged );
                REQUIRE( lines.isEmpty() );
            }
        }
    }
}

SCENARIO( "MergeController scheduleRebuild re-merges", "[mergecontroller]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );

    GIVEN( "A merge with an initial file" )
    {
        const auto pathA = writeTestFile( tmpDir, "a.log", { "line1" } );

        MergeController controller;
        controller.merge( { pathA }, false );

        WHEN( "The source file is modified and rebuild is triggered" )
        {
            // Rewrite the source file
            writeTestFile( tmpDir, "a.log", { "line1", "line2" } );

            // Ask explicitly; the watcher may already have asked, the
            // debounce folds both into one rebuild.
            bool signalReceived = false;
            QObject::connect( &controller, &MergeController::mergedFileUpdated,
                              [ &signalReceived ] { signalReceived = true; } );
            controller.scheduleRebuild();
            waitForUpdate( controller );

            THEN( "The merged file reflects the updated content" )
            {
                const auto lines = readAllLines( controller.mergedFilePath() );
                REQUIRE( lines.size() == 2 );
                REQUIRE( lines[ 0 ] == "line1" );
                REQUIRE( lines[ 1 ] == "line2" );
            }

            THEN( "The update signal was emitted" )
            {
                REQUIRE( signalReceived );
            }
        }
    }
}

SCENARIO( "MergeController follows its sources", "[mergecontroller]" )
{
    QTemporaryDir tmpDir;
    REQUIRE( tmpDir.isValid() );

    GIVEN( "A merge of two sources" )
    {
        const auto pathA = writeTestFile( tmpDir, "a.log", { "a1" } );
        const auto pathB = writeTestFile( tmpDir, "b.log", { "b1" } );

        MergeController controller;
        controller.merge( { pathA, pathB }, false );

        WHEN( "A line is appended to the second source" )
        {
            appendToFile( pathB, "b2" );

            THEN( "The merged file is rebuilt without anyone asking" )
            {
                REQUIRE( waitForUpdate( controller ) );
                REQUIRE( readAllLines( controller.mergedFilePath() )
                         == QStringList{ "a1", "b1", "b2" } );
            }
        }

        WHEN( "A source is truncated" )
        {
            writeTestFile( tmpDir, "a.log", {} );

            THEN( "Its lines leave the merged file, the other source stays" )
            {
                REQUIRE( waitForUpdate( controller ) );
                REQUIRE( readAllLines( controller.mergedFilePath() ) == QStringList{ "b1" } );

                AND_WHEN( "The truncated source grows again" )
                {
                    appendToFile( pathA, "a2" );

                    THEN( "The new lines are picked up" )
                    {
                        REQUIRE( waitForUpdate( controller ) );
                        REQUIRE( readAllLines( controller.mergedFilePath() )
                                 == QStringList{ "a2", "b1" } );
                    }
                }
            }
        }

        WHEN( "A source is replaced by a new file" )
        {
            const auto replacement = writeTestFile( tmpDir, "a.tmp", { "new" } );
            REQUIRE( QFile::remove( pathA ) );
            REQUIRE( QFile::rename( replacement, pathA ) );

            THEN( "The new content is merged and the new file is watched" )
            {
                REQUIRE( waitForUpdate( controller ) );
                REQUIRE( readAllLines( controller.mergedFilePath() )
                         == QStringList{ "new", "b1" } );

                appendToFile( pathA, "more" );
                REQUIRE( waitForUpdate( controller ) );
                REQUIRE( readAllLines( controller.mergedFilePath() )
                         == QStringList{ "new", "more", "b1" } );
            }
        }
    }
}
