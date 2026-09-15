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

#include <memory>
#include <vector>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <tbb/global_control.h>

#include "test_policies.h"
#include "test_utils.h"

#include "atomicflag.h"
#include "logdata.h"
#include "logdataworker.h"
#include "progress.h"

// Indexing is built entirely from an Indexing Policy (#94): no
// Configuration, no settings bootstrap, no portable-settings constant.
// These tests build that Policy from literals and check that indexing a
// file produces the expected line count and content under each axis the
// Policy controls.

namespace {

QString writeLines( QTemporaryFile& file, int lineCount, int paddingBytes = 0 )
{
    REQUIRE( file.open() );
    const QString padding( paddingBytes, QChar( 'x' ) );
    for ( int i = 0; i < lineCount; ++i ) {
        file.write( QStringLiteral( "line %1 %2\n" ).arg( i ).arg( padding ).toUtf8() );
    }
    file.flush();
    return file.fileName();
}

void attachAndWaitForIndexing( LogData& logData, const QString& fileName )
{
    SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
    logData.attachFile( fileName );
    REQUIRE( loadEndSpy.safeWait( 20000 ) );
}

} // namespace

SCENARIO( "Indexing follows the Indexing Policy it was built with", "[indexing]" )
{
    GIVEN( "a Policy using the compressed index" )
    {
        auto policy = testSettingsPolicies();
        policy.indexing.useCompressedIndex = true;
        policy.indexing.useIndexCache = false;

        QTemporaryFile file{ "indexing_compressed_XXXXXX" };
        const auto fileName = writeLines( file, 200 );

        WHEN( "the file is indexed" )
        {
            LogData logData{ policy.indexing, policy.search, policy.fileAccess, policy.decoding };
            attachAndWaitForIndexing( logData, fileName );

            THEN( "every line is indexed and readable back" )
            {
                REQUIRE( logData.getNbLine().get() == 200 );
                REQUIRE( logData.getLineString( LineNumber( 0 ) ) == "line 0 " );
                REQUIRE( logData.getLineString( LineNumber( 199 ) ) == "line 199 " );
            }
        }
    }

    GIVEN( "a Policy using the uncompressed (fast) index" )
    {
        auto policy = testSettingsPolicies();
        policy.indexing.useCompressedIndex = false;
        policy.indexing.useIndexCache = false;

        QTemporaryFile file{ "indexing_uncompressed_XXXXXX" };
        const auto fileName = writeLines( file, 200 );

        WHEN( "the file is indexed" )
        {
            LogData logData{ policy.indexing, policy.search, policy.fileAccess, policy.decoding };
            attachAndWaitForIndexing( logData, fileName );

            THEN( "every line is indexed and readable back" )
            {
                REQUIRE( logData.getNbLine().get() == 200 );
                REQUIRE( logData.getLineString( LineNumber( 0 ) ) == "line 0 " );
                REQUIRE( logData.getLineString( LineNumber( 199 ) ) == "line 199 " );
            }
        }
    }

    GIVEN( "a file spanning several indexing blocks and a Policy with a "
           "one-block read buffer" )
    {
        auto policy = testSettingsPolicies();
        policy.indexing.readBufferSizeMb = 1;
        policy.indexing.useIndexCache = false;

        // Each line is padded well past 1 KiB so a modest line count
        // still spans several 5 MiB indexing blocks, exercising the
        // prefetch limiter built from readBufferSizeMb with more than one
        // block in flight over the run.
        QTemporaryFile file{ "indexing_buffer_XXXXXX" };
        const auto fileName = writeLines( file, 20000, 1024 );

        WHEN( "the file is indexed with a single-block prefetch buffer" )
        {
            LogData logData{ policy.indexing, policy.search, policy.fileAccess, policy.decoding };
            attachAndWaitForIndexing( logData, fileName );

            THEN( "the whole file is still indexed correctly" )
            {
                REQUIRE( logData.getNbLine().get() == 20000 );
                REQUIRE( logData.getLineString( LineNumber( 0 ) ).startsWith( "line 0 " ) );
                REQUIRE( logData.getLineString( LineNumber( 19999 ) ).startsWith( "line 19999 " ) );
            }
        }

        WHEN( "the same file is indexed with a larger prefetch buffer" )
        {
            policy.indexing.readBufferSizeMb = 16;
            LogData logData{ policy.indexing, policy.search, policy.fileAccess, policy.decoding };
            attachAndWaitForIndexing( logData, fileName );

            THEN( "the result matches the single-block run" )
            {
                REQUIRE( logData.getNbLine().get() == 20000 );
                REQUIRE( logData.getLineString( LineNumber( 0 ) ).startsWith( "line 0 " ) );
                REQUIRE( logData.getLineString( LineNumber( 19999 ) ).startsWith( "line 19999 " ) );
            }
        }
    }
}

namespace {

// What one indexing run left behind, and what it cost.
struct IndexingRun {
    LinesCount lines;
    LineLength maxLength;
    QByteArray encoding;
    IndexedHash hash;
    std::vector<OffsetInFile> endOfLines;
    qint64 bytesIndexed = 0;
    std::vector<int> progress;
};

IndexingRun runFullIndex( const QString& fileName, const IndexingPolicy& policy )
{
    auto data = std::make_shared<IndexingData>();
    AtomicFlag interruptRequest;
    FullIndexOperation operation{ fileName, data, interruptRequest, policy };

    IndexingRun run;
    // Emitted from the indexing graph's serial parser or from the running
    // thread, never from two at once.
    QObject::connect( &operation, &IndexOperation::indexingProgressed,
                      [ &run ]( int progress ) { run.progress.push_back( progress ); } );
    REQUIRE( std::get<bool>( operation.run() ) );

    IndexingData::ConstAccessor accessor{ data.get() };
    run.lines = accessor.getNbLines();
    run.maxLength = accessor.getMaxLength();
    run.encoding = accessor.getEncodingGuess()->name();
    run.hash = accessor.getHash();
    for ( auto line = 0u; line < run.lines.get(); ++line ) {
        run.endOfLines.push_back( accessor.getEndOfLineOffset( LineNumber( line ) ) );
    }
    run.bytesIndexed = operation.bytesIndexed();
    return run;
}

void writeContent( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( file.write( content ) == content.size() );
}

void appendContent( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Append ) );
    REQUIRE( file.write( content ) == content.size() );
}

void overwriteByte( const QString& path, qint64 offset, char byte )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadWrite ) );
    REQUIRE( file.seek( offset ) );
    REQUIRE( file.write( &byte, 1 ) == 1 );
}

QByteArray linesOf( int first, int count, int paddingBytes = 0 )
{
    QByteArray content;
    const QByteArray padding( paddingBytes, 'x' );
    for ( int i = first; i < first + count; ++i ) {
        content += "line " + QByteArray::number( i ) + " " + padding + "\n";
    }
    return content;
}

// Where the last Log Line of an Index starts: the end of the line before it.
qint64 startOfLastLine( const IndexingRun& run )
{
    return run.endOfLines.size() > 1 ? run.endOfLines[ run.endOfLines.size() - 2 ].get() : 0;
}

void requireSameIndex( const IndexingRun& resumed, const IndexingRun& full )
{
    REQUIRE( resumed.lines == full.lines );
    REQUIRE( resumed.maxLength == full.maxLength );
    REQUIRE( resumed.encoding == full.encoding );
    REQUIRE( resumed.endOfLines == full.endOfLines );
    REQUIRE( resumed.hash.size == full.hash.size );
    REQUIRE( resumed.hash.fullDigest == full.hash.fullDigest );
    REQUIRE( resumed.hash.headerSize == full.hash.headerSize );
    REQUIRE( resumed.hash.headerDigest == full.hash.headerDigest );
    REQUIRE( resumed.hash.tailOffset == full.hash.tailOffset );
    REQUIRE( resumed.hash.tailSize == full.hash.tailSize );
    REQUIRE( resumed.hash.tailDigest == full.hash.tailDigest );
}

void requireMonotonicProgress( const IndexingRun& run )
{
    REQUIRE_FALSE( run.progress.empty() );
    for ( auto i = 1u; i < run.progress.size(); ++i ) {
        REQUIRE( run.progress[ i - 1 ] <= run.progress[ i ] );
    }
}

} // namespace

SCENARIO( "Reopening a grown Log File indexes only what was added to it", "[indexing][resume]" )
{
    QTemporaryDir cacheDir;
    QTemporaryDir logDir;
    REQUIRE( cacheDir.isValid() );
    REQUIRE( logDir.isValid() );

    auto policy = testSettingsPolicies().indexing;
    policy.useIndexCache = true;
    policy.indexCacheDirectory = cacheDir.path();
    policy.fastModificationDetection = GENERATE( false, true );

    auto noCache = policy;
    noCache.useIndexCache = false;

    const auto logFile = logDir.filePath( "grown.log" );

    GIVEN( "a cached Index of a Log File spanning several indexing blocks" )
    {
        // Over two 5 MiB blocks, so the header and tail digests cover
        // different ranges.
        writeContent( logFile, linesOf( 0, 11000, 1024 ) );
        const auto cached = runFullIndex( logFile, policy );
        REQUIRE( cached.lines.get() == 11000 );
        const auto cachedSize = cached.hash.size;

        WHEN( "it is reopened unchanged" )
        {
            const auto reopened = runFullIndex( logFile, policy );

            THEN( "nothing is indexed again" )
            {
                REQUIRE( reopened.bytesIndexed == 0 );
                requireSameIndex( reopened, cached );
            }
        }

        WHEN( "lines are appended and it is reopened" )
        {
            // Longer than any line before, so the longest line changes.
            appendContent( logFile, linesOf( 11000, 50, 3000 ) );
            const auto resumed = runFullIndex( logFile, policy );
            const auto full = runFullIndex( logFile, noCache );

            THEN( "only the last cached Log Line and the appended bytes are indexed" )
            {
                REQUIRE( resumed.bytesIndexed == full.hash.size - startOfLastLine( cached ) );
                REQUIRE( full.bytesIndexed == full.hash.size );
            }

            THEN( "the Index is the one a full re-index builds" )
            {
                REQUIRE( resumed.lines.get() == 11050 );
                requireSameIndex( resumed, full );
            }

            THEN( "progress starts at the cached fraction and never goes backwards" )
            {
                REQUIRE( resumed.progress.front()
                         == calculateProgress( startOfLastLine( cached ), full.hash.size ) );
                REQUIRE( resumed.progress.front() > 0 );
                requireMonotonicProgress( resumed );
            }

            AND_WHEN( "it is reopened again" )
            {
                const auto again = runFullIndex( logFile, policy );

                THEN( "the resumed Index was cached in turn" )
                {
                    REQUIRE( again.bytesIndexed == 0 );
                    requireSameIndex( again, full );
                }
            }
        }

        WHEN( "a byte in the stored tail range changes, lines are appended and it is reopened" )
        {
            REQUIRE( cached.hash.tailOffset > cached.hash.headerSize );
            overwriteByte( logFile, cachedSize - 10, 'y' );
            appendContent( logFile, linesOf( 11000, 50 ) );
            const auto reopened = runFullIndex( logFile, policy );
            const auto full = runFullIndex( logFile, noCache );

            THEN( "the whole Log File is indexed again" )
            {
                REQUIRE( reopened.bytesIndexed == full.hash.size );
                requireSameIndex( reopened, full );
                requireMonotonicProgress( reopened );
            }
        }

        WHEN( "a byte in the header changes, lines are appended and it is reopened" )
        {
            overwriteByte( logFile, 100, 'y' );
            appendContent( logFile, linesOf( 11000, 50 ) );
            const auto reopened = runFullIndex( logFile, policy );

            THEN( "the whole Log File is indexed again" )
            {
                REQUIRE( reopened.bytesIndexed == reopened.hash.size );
            }
        }
    }

    GIVEN( "a cached Index of a Log File whose last line has no newline" )
    {
        writeContent( logFile, "first\nsecond\npartial" );
        const auto cached = runFullIndex( logFile, policy );
        REQUIRE( cached.lines.get() == 3 );

        WHEN( "that line continues, more lines follow, and it is reopened" )
        {
            appendContent( logFile, " and a much longer continuation of it\nlast\n" );
            const auto resumed = runFullIndex( logFile, policy );
            const auto full = runFullIndex( logFile, noCache );

            THEN( "the continued line is one Log Line" )
            {
                REQUIRE( resumed.lines.get() == 4 );
                REQUIRE( resumed.bytesIndexed == full.hash.size - startOfLastLine( cached ) );
                requireSameIndex( resumed, full );
                requireMonotonicProgress( resumed );
            }

            THEN( "it reads back as one Log Line" )
            {
                LogData logData{ policy, testSettingsPolicies().search,
                                 testSettingsPolicies().fileAccess,
                                 testSettingsPolicies().decoding };
                attachAndWaitForIndexing( logData, logFile );
                REQUIRE( logData.getNbLine().get() == 4 );
                REQUIRE( logData.getLineString( LineNumber( 2 ) )
                         == "partial and a much longer continuation of it" );
            }
        }
    }

    GIVEN( "a cached Index of a short Log File in plain ASCII" )
    {
        writeContent( logFile, linesOf( 0, 20 ) );
        const auto cached = runFullIndex( logFile, policy );

        WHEN( "text only a different encoding covers is appended and it is reopened" )
        {
            QByteArray cyrillic;
            for ( int i = 0; i < 200; ++i ) {
                cyrillic += "Привет, это строка журнала номер " + QByteArray::number( i ) + "\n";
            }
            appendContent( logFile, cyrillic );
            const auto reopened = runFullIndex( logFile, policy );
            const auto full = runFullIndex( logFile, noCache );

            THEN( "the encoding is the one a full re-index detects" )
            {
                INFO( "cached " << cached.encoding.toStdString() << ", full "
                                << full.encoding.toStdString() );
                requireSameIndex( reopened, full );
            }
        }
    }

    GIVEN( "a cached Index of a Log File that then shrinks" )
    {
        writeContent( logFile, linesOf( 0, 100 ) );
        runFullIndex( logFile, policy );
        writeContent( logFile, linesOf( 0, 50 ) );

        WHEN( "it is reopened" )
        {
            const auto reopened = runFullIndex( logFile, policy );

            THEN( "the whole Log File is indexed again" )
            {
                REQUIRE( reopened.lines.get() == 50 );
                REQUIRE( reopened.bytesIndexed == reopened.hash.size );
            }
        }
    }
}

SCENARIO( "An indexing pass completes even when TBB has no worker thread to spare", "[indexing]" )
{
    GIVEN( "a file spanning several indexing blocks and a Policy with a one-block read buffer" )
    {
        auto policy = testSettingsPolicies();
        policy.indexing.readBufferSizeMb = 1;
        policy.indexing.useIndexCache = false;

        QTemporaryFile file{ "indexing_no_worker_XXXXXX" };
        const auto fileName = writeLines( file, 20000, 1024 );

        WHEN( "the file is indexed while no TBB worker thread is available to its graph" )
        {
            // TBB shares its workers between every graph in the process, so a
            // graph can find none free, as the Search in #142 did for 120 s. A
            // parallelism of 1 makes that certain: only the thread running the
            // indexing pass is left to process its blocks (#146).
            tbb::global_control noWorkers( tbb::global_control::max_allowed_parallelism, 1 );

            LogData logData{ policy.indexing, policy.search, policy.fileAccess, policy.decoding };
            attachAndWaitForIndexing( logData, fileName );

            THEN( "every line is indexed and readable back" )
            {
                REQUIRE( logData.getNbLine().get() == 20000 );
                REQUIRE( logData.getLineString( LineNumber( 19999 ) ).startsWith( "line 19999 " ) );
            }
        }
    }
}
