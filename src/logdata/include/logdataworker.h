/*
 * Copyright (C) 2009, 2010, 2014, 2015 Nicolas Bonnefon and other contributors
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

#ifndef LOGDATAWORKERTHREAD_H
#define LOGDATAWORKERTHREAD_H

#include "containers.h"
#include "linetypes.h"
#include <atomic>
#include <optional>
#include <qthreadpool.h>
#include <variant>

#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QTextCodec>

// No TBB here: the indexing graph is an implementation detail of
// logdataworker.cpp, and TBB is a private dependency of the log data
// library (#168).

#include "atomicflag.h"
#include "filedigest.h"
#include "settingspolicies.h"
#include "synchronization.h"

#include "encodingdetector.h"
#include "headerandtaildigests.h"
#include "indexedhash.h"
#include "linepositionarray.h"
#include "loadingstatus.h"

// A cached Index that indexing goes on from, rather than starting over.
struct ResumedIndex {
    // The cached line positions, their last Log Line already dropped: it may
    // have had no newline yet, and continued since.
    LinePositionArray linePosition;
    // The longest Log Line so far, a lower bound for the whole Log File.
    LineLength maxLength;
    // Where indexing goes on: where the dropped Log Line began.
    OffsetInFile offset;
    // The digest of the bytes before offset, needed only without fast
    // modification detection.
    FileDigest digestBeforeOffset;
    QTextCodec* encoding = nullptr;
    bool fastModificationDetection = true;
};

template <typename Data, typename LockGuard>
class IndexingDataAccessor {
public:
    IndexingDataAccessor( Data data )
        : data_( data )
        , guard_( data->dataMutex_ )
    {
    }

    ~IndexingDataAccessor() = default;

    qint64 getIndexedSize() const
    {
        return data_->getIndexedSize();
    }

    IndexedHash getHash() const
    {
        return data_->getHash();
    }

    // Get the length of the longest line
    LineLength getMaxLength() const
    {
        return data_->getMaxLength();
    }

    // Get the total number of lines
    LinesCount getNbLines() const
    {
        return data_->getNbLines();
    }

    // Get the position (in byte from the beginning of the file)
    // of the end of the passed line.
    OffsetInFile getEndOfLineOffset( LineNumber line ) const
    {
        return data_->getEndOfLineOffset( line );
    }

    logsquirl::vector<OffsetInFile> getEndOfLineOffsets( LineNumber line, LinesCount count ) const
    {
        return data_->getEndOfLineOffsets( line, count );
    }

    // Get the guessed encoding for the content.
    QTextCodec* getEncodingGuess() const
    {
        return data_->getEncodingGuess();
    }

    /// Returns the compressed line position array, or nullptr if using fast storage.
    const LinePositionArray* getCompressedLinePosition() const
    {
        return data_->getCompressedLinePosition();
    }

    void setEncodingGuess( QTextCodec* codec )
    {
        data_->setEncodingGuess( codec );
    }

    QTextCodec* getForcedEncoding() const
    {
        return data_->getForcedEncoding();
    }
    void forceEncoding( QTextCodec* codec )
    {
        return data_->forceEncoding( codec );
    }

    // Atomically add to all the existing
    // indexing data.
    void addAll( const logsquirl::vector<char>& block, LineLength length,
                 const FastLinePositionArray& linePosition, QTextCodec* encoding )
    {
        data_->addAll( block, length, linePosition, encoding );
    }

    void setHeaderHash( quint64 digest, qint64 size )
    {
        data_->hash_.headerSize = size;
        data_->hash_.headerDigest = digest;
    }

    void setTailHash( quint64 digest, qint64 offset, qint64 size )
    {
        data_->hash_.tailSize = size;
        data_->hash_.tailOffset = offset;
        data_->hash_.tailDigest = digest;
    }

    // The Log File is expected to be at least this long once the bytes
    // indexed from now on are added, so their header and tail digests need
    // not be taken of the bytes before its tail.
    void expectLogFileSize( qint64 size )
    {
        data_->headerAndTailDigests_.expectLogFileSize( size );
    }

    // The header and tail digests of the bytes indexed up to end, as taken
    // while they were added; nothing for what they do not cover.
    std::optional<RangeDigest> indexedHeaderDigest( qint64 end ) const
    {
        return data_->headerAndTailDigests_.header( end );
    }

    std::optional<RangeDigest> indexedTailDigest( qint64 end ) const
    {
        return data_->headerAndTailDigests_.tail( end );
    }

    // Takes the header and tail digests of indexed bytes read again from
    // the Log File, for those the digests taken while adding do not cover.
    void digestIndexedBytesAgain( qint64 offset, const char* data, qint64 size )
    {
        data_->headerAndTailDigests_.add( offset, data, size );
    }

    // The modification time the Log File had when its bytes were last
    // indexed, or checked in full; invalid when that is not known.
    QDateTime getIndexedModificationTime() const
    {
        return data_->indexedModificationTime_;
    }

    void setIndexedModificationTime( const QDateTime& modificationTime )
    {
        data_->indexedModificationTime_ = modificationTime;
    }

    int getProgress() const
    {
        return data_->getProgress();
    }

    void setProgress( int progress )
    {
        data_->setProgress( progress );
    }

    // Completely clear the indexing data. The Indexing Policy decides how
    // the line positions are stored and how modification is detected, and
    // is passed in by the operation doing the clearing rather than held
    // here: the operation's copy is fixed for the whole run, so a setting
    // changed mid-run cannot be picked up half way through it.
    void clear( const IndexingPolicy& policy )
    {
        data_->clear( policy );
    }

    /// Load index data from a CachedIndex (disk cache).
    void loadFromCache( LinePositionArray&& linePosition, LineLength maxLength,
                        const IndexedHash& hash, QTextCodec* encoding,
                        bool fastModificationDetection )
    {
        data_->loadFromCache( std::move( linePosition ), maxLength, hash, encoding,
                              fastModificationDetection );
    }

    /// Start from a cached Index, so that indexing goes on from its offset.
    void resumeFromCache( ResumedIndex&& resumed )
    {
        data_->resumeFromCache( std::move( resumed ) );
    }

    size_t allocatedSize() const
    {
        return data_->allocatedSize();
    }

private:
    Data data_;
    LockGuard guard_;
};

// This class is a thread-safe set of indexing data.
class IndexingData {
public:
    using ConstAccessor = IndexingDataAccessor<const IndexingData*, SharedLock>;
    using MutateAccessor = IndexingDataAccessor<IndexingData*, UniqueLock>;

    IndexingData();

private:
    qint64 getIndexedSize() const;

    IndexedHash getHash() const;

    // Get the length of the longest line
    LineLength getMaxLength() const;

    // Get the total number of lines
    LinesCount getNbLines() const;

    // Get the position (in byte from the beginning of the file)
    // of the end of the passed line.
    OffsetInFile getEndOfLineOffset( LineNumber line ) const;
    logsquirl::vector<OffsetInFile> getEndOfLineOffsets( LineNumber line, LinesCount count ) const;

    // Get the guessed encoding for the content.
    QTextCodec* getEncodingGuess() const;
    void setEncodingGuess( QTextCodec* codec );

    QTextCodec* getForcedEncoding() const;
    void forceEncoding( QTextCodec* codec );

    // Atomically add to all the existing
    // indexing data.
    void addAll( const logsquirl::vector<char>& block, LineLength length,
                 const FastLinePositionArray& linePosition, QTextCodec* encoding );

    // Completely clear the indexing data.
    void clear( const IndexingPolicy& policy );

    // Load index data from a CachedIndex (disk cache).
    void loadFromCache( LinePositionArray&& linePosition, LineLength maxLength,
                        const IndexedHash& hash, QTextCodec* encoding,
                        bool fastModificationDetection );

    // Start from a cached Index, going on from its offset.
    void resumeFromCache( ResumedIndex&& resumed );

    /// Returns the compressed line position array, or nullptr if using fast (uncompressed) storage.
    const LinePositionArray* getCompressedLinePosition() const;

    size_t allocatedSize() const;

    int getProgress() const;
    void setProgress( int progress );

private:
    mutable SharedMutex dataMutex_;

    using LinePositionArrayType = std::variant<LinePositionArray, FastLinePositionArray>;
    LinePositionArrayType linePosition_;

    LineLength maxLength_;

    int progress_{};

    FileDigest hashBuilder_;
    IndexedHash hash_;
    HeaderAndTailDigests headerAndTailDigests_;
    QDateTime indexedModificationTime_;

    QTextCodec* encodingGuess_{};
    QTextCodec* encodingForced_{};

    bool useFastModificationDetection_ = true;

    friend ConstAccessor;
    friend MutateAccessor;
};

struct IndexingState {

    EncodingParameters encodingParams;
    OffsetInFile::UnderlyingType pos{};
    LineLength::UnderlyingType max_length{};
    LineLength::UnderlyingType additional_spaces{};
    OffsetInFile::UnderlyingType end{};
    OffsetInFile::UnderlyingType file_size{};

    QTextCodec* encodingGuess{};
    QTextCodec* fileTextCodec{};
};

using OperationResult = std::variant<bool, MonitoredFileStatus>;

struct CachedIndex;

class IndexOperation : public QObject {
    Q_OBJECT
public:
    // The Indexing Policy is copied in, once, when the operation is built:
    // everything this run reads about indexing is fixed for its duration,
    // so the options dialog writing a setting from the UI thread while the
    // pass over the Log File is in flight cannot be observed by it. A
    // changed setting takes effect on the next run.
    IndexOperation( const QString& fileName, const std::shared_ptr<IndexingData>& indexingData,
                    AtomicFlag& interruptRequest, IndexingPolicy indexingPolicy )
        : fileName_( fileName )
        , indexing_data_( indexingData )
        , interruptRequest_( interruptRequest )
        , indexingPolicy_( indexingPolicy )
    {
    }

    // Run the indexing operation, returns true if it has been done
    // and false if it has been cancelled (results not copied). An exception
    // escaping the run is a failure of the engine: it is reported through
    // the operation's finishing signal as a failed status with a
    // description, never by opening a dialog, and never thrown further.
    OperationResult run();

    // How many bytes of the Log File this operation has read to index them.
    qint64 bytesIndexed() const
    {
        return bytesIndexed_.load();
    }

Q_SIGNALS:
    void indexingProgressed( int );
    // failure describes what went wrong when status is Failed, and is empty
    // otherwise.
    void indexingFinished( LoadingStatus status, const QString& failure );
    // failure is not empty when checking the Log File failed; the status is
    // then Truncated, so the Log File is indexed again from the start.
    void fileCheckFinished( MonitoredFileStatus status, const QString& failure );

protected:
    // The run itself, which run() reports the failure of.
    virtual OperationResult doRun() = 0;

    // Reports that the run failed as described, and returns what run()
    // returns then. By default the Index is dropped and indexing reported
    // Failed.
    virtual OperationResult reportFailure( const QString& failure );

    using BlockBuffer = logsquirl::vector<char>;
    using BlockData = std::pair<OffsetInFile::UnderlyingType, BlockBuffer*>;

    // Returns the total size indexed
    // Modify the passed linePosition and maxLength
    void doIndex( OffsetInFile initialPosition );

    QString fileName_;
    std::shared_ptr<IndexingData> indexing_data_;
    AtomicFlag& interruptRequest_;
    const IndexingPolicy indexingPolicy_;

private:
    FastLinePositionArray parseDataBlock( OffsetInFile::UnderlyingType blockBegining,
                                          const BlockBuffer& block, IndexingState& state ) const;

    void guessEncoding( const BlockBuffer& block, IndexingData::MutateAccessor& scopedAccessor,
                        IndexingState& state ) const;

    void recordHeaderAndTail( QFile& file, qint64 end,
                              IndexingData::MutateAccessor& scopedAccessor ) const;

    // The next block of the file for the indexing graph, with the time spent
    // reading it added to ioDuration; nothing once the file is read, reading
    // fails or the indexing is interrupted.
    std::optional<BlockData> readNextBlock( QFile& file, std::chrono::microseconds& ioDuration );
    void indexNextBlock( IndexingState& state, const BlockData& blockData );

    std::atomic<qint64> bytesIndexed_{ 0 };
};

class FullIndexOperation : public IndexOperation {
    Q_OBJECT
public:
    FullIndexOperation( const QString& fileName, const std::shared_ptr<IndexingData>& indexingData,
                        AtomicFlag& interruptRequest, IndexingPolicy indexingPolicy,
                        QTextCodec* forcedEncoding = nullptr )
        : IndexOperation( fileName, indexingData, interruptRequest, indexingPolicy )
        , forcedEncoding_( forcedEncoding )
    {
    }

protected:
    OperationResult doRun() override;

private:
    // Sets up the indexing data to go on from a cached Index built when the
    // Log File was shorter, and reports the progress already made. Returns
    // false, leaving the indexing data alone, when that would not give the
    // Index a full re-index builds.
    bool resumeFrom( CachedIndex& cached, qint64 fileSize );

    QTextCodec* forcedEncoding_;
};

class PartialIndexOperation : public IndexOperation {
    Q_OBJECT
public:
    PartialIndexOperation( const QString& fileName,
                           const std::shared_ptr<IndexingData>& indexingData,
                           AtomicFlag& interruptRequest, IndexingPolicy indexingPolicy )
        : IndexOperation( fileName, indexingData, interruptRequest, indexingPolicy )
    {
    }

protected:
    OperationResult doRun() override;
};

class CheckFileChangesOperation : public IndexOperation {
    Q_OBJECT
public:
    CheckFileChangesOperation( const QString& fileName,
                               const std::shared_ptr<IndexingData>& indexingData,
                               AtomicFlag& interruptRequest, IndexingPolicy indexingPolicy )
        : IndexOperation( fileName, indexingData, interruptRequest, indexingPolicy )
    {
    }

protected:
    OperationResult doRun() override;
    OperationResult reportFailure( const QString& failure ) override;

private:
    MonitoredFileStatus doCheckFileChanges();
};

class LogDataWorker : public QObject {
    Q_OBJECT

public:
    // Pass a pointer to the IndexingData (initially empty)
    // This object will change it when indexing (IndexingData must be thread safe!)
    // The Indexing Policy is what this worker knows about the settings: it
    // reads none itself.
    LogDataWorker( const std::shared_ptr<IndexingData>& indexing_data,
                   const IndexingPolicy& indexingPolicy );
    ~LogDataWorker() noexcept override;

    LogDataWorker( const LogDataWorker& ) = delete;
    LogDataWorker& operator=( const LogDataWorker&& ) = delete;

    LogDataWorker( LogDataWorker&& ) = delete;
    LogDataWorker& operator=( LogDataWorker&& ) = delete;

    // Attaches to a file on disk. Attaching to a non existant file
    // will work, it will just appear as an empty file.
    void attachFile( const QString& fileName );
    // Instructs the thread to start a new full indexing of the file, sending
    // signals as it progresses.
    void indexAll( QTextCodec* forcedEncoding = nullptr );
    // Instructs the thread to start a partial indexing (starting at
    // the end of the file as indexed).
    void indexAdditionalLines();

    void checkFileChanges();

    // Replaces the Indexing Policy used by the runs requested from now on.
    // A run already in flight keeps the Policy it was started with.
    void setIndexingPolicy( const IndexingPolicy& indexingPolicy );

    // Interrupts the indexing if one is in progress
    void interrupt();

Q_SIGNALS:
    // Sent during the indexing process to signal progress
    // percent being the percentage of completion.
    void indexingProgressed( int percent );
    // Sent when indexing is finished, signals the client
    // to copy the new data back. failure describes a Failed status.
    void indexingFinished( LoadingStatus status, const QString& failure );

    // Sent when check file is finished, signals the client
    // to copy the new data back. failure is not empty when the check failed.
    void checkFileChangesFinished( MonitoredFileStatus status, const QString& failure );

private Q_SLOTS:
    void onIndexingFinished( LoadingStatus status, const QString& failure );
    void onCheckFileFinished( MonitoredFileStatus result, const QString& failure );

private:
    OperationResult connectSignalsAndRun( IndexOperation* operationRequested );

    // Mutex to wait for operations
    QThreadPool operationsPool_;
    Mutex operationsMutex_;
    AtomicFlag interruptRequest_;

    QString fileName_;

    // Read and written under operationsMutex_, and copied into every
    // operation as it is requested, so that a run never reads it from the
    // pool thread while the UI thread is replacing it.
    IndexingPolicy indexingPolicy_;

    // Pointer to the owner's indexing data (we modify it)
    std::shared_ptr<IndexingData> indexing_data_;
};

#endif
