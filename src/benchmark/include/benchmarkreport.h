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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "processclock.h"

namespace logsquirl::benchmark {

// A summary of many durations of one kind, in milliseconds: the latency of
// every keystroke of a QuickFind, the time of every paint of a scroll. The
// percentiles are nearest-rank: p99 of 100 samples is the 99th smallest.
struct Distribution {
    std::size_t count = 0;
    double min = 0.0;
    double p50 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
    double mean = 0.0;

    static Distribution of( std::vector<double> milliseconds );

    // {"count", "min_ms", "p50_ms", "p99_ms", "max_ms", "mean_ms"}; only
    // "count" when there is no sample.
    QJsonObject toJson() const;
};

// What one benchmark run reports: one JSON object, written once at the end of
// the run (#666). BUILD.md, "Benchmark mode", documents every field and its
// unit.
//
// A scenario reports what happened through event() -- a name and the moment it
// happened, optionally with data of its own -- and what it concluded through
// setResult(). Neither needs a change here for a new scenario: the names and
// the data are the scenario's.
class BenchmarkReport {
public:
    // Raised when a field is renamed, removed or changes its meaning or unit;
    // a new field does not raise it. Readers check it before they read.
    static constexpr int FormatVersion = 1;
    static constexpr const char* FormatName = "logsquirl-benchmark";

    BenchmarkReport( QString scenario, ProcessClock clock );

    // The moment the scenario's measured part begins; every event is also
    // timed since this moment. Until it is marked, the scenario starts when
    // the report is made.
    void markScenarioStart( Clock::time_point moment = Clock::now() );

    // Something happened now, or at `moment`. Names are lower_snake_case,
    // such as `first_log_line_displayed`.
    void event( const QString& name, const QJsonObject& data = {} );
    void eventAt( const QString& name, Clock::time_point moment, const QJsonObject& data = {} );

    bool hasEvent( const QString& name ) const;
    // When the first event of that name happened, in milliseconds since the
    // scenario started.
    std::optional<double> millisecondsSinceScenarioStart( const QString& name ) const;

    // What the scenario concluded, under `key` in "results".
    void setResult( const QString& key, const QJsonValue& value );

    // An option the run was started with (--benchmark-option key=value).
    void setOption( const QString& key, const QString& value );
    void addLogFile( const QString& path, qint64 sizeBytes );
    // Which LogSquirl ran, and where.
    void setApplication( const QJsonObject& application );
    void setPlatform( const QJsonObject& platform );

    // The run did not do what it measures; the first reason is reported.
    void fail( const QString& reason );
    bool failed() const;

    // The whole report. The peak RSS is read by the caller at the end of the
    // run; none leaves it out.
    QJsonObject toJson( std::optional<std::int64_t> peakResidentBytes ) const;

private:
    struct Event {
        QString name;
        Clock::time_point moment;
        QJsonObject data;
    };

    QString scenario_;
    ProcessClock clock_;
    Clock::time_point scenarioStart_;
    std::vector<Event> events_;
    QJsonObject results_;
    QJsonObject options_;
    QJsonArray logFiles_;
    QJsonObject application_;
    QJsonObject platform_;
    std::optional<QString> failure_;
};

} // namespace logsquirl::benchmark
