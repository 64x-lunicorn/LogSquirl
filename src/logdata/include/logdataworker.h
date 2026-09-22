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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <qthreadpool.h>
#include <utility>
#include <variant>

#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QTextCodec>

namespace indexing_blocks {
struct IndexingBlock;
struct BlockReading;
class IndexingBlockPool;
} // namespace indexing_blocks

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

// The digests an indexing run takes of the bytes it indexes, going on from
// those of the bytes indexed before it. The run builds them outside the index
// lock, as it parses its blocks, and publishes what they come to.
struct IndexedBytesDigests {
    // Of every byte indexed; nothing without a full digest.
    std::optional<FileDigest> full;
    HeaderAndTailDigests headerAndTail;
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

    // Atomically add a block parsed beforehand to all the existing
    // indexing data: blockSize bytes more are indexed, and fullDigest, when
    // there is one, is the digest of every byte indexed so far.
    void addAll( qint64 blockSize, LineLength length, const FastLinePositionArray& linePosition,
                 QTextCodec* encoding, std::optional<quint64> fullDigest )
    {
        data_->addAll( blockSize, length, linePosition, encoding, fullDigest );
    }

    // Hands the digests of the bytes indexed so far to an indexing run, which
    // goes on building them outside the lock, and hands them back with
    // returnDigests() once it is done. Clearing the indexing data meanwhile
    // leaves digests of nothing to go on from.
    IndexedBytesDigests takeDigests()
    {
        return data_->takeDigests();
    }

    void returnDigests( IndexedBytesDigests&& digests )
    {
        data_->returnDigests( std::move( digests ) );
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
    void addAll( qint64 blockSize, LineLength length, const FastLinePositionArray& linePosition,
                 QTextCodec* encoding, std::optional<quint64> fullDigest );

    IndexedBytesDigests takeDigests();
    void returnDigests( IndexedBytesDigests&& digests );

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
    // Where the Log Line running out of the blocks stitched so far starts,
    // and how many spaces its tabs widen it by so far.
    OffsetInFile::UnderlyingType pos{};
    std::int64_t additional_spaces{};
    std::int64_t max_length{};
    OffsetInFile::UnderlyingType file_size{};

    QTextCodec* encodingGuess{};
    QTextCodec* fileTextCodec{};

    // Taken from the indexing data when the run starts, and built on as
    // blocks are parsed.
    std::optional<IndexedBytesDigests> digests;
};

using OperationResult = std::variant<bool, MonitoredFileStatus>;

struct CachedIndex;

// How an indexing run cuts its Log File into blocks, and who watches it do
// so. Handed to the operation when it is built, and not a Settings Policy:
// none of it comes from the settings store, and the shipped values are the
// defaults below. Every run the application starts takes the plan as it is.
struct IndexingBlockPlan {
    // The size of the blocks a Log File is read and parsed in. Smaller
    // blocks put more of them in flight at once for the same read buffer,
    // so more cores parse in parallel (#339); it is unrelated to the
    // encoding-detection sample, the header and tail digests and the Index
    // Cache resume check, which stay at logdataworker.cpp's DigestBlockSize
    // regardless of this value.
    static constexpr qint64 DefaultBlockSize = 1 * 1024 * 1024;

    // Only tests plan another block size than the default, tiny ones, so
    // that many Log Lines cross from one block into the next (#290).
    qint64 blockSize = DefaultBlockSize;

    // Called once, when the pass over the Log File is done, with how many
    // block buffers it allocated: they are reused from one block to the
    // next, and no more are allocated than the read buffer holds. The seam
    // belongs to whoever watches a run -- the tests of the read buffer do
    // -- and is empty in the plans the application makes, which watch
    // nothing.
    std::function<void( qint64 )> blockBuffersAllocated;
};

class IndexOperation : public QObject {
    Q_OBJECT
public:
    // The Indexing Policy is copied in, once, when the operation is built:
    // everything this run reads about indexing is fixed for its duration,
    // so the options dialog writing a setting from the UI thread while the
    // pass over the Log File is in flight cannot be observed by it. A
    // changed setting takes effect on the next run. The block plan is fixed
    // the same way, and defaulted: the application never plans another one.
    IndexOperation( const QString& fileName, const std::shared_ptr<IndexingData>& indexingData,
                    AtomicFlag& interruptRequest, IndexingPolicy indexingPolicy,
                    IndexingBlockPlan blockPlan = {} )
        : fileName_( fileName )
        , indexing_data_( indexingData )
        , interruptRequest_( interruptRequest )
        , indexingPolicy_( indexingPolicy )
        , blockPlan_( std::move( blockPlan ) )
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

    // Returns the total size indexed
    // Modify the passed linePosition and maxLength
    void doIndex( OffsetInFile initialPosition );

    QString fileName_;
    std::shared_ptr<IndexingData> indexing_data_;
    AtomicFlag& interruptRequest_;
    const IndexingPolicy indexingPolicy_;

private:
    void guessEncoding( const char* bytes, std::size_t size, IndexingState& state ) const;

    struct HeaderAndTail {
        // Nothing when the header recorded already is a whole block, which
        // appending cannot change.
        std::optional<RangeDigest> header;
        RangeDigest tail;
    };

    HeaderAndTail recordHeaderAndTail( QFile& file, qint64 end, HeaderAndTailDigests& digests,
                                       bool hasWholeBlockHeader ) const;

    // The next block of the file for the indexing graph, from the pool, with
    // the time spent reading it added to ioDuration; nothing once the file is
    // read, reading fails or the indexing is interrupted. The encoding is
    // detected from the first block.
    indexing_blocks::IndexingBlock* readNextBlock( QFile& file,
                                                   indexing_blocks::BlockReading& reading,
                                                   indexing_blocks::IndexingBlockPool& pool,
                                                   IndexingState& state,
                                                   std::chrono::microseconds& ioDuration );
    // Stitches a block parsed on its own to the blocks before it and
    // publishes it to the indexing data, in file order. Only publishing takes
    // the exclusive index lock: reading Log Lines waits for no more than the
    // block's offsets being appended.
    void indexNextBlock( IndexingState& state, const indexing_blocks::IndexingBlock& block );

    std::atomic<qint64> bytesIndexed_{ 0 };
    const IndexingBlockPlan blockPlan_;
};

// What asked for a Log File to be indexed in full, which decides how closely
// an Index the Index Cache hands out is checked against it (#337).
enum class FullIndexRequest {
    // A Log File is opened, or one being followed changed in the bytes it was
    // indexed from. A cached Index is checked by its header and tail, which
    // costs the same however large the Log File is.
    Automatic,
    // The user asked for the Log File to be read again. A cached Index is
    // then checked by the digest of every byte it was built from, so that a
    // Log File rewritten in place with the same size is noticed even where
    // its modification time is coarse or written late (#337).
    ExplicitReload,
};

class FullIndexOperation : public IndexOperation {
    Q_OBJECT
public:
    FullIndexOperation( const QString& fileName, const std::shared_ptr<IndexingData>& indexingData,
                        AtomicFlag& interruptRequest, IndexingPolicy indexingPolicy,
                        FullIndexRequest request = FullIndexRequest::Automatic,
                        QTextCodec* forcedEncoding = nullptr, IndexingBlockPlan blockPlan = {} )
        : IndexOperation( fileName, indexingData, interruptRequest, indexingPolicy,
                          std::move( blockPlan ) )
        , request_( request )
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

    // How closely a cached Index is checked against the Log File.
    DigestCoverage cachedIndexCoverage() const;

    FullIndexRequest request_;
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
    // signals as it progresses. What asked for it decides how closely a
    // cached Index is checked against the Log File (#337).
    void indexAll( QTextCodec* forcedEncoding = nullptr,
                   FullIndexRequest request = FullIndexRequest::Automatic );
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
