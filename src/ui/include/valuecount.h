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
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QFuture>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QVector>

#include "linetypes.h"

class AbstractLogData;
class LogFormatDefinition;

// How many distinct values a Value Count keeps. Beyond it the count stops and
// says there are too many, rather than showing a partial or approximate list.
constexpr size_t MaxDistinctValues = 100'000;

// One value and how often it occurs.
struct ValueCountEntry {
    QString value;
    uint64_t count = 0;
};

// A Value Count: the values in order of count, the most frequent first and
// equal counts by value, and how many Log Lines were counted.
struct ValueCountResult {
    QVector<ValueCountEntry> entries;
    // The Log Lines a value was read from; the share is taken over these.
    uint64_t linesCounted = 0;
    // More than the cap of distinct values occur; entries is then empty.
    bool tooManyDistinctValues = false;

    // The share of the counted Log Lines with this count, in percent.
    double sharePercent( uint64_t count ) const;
};

// The value a Log Line has, or nothing when the Log Line is not counted.
using ValueOfLine = std::function<std::optional<QString>( const QString& line )>;

// The value of a Log Format field. Log Lines the format does not match are not
// counted. The format is copied.
ValueOfLine fieldValueOf( const LogFormatDefinition& format, const QString& fieldName );

// The text a capture group of the regexp matches. Log Lines the regexp does not
// match, and those where the group matches nothing, are not counted.
ValueOfLine captureGroupValueOf( const QRegularExpression& regexp, int group );

// The number of capture groups of a regexp: 0 for one that is not valid.
int captureGroupCount( const QRegularExpression& regexp );

// Counts the values of the Log Lines [first, first + count) of logData,
// reading them in chunks. Returns nothing when cancel was set before it was
// done. linesDone counts the Log Lines read so far. Safe off the UI thread.
std::optional<ValueCountResult> countValues( const AbstractLogData& logData, LineNumber first,
                                             LinesCount count, const ValueOfLine& valueOf,
                                             size_t maxDistinctValues,
                                             const std::atomic<bool>& cancel,
                                             std::atomic<uint64_t>& linesDone );

// A Value Count taken on a worker thread. It is a snapshot: only the Log Lines
// there are when it starts are counted. Cancelling never waits for the worker,
// and what a cancelled count finds is discarded.
class ValueCounter : public QObject {
    Q_OBJECT

public:
    explicit ValueCounter( QObject* parent = nullptr );
    // Cancels the count and waits for its worker to stop, so that it does not
    // outlive the log data it reads.
    ~ValueCounter() override;

    ValueCounter( const ValueCounter& ) = delete;
    ValueCounter& operator=( const ValueCounter& ) = delete;

    // Counts the values of every Log Line of logData. A count that runs is
    // cancelled first.
    void start( std::shared_ptr<const AbstractLogData> logData, ValueOfLine valueOf,
                size_t maxDistinctValues = MaxDistinctValues );
    // Stops the count without waiting for it; finished() is not sent.
    void cancel();

    bool isRunning() const;
    // How far the count got, in percent.
    int progress() const;

Q_SIGNALS:
    // The count ran to its end.
    void finished( const ValueCountResult& result );

private:
    struct Job;

    void onFinished( const std::shared_ptr<Job>& job );

    std::shared_ptr<Job> running_;
    // Every count whose worker has not finished, cancelled ones too.
    std::vector<std::shared_ptr<Job>> jobs_;
};
