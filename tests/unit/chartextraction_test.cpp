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

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QStringList>
#include <QThreadPool>

#include "abstractlogdata.h"
#include "chartextraction.h"
#include "chartseries.h"

using namespace std::chrono_literals;

namespace {

// A Log File held in memory that the extraction's worker thread may read while
// the test changes it, and whose next read can be held until the test lets it
// go on.
class GrowingLogData : public AbstractLogData {
public:
    explicit GrowingLogData( QStringList lines )
        : lines_( std::move( lines ) )
    {
    }

    void setLines( const QStringList& lines )
    {
        const std::scoped_lock lock{ mutex_ };
        lines_ = lines;
    }

    // The next read waits until release() is called; it gives up after a
    // while, so that a test that would block forever fails instead.
    void holdNextRead()
    {
        const std::scoped_lock lock{ mutex_ };
        holdNextRead_ = true;
        released_ = false;
    }

    void release()
    {
        {
            const std::scoped_lock lock{ mutex_ };
            released_ = true;
        }
        condition_.notify_all();
    }

    bool waitUntilHeld()
    {
        std::unique_lock lock{ mutex_ };
        return condition_.wait_for( lock, 10s, [ this ] { return held_; } );
    }

    // The first Log Line of every block read, in the order read.
    std::vector<uint64_t> firstLinesRead() const
    {
        const std::scoped_lock lock{ mutex_ };
        return firstLinesRead_;
    }

    void forgetReads()
    {
        const std::scoped_lock lock{ mutex_ };
        firstLinesRead_.clear();
    }

protected:
    QString doGetLineString( LineNumber line ) const override
    {
        const std::scoped_lock lock{ mutex_ };
        return lineUnlocked( line );
    }
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        return doGetLineString( line );
    }
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        std::unique_lock lock{ mutex_ };
        if ( holdNextRead_ ) {
            holdNextRead_ = false;
            held_ = true;
            condition_.notify_all();
            condition_.wait_for( lock, 10s, [ this ] { return released_; } );
            held_ = false;
        }
        firstLinesRead_.push_back( first.get() );
        logsquirl::vector<QString> result;
        for ( uint64_t i = 0;
              i < count.get() && first.get() + i < static_cast<uint64_t>( lines_.size() ); ++i ) {
            result.push_back( lineUnlocked( LineNumber( first.get() + i ) ) );
        }
        return result;
    }
    logsquirl::vector<QString> doGetExpandedLines( LineNumber first,
                                                   LinesCount count ) const override
    {
        return doGetLines( first, count );
    }
    LineNumber doGetLineNumber( LineNumber index ) const override
    {
        return index;
    }
    LinesCount doGetNbLine() const override
    {
        const std::scoped_lock lock{ mutex_ };
        return LinesCount( static_cast<LinesCount::UnderlyingType>( lines_.size() ) );
    }
    LineLength doGetMaxLength() const override
    {
        return LineLength( 0 );
    }
    LineLength doGetLineLength( LineNumber ) const override
    {
        return LineLength( 0 );
    }
    void doSetDisplayEncoding( const char* ) override {}
    const TextEncoding* doGetDisplayEncoding() const override
    {
        return nullptr;
    }
    void doAttachReader() const override {}
    void doDetachReader() const override {}

private:
    QString lineUnlocked( LineNumber line ) const
    {
        return line.get() < static_cast<uint64_t>( lines_.size() )
                   ? lines_[ static_cast<qsizetype>( line.get() ) ]
                   : QString{};
    }

    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    QStringList lines_;
    mutable bool holdNextRead_ = false;
    mutable bool held_ = false;
    bool released_ = true;
    mutable std::vector<uint64_t> firstLinesRead_;
};

ChartSeriesDefinition numericSeries()
{
    ChartSeriesDefinition def;
    def.id = "duration";
    def.name = "Duration";
    def.pattern = R"(took (\d+) ms)";
    def.captureGroup = 1;
    def.compilePattern();
    return def;
}

ChartSeriesDefinition countSeries()
{
    ChartSeriesDefinition def;
    def.id = "errors";
    def.name = "Errors";
    def.pattern = "ERROR";
    def.captureGroup = 0;
    def.compilePattern();
    return def;
}

// Log Lines per second, in buckets of two seconds.
ChartSeriesDefinition bucketedSeries()
{
    ChartSeriesDefinition def;
    def.id = "rate";
    def.name = "Rate";
    def.pattern = R"(^\d\d:\d\d:\d\d (?:INFO|ERROR))";
    def.captureGroup = 0;
    def.xPattern = R"(^(\d\d:\d\d:\d\d))";
    def.xCaptureGroup = 1;
    def.xTimestampFormat = "HH:mm:ss";
    def.bucketSizeMs = 2000;
    def.compilePattern();
    return def;
}

QVector<ChartSeriesDefinition> allSeries()
{
    return { numericSeries(), countSeries(), bucketedSeries() };
}

QString logLine( int second, const char* level, int millis )
{
    return QStringLiteral( "10:00:%1 %2 request took %3 ms" )
        .arg( second, 2, 10, QLatin1Char( '0' ) )
        .arg( QLatin1String( level ) )
        .arg( millis );
}

bool waitForExtraction( ChartExtraction& extraction )
{
    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < 10'000 ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThreadPool::globalInstance()->waitForDone( 10 );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        if ( !extraction.isExtracting() ) {
            return true;
        }
    }
    return false;
}

void requireSamePoints( const ChartExtraction& actual, const ChartExtraction& expected,
                        qsizetype seriesCount )
{
    for ( qsizetype series = 0; series < seriesCount; ++series ) {
        INFO( "series " << series );
        const auto& a = actual.points( series );
        const auto& e = expected.points( series );
        REQUIRE( a.size() == e.size() );
        for ( qsizetype i = 0; i < a.size(); ++i ) {
            INFO( "point " << i );
            REQUIRE( a[ i ].line == e[ i ].line );
            REQUIRE( a[ i ].xValue == e[ i ].xValue );
            REQUIRE( a[ i ].value == e[ i ].value );
            REQUIRE( a[ i ].xLabel == e[ i ].xLabel );
        }
    }
}

// What a chart extracted from the whole of these Log Lines at once.
std::unique_ptr<ChartExtraction> fullExtraction( const QStringList& lines )
{
    auto extraction = std::make_unique<ChartExtraction>();
    extraction->setUpdateDelay( 0ms );
    extraction->setLogData( std::make_shared<GrowingLogData>( lines ) );
    extraction->setSeries( allSeries() );
    extraction->update();
    REQUIRE( waitForExtraction( *extraction ) );
    return extraction;
}

} // namespace

SCENARIO( "A chart follows a growing Log File incrementally", "[chartextraction]" )
{
    GIVEN( "A chart extracted from a Log File" )
    {
        QStringList lines{ logLine( 0, "INFO", 10 ), logLine( 0, "ERROR", 20 ),
                           logLine( 1, "INFO", 30 ), logLine( 3, "INFO", 40 ) };
        auto logData = std::make_shared<GrowingLogData>( lines );

        ChartExtraction extraction;
        extraction.setUpdateDelay( 0ms );
        extraction.setLogData( logData );
        extraction.setSeries( allSeries() );
        extraction.update();
        REQUIRE( waitForExtraction( extraction ) );
        requireSamePoints( extraction, *fullExtraction( lines ), 3 );

        WHEN( "The Log File grows several times" )
        {
            for ( int growth = 0; growth < 4; ++growth ) {
                logData->forgetReads();
                const auto linesBefore = lines.size();
                for ( int i = 0; i < 5; ++i ) {
                    lines.append( logLine( 4 + growth * 2 + i / 3, i % 2 ? "ERROR" : "INFO",
                                           100 + growth * 10 + i ) );
                }
                logData->setLines( lines );
                extraction.update();
                REQUIRE( waitForExtraction( extraction ) );

                INFO( "growth " << growth );
                // Only the Log Lines from the last one extracted on are read,
                const auto reads = logData->firstLinesRead();
                REQUIRE( !reads.empty() );
                REQUIRE( reads.front() == static_cast<uint64_t>( linesBefore - 1 ) );
                // and the chart equals an extraction of the whole Log File.
                requireSamePoints( extraction, *fullExtraction( lines ), 3 );
            }
        }

        WHEN( "The last Log Line was incomplete and is completed later" )
        {
            lines.append( QStringLiteral( "10:00:05 INFO request took 7" ) );
            logData->setLines( lines );
            extraction.update();
            REQUIRE( waitForExtraction( extraction ) );
            requireSamePoints( extraction, *fullExtraction( lines ), 3 );

            lines.last() = logLine( 5, "ERROR", 77 );
            lines.append( logLine( 5, "INFO", 78 ) );
            logData->setLines( lines );
            extraction.update();
            REQUIRE( waitForExtraction( extraction ) );

            THEN( "The chart equals an extraction of the whole Log File" )
            {
                requireSamePoints( extraction, *fullExtraction( lines ), 3 );
            }
        }

        WHEN( "The last Log Line was alone in its bucket and is completed into the bucket before" )
        {
            lines.append( QStringLiteral( "10:00:04 INFO request" ) );
            logData->setLines( lines );
            extraction.update();
            REQUIRE( waitForExtraction( extraction ) );

            lines.last() = logLine( 3, "INFO", 1 );
            lines.append( logLine( 3, "INFO", 2 ) );
            logData->setLines( lines );
            extraction.update();
            REQUIRE( waitForExtraction( extraction ) );

            THEN( "The chart equals an extraction of the whole Log File" )
            {
                requireSamePoints( extraction, *fullExtraction( lines ), 3 );
            }
        }

        WHEN( "The Log File was truncated and grew again past its old end" )
        {
            lines = QStringList{ logLine( 7, "ERROR", 1 ), logLine( 7, "INFO", 2 ),
                                 logLine( 8, "INFO", 3 ),  logLine( 9, "ERROR", 4 ),
                                 logLine( 9, "INFO", 5 ),  logLine( 9, "INFO", 6 ) };
            logData->setLines( lines );
            logData->forgetReads();
            extraction.restart();
            extraction.update();
            REQUIRE( waitForExtraction( extraction ) );

            THEN( "It is extracted again from the first Log Line" )
            {
                REQUIRE( logData->firstLinesRead().front() == 0 );
                requireSamePoints( extraction, *fullExtraction( lines ), 3 );
            }
        }

        WHEN( "The Log File has fewer Log Lines than were extracted" )
        {
            lines = QStringList{ logLine( 7, "ERROR", 1 ) };
            logData->setLines( lines );
            logData->forgetReads();
            extraction.update();
            REQUIRE( waitForExtraction( extraction ) );

            THEN( "It is extracted again from the first Log Line" )
            {
                REQUIRE( logData->firstLinesRead().front() == 0 );
                requireSamePoints( extraction, *fullExtraction( lines ), 3 );
            }
        }
    }
}

SCENARIO( "Updates of a chart are debounced", "[chartextraction]" )
{
    GIVEN( "A chart with an update delay, extracted from a Log File" )
    {
        QStringList lines{ logLine( 0, "INFO", 10 ) };
        auto logData = std::make_shared<GrowingLogData>( lines );

        ChartExtraction extraction;
        extraction.setUpdateDelay( 0ms );
        extraction.setLogData( logData );
        extraction.setSeries( allSeries() );
        extraction.update();
        REQUIRE( waitForExtraction( extraction ) );
        extraction.setUpdateDelay( 200ms );

        WHEN( "The Log File grows several times within the delay" )
        {
            QSignalSpy extracted( &extraction, &ChartExtraction::extracted );
            logData->forgetReads();
            for ( int i = 0; i < 5; ++i ) {
                lines.append( logLine( 1, "INFO", i ) );
                logData->setLines( lines );
                extraction.update();
            }

            THEN( "Nothing is extracted before the delay, and once after it" )
            {
                REQUIRE( logData->firstLinesRead().empty() );
                REQUIRE( extracted.wait( 5000 ) );
                REQUIRE( waitForExtraction( extraction ) );
                REQUIRE( extracted.size() == 1 );
                REQUIRE( logData->firstLinesRead().size() == 1 );
                requireSamePoints( extraction, *fullExtraction( lines ), 3 );
            }
        }
    }
}

SCENARIO( "Cancelling a chart extraction does not wait for it", "[chartextraction]" )
{
    GIVEN( "An extraction whose worker is held in its read of the Log File" )
    {
        const QStringList lines{ logLine( 0, "INFO", 10 ), logLine( 1, "ERROR", 20 ) };
        auto logData = std::make_shared<GrowingLogData>( lines );
        logData->holdNextRead();

        ChartExtraction extraction;
        extraction.setUpdateDelay( 0ms );
        extraction.setLogData( logData );
        extraction.setSeries( { numericSeries() } );
        extraction.update();
        REQUIRE( logData->waitUntilHeld() );

        WHEN( "The series change while it is held" )
        {
            QSignalSpy extracted( &extraction, &ChartExtraction::extracted );
            QElapsedTimer timer;
            timer.start();
            extraction.setSeries( { countSeries() } );
            extraction.update();
            const auto elapsed = timer.elapsed();

            THEN( "Cancelling returns at once, and the stale result is discarded" )
            {
                CHECK( elapsed < 1000 );

                // The new extraction finishes while the old one is still held.
                REQUIRE( extracted.wait( 5000 ) );
                REQUIRE( extraction.points( 0 ).size() == 1 );
                REQUIRE( extraction.points( 0 ).first().line == 1_lnum );

                logData->release();
                QThreadPool::globalInstance()->waitForDone( 10'000 );
                QCoreApplication::processEvents();
                QCoreApplication::processEvents();

                REQUIRE( extracted.size() == 1 );
                REQUIRE( extraction.points( 0 ).size() == 1 );
                REQUIRE( extraction.points( 0 ).first().line == 1_lnum );
            }
            logData->release();
        }
    }
}

SCENARIO( "A chart's series may be set before its Log File", "[chartextraction]" )
{
    GIVEN( "Series set before the log data" )
    {
        ChartExtraction extraction;
        extraction.setUpdateDelay( 0ms );
        extraction.setSeries( { numericSeries() } );
        extraction.setLogData(
            std::make_shared<GrowingLogData>( QStringList{ logLine( 0, "INFO", 10 ) } ) );

        THEN( "The series has no points until extracted, then those of the Log File" )
        {
            REQUIRE( extraction.points( 0 ).isEmpty() );
            extraction.update();
            REQUIRE( waitForExtraction( extraction ) );
            REQUIRE( extraction.points( 0 ).size() == 1 );
            REQUIRE( extraction.points( 0 ).first().value == 10.0 );
        }
    }
}

// Two series for the same word, one matching case and one not, each count
// their own Log Lines (#411).
SCENARIO( "Series for the same pattern differing in case count apart", "[chartextraction]" )
{
    GrowingLogData logData{ { "an error", "an Error", "an ERROR", "all good" } };

    auto matchingCase = countSeries();
    matchingCase.pattern = "error";
    matchingCase.compilePattern();
    auto ignoringCase = matchingCase;
    ignoringCase.matchCase = false;
    ignoringCase.compilePattern();

    std::atomic<bool> cancel{ false };
    std::atomic<uint64_t> linesDone{ 0 };
    const auto points = extractChartPoints( logData, { matchingCase, ignoringCase },
                                            LineNumber( 0 ), LinesCount( 4 ), cancel, linesDone );

    REQUIRE( points.has_value() );
    REQUIRE( points->at( 0 ).size() == 1 );
    REQUIRE( points->at( 1 ).size() == 3 );
}
