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
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <QObject>
#include <QTimer>
#include <QVector>

#include "chartseries.h"
#include "linetypes.h"

class AbstractLogData;

// The points extracted for each series of a chart, in the order of the series,
// each in Log Line order and not yet bucketed.
using ChartRawPoints = QVector<QVector<ChartPoint>>;

// Extracts the points of every series from the Log Lines [first, first + count)
// of logData. Returns nothing when cancel was set before it was done.
// linesDone counts the Log Lines read so far. Safe off the UI thread.
std::optional<ChartRawPoints> extractChartPoints( const AbstractLogData& logData,
                                                  const QVector<ChartSeriesDefinition>& series,
                                                  LineNumber first, LinesCount count,
                                                  const std::atomic<bool>& cancel,
                                                  std::atomic<uint64_t>& linesDone );

// The points one series of a chart shows, kept so that the points of Log Lines
// appended later can be merged in without extracting from the first Log Line.
// A bucketed series keeps the raw points of the buckets that may still change.
class ChartSeriesPoints {
public:
    explicit ChartSeriesPoints( qint64 bucketSizeMs = 0 );

    // Drops the points of the Log Lines from `from` on, and adds `appended`,
    // the points of the Log Lines from `from` on, in Log Line order. Only the
    // last Log Line merged before may be merged again (it may have been
    // incomplete): a bucketed series keeps no more than that needs.
    void merge( LineNumber from, const QVector<ChartPoint>& appended );

    // In Log Line order.
    const QVector<ChartPoint>& points() const
    {
        return points_;
    }

private:
    qint64 bucketSizeMs_;
    QVector<ChartPoint> points_;
    // Bucketed: the raw points of the last openBuckets_ buckets of points_.
    QVector<ChartPoint> openRaw_;
    qsizetype openBuckets_ = 0;
};

// The data of a chart that follows its Log File. The points are extracted on a
// worker thread: from the first Log Line when the series changed or the Log
// File was truncated, and otherwise only from the Log Lines appended since the
// last extraction, merged into the points extracted before. Growth is picked
// up after an update delay, so that a busy Log File is not extracted from on
// every change. Cancelling an extraction never waits for its worker; what a
// cancelled extraction finds is discarded.
class ChartExtraction : public QObject {
    Q_OBJECT

public:
    static constexpr std::chrono::milliseconds DefaultUpdateDelay{ 250 };

    explicit ChartExtraction( QObject* parent = nullptr );
    // Cancels every extraction and waits for their workers to stop, so that
    // none of them outlives the log data it reads.
    ~ChartExtraction() override;

    ChartExtraction( const ChartExtraction& ) = delete;
    ChartExtraction& operator=( const ChartExtraction& ) = delete;

    // The Log File extracted from. Set it before the first update().
    void setLogData( std::shared_ptr<const AbstractLogData> logData );

    // How long update() waits before extracting appended Log Lines.
    void setUpdateDelay( std::chrono::milliseconds delay );

    // Replaces the series: their points are dropped, a running extraction is
    // cancelled, and the next one starts from the first Log Line.
    void setSeries( const QVector<ChartSeriesDefinition>& series );

    // The Log File was truncated or loaded again from its start: a running
    // extraction is cancelled and the next one starts from the first Log Line.
    // The points extracted so far stay until then.
    void restart();

    // Brings the points up to date with the Log File: at once when it has to
    // be extracted from the first Log Line, otherwise after the update delay,
    // and after the running extraction when there is one.
    void update();

    // An extraction runs or an update waits for its delay.
    bool isExtracting() const;
    // How far the running extraction got, in percent.
    int progress() const;
    // How many Log Lines the running extraction reads.
    LinesCount linesToExtract() const;

    // The points of the series at this index, in Log Line order.
    const QVector<ChartPoint>& points( qsizetype series ) const;

Q_SIGNALS:
    // An extraction started on a worker thread.
    void started();
    // An extraction finished and its points are merged in.
    void extracted();

private:
    struct Job;

    void startExtraction();
    void cancelRunning();
    // Drops every point, keeping one empty set of points per series.
    void clearPoints();
    void onFinished( const std::shared_ptr<Job>& job );

    std::shared_ptr<const AbstractLogData> logData_;
    QVector<ChartSeriesDefinition> series_;
    std::vector<ChartSeriesPoints> points_;
    // The Log Lines the points were extracted from, from the first one on.
    LinesCount linesExtracted_;
    bool extractFromStart_ = true;
    bool updateRequested_ = false;

    QTimer delayTimer_;
    // The extraction whose points are used, if one runs.
    std::shared_ptr<Job> running_;
    // Every extraction whose worker has not finished, cancelled ones too.
    std::vector<std::shared_ptr<Job>> jobs_;
};
