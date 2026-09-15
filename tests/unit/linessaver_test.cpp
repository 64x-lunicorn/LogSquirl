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

// Saving lines to a file runs off the UI thread, reports its progress and its
// end on the UI thread, and stops when interrupted (#157).

#include <catch2/catch.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include <QBuffer>
#include <QSignalSpy>
#include <QTextCodec>
#include <QThread>

#include "fake_log_data.h"
#include "linessaver.h"

namespace {

#if defined( Q_OS_WIN )
const QByteArray LineEnding = "\n";
#else
const QByteArray LineEnding = "\r\n";
#endif

// Log Lines with some text beyond ASCII.
QStringList logLines( int count )
{
    QStringList lines;
    lines.reserve( count );
    for ( int line = 0; line < count; ++line ) {
        lines.append( QStringLiteral( "line %1 éè 中" ).arg( line ) );
    }
    return lines;
}

DisplayedLinesReader readerOf( const FakeLogData& logFile )
{
    return [ &logFile ]( LineNumber first, LinesCount count ) {
        return logFile.getLines( first, count );
    };
}

QByteArray utf8File( const QStringList& lines, int begin, int end )
{
    QByteArray bytes = "\xEF\xBB\xBF";
    for ( int line = begin; line < end; ++line ) {
        bytes += lines[ line ].toUtf8() + LineEnding;
    }
    return bytes;
}

QByteArray save( const DisplayedLinesReader& readLines, LineNumber begin, LineNumber end,
                 const QTextCodec* codec = nullptr )
{
    QBuffer output;
    output.open( QIODevice::WriteOnly );
    AtomicFlag interrupt;
    REQUIRE( saveDisplayedLines( readLines, begin, end, codec, output, interrupt, []( int ) {} ) );
    return output.data();
}

} // namespace

SCENARIO( "saving lines writes each line encoded, with its line ending, after the Byte Order "
          "Mark",
          "[linessaver]" )
{
    GIVEN( "a Log File of several chunks of lines" )
    {
        const auto lines = logLines( 12345 );
        FakeLogData logFile{ lines };

        THEN( "saving every line with no Encoding writes them as UTF-8" )
        {
            REQUIRE( save( readerOf( logFile ), 0_lnum, 12345_lnum )
                     == utf8File( lines, 0, 12345 ) );
        }

        THEN( "saving a range across chunks writes only the lines in the range" )
        {
            REQUIRE( save( readerOf( logFile ), 4990_lnum, 11000_lnum )
                     == utf8File( lines, 4990, 11000 ) );
        }

        THEN( "saving a few lines writes them" )
        {
            REQUIRE( save( readerOf( logFile ), 7_lnum, 10_lnum ) == utf8File( lines, 7, 10 ) );
        }

        THEN( "saving as UTF-16LE writes its Byte Order Mark once" )
        {
            QByteArray expected = "\xFF\xFE";
            for ( int line = 0; line < 6000; ++line ) {
                const auto text = lines[ line ] + QString::fromLatin1( LineEnding );
                for ( const auto unit : text ) {
                    expected += static_cast<char>( unit.unicode() & 0xFF );
                    expected += static_cast<char>( unit.unicode() >> 8 );
                }
            }
            REQUIRE( save( readerOf( logFile ), 0_lnum, 6000_lnum,
                           QTextCodec::codecForName( "UTF-16LE" ) )
                     == expected );
        }

        THEN( "saving as ISO-8859-1 writes no Byte Order Mark" )
        {
            const FakeLogData latinFile{ { QStringLiteral( "café" ), QStringLiteral( "b" ) } };
            REQUIRE( save( readerOf( latinFile ), 0_lnum, 2_lnum,
                           QTextCodec::codecForName( "ISO-8859-1" ) )
                     == QByteArray( "caf\xE9" ) + LineEnding + "b" + LineEnding );
        }
    }
}

SCENARIO( "saving lines writes every line of the range, whatever its size", "[linessaver]" )
{
    GIVEN( "a Log File of two chunks of 5,000 lines and some more" )
    {
        const auto lines = logLines( 12345 );
        FakeLogData logFile{ lines };

        const auto [ begin, end ]
            = GENERATE( std::pair{ 0, 0 }, std::pair{ 0, 4999 }, std::pair{ 0, 5000 },
                        std::pair{ 0, 5001 }, std::pair{ 0, 10000 }, std::pair{ 2000, 12000 } );

        THEN( "saving lines " << begin << " to " << end << " writes exactly those lines" )
        {
            REQUIRE( save( readerOf( logFile ),
                           LineNumber( static_cast<LineNumber::UnderlyingType>( begin ) ),
                           LineNumber( static_cast<LineNumber::UnderlyingType>( end ) ) )
                     == utf8File( lines, begin, end ) );
        }
    }
}

SCENARIO( "an interrupted save stops reading and writing", "[linessaver]" )
{
    const auto lines = logLines( 50001 );
    FakeLogData logFile{ lines };

    GIVEN( "a save interrupted while it reads its second chunk" )
    {
        AtomicFlag interrupt;
        std::atomic<int> chunksRead = 0;
        const DisplayedLinesReader readLines = [ & ]( LineNumber first, LinesCount count ) {
            if ( ++chunksRead == 2 ) {
                interrupt.set();
            }
            return logFile.getLines( first, count );
        };

        QBuffer output;
        output.open( QIODevice::WriteOnly );
        const auto isOk = saveDisplayedLines( readLines, 0_lnum, 50001_lnum, nullptr, output,
                                              interrupt, []( int ) {} );

        THEN( "it reports that it did not save everything" )
        {
            REQUIRE_FALSE( isOk );
        }
        THEN( "it reads no chunk after the one it was interrupted in" )
        {
            REQUIRE( chunksRead == 2 );
        }
        THEN( "it writes at most the chunks it read before" )
        {
            REQUIRE( output.data().size() <= utf8File( lines, 0, 5000 ).size() );
        }
    }

    GIVEN( "a save whose writes fail" )
    {
        QBuffer output;
        output.open( QIODevice::ReadOnly );
        AtomicFlag interrupt;

        THEN( "it reports that it did not save everything" )
        {
            REQUIRE_FALSE( saveDisplayedLines( readerOf( logFile ), 0_lnum, 50001_lnum, nullptr,
                                               output, interrupt, []( int ) {} ) );
        }
    }
}

SCENARIO( "a LinesSaver saves off the UI thread and reports on the UI thread", "[linessaver]" )
{
    const auto lines = logLines( 32001 );
    FakeLogData logFile{ lines };
    const auto* uiThread = QThread::currentThread();

    QBuffer output;
    output.open( QIODevice::WriteOnly );
    AtomicFlag interrupt;
    LinesSaver saver;

    GIVEN( "a save of several chunks" )
    {
        std::atomic<bool> readOnUiThread = false;
        const DisplayedLinesReader readLines = [ & ]( LineNumber first, LinesCount count ) {
            if ( QThread::currentThread() == uiThread ) {
                readOnUiThread = true;
            }
            return logFile.getLines( first, count );
        };

        std::vector<int> progress;
        bool progressOffUiThread = false;
        QObject::connect(
            &saver, &LinesSaver::progressed, &saver,
            [ & ]( int value ) {
                progressOffUiThread |= QThread::currentThread() != uiThread;
                progress.push_back( value );
            },
            Qt::DirectConnection );
        QSignalSpy finished( &saver, &LinesSaver::finished );

        saver.save( readLines, 0_lnum, 32001_lnum, nullptr, &output, interrupt );
        REQUIRE( finished.wait( 10000 ) );

        THEN( "it reads no line on the UI thread" )
        {
            REQUIRE_FALSE( readOnUiThread );
        }
        THEN( "its progress rises, and reaches the UI thread only" )
        {
            REQUIRE( progress.size() > 1 );
            REQUIRE( std::is_sorted( progress.begin(), progress.end() ) );
            REQUIRE( progress.front() > 0 );
            REQUIRE( progress.back() <= 1000 );
            REQUIRE_FALSE( progressOffUiThread );
        }
        THEN( "it reports success and writes every line" )
        {
            REQUIRE( finished.at( 0 ).at( 0 ).toBool() );
            REQUIRE( saver.waitForResult() );
            REQUIRE( output.data() == utf8File( lines, 0, 32001 ) );
        }
    }

    GIVEN( "a save cancelled when its first progress reaches the UI thread" )
    {
        std::atomic<int> chunksRead = 0;
        const DisplayedLinesReader readLines = [ & ]( LineNumber first, LinesCount count ) {
            // Every chunk after the first waits for the cancel, so the save
            // cannot finish before it.
            if ( ++chunksRead > 1 ) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
                while ( !interrupt && std::chrono::steady_clock::now() < deadline ) {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
                }
            }
            return logFile.getLines( first, count );
        };

        QObject::connect( &saver, &LinesSaver::progressed, [ & ]( int ) { interrupt.set(); } );
        QSignalSpy finished( &saver, &LinesSaver::finished );

        saver.save( readLines, 0_lnum, 32001_lnum, nullptr, &output, interrupt );
        REQUIRE( finished.wait( 10000 ) );

        THEN( "it stops, reports that it did not save everything, and writes less" )
        {
            REQUIRE( interrupt );
            REQUIRE_FALSE( finished.at( 0 ).at( 0 ).toBool() );
            REQUIRE( chunksRead < 7 );
            REQUIRE( output.data().size() < utf8File( lines, 0, 32001 ).size() );
        }
    }
}
