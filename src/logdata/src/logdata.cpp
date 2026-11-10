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

#include <optional>
#include <utility>

#include <QFileInfo>

#include "containers.h"
#include "linetypes.h"
#include "log.h"
#include "logfiltereddata.h"
#include "loglinereader.h"

#include "indexoperation.h"
#include "logdata.h"
#include "logdatametatypes.h"
#include "logdataworker.h"

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

    // A Search left running past its shutdown timeout still holds the block
    // source: from here on it reads nothing (#832).
    searchBlockSource_->forgetLogData();
    operationQueue_.shutdown();
}

void LogData::setIndexingPolicy( const IndexingPolicy& indexingPolicy )
{
    operationQueue_.setIndexingPolicy( indexingPolicy );
}

const SearchPolicy& LogData::searchPolicy() const
{
    return searchPolicy_;
}

void LogData::setSearchPolicy( const SearchPolicy& searchPolicy )
{
    searchPolicy_ = searchPolicy;
}

void LogData::setDecodingPolicy( const DecodingPolicy& decodingPolicy )
{
    {
        IndexingData::MutateAccessor scopedAccessor{ indexing_data_.get() };
        decodingPolicy_ = decodingPolicy;
    }

    // Views paint what they read before until they are told to read again;
    // the lock is released first, as they read Log Lines straight away.
    Q_EMIT logLinesChanged( 0_lnum );
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
    attached_file_ = std::make_shared<FileHolder>( fileAccessPolicy_.keepFileClosed );
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
    // It hears of the Log Lines changed through logLinesChanged(), which it
    // connects itself to.
    return std::make_unique<LogFilteredData>( this, searchPolicy_ );
}

void LogData::reload( const TextEncoding* forcedEncoding )
{
    operationQueue_.interrupt();

    // Told at once, not only once indexed again: until then no Search may be
    // served what it found in the Log Lines as they were read before.
    Q_EMIT logLinesChanged( 0_lnum );

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

void LogData::indexingFinished( LoadingStatus status, const QString& failure,
                                LineNumber firstChanged )
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

    // As the index job tells it: after a Partial only the last Log Line
    // indexed before data was added can have changed; after an Attach or a
    // Full any of them can have.
    Q_EMIT logLinesChanged( firstChanged );

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
        Q_EMIT logLinesChanged( 0_lnum );
        operationQueue_.enqueueJob( FullReindexJob{} );
        break;
    case MonitoredFileStatus::DataAdded:
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
        Q_EMIT logLinesChanged( 0_lnum );
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
    return reader().readLines( first_line, number );
}

logsquirl::vector<QString> LogData::doGetExpandedLines( LineNumber first_line,
                                                        LinesCount number ) const
{
    return reader().readExpandedLines( first_line, number );
}

std::shared_ptr<const SearchBlockSource> LogData::searchBlockSource() const
{
    return searchBlockSource_;
}

LinesCount LogDataBlockSource::getNbLines() const
{
    std::lock_guard lock( mutex_ );
    return logData_ ? logData_->getNbLine() : 0_lcount;
}

RawLines LogDataBlockSource::getLinesRaw( LineNumber first, LinesCount number ) const
{
    // The handles are taken under the lock, the block is read without it: a
    // read that outlasts the LogData holds everything it touches.
    auto handles = [ this ]() -> std::optional<ReadHandles> {
        std::lock_guard lock( mutex_ );
        if ( !logData_ ) {
            return std::nullopt;
        }
        return logData_->readHandles();
    }();
    if ( !handles ) {
        RawLines rawLines;
        rawLines.startLine = first;
        return rawLines;
    }
    return LogLineReader( std::move( *handles ) )
        .readRawLines( first, number, SearchBlockMaxBytes );
}

void LogDataBlockSource::attachReader() const
{
    if ( const auto file = attachedFile() ) {
        file->attachReader();
    }
}

void LogDataBlockSource::detachReader() const
{
    if ( const auto file = attachedFile() ) {
        file->detachReader();
    }
}

void LogDataBlockSource::forgetLogData()
{
    std::lock_guard lock( mutex_ );
    logData_ = nullptr;
}

std::shared_ptr<FileHolder> LogDataBlockSource::attachedFile() const
{
    std::lock_guard lock( mutex_ );
    return logData_ ? logData_->attached_file_ : nullptr;
}

ReadHandles LogData::readHandles() const
{
    // The decoder carries the Encoding's parameters: one lock, not two, on
    // every read of Log Lines.
    return ReadHandles{ .index = indexing_data_,
                        .file = attached_file_,
                        .decoder = codec_.makeDecoder(),
                        .decodingPolicy = decodingPolicy_.load() };
}

LogLineReader LogData::reader() const
{
    return LogLineReader( readHandles() );
}

logsquirl::vector<QString> LogData::getLinesSparse( std::span<const LineNumber> lines ) const
{
    return reader().readLinesSparse( lines );
}

logsquirl::vector<QString>
LogData::doGetExpandedLinesSparse( std::span<const LineNumber> lines ) const
{
    return reader().readExpandedLinesSparse( lines );
}

logsquirl::vector<AnsiColoredText>
LogData::getAnsiColoredLinesSparse( std::span<const LineNumber> lines ) const
{
    return reader().readAnsiColoredLinesSparse( lines );
}

logsquirl::vector<AnsiColoredText> LogData::doGetAnsiColoredLines( LineNumber firstLine,
                                                                   LinesCount number ) const
{
    return reader().readAnsiColoredLines( firstLine, number );
}

void LogData::readLinePrefixes(
    std::span<const LineNumber> lines, qint64 maxBytes,
    const std::function<bool( QString&& text, bool cut )>& onLine ) const
{
    reader().readLinePrefixes( lines, maxBytes, onLine );
}

std::string LogData::getUtf8LinesSparse( std::span<const LineNumber> lines ) const
{
    return reader().readUtf8LinesSparse( lines );
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
