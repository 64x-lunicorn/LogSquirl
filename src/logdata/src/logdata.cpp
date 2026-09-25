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
#include <optional>
#include <qregularexpression.h>
#include <string_view>
#include <utility>
#include <vector>

#include <QFileInfo>
#include <QtEndian>

#include <simdutf.h>

#include "ansicolorsequences.h"
#include "containers.h"
#include "linetypes.h"
#include "log.h"
#include "logfiltereddata.h"
#include "loglinetext.h"
#include "sparselineread.h"

#include "logdata.h"
#include "logdatametatypes.h"

namespace {

// What a Log Line reads as when it cannot be read.
constexpr std::string_view LineTooLongWarning = "LOGSQUIRL WARNING: this line is too long";
constexpr std::string_view FileReadFailedWarning = "LOGSQUIRL WARNING: file read failed";
constexpr std::string_view NotEnoughMemoryWarning = "LOGSQUIRL WARNING: not enough memory";
constexpr std::string_view LinesNotReadWarning
    = "LOGSQUIRL WARNING: failed to read some lines before this one";

QString asQString( std::string_view warning )
{
    return QString::fromLatin1( warning.data(), static_cast<qsizetype>( warning.size() ) );
}

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
    , operationQueue_( [ this ] { attached_file_->attachReader(); } )
    , indexingPolicy_( indexingPolicy )
    , searchPolicy_( searchPolicy )
    , fileAccessPolicy_( fileAccessPolicy )
    , codec_( TextEncoding::forName( "ISO-8859-1" ) )
    , decodingPolicy_( decodingPolicy )
{
    registerLogDataMetaTypes();

    auto worker = std::make_unique<LogDataWorker>( indexing_data_, indexingPolicy_ );

    // Forward the update signal
    connect( worker.get(), &LogDataWorker::indexingProgressed, this, &LogData::loadingProgressed );
    connect( worker.get(), &LogDataWorker::indexingFinished, this, &LogData::indexingFinished,
             Qt::QueuedConnection );
    connect( worker.get(), &LogDataWorker::checkFileChangesFinished, this,
             &LogData::checkFileChangesFinished, Qt::QueuedConnection );

    operationQueue_.setWorker( std::move( worker ) );

    if ( fileAccessPolicy_.keepFileClosed ) {
        LOG_INFO << "Keep file closed option is set";
    }

    if ( fileAccessPolicy_.defaultEncodingMib >= 0 ) {
        const auto* defaultEncoding = TextEncoding::forMib( fileAccessPolicy_.defaultEncodingMib );
        if ( !defaultEncoding ) {
            LOG_WARNING << "Unknown default encoding " << fileAccessPolicy_.defaultEncodingMib
                        << ", using the one of the locale";
            defaultEncoding = TextEncoding::forLocale();
        }
        codec_.setCodec( defaultEncoding );
    }
}

LogData::~LogData()
{
    LOG_DEBUG << "Destroying log data";

    operationQueue_.shutdown();
}

void LogData::setIndexingPolicy( const IndexingPolicy& indexingPolicy )
{
    indexingPolicy_ = indexingPolicy;
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
    attached_file_->detachReader();

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
    attached_file_->detachReader();

    LOG_INFO << "File " << indexingFileName_ << " status " << static_cast<uint8_t>( status );

    // What is queued meets the index job already waiting, if any, under the
    // job rule: a Full it queues is not lost to a later Check, and a Partial
    // waits behind a Check that could still find a truncation.
    switch ( status ) {
    case MonitoredFileStatus::Truncated:
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

void LogData::doSetDisplayEncoding( const char* encoding )
{
    LOG_DEBUG << "AbstractLogData::setDisplayEncoding: " << encoding;
    codec_.setCodec( TextEncoding::forName( encoding ) );
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

LogData::RawLines LogData::getLinesRaw( LineNumber firstLine, LinesCount number ) const
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
        processedLines.push_back( asQString( NotEnoughMemoryWarning ) );
    }

    processedLines.reserve( number.get() - processedLines.size() );
    while ( processedLines.size() < number.get() ) {
        processedLines.push_back( asQString( LinesNotReadWarning ) );
    }

    return processedLines;
}

logsquirl::vector<QString> LogData::getLinesSparse( std::span<const LineNumber> lines ) const
{
    return getSparseLinesFromFile( lines, unchanged );
}

logsquirl::vector<QString>
LogData::doGetExpandedLinesSparse( std::span<const LineNumber> lines ) const
{
    return getSparseLinesFromFile( lines, untabified );
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

        for ( const auto& line : read.lines ) {
            const auto length = line.end - line.begin - lineFeedWidth;

            SparseReadLine readLine{ .request = line.request,
                                     .bytes = {},
                                     .warning = {},
                                     .hideAnsiColorSequences = hideAnsiColorSequences };
            constexpr auto maxlength = std::numeric_limits<int>::max() / 2;
            if ( length >= maxlength ) {
                readLine.warning = LineTooLongWarning;
            }
            else if ( line.begin + length > std::max( bytesRead, qint64{ 0 } ) ) {
                readLine.warning = FileReadFailedWarning;
            }
            else {
                readLine.bytes = std::string_view( buffer.data() + line.begin,
                                                   static_cast<std::size_t>( length ) );
            }
            onLine( std::as_const( readLine ) );
        }
    }
}

logsquirl::vector<QString>
LogData::getSparseLinesFromFile( std::span<const LineNumber> lines,
                                 QString ( *processLine )( QString&& ) ) const
{
    logsquirl::vector<QString> text( lines.size() );
    logsquirl::vector<bool> isRead( lines.size(), false );

    try {
        // One decoder for the whole read: making one costs a converter, dear
        // for the legacy Encodings. Its state is reset for every Log Line.
        const auto textDecoder = codec_.makeDecoder();
        readSparseLines( lines, [ & ]( const SparseReadLine& line ) {
            QString decodedLine;
            if ( !line.warning.empty() ) {
                decodedLine = asQString( line.warning );
            }
            else {
                // Each Log Line is decoded on its own, as getLineString()
                // does: a character cut short at the end of one must not
                // reach the next, and a byte order mark starting any of them
                // is dropped.
                textDecoder.decoder->resetState();
                decodedLine = textDecoder.decode( line.bytes.data(),
                                                  static_cast<qsizetype>( line.bytes.size() ) );
                if ( line.hideAnsiColorSequences ) {
                    removeAnsiColorSequences( decodedLine );
                }
                trimToLogLineText( decodedLine );
            }

            text[ line.request ] = processLine( std::move( decodedLine ) );
            isRead[ line.request ] = true;
        } );
    } catch ( const std::bad_alloc& e ) {
        LOG_ERROR << "not enough memory " << e.what();
        for ( std::size_t request = 0; request < lines.size(); ++request ) {
            if ( !isRead[ request ] ) {
                text[ request ] = asQString( NotEnoughMemoryWarning );
                isRead[ request ] = true;
            }
        }
    }

    // What a Log Line reads as when it is past the last one, or its read did
    // not happen: the same as when it is read on its own.
    for ( std::size_t request = 0; request < lines.size(); ++request ) {
        if ( !isRead[ request ] ) {
            text[ request ] = asQString( LinesNotReadWarning );
        }
    }

    return text;
}

std::string LogData::getUtf8LinesSparse( std::span<const LineNumber> lines ) const
{
    static constexpr int Utf8Mib = 106;

    // The text of each Log Line, with its line feed, lands in pieces in the
    // order the Log Lines are read; where each one lies is kept by request.
    struct Piece {
        std::size_t begin = 0;
        std::size_t size = 0;
        bool isRead = false;
    };
    std::string pieces;
    logsquirl::vector<Piece> placed( lines.size() );

    try {
        const auto encodingParams = codec_.encodingParameters();
        const bool isUtf8 = codec_.mibEnum() == Utf8Mib;
        const auto textDecoder = codec_.makeDecoder();

        readSparseLines( lines, [ & ]( const SparseReadLine& line ) {
            const auto begin = pieces.size();
            const auto& text = line.bytes;

            // A decoder replaces what is not UTF-8: that is not copied as it
            // is.
            const bool isCopiedAsRead
                = line.warning.empty() && encodingParams.isUtf8Compatible
                  && ( !line.hideAnsiColorSequences
                       || text.find( '\x1B' ) == std::string_view::npos )
                  && ( simdutf::validate_ascii( text.data(), text.size() )
                       || ( isUtf8 && simdutf::validate_utf8( text.data(), text.size() ) ) );

            if ( !line.warning.empty() ) {
                pieces.append( line.warning );
            }
            else if ( isCopiedAsRead ) {
                pieces.append( trimToLogLineText( text ) );
            }
            else {
                // Decoded on its own, as getLineString() does.
                textDecoder.decoder->resetState();
                auto decodedLine
                    = textDecoder.decode( text.data(), static_cast<qsizetype>( text.size() ) );
                if ( line.hideAnsiColorSequences ) {
                    removeAnsiColorSequences( decodedLine );
                }
                trimToLogLineText( decodedLine );
                pieces += decodedLine.toStdString();
            }
            pieces += '\n';

            placed[ line.request ] = Piece{ begin, pieces.size() - begin, true };
        } );
    } catch ( const std::bad_alloc& e ) {
        LOG_ERROR << "not enough memory " << e.what();
        std::string text;
        for ( std::size_t request = 0; request < lines.size(); ++request ) {
            text.append( NotEnoughMemoryWarning );
            text += '\n';
        }
        return text;
    }

    // Log Lines asked for once each and in ascending order were read in the
    // order asked: the pieces are the text already.
    std::size_t expectedBegin = 0;
    const bool isInOrder
        = std::all_of( placed.begin(), placed.end(), [ &expectedBegin ]( const Piece& piece ) {
              const bool follows = piece.isRead && piece.begin == expectedBegin;
              expectedBegin += piece.size;
              return follows;
          } );
    if ( isInOrder ) {
        return pieces;
    }

    std::string text;
    text.reserve( pieces.size() );
    for ( const auto& piece : placed ) {
        if ( piece.isRead ) {
            text.append( pieces, piece.begin, piece.size );
        }
        else {
            text.append( LinesNotReadWarning );
            text += '\n';
        }
    }
    return text;
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

logsquirl::vector<QString> RawLines::decodeLines() const
{
    if ( this->endOfLines.empty() ) {
        return logsquirl::vector<QString>();
    }

    logsquirl::vector<QString> decodedLines;
    decodedLines.reserve( this->endOfLines.size() );

    try {
        qint64 lineStart = 0;
        size_t currentLineIndex = 0;
        const auto lineFeedWidth = textDecoder.encodingParams.lineFeedWidth;
        for ( const auto& lineEnd : this->endOfLines ) {
            const auto length = lineEnd - lineStart - lineFeedWidth;
            LOG_DEBUG << "line " << this->startLine.get() + currentLineIndex << ", length "
                      << length;

            constexpr auto maxlength = std::numeric_limits<int>::max() / 2;
            if ( length >= maxlength ) {
                decodedLines.push_back( asQString( LineTooLongWarning ) );
                break;
            }

            if ( lineStart + length > logsquirl::ssize( buffer ) ) {
                decodedLines.push_back( asQString( FileReadFailedWarning ) );
                LOG_WARNING << "not enough data in buffer";
                break;
            }

            auto decodedLine = textDecoder.decode( buffer.data() + lineStart,
                                                   type_safe::narrow_cast<int>( length ) );

            if ( hideAnsiColorSequences ) {
                removeAnsiColorSequences( decodedLine );
            }
            trimToLogLineText( decodedLine );

            decodedLines.push_back( std::move( decodedLine ) );

            lineStart = lineEnd;
        }
    } catch ( const std::bad_alloc& ) {
        LOG_ERROR << "not enough memory";
        decodedLines.push_back( asQString( NotEnoughMemoryWarning ) );
    }

    decodedLines.reserve( this->endOfLines.size() - decodedLines.size() );
    while ( decodedLines.size() < this->endOfLines.size() ) {
        decodedLines.emplace_back(
            "LOGSQUIRL WARNING: failed to decode some lines before this one" );
    }

    return decodedLines;
}

namespace {

// The Encodings a Search converts to UTF-8 straight from the bytes of each Log
// Line, found from the known line ends, without decoding the block to a
// QString first (#291). Any other Encoding is decoded as a whole.
enum class DirectEncoding { None, Utf8, Latin1, Utf16LE, Utf16BE };

DirectEncoding directEncodingOf( const EncodingParameters& encodingParams )
{
    if ( encodingParams.isUtf8Compatible ) {
        return DirectEncoding::Utf8;
    }
    if ( encodingParams.isLatin1 ) {
        return DirectEncoding::Latin1;
    }
    if ( encodingParams.isUtf16LE ) {
        return DirectEncoding::Utf16LE;
    }
    if ( encodingParams.isUtf16BE ) {
        return DirectEncoding::Utf16BE;
    }
    return DirectEncoding::None;
}

bool isUtf16( DirectEncoding encoding )
{
    return encoding == DirectEncoding::Utf16LE || encoding == DirectEncoding::Utf16BE;
}

const char16_t* asUtf16( std::string_view bytes )
{
    return reinterpret_cast<const char16_t*>( bytes.data() );
}

char16_t codeUnitAt( std::string_view bytes, std::size_t index, DirectEncoding encoding )
{
    const auto first = static_cast<unsigned char>( bytes[ index ] );
    const auto second = static_cast<unsigned char>( bytes[ index + 1 ] );
    return encoding == DirectEncoding::Utf16LE ? static_cast<char16_t>( first | ( second << 8 ) )
                                               : static_cast<char16_t>( ( first << 8 ) | second );
}

std::string_view withoutLineFeed( std::string_view line, DirectEncoding encoding )
{
    if ( isUtf16( encoding ) ) {
        if ( line.size() >= 2 && codeUnitAt( line, line.size() - 2, encoding ) == u'\n' ) {
            line.remove_suffix( 2 );
        }
    }
    else if ( !line.empty() && line.back() == '\n' ) {
        line.remove_suffix( 1 );
    }
    return line;
}

bool containsEscape( std::string_view line, DirectEncoding encoding )
{
    if ( !isUtf16( encoding ) ) {
        return line.find( '\x1B' ) != std::string_view::npos;
    }
    for ( std::size_t index = 0; index + 1 < line.size(); index += 2 ) {
        if ( codeUnitAt( line, index, encoding ) == u'\x1B' ) {
            return true;
        }
    }
    return false;
}

bool isValid( std::string_view line, DirectEncoding encoding )
{
    switch ( encoding ) {
    case DirectEncoding::Utf16LE:
        return simdutf::validate_utf16le( asUtf16( line ), line.size() / 2 );
    case DirectEncoding::Utf16BE:
        return simdutf::validate_utf16be( asUtf16( line ), line.size() / 2 );
    default:
        return true;
    }
}

QString decode( std::string_view line, DirectEncoding encoding )
{
    const auto size = static_cast<qsizetype>( line.size() );
    switch ( encoding ) {
    case DirectEncoding::Latin1:
        return QString::fromLatin1( QByteArrayView( line ) );
    case DirectEncoding::Utf16LE:
    case DirectEncoding::Utf16BE: {
        QString text( size / 2, Qt::Uninitialized );
        if ( encoding == DirectEncoding::Utf16LE ) {
            qFromLittleEndian<char16_t>( line.data(), size / 2, text.data() );
        }
        else {
            qFromBigEndian<char16_t>( line.data(), size / 2, text.data() );
        }
        return text;
    }
    default:
        return QString::fromUtf8( QByteArrayView( line ) );
    }
}

std::size_t utf8SizeOf( std::string_view line, DirectEncoding encoding )
{
    switch ( encoding ) {
    case DirectEncoding::Latin1:
        return simdutf::utf8_length_from_latin1( line.data(), line.size() );
    case DirectEncoding::Utf16LE:
        return simdutf::utf8_length_from_utf16le( asUtf16( line ), line.size() / 2 );
    case DirectEncoding::Utf16BE:
        return simdutf::utf8_length_from_utf16be( asUtf16( line ), line.size() / 2 );
    default:
        return line.size();
    }
}

// Converts a line valid in its encoding; returns the UTF-8 bytes written.
std::size_t convertToUtf8( std::string_view line, DirectEncoding encoding, char* utf8 )
{
    switch ( encoding ) {
    case DirectEncoding::Latin1:
        return simdutf::convert_latin1_to_utf8( line.data(), line.size(), utf8 );
    case DirectEncoding::Utf16LE:
        return simdutf::convert_valid_utf16le_to_utf8( asUtf16( line ), line.size() / 2, utf8 );
    case DirectEncoding::Utf16BE:
        return simdutf::convert_valid_utf16be_to_utf8( asUtf16( line ), line.size() / 2, utf8 );
    default:
        std::copy( line.begin(), line.end(), utf8 );
        return line.size();
    }
}

// The UTF-8 of text, in a buffer exactly its size. Invalid UTF-16 is mapped as
// QString::toUtf8() maps it.
QByteArray toUtf8( const QString& text )
{
    const auto* const utf16 = reinterpret_cast<const char16_t*>( text.constData() );
    const auto size = static_cast<std::size_t>( text.size() );
    if ( !simdutf::validate_utf16( utf16, size ) ) {
        return text.toUtf8();
    }
    QByteArray utf8( static_cast<qsizetype>( simdutf::utf8_length_from_utf16( utf16, size ) ),
                     Qt::Uninitialized );
    const auto written = simdutf::convert_valid_utf16_to_utf8( utf16, size, utf8.data() );
    utf8.truncate( static_cast<qsizetype>( written ) );
    return utf8;
}

// Converts a block of Log Lines in a Latin-1 or UTF-16 encoding to UTF-8 at
// once into utf8, and splits it at each line feed into the text of its Log
// Lines. Does nothing and returns false unless the block is valid in its
// encoding and has lineCount Log Lines.
bool convertValidBlock( std::string_view block, DirectEncoding encoding, std::size_t lineCount,
                        QByteArray& utf8, logsquirl::vector<std::string_view>& lines )
{
    if ( block.empty() ) {
        return false;
    }

    // Validated while converted, into room for the longest UTF-8 it can take:
    // one pass over the block rather than three.
    QByteArray converted( static_cast<qsizetype>( block.size() * 2 ), Qt::Uninitialized );
    std::size_t written = 0;
    switch ( encoding ) {
    case DirectEncoding::Latin1:
        written = simdutf::convert_latin1_to_utf8( block.data(), block.size(), converted.data() );
        break;
    case DirectEncoding::Utf16LE:
        written = simdutf::convert_utf16le_to_utf8( asUtf16( block ), block.size() / 2,
                                                    converted.data() );
        break;
    case DirectEncoding::Utf16BE:
        written = simdutf::convert_utf16be_to_utf8( asUtf16( block ), block.size() / 2,
                                                    converted.data() );
        break;
    default:
        return false;
    }
    if ( written == 0 ) {
        return false;
    }
    converted.truncate( static_cast<qsizetype>( written ) );

    // A line feed is the only code unit whose UTF-8 has a line feed byte.
    logsquirl::vector<std::string_view> split;
    split.reserve( lineCount );
    std::string_view rest( converted.constData(), static_cast<std::size_t>( converted.size() ) );
    for ( auto lineFeed = rest.find( '\n' ); lineFeed != std::string_view::npos;
          lineFeed = rest.find( '\n' ) ) {
        split.push_back( trimToLogLineText( rest.substr( 0, lineFeed ) ) );
        rest.remove_prefix( lineFeed + 1 );
    }
    if ( !rest.empty() ) {
        split.push_back( trimToLogLineText( rest ) );
    }
    if ( split.size() != lineCount ) {
        return false;
    }

    utf8 = std::move( converted );
    lines = std::move( split );
    return true;
}

// Where a Log Line of the block goes in its UTF-8 view.
struct LineToConvert {
    // The Log Line's bytes in the block, without its line feed.
    std::string_view bytes;
    // Its UTF-8, when it had to be decoded first: to hide its ANSI color
    // sequences, or because it is not valid in its encoding.
    std::optional<QByteArray> decoded;
    std::size_t utf8Size{};
    std::size_t utf8Offset{};
};

} // namespace

logsquirl::vector<std::string_view> RawLines::buildUtf8View() const
{
    logsquirl::vector<std::string_view> lines;
    if ( this->endOfLines.empty() || textDecoder.decoder == nullptr ) {
        return lines;
    }

    // However a Log Line gets into the view, it is trimmed to its text where it
    // is split off: a Search matches it as it is displayed (#522).
    const auto encoding = directEncodingOf( textDecoder.encodingParams );
    const auto codeUnitWidth = isUtf16( encoding ) ? 2 : 1;

    // The known line ends split the block only when they lie in it, on code
    // unit boundaries; a UTF-16 Log Line cut in the middle of a code unit is
    // decoded as a whole block is.
    auto lineEndsSplitTheBlock = encoding != DirectEncoding::None;
    qint64 previousLineEnd = 0;
    for ( const auto lineEnd : endOfLines ) {
        if ( lineEnd < previousLineEnd || lineEnd > logsquirl::ssize( buffer )
             || ( lineEnd - previousLineEnd ) % codeUnitWidth != 0 ) {
            lineEndsSplitTheBlock = false;
            break;
        }
        previousLineEnd = lineEnd;
    }

    try {
        lines.reserve( endOfLines.size() );

        // Every ANSI color sequence starts with the escape character, and in
        // each of these encodings an escape code unit has an escape byte.
        const auto hidesAnsiColorSequences
            = hideAnsiColorSequences
              && std::find( buffer.begin(), buffer.end(), '\x1B' ) != buffer.end();

        // Log Lines are converted one by one only where that is faster than
        // the block (#291): a UTF-8 block is searched where it was read; a
        // block without ANSI color sequences to hide, valid in its encoding,
        // is converted at once; a UTF-16 block whose sequences are hidden is
        // decoded at once, which removes them in one pass.
        const auto convertsLineByLine
            = lineEndsSplitTheBlock && ( !isUtf16( encoding ) || !hidesAnsiColorSequences );
        if ( convertsLineByLine && encoding != DirectEncoding::Utf8 && !hidesAnsiColorSequences
             && convertValidBlock(
                 std::string_view( buffer.data(), static_cast<std::size_t>( endOfLines.back() ) ),
                 encoding, endOfLines.size(), utf8Data_, lines ) ) {
            // Converted as a whole.
        }
        else if ( convertsLineByLine ) {
            std::vector<LineToConvert> toConvert( endOfLines.size() );
            std::size_t utf8Size = 0;
            std::size_t lineStart = 0;
            for ( std::size_t index = 0; index < endOfLines.size(); ++index ) {
                const auto lineEnd = static_cast<std::size_t>( endOfLines[ index ] );
                auto& line = toConvert[ index ];
                line.bytes = withoutLineFeed(
                    std::string_view( buffer.data() + lineStart, lineEnd - lineStart ), encoding );
                lineStart = lineEnd;

                const auto hasAnsiColorSequences
                    = hidesAnsiColorSequences && containsEscape( line.bytes, encoding );
                if ( hasAnsiColorSequences || !isValid( line.bytes, encoding ) ) {
                    auto text = decode( line.bytes, encoding );
                    if ( hasAnsiColorSequences ) {
                        removeAnsiColorSequences( text );
                    }
                    line.decoded = toUtf8( text );
                    line.utf8Size = static_cast<std::size_t>( line.decoded->size() );
                }
                else {
                    line.utf8Size = utf8SizeOf( line.bytes, encoding );
                }

                // A UTF-8 Log Line is searched as it was read, where it was read.
                if ( line.decoded || encoding != DirectEncoding::Utf8 ) {
                    line.utf8Offset = utf8Size;
                    utf8Size += line.utf8Size;
                }
            }

            utf8Data_ = QByteArray( static_cast<qsizetype>( utf8Size ), Qt::Uninitialized );
            for ( auto& line : toConvert ) {
                if ( line.decoded ) {
                    std::copy( line.decoded->cbegin(), line.decoded->cend(),
                               utf8Data_.data() + line.utf8Offset );
                    lines.push_back( trimToLogLineText( std::string_view(
                        utf8Data_.constData() + line.utf8Offset, line.utf8Size ) ) );
                }
                else if ( encoding == DirectEncoding::Utf8 ) {
                    lines.push_back( trimToLogLineText( line.bytes ) );
                }
                else {
                    const auto written
                        = convertToUtf8( line.bytes, encoding, utf8Data_.data() + line.utf8Offset );
                    lines.push_back( trimToLogLineText(
                        std::string_view( utf8Data_.constData() + line.utf8Offset, written ) ) );
                }
            }
        }
        else {
            auto utf16Data = textDecoder.decode( buffer.data(), logsquirl::isize( buffer ) );
            if ( hideAnsiColorSequences ) {
                removeAnsiColorSequences( utf16Data );
            }
            utf8Data_ = toUtf8( utf16Data );

            std::string_view wholeString( utf8Data_.constData(),
                                          static_cast<std::size_t>( utf8Data_.size() ) );
            auto nextLineFeed = wholeString.find( '\n' );
            while ( nextLineFeed != std::string_view::npos ) {
                lines.push_back( trimToLogLineText( wholeString.substr( 0, nextLineFeed ) ) );
                wholeString.remove_prefix( nextLineFeed + 1 );
                nextLineFeed = wholeString.find( '\n' );
            }

            if ( !wholeString.empty() ) {
                lines.push_back( trimToLogLineText( wholeString ) );
            }
        }

    } catch ( const std::exception& e ) {
        LOG_ERROR << "failed to transform lines to utf8 " << e.what();
        lines.clear();
        lines.resize( this->endOfLines.size() );
    }

    return lines;
}
