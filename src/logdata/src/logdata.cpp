/*
 * Copyright (C) 2009, 2010, 2013, 2014, 2015 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements LogData, the content of a log file.

#include <algorithm>
#include <limits>
#include <numeric>
#include <qregularexpression.h>
#include <string_view>
#include <utility>
#include <vector>

#include <QFileInfo>

#include "ansicolorsequences.h"
#include "containers.h"
#include "linetypes.h"
#include "log.h"
#include "logfiltereddata.h"
#include "loglinetext.h"
#include "sparselineread.h"

#include "logdata.h"
#include "logdatametatypes.h"
#include "logdataworker.h"

namespace {

// A Log Line as getLineString() returns it: its text, as it is.
QString unchanged( QString&& lineText )
{
    return std::move( lineText );
}

// A Log Line as getExpandedLineString() returns it, from its text.
QString untabified( QString&& lineText )
{
    return untabify( std::move( lineText ) );
}

} // namespace

LogData::LogData( const IndexingPolicy& indexingPolicy, const SearchPolicy& searchPolicy,
                  const FileAccessPolicy& fileAccessPolicy, const DecodingPolicy& decodingPolicy )
    : AbstractLogData()
    , indexing_data_( std::make_shared<IndexingData>() )
    , searchPolicy_( searchPolicy )
    , fileAccessPolicy_( fileAccessPolicy )
    , codec_( TextEncoding::forName( "ISO-8859-1" ) )
    , decodingPolicy_( decodingPolicy )
{
    registerLogDataMetaTypes();

    // The worker's Background Run keeps the Log File open for as long as an
    // index run reads it.
    auto worker = std::make_unique<LogDataWorker>(
        indexing_data_, indexingPolicy,
        LogDataWorker::Reader{ [ this ] { doAttachReader(); }, [ this ] { doDetachReader(); } } );

    // Reported on this object's thread, by the worker's Background Run.
    connect( worker.get(), &LogDataWorker::indexingProgressed, this, &LogData::loadingProgressed );
    connect( worker.get(), &LogDataWorker::indexingFinished, this, &LogData::indexingFinished );
    connect( worker.get(), &LogDataWorker::checkFileChangesFinished, this,
             &LogData::checkFileChangesFinished );

    operationQueue_.setRunner( std::move( worker ) );

    if ( fileAccessPolicy_.keepFileClosed ) {
        LOG_INFO << "Keep file closed option is set";
    }

    // The Policy forces an Encoding this build knows, or none (#552). One it
    // does not know is none forced, as the index worker takes it too.
    if ( fileAccessPolicy_.defaultEncodingMib >= 0 ) {
        const auto* defaultEncoding = TextEncoding::forMib( fileAccessPolicy_.defaultEncodingMib );
        Q_ASSERT( defaultEncoding != nullptr );
        if ( defaultEncoding ) {
            codec_.setCodec( defaultEncoding );
        }
    }
}

LogData::~LogData()
{
    LOG_DEBUG << "Destroying log data";

    operationQueue_.shutdown();
}

void LogData::setIndexingPolicy( const IndexingPolicy& indexingPolicy )
{
    operationQueue_.setIndexingPolicy( indexingPolicy );
}

void LogData::setSearchPolicy( const SearchPolicy& searchPolicy )
{
    searchPolicy_ = searchPolicy;

    for ( const auto& filteredData : filteredData_ ) {
        if ( filteredData ) {
            filteredData->setSearchPolicy( searchPolicy );
        }
    }
}

void LogData::setDecodingPolicy( const DecodingPolicy& decodingPolicy )
{
    {
        IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
        decodingPolicy_ = decodingPolicy;
    }

    // Views paint what they read before until they are told to read again;
    // the lock is released first, as they read Log Lines straight away.
    logLinesChanged();
    Q_EMIT decodingPolicyChanged();
}

void LogData::attachFile( const QString& fileName )
{
    LOG_DEBUG << "LogData::attachFile " << fileName.toStdString();

    if ( attached_file_ ) {
        // We cannot reattach
        throw CantReattachErr();
    }

    indexingFileName_ = fileName;
    attached_file_.reset( new FileHolder( fileAccessPolicy_.keepFileClosed ) );
    attached_file_->open( indexingFileName_ );

    operationQueue_.enqueueJob( AttachJob{ fileName, fileAccessPolicy_.defaultEncodingMib } );
}

void LogData::interruptLoading()
{
    operationQueue_.interrupt();
}

qint64 LogData::getFileSize() const
{
    return IndexingData::ConstAccessor{ indexing_data_.get() }.getIndexedSize();
}

QDateTime LogData::getLastModifiedDate() const
{
    return lastModifiedDate_;
}

// Return an initialised LogFilteredData. The search is not started.
std::unique_ptr<LogFilteredData> LogData::getNewFilteredData() const
{
    auto filteredData = std::make_unique<LogFilteredData>( this, searchPolicy_ );

    // Forget the ones that have since been destroyed while we are here, so
    // this list cannot grow without bound over a long session.
    filteredData_.erase( std::remove( filteredData_.begin(), filteredData_.end(), nullptr ),
                         filteredData_.end() );
    filteredData_.emplace_back( filteredData.get() );

    return filteredData;
}

void LogData::reload( const TextEncoding* forcedEncoding )
{
    operationQueue_.interrupt();

    // Told at once, not only once indexed again: until then no Search may be
    // served what it found in the Log Lines as they were read before.
    logLinesChanged();

    // Re-open the file, useful in case the file has been moved
    attached_file_->reOpenFile();

    // The user asked for the Log File to be read again, so a cached Index is
    // taken only while every byte it was built from is still the same (#337).
    operationQueue_.enqueueJob(
        FullReindexJob{ FullIndexRequest::ExplicitReload, forcedEncoding } );
}

void LogData::fileChangedOnDisk( const QString& filename )
{
    LOG_INFO << "signalFileChanged " << filename << ", indexed file " << indexingFileName_;

    if ( !attached_file_ ) {
        LOG_WARNING << "no Log File attached, nothing to check";
        return;
    }

    QFileInfo info( indexingFileName_ );
    const auto currentFileId = FileId::getFileId( indexingFileName_ );
    const auto attachedFileId = attached_file_->getFileId();

    const auto indexedHash = IndexingData::ConstAccessor{ indexing_data_.get() }.getHash();

    LOG_INFO << "current indexed fileSize=" << indexedHash.size;
    LOG_INFO << "current indexed hash=" << indexedHash.fullDigest;
    LOG_INFO << "info file_->size()=" << info.size();

    LOG_INFO << "attached_file_->size()=" << attached_file_->size();
    LOG_INFO << "attached_file_id_ index " << attachedFileId.fileIndex;
    LOG_INFO << "currentFileId index " << currentFileId.fileIndex;

    // In absence of any clearer information, we use the following size comparison
    // to determine whether we are following the same file or not (i.e. the file
    // has been moved and the inode we are following is now under a new name, if for
    // instance log has been rotated). We want to follow the name so we have to reopen
    // the file to ensure we are reading the right one.
    // This is a crude heuristic but necessary for notification services that do not
    // give details (e.g. kqueues)

    const bool isFileIdChanged = attachedFileId != currentFileId;

    if ( !isFileIdChanged && filename != indexingFileName_ ) {
        LOG_INFO << "ignore other file update";
        return;
    }

    if ( isFileIdChanged || ( info.size() != attached_file_->size() )
         || ( !attached_file_->isOpen() ) ) {

        LOG_INFO << "Inconsistent size, or file index, the file might have changed, re-opening";

        attached_file_->reOpenFile();
    }

    operationQueue_.enqueueJob( CheckForChangesJob{} );
}

void LogData::indexingFinished( LoadingStatus status, const QString& failure )
{
    LOG_INFO << "indexingFinished for: " << indexingFileName_
             << ( status == LoadingStatus::Successful ) << ", found "
             << IndexingData::ConstAccessor{ indexing_data_.get() }.getNbLines() << " lines.";

    if ( status == LoadingStatus::Successful ) {
        // Update the modified date/time if the file exists
        lastModifiedDate_ = QDateTime();
        QFileInfo fileInfo( indexingFileName_ );
        if ( fileInfo.exists() )
            lastModifiedDate_ = fileInfo.lastModified();
    }

    // After a Partial only the last Log Line indexed before data was added
    // can have changed; after an Attach or a Full any of them can have.
    if ( operationQueue_.isPartialReindexRunning() ) {
        logLinesChanged( nbLinesBeforeDataAdded_.get() > 0
                             ? LineNumber( nbLinesBeforeDataAdded_.get() - 1 )
                             : 0_lnum );
    }
    else {
        logLinesChanged();
    }

    LOG_DEBUG << "Sending indexingFinished.";
    Q_EMIT loadingFinished( status, failure );

    operationQueue_.finishJobAndStartNext();
}

void LogData::checkFileChangesFinished( MonitoredFileStatus status, const QString& failure )
{
    LOG_INFO << "File " << indexingFileName_ << " status " << static_cast<uint8_t>( status );

    // What is queued meets the index job already waiting, if any, under the
    // job rule: a Full it queues is not lost to a later Check, and a Partial
    // waits behind a Check that could still find a truncation.
    switch ( status ) {
    case MonitoredFileStatus::Truncated:
        // Told at once, not only once indexed again: until then no Search may
        // be served what it found in the Log Lines that were there before.
        logLinesChanged();
        operationQueue_.enqueueJob( FullReindexJob{} );
        break;
    case MonitoredFileStatus::DataAdded:
        nbLinesBeforeDataAdded_ = doGetNbLine();
        operationQueue_.enqueueJob( PartialReindexJob{} );
        break;
    case MonitoredFileStatus::Unchanged:
        break;
    }

    if ( status != MonitoredFileStatus::Unchanged ) {
        Q_EMIT fileChanged( status, failure );
    }
    else {
        Q_EMIT fileUnchanged();
    }

    operationQueue_.finishJobAndStartNext();
}

//
// Implementation of virtual functions
//
LinesCount LogData::doGetNbLine() const
{
    return IndexingData::ConstAccessor{ indexing_data_.get() }.getNbLines();
}

LineLength LogData::doGetMaxLength() const
{
    return IndexingData::ConstAccessor{ indexing_data_.get() }.getMaxLength();
}

LineLength LogData::doGetLineLength( LineNumber line ) const
{
    if ( line >= IndexingData::ConstAccessor{ indexing_data_.get() }.getNbLines() ) {
        return 0_length; /* exception? */
    }

    // Use allocation-free length calculation instead of building the full expanded string
    return getUntabifiedLength( doGetLineString( line ) );
}

void LogData::setDisplayEncoding( const TextEncoding& encoding )
{
    LOG_DEBUG << "LogData::setDisplayEncoding: " << encoding.name().constData();
    codec_.setCodec( &encoding );
    auto needReload = false;
    auto useGuessedCodec = false;

    {
        IndexingData::ConstAccessor scopedAccessor{ indexing_data_.get() };

        const TextEncoding* currentIndexCodec = scopedAccessor.getForcedEncoding();
        if ( !currentIndexCodec ) {
            currentIndexCodec = scopedAccessor.getEncodingGuess();
        }

        if ( currentIndexCodec && codec_.mibEnum() != currentIndexCodec->mibEnum() ) {
            if ( codec_.encodingParameters() != EncodingParameters( currentIndexCodec ) ) {
                needReload = true;
                useGuessedCodec = codec_.mibEnum() == scopedAccessor.getEncodingGuess()->mibEnum();
            }
        }
    }

    if ( needReload ) {
        reload( useGuessedCodec ? nullptr : codec_.codec() );
    }
    else {
        // Indexed as it was, but the Log Lines decode differently.
        logLinesChanged();
    }
}

void LogData::logLinesChanged( LineNumber firstChanged ) const
{
    for ( const auto& filteredData : filteredData_ ) {
        if ( filteredData ) {
            filteredData->logLinesChanged( firstChanged );
        }
    }
}

const TextEncoding* LogData::doGetDisplayEncoding() const
{
    return codec_.codec();
}

QString LogData::doGetLineString( LineNumber line ) const
{
    const auto lines = doGetLines( line, 1_lcount );
    return lines.empty() ? QString{} : lines.front();
}

QString LogData::doGetExpandedLineString( LineNumber line ) const
{
    return untabify( doGetLineString( line ) );
}

// Note this function is also called from the LogFilteredDataWorker thread, so
// data must be protected because they are changed in the main thread (by
// indexingFinished).
logsquirl::vector<QString> LogData::doGetLines( LineNumber first_line, LinesCount number ) const
{
    return getLinesFromFile( first_line, number, unchanged );
}

logsquirl::vector<QString> LogData::doGetExpandedLines( LineNumber first_line,
                                                        LinesCount number ) const
{
    return getLinesFromFile( first_line, number, untabified );
}

LineNumber LogData::doGetLineNumber( LineNumber index ) const
{
    return index;
}

const SearchBlockSource& LogData::searchBlockSource() const
{
    return searchBlockSource_;
}

LinesCount LogDataBlockSource::getNbLines() const
{
    return logData_.getNbLine();
}

RawLines LogDataBlockSource::getLinesRaw( LineNumber first, LinesCount number ) const
{
    return logData_.getLinesRaw( first, number );
}

void LogDataBlockSource::attachReader() const
{
    logData_.attachReader();
}

void LogDataBlockSource::detachReader() const
{
    logData_.detachReader();
}

RawLines LogData::getLinesRaw( LineNumber firstLine, LinesCount number ) const
{
    RawLines rawLines;
    rawLines.startLine = firstLine;

    try {
        OffsetInFile::UnderlyingType firstByte = 0;
        logsquirl::vector<OffsetInFile> endOfLines;
        {
            // Only the offsets are taken under the index lock: the file is
            // read without it, so indexing can publish a block meanwhile.
            IndexingData::ConstAccessor scopedAccessor{ indexing_data_.get() };
            if ( ( firstLine + number ).get() > scopedAccessor.getNbLines().get() ) {
                LOG_WARNING << "Lines out of bound asked for";
                return {}; /* exception? */
            }

            rawLines.hideAnsiColorSequences = decodingPolicy_.hideAnsiColorSequences;

            firstByte = ( firstLine == 0_lnum )
                            ? 0
                            : scopedAccessor.getEndOfLineOffset( firstLine - 1_lcount ).get();

            endOfLines = scopedAccessor.getEndOfLineOffsets( firstLine, number );
        }

        // Guard against the (rare) case where the index has been concurrently truncated
        // between the firstByte lookup above and this call: back() on an empty vector is
        // undefined behavior.
        if ( endOfLines.empty() ) {
            LOG_DEBUG << "no end-of-line offsets returned for firstLine=" << firstLine
                      << " number=" << number;
            return rawLines;
        }

        const auto lastByte = endOfLines.back().get();

        rawLines.endOfLines.reserve( endOfLines.size() );
        std::transform(
            endOfLines.begin(), endOfLines.end(), std::back_inserter( rawLines.endOfLines ),
            [ firstByte ]( const OffsetInFile& offset ) { return offset.get() - firstByte; } );

        const auto bytesToRead = lastByte - firstByte;
        LOG_DEBUG << "will try to read:" << bytesToRead << " bytes";
        rawLines.buffer.resize( static_cast<std::size_t>( bytesToRead ) );

        qint64 bytesRead = 0;
        {
            ScopedFileHolder<FileHolder> fileHolder( attached_file_.get() );
            fileHolder.getFile()->seek( firstByte );
            bytesRead = fileHolder.getFile()->read( rawLines.buffer.data(), bytesToRead );
        }

        if ( bytesRead != bytesToRead ) {
            // The Log File is shorter than its Index says, as when it was cut
            // short on disk and is not indexed again yet: only the bytes read
            // are decoded, and the Log Lines past them read as a warning.
            LOG_DEBUG << "failed to read " << bytesToRead << " bytes, got " << bytesRead;
            rawLines.buffer.resize(
                static_cast<std::size_t>( std::max( bytesRead, qint64{ 0 } ) ) );
        }

        LOG_DEBUG << "done reading lines:" << rawLines.buffer.size();
        rawLines.textDecoder = codec_.makeDecoder();
        return rawLines;

    } catch ( const std::bad_alloc& ) {
        LOG_ERROR << "not enough memory";
        rawLines.endOfLines.clear();
        rawLines.buffer.clear();
        return rawLines;
    }
}

logsquirl::vector<QString> LogData::getLinesFromFile( LineNumber firstLine, LinesCount number,
                                                      QString ( *processLine )( QString&& ) ) const
{
    LOG_DEBUG << "firstLine:" << firstLine << " nb:" << number;

    if ( number.get() == 0 ) {
        return logsquirl::vector<QString>();
    }

    logsquirl::vector<QString> processedLines;
    try {
        const auto rawLines = getLinesRaw( firstLine, number );
        auto decodedLines = rawLines.decodeLines();

        processedLines.reserve( decodedLines.size() );

        for ( auto&& line : decodedLines ) {
            processedLines.push_back( processLine( std::move( line ) ) );
        }

    } catch ( const std::bad_alloc& e ) {
        LOG_ERROR << "not enough memory " << e.what();
        processedLines.push_back( warningText( NotEnoughMemoryWarning ) );
    }

    processedLines.reserve( number.get() - processedLines.size() );
    while ( processedLines.size() < number.get() ) {
        processedLines.push_back( warningText( LinesNotReadWarning ) );
    }

    return processedLines;
}

template <typename OnLine>
void LogData::readSparseLines( std::span<const LineNumber> lines, OnLine&& onLine ) const
{
    // Everything the reads need from the Index and the Decoding Policy is
    // copied under the index lock; the file is read without it, so indexing
    // can publish a block meanwhile (#289). A read cut short by a Log File
    // shorter than its Index reads as a warning below.
    logsquirl::vector<SparseRead> reads;
    bool hideAnsiColorSequences = false;
    {
        IndexingData::ConstAccessor scopedAccessor{ indexing_data_.get() };
        reads = planSparseRead( lines, scopedAccessor.getNbLines(),
                                [ &scopedAccessor ]( LineNumber first, LinesCount count ) {
                                    return scopedAccessor.getEndOfLineOffsets( first, count );
                                } );
        hideAnsiColorSequences = decodingPolicy_.hideAnsiColorSequences;
    }

    if ( reads.empty() ) {
        return;
    }

    ScopedFileHolder<FileHolder> fileHolder( attached_file_.get() );
    const auto lineFeedWidth = codec_.encodingParameters().lineFeedWidth;

    logsquirl::vector<char> buffer;
    for ( const auto& read : reads ) {
        buffer.resize( static_cast<std::size_t>( read.size ) );
        fileHolder.getFile()->seek( read.firstByte.get() );
        const auto bytesRead = fileHolder.getFile()->read( buffer.data(), read.size );
        if ( bytesRead != read.size ) {
            LOG_DEBUG << "failed to read " << read.size << " bytes, got " << bytesRead;
        }

        const std::string_view bytes(
            buffer.data(), static_cast<std::size_t>( std::max( bytesRead, qint64{ 0 } ) ) );
        for ( const auto& line : read.lines ) {
            onLine( readLogLine( line.request, bytes, line.begin,
                                 line.end - line.begin - lineFeedWidth, hideAnsiColorSequences ) );
        }
    }
}

logsquirl::vector<QString> LogData::getLinesSparse( std::span<const LineNumber> lines ) const
{
    return decodeReadLogLines<QString>(
        codec_, lines.size(),
        [ this, lines ]( const auto& onLine ) { readSparseLines( lines, onLine ); }, logLineText );
}

logsquirl::vector<QString>
LogData::doGetExpandedLinesSparse( std::span<const LineNumber> lines ) const
{
    return decodeReadLogLines<QString>(
        codec_, lines.size(),
        [ this, lines ]( const auto& onLine ) { readSparseLines( lines, onLine ); },
        []( QString&& decodedLine, bool hideAnsiColorSequences ) {
            return untabified( logLineText( std::move( decodedLine ), hideAnsiColorSequences ) );
        } );
}

logsquirl::vector<AnsiColoredText>
LogData::getAnsiColoredLinesSparse( std::span<const LineNumber> lines ) const
{
    return decodeReadLogLines<AnsiColoredText>(
        codec_, lines.size(),
        [ this, lines ]( const auto& onLine ) { readSparseLines( lines, onLine ); },
        ansiColoredLogLineText );
}

logsquirl::vector<AnsiColoredText> LogData::doGetAnsiColoredLines( LineNumber firstLine,
                                                                   LinesCount number ) const
{
    if ( number.get() == 0 ) {
        return {};
    }

    logsquirl::vector<AnsiColoredText> lines;
    try {
        lines = getLinesRaw( firstLine, number ).decodeAnsiColoredLines();
    } catch ( const std::bad_alloc& e ) {
        LOG_ERROR << "not enough memory " << e.what();
        lines.push_back( AnsiColoredText{ warningText( NotEnoughMemoryWarning ) } );
    }

    while ( lines.size() < number.get() ) {
        lines.push_back( AnsiColoredText{ warningText( LinesNotReadWarning ) } );
    }
    return lines;
}

std::string LogData::getUtf8LinesSparse( std::span<const LineNumber> lines ) const
{
    return decodeReadLogLinesToUtf8( codec_, lines.size(), [ this, lines ]( const auto& onLine ) {
        readSparseLines( lines, onLine );
    } );
}

const TextEncoding* LogData::getDetectedEncoding() const
{
    return IndexingData::ConstAccessor{ indexing_data_.get() }.getEncodingGuess();
}

void LogData::doAttachReader() const
{
    // Before a file is attached there is nothing to keep open, and nothing
    // to read either.
    if ( attached_file_ ) {
        attached_file_->attachReader();
    }
}

void LogData::doDetachReader() const
{
    if ( attached_file_ ) {
        attached_file_->detachReader();
    }
}
