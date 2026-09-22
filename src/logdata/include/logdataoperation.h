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
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#ifndef LOGDATAOPERATION_H
#define LOGDATAOPERATION_H

#include <variant>

#include "logdataworker.h"

#include "synchronization.h"

// This class models an indexing operation.
// It exists to permit LogData to delay the operation if another
// one is ongoing (operations are asynchronous)
class LogDataOperation {
public:
    LogDataOperation() = default;
    explicit LogDataOperation( const QString& fileName )
        : filename_( fileName )
    {
    }

    // Permit each child to have its destructor
    virtual ~LogDataOperation() = default;

    void start( LogDataWorker* workerThread ) const
    {
        doStart( *workerThread );
    }
    const QString& getFilename() const
    {
        return filename_;
    }

protected:
    virtual void doStart( LogDataWorker& workerThread ) const = 0;
    QString filename_;
};

// Attaching a new file (change name + full index)
class AttachOperation : public LogDataOperation {
public:
    // The default Encoding comes from the File Access Policy the LogData
    // was built with; negative means "detect it rather than force one". A
    // forced Encoding, handed over by a reload that arrived while the
    // Attach was waiting, is indexed under instead.
    AttachOperation( const QString& fileName, int defaultEncodingMib,
                     QTextCodec* forcedEncoding = nullptr )
        : LogDataOperation( fileName )
        , defaultEncodingMib_( defaultEncodingMib )
        , forcedEncoding_( forcedEncoding )
    {
    }

    int defaultEncodingMib() const
    {
        return defaultEncodingMib_;
    }
    QTextCodec* forcedEncoding() const
    {
        return forcedEncoding_;
    }

protected:
    void doStart( LogDataWorker& workerThread ) const override;

private:
    int defaultEncodingMib_;
    QTextCodec* forcedEncoding_;
};

// Reindexing the current file. What asked for it is carried through to the
// indexing run, which checks a cached Index the more closely the more the
// user asked for the Log File to be read again (#337).
class FullReindexOperation : public LogDataOperation {
public:
    explicit FullReindexOperation( FullIndexRequest request = FullIndexRequest::Automatic,
                                   QTextCodec* forcedEncoding = nullptr )
        : request_( request )
        , forcedEncoding_( forcedEncoding )
    {
    }

    FullIndexRequest request() const
    {
        return request_;
    }
    QTextCodec* forcedEncoding() const
    {
        return forcedEncoding_;
    }

protected:
    void doStart( LogDataWorker& workerThread ) const override;

private:
    FullIndexRequest request_;
    QTextCodec* forcedEncoding_;
};

// Indexing part of the current file (from fileSize)
class PartialReindexOperation : public LogDataOperation {
protected:
    void doStart( LogDataWorker& workerThread ) const override;
};

// Checking the file for changes on disk: growth, truncation or replacement
class CheckDataChangesOperation : public LogDataOperation {
protected:
    void doStart( LogDataWorker& workerThread ) const override;
};

// An index job waiting to run, or nothing.
using IndexJob = std::variant<std::monostate, AttachOperation, FullReindexOperation,
                              PartialReindexOperation, CheckDataChangesOperation>;

// The job rule: of the index job waiting to run and one that arrives while
// another runs, the one that waits from now on. Only one waits, and the
// stronger covers the weaker, strongest first:
//
//     Attach > Full (explicit reload) > Full (automatic) > Check > Partial
//
// - An Attach indexes everything anyway. A reload with a forced Encoding
//   that meets it hands that Encoding to the Attach.
// - A Full reads everything, so it covers a Check and a Partial; an
//   explicit reload checks a cached Index more closely than an automatic
//   Full does (#337), so it is the one kept.
// - A Check covers a Partial: a waiting Partial must not swallow a Check
//   that could find a truncation, and a Check that finds only growth queues
//   the Partial again.
//
// Between two jobs of the same strength the one that arrives wins: it is the
// latest request, a later reload's Encoding among them.
IndexJob waitingIndexJob( IndexJob waiting, IndexJob arriving );

class OperationQueue {
public:
    explicit OperationQueue( std::function<void()> beforeOperationStart );

    void setWorker( std::unique_ptr<LogDataWorker>&& worker );

    // Hands a changed Indexing Policy to the worker, if there is one.
    void setIndexingPolicy( const IndexingPolicy& indexingPolicy );

    void interrupt();
    void shutdown();

    template <typename Op, typename... Args>
    void enqueueOperation( Args&&... args )
    {
        enqueueOperation( Op{ std::forward<Args>( args )... } );
    }

    void finishOperationAndStartNext();

    // Whether the index job running is a Partial, which leaves the Log
    // Lines indexed before it as they were.
    bool isPartialReindexRunning() const;

private:
    void enqueueOperation( IndexJob&& operation );
    void tryStartPendingOperation();

    std::function<void()> beforeOperationStart_;

private:
    mutable Mutex mutex_;

    IndexJob executingOperation_;
    // Decided by waitingIndexJob() whenever another one arrives.
    IndexJob pendingOperation_;

    std::unique_ptr<LogDataWorker> worker_;
};

#endif