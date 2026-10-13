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

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include <QDate>
#include <QFuture>
#include <QObject>
#include <Qt>

#include "linetypes.h"
#include "rowmapping.h"
#include "tablesortkeys.h"

class AbstractLogData;
class LogFormatDefinition;

// What one sort of the Table View's Rows reads and gives (Sort Order): the
// keys of a column for the Log Lines not keyed yet, read as the cells are
// computed, and the Rows in order. Plain code, safe off the UI thread.
struct TableSortRequest {
    // Read off the UI thread: the Log File must outlive the sort, which the
    // TableSorter waits for when told to stop.
    const AbstractLogData* logData = nullptr;
    // The sort's own copy of the Log Format, and the date Timestamps without
    // a year take theirs from, as the model's cells take it.
    std::shared_ptr<const LogFormatDefinition> format;
    QDate modificationDate;
    int column = 0;
    Qt::SortOrder order = Qt::AscendingOrder;
    // How many Log Lines the Log File has: the keys are read for [0,
    // lineCount), the ones not keyed yet.
    LinesCount lineCount;
    // Which Log Lines are Rows, in Log Line order: the Rows of this mapping
    // while the Log File has lineCount Log Lines are what is sorted.
    std::shared_ptr<const RowMapping> base;
    // The keys of the column read so far, for the Log Lines [0, size()); none
    // when no Log Line is keyed yet. Only the Log Lines beyond are read.
    std::shared_ptr<const TableSortKeys> keys;
    // The Rows in the Sort Order so far, which the Rows of base beyond them
    // are merged into: the keys' direction must be the request's, and the
    // Rows must be the first of base's. Null sorts every Row anew.
    std::shared_ptr<const SortedRows> rows;
};

struct TableSortResult {
    int column = 0;
    Qt::SortOrder order = Qt::AscendingOrder;
    // The keys of every Log Line of the request.
    std::shared_ptr<const TableSortKeys> keys;
    // Every Log Line of the request in the Sort Order.
    std::shared_ptr<const SortedRows> rows;
    // Whether the appended Log Lines were merged into the Rows of the
    // request; inserted then says where they landed.
    bool merged = false;
    std::vector<RowRun> inserted;
};

// Sorts the Rows as requested: reads the keys of the Log Lines not keyed yet
// in chunks, then sorts every Log Line, or merges the ones read into the Rows
// given. Returns nothing when cancel was set before it was done. linesDone
// counts the Log Lines read so far.
std::optional<TableSortResult> sortTableRows( const TableSortRequest& request,
                                              const std::atomic<bool>& cancel,
                                              std::atomic<uint64_t>& linesDone );

// A sort of the Table View's Rows on a worker thread, modelled on the Value
// Count's worker: one sort at a time, a new one cancelling the one under way,
// which reports nothing. Cancelling never waits for the worker; stopping
// does, so that no worker outlives the Log File it reads.
class TableSorter : public QObject {
    Q_OBJECT

public:
    explicit TableSorter( QObject* parent = nullptr );
    // Stops every sort and waits for its worker.
    ~TableSorter() override;

    TableSorter( const TableSorter& ) = delete;
    TableSorter& operator=( const TableSorter& ) = delete;

    // Starts the sort; a sort under way is cancelled first.
    void start( TableSortRequest request );
    // Stops the sort under way without waiting for it; finished() is not
    // sent for it.
    void cancel();
    // Cancels, and waits until every worker has stopped: before the Log File
    // the sorts read goes.
    void stopAndWait();

    bool isRunning() const;
    // How far the sort under way got, in percent of the Log Lines it reads.
    int progress() const;

Q_SIGNALS:
    // The sort ran to its end.
    void finished( const TableSortResult& result );

private:
    struct Job;

    void onFinished( const std::shared_ptr<Job>& job );

    std::shared_ptr<Job> running_;
    // Every sort whose worker has not finished, cancelled ones too.
    std::vector<std::shared_ptr<Job>> jobs_;
};
