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
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <memory>
#include <vector>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <tbb/global_control.h>

#include "test_policies.h"
#include "test_utils.h"

#include "atomicflag.h"
#include "filedigest.h"
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

IndexingRun runFullIndex( const QString& fileName, const IndexingPolicy& policy,
                          FullIndexRequest request = FullIndexRequest::Automatic )
{
    auto data = std::make_shared<IndexingData>();
    AtomicFlag interruptRequest;
    FullIndexOperation operation{ fileName, data, interruptRequest, policy, request };

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

// Writes the bytes over those at offset, leaving the rest of the Log File --
// its size included -- as it was.
void overwriteBytes( const QString& path, qint64 offset, const QByteArray& bytes )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadWrite ) );
    REQUIRE( file.seek( offset ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
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

SCENARIO( "An interrupt while a cached Index is checked stops indexing", "[indexing][resume]" )
{
    QTemporaryDir cacheDir;
    QTemporaryDir logDir;
    REQUIRE( cacheDir.isValid() );
    REQUIRE( logDir.isValid() );

    auto policy = testSettingsPolicies().indexing;
    policy.useIndexCache = true;
    policy.indexCacheDirectory = cacheDir.path();
    // Going on from the cached Index first reads the bytes before the last
    // cached Log Line again, to build their digest.
    policy.fastModificationDetection = false;

    const auto logFile = logDir.filePath( "grown.log" );

    GIVEN( "a cached Index of a Log File that has grown since" )
    {
        writeContent( logFile, linesOf( 0, 20 ) );
        runFullIndex( logFile, policy );
        appendContent( logFile, linesOf( 20, 20 ) );

        WHEN( "it is reopened by a run interrupted before it starts" )
        {
            auto data = std::make_shared<IndexingData>();
            AtomicFlag interruptRequest{ true };
            FullIndexOperation operation{ logFile, data, interruptRequest, policy };

            std::vector<int> progress;
            QObject::connect( &operation, &IndexOperation::indexingProgressed,
                              [ &progress ]( int percent ) { progress.push_back( percent ); } );
            const auto result = operation.run();

            THEN( "it reports not having finished" )
            {
                REQUIRE_FALSE( std::get<bool>( result ) );
            }

            THEN( "it stops there: no indexing of the whole Log File is started" )
            {
                REQUIRE( progress.empty() );
                REQUIRE( operation.bytesIndexed() == 0 );
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

namespace {

// An Index followed as its Log File changes, the way an Open Log File
// follows it: a full index first, then a check on every change and a
// partial index for what was appended.
class FollowedIndex {
public:
    FollowedIndex( const QString& fileName, const IndexingPolicy& policy )
        : fileName_( fileName )
        , policy_( policy )
    {
        FullIndexOperation operation{ fileName_, data_, interruptRequest_, policy_ };
        REQUIRE( std::get<bool>( operation.run() ) );
    }

    MonitoredFileStatus checkForChanges()
    {
        CheckFileChangesOperation operation{ fileName_, data_, interruptRequest_, policy_ };
        return std::get<MonitoredFileStatus>( operation.run() );
    }

    // Returns how many bytes the partial index read.
    qint64 indexAppendedLines()
    {
        PartialIndexOperation operation{ fileName_, data_, interruptRequest_, policy_ };
        REQUIRE( std::get<bool>( operation.run() ) );
        return operation.bytesIndexed();
    }

    IndexedHash hash() const
    {
        return IndexingData::ConstAccessor{ data_.get() }.getHash();
    }

    LinesCount lines() const
    {
        return IndexingData::ConstAccessor{ data_.get() }.getNbLines();
    }

private:
    QString fileName_;
    IndexingPolicy policy_;
    std::shared_ptr<IndexingData> data_ = std::make_shared<IndexingData>();
    AtomicFlag interruptRequest_;
};

constexpr qint64 IndexingBlock = 5 * 1024 * 1024;

quint64 digestOfFileRange( const QString& path, qint64 offset, qint64 size )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    REQUIRE( file.seek( offset ) );
    const auto bytes = file.read( size );
    REQUIRE( bytes.size() == size );
    FileDigest digest;
    digest.addData( bytes );
    return digest.digest();
}

// The header and tail digests recorded are those of the Log File as it is,
// and the tail is at least half an indexing block long.
void requireHeaderAndTailOf( const IndexedHash& hash, const QString& path )
{
    const auto size = QFileInfo( path ).size();
    REQUIRE( hash.size == size );
    REQUIRE( hash.headerSize == std::min( size, IndexingBlock ) );
    REQUIRE( hash.headerDigest == digestOfFileRange( path, 0, hash.headerSize ) );
    REQUIRE( hash.tailOffset + hash.tailSize == size );
    REQUIRE( hash.tailSize >= std::min( size, IndexingBlock / 2 ) );
    REQUIRE( hash.tailSize <= IndexingBlock );
    REQUIRE( hash.tailDigest == digestOfFileRange( path, hash.tailOffset, hash.tailSize ) );
}

void setModificationTime( const QString& path, const QDateTime& time )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadWrite ) );
    REQUIRE( file.setFileTime( time, QFileDevice::FileModificationTime ) );
}

} // namespace

SCENARIO( "Following a growing Log File reads only what was appended", "[indexing][follow]" )
{
    QTemporaryDir logDir;
    REQUIRE( logDir.isValid() );

    auto policy = testSettingsPolicies().indexing;
    policy.useIndexCache = false;
    policy.fastModificationDetection = GENERATE( false, true );
    INFO( "fast modification detection " << policy.fastModificationDetection );

    const auto logFile = logDir.filePath( "followed.log" );

    GIVEN( "an Index of a Log File spanning several indexing blocks" )
    {
        // About 11 MiB: the header and tail digests cover different ranges,
        // with bytes between them.
        const auto content = linesOf( 0, 11000, 1024 );
        writeContent( logFile, content );
        FollowedIndex index( logFile, policy );
        requireHeaderAndTailOf( index.hash(), logFile );

        WHEN( "small appends follow one another, each checked and indexed" )
        {
            // 300 KiB each, together crossing a multiple of half a block.
            for ( int append = 0; append < 12; ++append ) {
                const auto appended = linesOf( 11000 + append * 300, 300, 1000 );
                appendContent( logFile, appended );
                INFO( "append " << append );

                REQUIRE( index.checkForChanges() == MonitoredFileStatus::DataAdded );
                REQUIRE( index.indexAppendedLines() == appended.size() );
                requireHeaderAndTailOf( index.hash(), logFile );
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::Unchanged );
            }

            THEN( "the Index is the one a full index builds" )
            {
                const auto full = runFullIndex( logFile, policy );
                const auto followed = index.hash();
                REQUIRE( index.lines() == full.lines );
                REQUIRE( followed.size == full.hash.size );
                REQUIRE( followed.fullDigest == full.hash.fullDigest );
                REQUIRE( followed.headerDigest == full.hash.headerDigest );
                REQUIRE( followed.tailOffset == full.hash.tailOffset );
                REQUIRE( followed.tailSize == full.hash.tailSize );
                REQUIRE( followed.tailDigest == full.hash.tailDigest );
            }
        }

        WHEN( "a byte of its header changes before appended lines are indexed" )
        {
            overwriteByte( logFile, 100, 'y' );
            appendContent( logFile, linesOf( 11000, 10 ) );
            index.indexAppendedLines();

            THEN( "indexing the appended lines does not take the changed header over" )
            {
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::Truncated );
            }
        }

        WHEN( "a byte of its tail changes before appended lines are indexed" )
        {
            overwriteByte( logFile, content.size() - 10, 'y' );
            appendContent( logFile, linesOf( 11000, 10 ) );
            index.indexAppendedLines();

            THEN( "indexing the appended lines does not take the changed tail over" )
            {
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::Truncated );
            }
        }

        WHEN( "it is truncated" )
        {
            writeContent( logFile, content.left( content.size() / 2 ) );

            THEN( "the check tells it was truncated" )
            {
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::Truncated );
            }
        }

        WHEN( "it is replaced by a smaller Log File" )
        {
            writeContent( logFile, linesOf( 1, 100 ) );

            THEN( "the check tells it was truncated" )
            {
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::Truncated );
            }
        }

        WHEN( "it is replaced by a Log File of the same size and other content" )
        {
            auto replaced = content;
            replaced.replace( "line", "LINE" );
            REQUIRE( replaced.size() == content.size() );
            writeContent( logFile, replaced );

            THEN( "the check tells it was truncated" )
            {
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::Truncated );
            }
        }

        WHEN( "it is replaced by a larger Log File with another header" )
        {
            writeContent( logFile, "another header\n" + content + linesOf( 0, 10 ) );

            THEN( "the check tells it was truncated" )
            {
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::Truncated );
            }
        }

        WHEN( "it is appended to, and a byte of its tail changed" )
        {
            overwriteByte( logFile, content.size() - 10, 'y' );
            appendContent( logFile, linesOf( 11000, 10 ) );

            THEN( "the check tells it was truncated" )
            {
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::Truncated );
            }
        }

        WHEN( "a byte between its header and tail changes in place, and it is modified later" )
        {
            const auto indexed = QFileInfo( logFile ).lastModified();
            overwriteByte( logFile, IndexingBlock + IndexingBlock / 4, 'y' );
            setModificationTime( logFile, indexed.addSecs( 10 ) );

            THEN( "only a check without fast modification detection reads it and notices" )
            {
                REQUIRE( index.checkForChanges()
                         == ( policy.fastModificationDetection ? MonitoredFileStatus::Unchanged
                                                               : MonitoredFileStatus::Truncated ) );
            }
        }

        WHEN( "a byte between its header and tail changes in place, its modification time kept" )
        {
            const auto indexed = QFileInfo( logFile ).lastModified();
            overwriteByte( logFile, IndexingBlock + IndexingBlock / 4, 'y' );
            setModificationTime( logFile, indexed );

            THEN( "the check does not read it again, and takes it for unchanged" )
            {
                // The risk accepted so that a change notification for bytes
                // already indexed costs no read of the whole Log File.
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::Unchanged );
            }
        }

        WHEN( "a byte between its header and tail changes and lines are appended" )
        {
            overwriteByte( logFile, IndexingBlock + IndexingBlock / 4, 'y' );
            appendContent( logFile, linesOf( 11000, 10 ) );

            THEN( "the check takes it for grown: only the header and tail are read" )
            {
                // The risk accepted so that following a growing Log File does
                // not read all of it for every append; a reload reads it all.
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::DataAdded );
            }
        }
    }

    GIVEN( "an Index of a Log File shorter than one indexing block" )
    {
        writeContent( logFile, linesOf( 0, 100 ) );
        FollowedIndex index( logFile, policy );

        WHEN( "it grows past one indexing block in appends" )
        {
            for ( int append = 0; append < 8; ++append ) {
                const auto appended = linesOf( 100 + append * 1000, 1000, 1000 );
                appendContent( logFile, appended );
                INFO( "append " << append );

                REQUIRE( index.checkForChanges() == MonitoredFileStatus::DataAdded );
                REQUIRE( index.indexAppendedLines() == appended.size() );
                requireHeaderAndTailOf( index.hash(), logFile );
            }

            THEN( "its whole first block is its header" )
            {
                REQUIRE( index.hash().headerSize == IndexingBlock );
            }
        }

        WHEN( "it is replaced by a larger Log File with another header" )
        {
            writeContent( logFile, "another header\n" + linesOf( 0, 200 ) );

            THEN( "the check tells it was truncated" )
            {
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::Truncated );
            }
        }
    }
}

namespace {

// Bytes with far more Log Lines in them than the ones they replace: as many
// bytes, with a newline every hundred of them.
QByteArray shortLinesOverSameBytes( qint64 size )
{
    QByteArray bytes( static_cast<qsizetype>( size ), 'r' );
    for ( qint64 offset = 99; offset < size; offset += 100 ) {
        bytes[ static_cast<qsizetype>( offset ) ] = '\n';
    }
    bytes[ static_cast<qsizetype>( size - 1 ) ] = '\n';
    return bytes;
}

// The first Log Line of the run whose end lies past the offset.
size_t lineEndingAfter( const IndexingRun& run, qint64 offset )
{
    const auto line
        = std::upper_bound( run.endOfLines.begin(), run.endOfLines.end(), offset,
                            []( qint64 value, OffsetInFile end ) { return value < end.get(); } );
    return static_cast<size_t>( line - run.endOfLines.begin() );
}

} // namespace

SCENARIO( "An explicit reload notices a Log File rewritten in place with the same size",
          "[indexing][reload]" )
{
    QTemporaryDir cacheDir;
    QTemporaryDir logDir;
    REQUIRE( cacheDir.isValid() );
    REQUIRE( logDir.isValid() );

    auto policies = testSettingsPolicies();
    policies.indexing.useIndexCache = true;
    policies.indexing.indexCacheDirectory = cacheDir.path();
    const auto policy = policies.indexing;

    auto noCache = policy;
    noCache.useIndexCache = false;

    const auto logFile = logDir.filePath( "rewritten.log" );

    GIVEN( "a cached Index of a Log File with bytes between its header and its tail" )
    {
        // Over two 5 MiB blocks, so the Index Cache's header and tail digests
        // leave bytes in the middle they say nothing about.
        writeContent( logFile, linesOf( 0, 11000, 1024 ) );
        const auto cached = runFullIndex( logFile, policy );
        REQUIRE( cached.lines.get() == 11000 );
        REQUIRE( cached.hash.tailOffset > cached.hash.headerSize );

        // The first Log Line that begins past the header, and a thousand
        // after it: bytes no recorded digest covers.
        const auto firstRewritten = lineEndingAfter( cached, cached.hash.headerSize ) + 1;
        const auto rewrittenStart = cached.endOfLines[ firstRewritten - 1 ].get();
        const auto rewrittenEnd = cached.endOfLines[ firstRewritten + 999 ].get();
        REQUIRE( rewrittenStart > cached.hash.headerSize );
        REQUIRE( rewrittenEnd < cached.hash.tailOffset );

        WHEN( "it is followed as it grows" )
        {
            FollowedIndex index( logFile, policy );
            const auto appended = linesOf( 11000, 300, 1000 );
            appendContent( logFile, appended );

            THEN( "only what was appended is read" )
            {
                REQUIRE( index.checkForChanges() == MonitoredFileStatus::DataAdded );
                REQUIRE( index.indexAppendedLines() == appended.size() );
            }
        }

        WHEN( "those bytes are rewritten in place, its size and modification time kept" )
        {
            const auto indexedSize = QFileInfo( logFile ).size();
            const auto indexedTime = QFileInfo( logFile ).lastModified();

            overwriteBytes( logFile, rewrittenStart,
                            shortLinesOverSameBytes( rewrittenEnd - rewrittenStart ) );
            // Setting the modification time back is the whole point: it is
            // what a file system with coarse timestamps, or one that writes
            // the time late, leaves the application with (#337).
            setModificationTime( logFile, indexedTime );
            REQUIRE( QFileInfo( logFile ).size() == indexedSize );
            REQUIRE( QFileInfo( logFile ).lastModified() == indexedTime );

            const auto rewritten = runFullIndex( logFile, noCache );
            REQUIRE( rewritten.lines > cached.lines );

            THEN( "an explicit reload reads the whole Log File again" )
            {
                const auto reloaded
                    = runFullIndex( logFile, policy, FullIndexRequest::ExplicitReload );
                REQUIRE( reloaded.bytesIndexed == reloaded.hash.size );
                REQUIRE( reloaded.lines == rewritten.lines );
            }

            THEN( "opening it again reads no more than its header and tail" )
            {
                // The Index Cache's cheap check is what opening and following
                // a Log File still cost: the rewrite goes unnoticed there.
                const auto reopened = runFullIndex( logFile, policy );
                REQUIRE( reopened.bytesIndexed == 0 );
                REQUIRE( reopened.lines == cached.lines );
            }

            THEN( "with fast modification detection a reload keeps the cheap check" )
            {
                // That setting is the user asking not to pay for reading a
                // Log File again, and no full digest is recorded under it.
                auto fast = policy;
                fast.fastModificationDetection = true;
                const auto reloaded
                    = runFullIndex( logFile, fast, FullIndexRequest::ExplicitReload );
                REQUIRE( reloaded.bytesIndexed == 0 );
            }

            THEN( "an explicit reload shows the new Log Lines" )
            {
                LogData logData{ policy, policies.search, policies.fileAccess, policies.decoding };
                attachAndWaitForIndexing( logData, logFile );
                REQUIRE( logData.getNbLine() == cached.lines );

                SafeQSignalSpy reloadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
                logData.reload();
                REQUIRE( reloadEndSpy.safeWait( 20000 ) );

                REQUIRE( logData.getNbLine() == rewritten.lines );
                REQUIRE( logData.getLineString( LineNumber( firstRewritten ) )
                         == QString( 99, QChar( 'r' ) ) );
            }
        }
    }
}
