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

#include "chartextraction.h"

#include <algorithm>
#include <cmath>

#include <QDate>
#include <QDateTime>
#include <QFutureWatcher>
#include <QHash>
#include <QRegularExpression>
#include <QtConcurrent>

#include "abstractlogdata.h"

// ---------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------

std::optional<ChartRawPoints> extractChartPoints( const AbstractLogData& logData,
                                                  const QVector<ChartSeriesDefinition>& series,
                                                  LineNumber first, LinesCount count,
                                                  const std::atomic<bool>& cancel,
                                                  std::atomic<uint64_t>& linesDone )
{
    ChartRawPoints result( series.size() );

    // ---------------------------------------------------------------
    // Pre-compute regex groups to avoid redundant matches per line.
    //
    // When using format-aware templates many series share the same
    // Y-pattern (the format's main regex) and/or the same X-pattern
    // (timestamp extraction).  Grouping them lets us run each unique
    // regex only once per line and distribute the result.
    //
    // Additionally, when xPattern == pattern we can reuse the
    // Y-match for X extraction — eliminating the separate X-regex
    // run entirely.
    // ---------------------------------------------------------------

    // Group series indices by unique Y-pattern string.
    struct RegexGroup {
        QRegularExpression regex;
        QVector<int> indices;
    };

    QHash<QString, RegexGroup> yGroups;
    for ( int si = 0; si < series.size(); ++si ) {
        const auto& s = series[ si ];
        if ( !s.compiledRegex.isValid() ) {
            continue;
        }
        auto& g = yGroups[ s.pattern ];
        if ( g.indices.isEmpty() ) {
            g.regex = s.compiledRegex;
        }
        g.indices.append( si );
    }

    // Collect unique X-regexes that differ from their Y-pattern.
    QHash<QString, QRegularExpression> uniqueXRegexes;
    for ( int si = 0; si < series.size(); ++si ) {
        const auto& s = series[ si ];
        if ( !s.hasCustomXAxis() || !s.compiledXRegex.isValid() ) {
            continue;
        }
        if ( s.xPattern == s.pattern ) {
            continue; // will reuse Y match
        }
        uniqueXRegexes.insert( s.xPattern, s.compiledXRegex );
    }

    // Track which series can reuse the Y-match for X extraction.
    QVector<bool> reuseYForX( series.size(), false );
    for ( int si = 0; si < series.size(); ++si ) {
        const auto& s = series[ si ];
        if ( s.hasCustomXAxis() && s.xPattern == s.pattern ) {
            reuseYForX[ si ] = true;
        }
    }

    // Cache QDate::currentDate() outside the hot loop.
    const auto currentYear = QDate::currentDate().year();

    constexpr uint64_t batchSize = 5000;

    const auto end = first.get() + count.get();
    for ( uint64_t start = first.get(); start < end; start += batchSize ) {
        if ( cancel.load() ) {
            return std::nullopt;
        }

        const auto batch = std::min( batchSize, end - start );
        const auto lines = logData.getExpandedLines( LineNumber( start ), LinesCount( batch ) );

        for ( uint64_t i = 0; i < static_cast<uint64_t>( lines.size() ); ++i ) {
            const auto lineNum = LineNumber( start + i );
            const auto& lineText = lines[ static_cast<size_t>( i ) ];

            // 1. Run each unique Y-regex once for this line.
            QHash<QString, QRegularExpressionMatch> yCache;
            for ( auto it = yGroups.cbegin(); it != yGroups.cend(); ++it ) {
                auto m = it.value().regex.match( lineText );
                if ( m.hasMatch() ) {
                    yCache.insert( it.key(), std::move( m ) );
                }
            }
            if ( yCache.isEmpty() ) {
                continue; // no series matches this line
            }

            // 2. Run each unique X-regex once (only those
            //    that differ from Y-pattern).
            QHash<QString, QRegularExpressionMatch> xCache;
            for ( auto it = uniqueXRegexes.cbegin(); it != uniqueXRegexes.cend(); ++it ) {
                auto m = it.value().match( lineText );
                if ( m.hasMatch() ) {
                    xCache.insert( it.key(), std::move( m ) );
                }
            }

            // 3. Timestamp parse cache: same raw text on the
            //    same line always yields the same epoch-ms.
            QHash<QString, double> tsCache;

            // 4. Distribute cached matches to all series.
            for ( int si = 0; si < series.size(); ++si ) {
                const auto& s = series[ si ];

                auto yIt = yCache.constFind( s.pattern );
                if ( yIt == yCache.cend() ) {
                    continue;
                }
                const auto& yMatch = yIt.value();

                // Extract Y value.
                double yVal = 1.0;
                if ( s.captureGroup > 0 && yMatch.lastCapturedIndex() >= s.captureGroup ) {
                    bool ok = false;
                    yVal = yMatch.captured( s.captureGroup ).toDouble( &ok );
                    if ( !ok ) {
                        yVal = 1.0;
                    }
                }

                // Extract X value.
                double xVal = static_cast<double>( lineNum.get() );
                QString xLabel;

                if ( s.hasCustomXAxis() ) {
                    // Pick the match to read X from: either
                    // the Y-match (when patterns are the same)
                    // or the dedicated X-match.
                    const QRegularExpressionMatch* xMatchPtr = nullptr;
                    if ( reuseYForX[ si ] ) {
                        xMatchPtr = &yMatch;
                    }
                    else {
                        auto xIt = xCache.constFind( s.xPattern );
                        if ( xIt != xCache.cend() ) {
                            xMatchPtr = &xIt.value();
                        }
                    }

                    if ( xMatchPtr && xMatchPtr->lastCapturedIndex() >= s.xCaptureGroup ) {
                        const auto captured = xMatchPtr->captured( s.xCaptureGroup );

                        if ( s.isTimestampXAxis() ) {
                            // Check per-line timestamp cache.
                            auto tsIt = tsCache.constFind( captured );
                            if ( tsIt != tsCache.cend() ) {
                                xVal = tsIt.value();
                                xLabel = captured;
                            }
                            else {
                                auto dt = QDateTime::fromString( captured, s.xTimestampFormat );
                                if ( dt.isValid() ) {
                                    if ( dt.date().year() < 1970 ) {
                                        dt.setDate( QDate( currentYear, dt.date().month(),
                                                           dt.date().day() ) );
                                    }
                                    xVal = static_cast<double>( dt.toMSecsSinceEpoch() );
                                    xLabel = captured;
                                    tsCache.insert( captured, xVal );
                                }
                            }
                        }
                        else {
                            bool ok = false;
                            const auto numVal = captured.toDouble( &ok );
                            if ( ok ) {
                                xVal = numVal;
                            }
                        }
                    }
                }

                result[ si ].append( { lineNum, xVal, yVal, xLabel } );
            }
        }

        linesDone.store( start + batch - first.get() );
    }

    return result;
}

// ---------------------------------------------------------------------------
// Merging
// ---------------------------------------------------------------------------

namespace {

// The first point of a Log Line from `line` on, in points in Log Line order.
QVector<ChartPoint>::iterator firstPointFrom( QVector<ChartPoint>& points, LineNumber line )
{
    return std::lower_bound(
        points.begin(), points.end(), line,
        []( const ChartPoint& point, LineNumber l ) { return point.line < l; } );
}

// Aggregates raw points into time buckets, summing the Y values of consecutive
// points in the same bucket, and appends the buckets to `buckets`. Returns the
// index of the first raw point of each bucket.
QVector<qsizetype> appendBuckets( const QVector<ChartPoint>& raw, qint64 bucketSizeMs,
                                  QVector<ChartPoint>& buckets )
{
    QVector<qsizetype> firstRawPoints;
    if ( raw.isEmpty() ) {
        return firstRawPoints;
    }

    const auto bucket = static_cast<double>( bucketSizeMs );
    const auto appendBucket = [ & ]( double bucketStart, double sum, LineNumber line ) {
        const double mid = bucketStart + bucket / 2.0;
        const auto dt = QDateTime::fromMSecsSinceEpoch( static_cast<qint64>( mid ) );
        buckets.append( { line, mid, sum, dt.toString( "HH:mm:ss" ) } );
    };

    double bucketStart = std::floor( raw.first().xValue / bucket ) * bucket;
    double bucketSum = 0.0;
    LineNumber bucketLine = raw.first().line;
    firstRawPoints.append( 0 );

    for ( qsizetype i = 0; i < raw.size(); ++i ) {
        const auto& pt = raw[ i ];
        const double ptBucket = std::floor( pt.xValue / bucket ) * bucket;
        if ( ptBucket != bucketStart ) {
            appendBucket( bucketStart, bucketSum, bucketLine );
            bucketStart = ptBucket;
            bucketSum = 0.0;
            bucketLine = pt.line;
            firstRawPoints.append( i );
        }
        bucketSum += pt.value;
    }
    appendBucket( bucketStart, bucketSum, bucketLine );

    return firstRawPoints;
}

} // namespace

ChartSeriesPoints::ChartSeriesPoints( qint64 bucketSizeMs )
    : bucketSizeMs_( bucketSizeMs )
{
}

void ChartSeriesPoints::merge( LineNumber from, const QVector<ChartPoint>& appended )
{
    if ( bucketSizeMs_ <= 0 ) {
        points_.erase( firstPointFrom( points_, from ), points_.end() );
        points_.append( appended );
        return;
    }

    // The open buckets are built again from their raw points and the appended
    // ones: the sums are taken over the same points in the same order as
    // bucketing all points at once would.
    points_.resize( points_.size() - openBuckets_ );
    openRaw_.erase( firstPointFrom( openRaw_, from ), openRaw_.end() );
    openRaw_.append( appended );

    const auto firstRawPoints = appendBuckets( openRaw_, bucketSizeMs_, points_ );
    const auto buckets = firstRawPoints.size();
    if ( buckets == 0 ) {
        openBuckets_ = 0;
        return;
    }

    // Merging again drops at most the point of the last Log Line, the last raw
    // point. Points appended then can only join the last bucket left, which is
    // the one before when that point is alone in its bucket.
    auto firstOpen = buckets - 1;
    if ( openRaw_.size() - firstRawPoints[ firstOpen ] == 1 && firstOpen > 0 ) {
        --firstOpen;
    }
    openBuckets_ = buckets - firstOpen;
    openRaw_.remove( 0, firstRawPoints[ firstOpen ] );
}

// ---------------------------------------------------------------------------
// Following the Log File
// ---------------------------------------------------------------------------

struct ChartExtraction::Job {
    LineNumber first;
    LinesCount count;
    bool fromStart = false;
    std::atomic<bool> cancelled{ false };
    std::atomic<uint64_t> linesDone{ 0 };
    QFuture<std::optional<ChartRawPoints>> future;
};

ChartExtraction::ChartExtraction( QObject* parent )
    : QObject( parent )
{
    delayTimer_.setSingleShot( true );
    delayTimer_.setInterval( DefaultUpdateDelay );
    connect( &delayTimer_, &QTimer::timeout, this, [ this ]() {
        if ( !running_ && updateRequested_ ) {
            startExtraction();
        }
    } );
}

ChartExtraction::~ChartExtraction()
{
    for ( const auto& job : jobs_ ) {
        job->cancelled.store( true );
    }
    for ( const auto& job : jobs_ ) {
        job->future.waitForFinished();
    }
}

void ChartExtraction::setLogData( std::shared_ptr<const AbstractLogData> logData )
{
    cancelRunning();
    logData_ = std::move( logData );
    clearPoints();
    extractFromStart_ = true;
}

void ChartExtraction::setUpdateDelay( std::chrono::milliseconds delay )
{
    delayTimer_.setInterval( delay );
}

void ChartExtraction::setSeries( const QVector<ChartSeriesDefinition>& series )
{
    cancelRunning();
    series_ = series;
    for ( auto& s : series_ ) {
        s.points.clear();
    }
    clearPoints();
    extractFromStart_ = true;
}

void ChartExtraction::clearPoints()
{
    points_.clear();
    for ( const auto& s : series_ ) {
        points_.emplace_back( s.isBucketed() ? s.bucketSizeMs : 0 );
    }
    linesExtracted_ = 0_lcount;
}

void ChartExtraction::restart()
{
    cancelRunning();
    extractFromStart_ = true;
}

void ChartExtraction::update()
{
    updateRequested_ = true;
    if ( running_ ) {
        return;
    }
    if ( extractFromStart_ ) {
        delayTimer_.stop();
        startExtraction();
    }
    else if ( !delayTimer_.isActive() ) {
        delayTimer_.start();
    }
}

bool ChartExtraction::isExtracting() const
{
    return running_ != nullptr || delayTimer_.isActive();
}

int ChartExtraction::progress() const
{
    if ( !running_ || running_->count.get() == 0 ) {
        return 0;
    }
    return static_cast<int>( ( running_->linesDone.load() * 100 ) / running_->count.get() );
}

LinesCount ChartExtraction::linesToExtract() const
{
    return running_ ? running_->count : 0_lcount;
}

const QVector<ChartPoint>& ChartExtraction::points( qsizetype series ) const
{
    return points_.at( static_cast<size_t>( series ) ).points();
}

void ChartExtraction::cancelRunning()
{
    if ( running_ ) {
        running_->cancelled.store( true );
        running_.reset();
        // Its worker stops at its next batch; what it found is discarded.
    }
}

void ChartExtraction::startExtraction()
{
    updateRequested_ = false;
    if ( !logData_ || series_.isEmpty() ) {
        return;
    }

    const auto lines = logData_->getNbLine();
    const bool fromStart = extractFromStart_ || lines < linesExtracted_;

    auto job = std::make_shared<Job>();
    job->fromStart = fromStart;
    // The last Log Line extracted is extracted again: it may have been
    // incomplete, and been completed since.
    job->first = ( fromStart || linesExtracted_.get() == 0 )
                     ? 0_lnum
                     : LineNumber( linesExtracted_.get() - 1 );
    job->count = LinesCount( lines.get() - job->first.get() );
    if ( !fromStart && lines.get() == 0 ) {
        return;
    }
    extractFromStart_ = false;

    running_ = job;
    jobs_.push_back( job );

    auto* watcher = new QFutureWatcher<std::optional<ChartRawPoints>>( this );
    connect( watcher, &QFutureWatcher<std::optional<ChartRawPoints>>::finished, this,
             [ this, job, watcher ]() {
                 watcher->deleteLater();
                 onFinished( job );
             } );

    job->future
        = QtConcurrent::run( [ logData = logData_, series = series_,
                               state = job.get() ]() mutable -> std::optional<ChartRawPoints> {
              // Released here, before the result is reported: the owner, which
              // waits for that on destruction, keeps the last reference.
              const auto data = std::move( logData );
              return extractChartPoints( *data, series, state->first, state->count,
                                         state->cancelled, state->linesDone );
          } );
    watcher->setFuture( job->future );

    Q_EMIT started();
}

void ChartExtraction::onFinished( const std::shared_ptr<Job>& job )
{
    jobs_.erase( std::remove( jobs_.begin(), jobs_.end(), job ), jobs_.end() );
    if ( job != running_ ) {
        return; // cancelled: stale
    }
    running_.reset();

    const auto result = job->future.result();
    if ( result ) {
        if ( job->fromStart ) {
            clearPoints();
        }
        for ( qsizetype i = 0; i < series_.size() && i < result->size(); ++i ) {
            points_[ static_cast<size_t>( i ) ].merge( job->first, ( *result )[ i ] );
        }
        linesExtracted_ = LinesCount( job->first.get() + job->count.get() );
        Q_EMIT extracted();
    }

    if ( updateRequested_ ) {
        updateRequested_ = false;
        update();
    }
}
