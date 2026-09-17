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

// A chart following a growing Log File (#298): a Log File of about 128 MiB is
// charted once, then Log Lines are appended to it and the chart is brought up
// to date after each load, as the crawler widget does with a visible chart.
//
// Uses only what the chart panel offered before #298 -- setLogData(),
// setSeriesDefinitions(), extractData() and seriesDefinitions() -- so the same
// file measures both sides of an A/B comparison. Where the panel has an update
// delay, it is set to zero, so that the delay is not measured. See
// tests/benchmarks/README.md.

#include "chartpanel.h"
#include "chartseries.h"
#include "generated_log_file.h"
#include "logdata.h"
#include "persistentinfo.h"
#include "test_policies.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <chrono>
#include <memory>
#include <string>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

const bool PersistentInfo::ForcePortable = true;

using namespace logdatabenchmark;

namespace {

constexpr auto Shape = LogFileShape::ShortLines;

// About 128 MiB, or the number of MiB in LOGSQUIRL_BENCHMARK_LOG_FILE_MB.
std::uint64_t logFileBytes()
{
    constexpr std::uint64_t Mib = 1024 * 1024;
    return qEnvironmentVariableIsEmpty( "LOGSQUIRL_BENCHMARK_LOG_FILE_MB" )
               ? 128 * Mib
               : generatedLogFileBytes();
}

QVector<ChartSeriesDefinition> chartSeries()
{
    ChartSeriesDefinition duration;
    duration.id = "duration";
    duration.name = "Duration";
    duration.pattern = R"(handled in (\d+) ms)"; // every Log Line
    duration.captureGroup = 1;

    ChartSeriesDefinition errors;
    errors.id = "errors";
    errors.name = "Errors";
    errors.pattern = " ERROR ";
    errors.captureGroup = 0;

    ChartSeriesDefinition rate;
    rate.id = "rate";
    rate.name = "Rate";
    rate.pattern = R"(^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\.\d+ (INFO|WARN|ERROR))";
    rate.captureGroup = 0;
    rate.xPattern = rate.pattern;
    rate.xCaptureGroup = 1;
    rate.xTimestampFormat = "yyyy-MM-dd HH:mm:ss";
    rate.bucketSizeMs = 1000;

    QVector<ChartSeriesDefinition> series{ duration, errors, rate };
    for ( auto& s : series ) {
        s.compilePattern();
    }
    return series;
}

class ChartedLogFile {
public:
    ChartedLogFile()
    {
        REQUIRE( dir_.isValid() );
        fileName_ = dir_.filePath( "chart_follow_benchmark.log" );
        bool written = false;
        lineCount_ = writeGeneratedLogFile( fileName_, Shape, logFileBytes(), written );
        REQUIRE( written );

        const auto policies = testSettingsPolicies();
        logData_ = std::make_shared<LogData>( policies.indexing, policies.search,
                                              policies.fileAccess, policies.decoding );
        QEventLoop loop;
        QObject::connect( logData_.get(), &LogData::loadingFinished, &loop, &QEventLoop::quit );
        logData_->attachFile( fileName_ );
        loop.exec();
        REQUIRE( logData_->getNbLine().get() == lineCount_ );

#if __has_include( "chartextraction.h" )
        panel_.setUpdateDelay( std::chrono::milliseconds{ 0 } );
#endif
        panel_.setLogData( logData_ );
        panel_.setSeriesDefinitions( chartSeries() );
        panel_.extractData();
        REQUIRE( waitForChart( 30 * 60'000 ) );
    }

    // Appends Log Lines and has the log data load them, as a change on disk
    // does.
    void append( std::uint64_t count )
    {
        std::string lines;
        for ( std::uint64_t i = 0; i < count; ++i ) {
            appendGeneratedLogLine( Shape, lineCount_++, lines );
        }
        QFile out{ fileName_ };
        REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Append ) );
        REQUIRE( out.write( lines.data(), static_cast<qint64>( lines.size() ) )
                 == static_cast<qint64>( lines.size() ) );
        out.close();

        QEventLoop loop;
        QObject::connect( logData_.get(), &LogData::loadingFinished, &loop, &QEventLoop::quit );
        QTimer::singleShot( 60'000, &loop, &QEventLoop::quit );
        logData_->fileChangedOnDisk( fileName_ );
        loop.exec();
        REQUIRE( logData_->getNbLine().get() == lineCount_ );
    }

    // What the crawler widget does when a load of a visible chart's Log File
    // finished.
    void loaded()
    {
        panel_.extractData();
    }

    // Waits until the chart shows a point for every Log Line.
    bool waitForChart( int timeoutMs = 600'000 )
    {
        QElapsedTimer timer;
        timer.start();
        while ( timer.elapsed() < timeoutMs ) {
            const auto series = panel_.seriesDefinitions();
            if ( static_cast<std::uint64_t>( series.first().points.size() ) == lineCount_ ) {
                return true;
            }
            QCoreApplication::processEvents( QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents,
                                             1 );
        }
        return false;
    }

private:
    QTemporaryDir dir_;
    QString fileName_;
    std::uint64_t lineCount_ = 0;
    std::shared_ptr<LogData> logData_;
    ChartPanel panel_;
};

} // namespace

TEST_CASE( "A chart following a growing Log File", "[chart-follow-benchmark]" )
{
    ChartedLogFile logFile;

    // One append of a few KB, the chart brought up to date after it.
    BENCHMARK( "append 20 Log Lines, chart updated" )
    {
        logFile.append( 20 );
        logFile.loaded();
        return logFile.waitForChart();
    };

    // A busy Log File: ten appends loaded one after the other, the chart asked
    // to update after each load without waiting for it.
    BENCHMARK( "10 appends of 20 Log Lines in quick succession, chart updated" )
    {
        for ( int i = 0; i < 10; ++i ) {
            logFile.append( 20 );
            logFile.loaded();
        }
        return logFile.waitForChart();
    };
}

int main( int argc, char* argv[] )
{
    // Offscreen unless a platform was asked for.
    if ( qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) ) {
        qputenv( "QT_QPA_PLATFORM", "offscreen" );
    }
    QApplication app( argc, argv );
    return Catch::Session().run( argc, argv );
}
