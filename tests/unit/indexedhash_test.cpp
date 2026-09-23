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

#include "indexedhash.h"
#include "indexedhashfixture.h"

#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// The rule that decides whether an Index still fits its Log File, on its
// own. The Index Cache and the change detection of an Open Log File both ask
// it, so each case is checked with every kind of digest coverage.

SCENARIO( "One rule decides whether an Index still fits its Log File", "[indexedhash]" )
{
    QTemporaryDir logDir;
    REQUIRE( logDir.isValid() );

    const auto coverage = GENERATE( DigestCoverage::HeaderAndTail, DigestCoverage::Full,
                                    DigestCoverage::FullUnlessGrown );

    GIVEN( "the hash recorded for a Log File shorter than one digest block" )
    {
        const auto logFile = logDir.filePath( "short.log" );
        const QByteArray content = "first line\nsecond line\nthird line\n";
        writeFile( logFile, content );
        const auto recorded = hashOfFile( logFile );

        THEN( "the unchanged Log File is Unchanged" )
        {
            REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Unchanged );
        }

        WHEN( "the Log File grows and the recorded bytes are intact" )
        {
            writeFile( logFile, content + "fourth line\n" );

            THEN( "it has Grown" )
            {
                REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Grown );
            }
        }

        WHEN( "the Log File grows and a recorded byte changes" )
        {
            writeFile( logFile, "FIRST line\nsecond line\nthird line\nfourth line\n" );

            THEN( "it has Changed" )
            {
                REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Changed );
            }
        }

        WHEN( "the Log File is truncated" )
        {
            writeFile( logFile, "first line\n" );

            THEN( "it has Changed" )
            {
                REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Changed );
            }
        }

        WHEN( "the Log File is emptied" )
        {
            writeFile( logFile, {} );

            THEN( "it has Changed" )
            {
                REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Changed );
            }
        }

        WHEN( "the Log File is modified in place, keeping its size" )
        {
            writeFile( logFile, "first line\nSECOND line\nthird line\n" );

            THEN( "it has Changed" )
            {
                REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Changed );
            }
        }

        WHEN( "the Log File is deleted" )
        {
            REQUIRE( QFile::remove( logFile ) );

            THEN( "it has Changed" )
            {
                REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Changed );
            }
        }

#ifndef Q_OS_WIN
        WHEN( "the Log File exists but cannot be opened for now" )
        {
            REQUIRE( QFile::setPermissions( logFile, QFileDevice::Permissions{} ) );
            // A process running with root privileges opens it all the same.
            const bool lockedOut = !QFile( logFile ).open( QIODevice::ReadOnly );
            const auto fit = indexFit( recorded, logFile, coverage );
            REQUIRE( QFile::setPermissions( logFile,
                                            QFileDevice::ReadOwner | QFileDevice::WriteOwner ) );

            THEN( "it is unreadable, which says nothing about the Index" )
            {
                if ( lockedOut ) {
                    REQUIRE( fit == IndexFit::LogFileUnreadable );
                }
                else {
                    WARN( "the Log File stayed readable, so there is nothing to check" );
                }
            }
        }
#endif
    }

    GIVEN( "the hash recorded for a Log File longer than two digest blocks" )
    {
        const auto logFile = logDir.filePath( "long.log" );
        auto content = QByteArray( 2 * DigestBlockSize + DigestBlockSize / 2, 'x' );
        writeFile( logFile, content );
        const auto recorded = hashOfFile( logFile );

        THEN( "the unchanged Log File is Unchanged" )
        {
            REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Unchanged );
        }

        WHEN( "the Log File grows and the recorded bytes are intact" )
        {
            writeFile( logFile, content + QByteArray( 1000, 'z' ) );

            THEN( "it has Grown" )
            {
                REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Grown );
            }
        }

        WHEN( "the Log File grows and a byte in its recorded tail changes" )
        {
            content[ content.size() - 1 ] = 'y';
            writeFile( logFile, content + QByteArray( 1000, 'z' ) );

            THEN( "it has Changed" )
            {
                REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Changed );
            }
        }

        WHEN( "the Log File is truncated" )
        {
            writeFile( logFile, content.left( content.size() - 1 ) );

            THEN( "it has Changed" )
            {
                REQUIRE( indexFit( recorded, logFile, coverage ) == IndexFit::Changed );
            }
        }

        WHEN( "a byte between the header and the tail is modified in place" )
        {
            content[ DigestBlockSize + DigestBlockSize / 4 ] = 'y';
            writeFile( logFile, content );
            const auto fit = indexFit( recorded, logFile, coverage );

            THEN( "only the full digest notices the change" )
            {
                REQUIRE( fit
                         == ( coverage == DigestCoverage::HeaderAndTail ? IndexFit::Unchanged
                                                                        : IndexFit::Changed ) );
            }
        }

        WHEN( "a byte between the header and the tail is modified and the Log File grows" )
        {
            content[ DigestBlockSize + DigestBlockSize / 4 ] = 'y';
            writeFile( logFile, content + QByteArray( 1000, 'z' ) );
            const auto fit = indexFit( recorded, logFile, coverage );

            THEN( "only a full digest taken of a grown Log File notices the change" )
            {
                // The risk following a growing Log File accepts: it is not
                // read again end to end for every append.
                REQUIRE(
                    fit
                    == ( coverage == DigestCoverage::Full ? IndexFit::Changed : IndexFit::Grown ) );
            }
        }
    }
}
