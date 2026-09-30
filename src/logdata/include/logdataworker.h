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

#include <functional>
#include <memory>

#include <QObject>
#include <QString>

#include "indexjob.h"
#include "indexjobrunner.h"
#include "loadingstatus.h"

// The Index of a Log File as indexing builds it, safe to read while it does.
// What it holds, how it is locked and the index operations that build it are
// the engine's own, in a private header beside the worker's implementation
// (#646).
class IndexingData;

// Runs the index jobs of one Log File through a Background Run, one at a
// time, which keeps the Log File's reader attached for exactly as long as each
// run lasts and reports each run finished once. Which index job runs next is
// the job rule's, and up to whoever hands the jobs in: the operation queue,
// whose runner port this worker implements (#550).
class LogDataWorker : public QObject, public IndexJobRunner {
    Q_OBJECT

public:
    // What stays open while an index run reads the Log File. Either may be
    // empty.
    struct Reader {
        std::function<void()> attach;
        std::function<void()> detach;
    };

    // Pass a pointer to the IndexingData (initially empty)
    // This object will change it when indexing (IndexingData must be thread safe!)
    // The Indexing Policy is what this worker knows about the settings: it
    // reads none itself.
    LogDataWorker( const std::shared_ptr<IndexingData>& indexing_data,
                   const IndexingPolicy& indexingPolicy, Reader reader = {} );
    // Shuts the Background Run down: the run in flight is interrupted and
    // waited for as a Search is, and nothing is reported any more.
    ~LogDataWorker() override;

    LogDataWorker( const LogDataWorker& ) = delete;
    LogDataWorker& operator=( const LogDataWorker& ) = delete;

    LogDataWorker( LogDataWorker&& ) = delete;
    LogDataWorker& operator=( LogDataWorker&& ) = delete;

    // Starts running the index job on the worker's thread and returns once it
    // has started. A run still in flight is superseded by it: whoever hands
    // the jobs in (the job rule) starts one only once the one before is
    // reported finished. Its
    // progress and its end are sent as the signals below, on the thread this
    // worker lives on. Nothing runs for no job.
    void run( const IndexJob& job ) override;

    // Replaces the Indexing Policy used by the runs requested from now on.
    // A run already in flight keeps the Policy it was started with.
    void setIndexingPolicy( const IndexingPolicy& indexingPolicy ) override;

    // Interrupts the index run in flight, if any. Does not wait for it: it is
    // reported finished, Interrupted, once it has stopped.
    void interrupt() override;

Q_SIGNALS:
    // Sent during the indexing process to signal progress
    // percent being the percentage of completion.
    void indexingProgressed( int percent );
    // Sent when an Attach, a Full or a Partial is finished, signals the
    // client to copy the new data back. failure describes a Failed status.
    void indexingFinished( LoadingStatus status, const QString& failure );

    // Sent when a Check is finished, signals the client to copy the new data
    // back. failure is not empty when the check failed.
    void checkFileChangesFinished( MonitoredFileStatus status, const QString& failure );

private:
    // The Background Run the index runs go through, defined with the worker.
    class IndexRun;

    // Starts a new full indexing of the file. What asked for it decides how
    // closely a cached Index is checked against the Log File (#337).
    void indexAll( const TextEncoding* forcedEncoding, FullIndexRequest request );
    // Starts a partial indexing, at the end of the file as indexed.
    void indexAdditionalLines();
    void checkFileChanges();

    // The Log File attached, taken by every run as it is started. Only on the
    // thread this worker lives on, like everything below but the run itself.
    QString fileName_;

    // Pointer to the owner's indexing data (we modify it)
    std::shared_ptr<IndexingData> indexing_data_;

    // Declared last, so it shuts down -- and no run touches the indexing data
    // any more -- before anything else here is destroyed.
    std::unique_ptr<IndexRun> run_;
};

#endif
