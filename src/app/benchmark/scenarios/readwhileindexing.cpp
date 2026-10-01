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

// The scenario "read-while-indexing" (#686): opens one Log File and, while it
// is indexed, reads it from the UI thread at a fixed rate, as a view does
// that a user scrolls through meanwhile: the
// number of Log Lines (getNbLine), one Log Line (getLineString) and a screen
// of lines Log Lines (getExpandedLines), each read timed on its own. The
// indexing itself reports its wall time, the CPU time the process spent in it
// and their ratio, the effective parallelism.
//
// What instruction counts cannot see (#671): Cachegrind runs one thread at a
// time, so a read that waits for the index lock (#289) and indexing that
// falls back to one thread (#290) cost no instruction more. Here the first
// shows as longer reads, the second as a parallelism near 1 and a longer wall
// time.
//
// The window is opened first with a small Log File of the scenario's own, in
// the run's directory, loaded and shown, unmeasured: once a window has
// loaded the plugins, a Log File asked for opens at once, so the stretch of
// indexing measured starts with the request to open the Log File -- the
// scenario's start -- and not some time into its indexing, and nothing it
// waits for is in it. It ends with the Index finished. Its CPU time is the
// whole process's, the UI thread's tab, reads and paints included.
//
// The reads start when the Log File's tab opens and end with its Index
// finished: every read_interval_ms one read of each kind, the screen at a
// place of its own each time -- spread over the Log Lines indexed so far by
// the golden ratio, so that no two are near each other -- and getLineString
// of its first Log Line. A read that is due while one is still under way is
// late, as a paint would be, and none is made up for. Before the first Log
// Lines are indexed only getNbLine is read.
//
// Options:
//   read_interval_ms  the time from one read to the next; 2, so that even a
//                     Log File indexed in a fraction of a second is read
//                     often enough for a 99th percentile
//   lines             the Log Lines of a screen, getExpandedLines; 60
//
// Events:
//   log_file_opened  the Log File's tab opened; its loading had started
//   index_finished   the Index is complete; data: log_line_count, read_count,
//                    the reads of getNbLine until then
// Results:
//   nb_line_latency          every getNbLine: count, min_ms, p50_ms, p99_ms,
//                            max_ms, mean_ms
//   line_string_latency      every getLineString, the same
//   expanded_lines_latency   every getExpandedLines of lines Log Lines (fewer
//                            while fewer are indexed), the same
//   read_count               the reads of getNbLine
//   indexing                 wall_ms, from the scenario's start, the request
//                            to open the Log File, to index_finished;
//                            cpu_ms, the process's CPU time in it, every
//                            thread together; parallelism, cpu_ms / wall_ms
//                            (cpu_ms and parallelism where the platform tells
//                            the CPU time)
//   index_mb_per_s           log_file_bytes in MB (10^6 bytes) over wall_ms
//   log_line_count           Log Lines of the Log File
//   log_file_bytes           its size
//
// The run fails when the Index finished before a single Log Line could be
// read: the Log File is too small for what is measured.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

#include "abstractlogdata.h"
#include "benchmarkscenario.h"
#include "crawlerwidget.h"
#include "loadedlogfile.h"
#include "loadingstatus.h"
#include "logfileprovenance.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "processwork.h"
#include "scenariorun.h"
#include "tabbedcrawlerwidget.h"

namespace logsquirl::benchmark {

namespace {

class ReadWhileIndexing : public Scenario {
public:
    void prepare( ScenarioRun& run ) override
    {
        if ( run.numberOption( "read_interval_ms", 2, 1, readIntervalMs_, failure_ ) ) {
            run.numberOption( "lines", 60, 1, lines_, failure_ );
        }
    }

    void start( ScenarioRun& run ) override
    {
        run_ = &run;
        if ( !failure_.isEmpty() ) {
            run.fail( failure_ );
            return;
        }
        if ( run.logFiles().size() != 1 ) {
            run.fail( QStringLiteral( "read-while-indexing opens exactly one Log File, %1 given" )
                          .arg( run.logFiles().size() ) );
            return;
        }

        // A Log File of a few Log Lines, which the window loads first.
        const auto first = QDir( run.dataDirectory() ).filePath( "first.log" );
        QFile file( first );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
            run.fail( QStringLiteral( "cannot write the Log File: %1" ).arg( file.errorString() ) );
            return;
        }
        for ( int line = 0; line < 10; ++line ) {
            file.write( QByteArray( "2026-10-01 12:00:00.000 INFO first Log Line " )
                        + QByteArray::number( line ) + '\n' );
        }
        file.close();

        firstLogFile_ = std::make_unique<LoadedLogFile>(
            run, QStringLiteral( "read-while-indexing" ), first,
            [ this ]( MainWindow& window, CrawlerWidget& ) { openMeasured( window ); } );
        firstLogFile_->open();
    }

private:
    void openMeasured( MainWindow& window )
    {
        auto* tabs = window.findChild<TabbedCrawlerWidget*>();
        if ( tabs == nullptr ) {
            run_->fail( QStringLiteral( "the window has no tabs" ) );
            return;
        }
        // The tab of the Log File is made current as it opens.
        QObject::connect( tabs, &QTabWidget::currentChanged, run_->context(), [ this, tabs ] {
            if ( auto* crawler = qobject_cast<CrawlerWidget*>( tabs->currentWidget() ) ) {
                opened( crawler );
            }
        } );

        indexingStarted_ = ProcessWork::Moment::now();
        run_->report().markScenarioStart( indexingStarted_.wall );
        if ( !window.openLogFile( run_->logFiles().front(), LogFileProvenance::ordinary() ) ) {
            run_->fail( QStringLiteral( "the Log File did not open" ) );
            return;
        }
        if ( !mainView_ ) {
            run_->fail( QStringLiteral( "the Log File did not open at once" ) );
        }
    }

    void opened( CrawlerWidget* crawler )
    {
        if ( mainView_ || run_->report().hasEvent( "log_file_opened" ) ) {
            return;
        }
        run_->report().event( "log_file_opened" );

        mainView_ = crawler->findChild<LogMainView*>();
        if ( !mainView_ ) {
            run_->fail( QStringLiteral( "the Log File's tab has no Text View" ) );
            return;
        }

        // Its loading has only started, and the event loop has not got to
        // any of it yet: neither the end of the load nor a read is missed.
        QObject::connect( crawler, &CrawlerWidget::loadingFinished, run_->context(),
                          [ this ]( LoadingStatus status, const QString& failure ) {
                              loadingFinished( status, failure );
                          } );

        reads_ = new QTimer( run_->context() );
        reads_->setTimerType( Qt::PreciseTimer );
        reads_->setInterval( readIntervalMs_ );
        QObject::connect( reads_, &QTimer::timeout, run_->context(), [ this ] { read(); } );
        reads_->start();
        read();
    }

    void read()
    {
        if ( !mainView_ ) {
            return;
        }
        // The Log File the Text View shows, read as the view reads it.
        const auto& logFile = mainView_->lineMapping().logFile();

        const auto beforeCount = Clock::now();
        const auto indexed = logFile.getNbLine();
        const auto counted = Clock::now();
        nbLine_.push_back( millisecondsBetween( beforeCount, counted ) );

        if ( indexed.get() == 0 ) {
            return;
        }
        const auto screen = std::min<LinesCount::UnderlyingType>(
            static_cast<LinesCount::UnderlyingType>( lines_ ), indexed.get() );
        // The fractional parts of the multiples of the golden ratio: each
        // read far from the reads before it.
        const auto place = std::fmod( static_cast<double>( screenReads_ ) * GoldenRatio, 1.0 );
        ++screenReads_;
        const auto first = LineNumber( static_cast<LineNumber::UnderlyingType>(
            place * static_cast<double>( indexed.get() - screen ) ) );

        const auto beforeLine = Clock::now();
        logFile.getLineString( first );
        const auto lineRead = Clock::now();
        lineString_.push_back( millisecondsBetween( beforeLine, lineRead ) );

        const auto beforeScreen = Clock::now();
        logFile.getExpandedLines( first, LinesCount( screen ) );
        const auto screenRead = Clock::now();
        expandedLines_.push_back( millisecondsBetween( beforeScreen, screenRead ) );
    }

    void loadingFinished( LoadingStatus status, const QString& failure )
    {
        if ( run_->report().hasEvent( "index_finished" ) ) {
            return;
        }
        const auto ended = ProcessWork::Moment::now();
        const auto indexing = indexingStarted_.until( ended );
        if ( reads_ ) {
            reads_->stop();
        }
        if ( status != LoadingStatus::Successful ) {
            run_->fail( QStringLiteral( "the Log File was not indexed: %1" )
                            .arg( failure.isEmpty() ? QStringLiteral( "interrupted" ) : failure ) );
            return;
        }

        const auto lines
            = static_cast<qint64>( mainView_ ? mainView_->lineMapping().logLineCount().get() : 0 );
        const auto readCount = static_cast<qint64>( nbLine_.size() );
        run_->report().eventAt(
            "index_finished", ended.wall,
            QJsonObject{ { "log_line_count", lines }, { "read_count", readCount } } );

        if ( lineString_.empty() ) {
            run_->fail( QStringLiteral( "the Log File was indexed before a Log Line could be read: "
                                        "it is too small to read while it is indexed" ) );
            return;
        }

        auto& report = run_->report();
        report.setResult( "nb_line_latency", Distribution::of( nbLine_ ).toJson() );
        report.setResult( "line_string_latency", Distribution::of( lineString_ ).toJson() );
        report.setResult( "expanded_lines_latency", Distribution::of( expandedLines_ ).toJson() );
        report.setResult( "read_count", readCount );
        report.setResult( "indexing", indexing.toJson() );

        const auto bytes = QFileInfo( run_->logFiles().front() ).size();
        report.setResult( "log_line_count", lines );
        report.setResult( "log_file_bytes", bytes );
        if ( indexing.wallMs > 0.0 ) {
            report.setResult( "index_mb_per_s",
                              static_cast<double>( bytes ) / 1e6 / ( indexing.wallMs / 1000.0 ) );
        }
        run_->finish();
    }

    static constexpr double GoldenRatio = 0.6180339887498949;

    QString failure_;
    int readIntervalMs_ = 2;
    int lines_ = 60;

    ScenarioRun* run_ = nullptr;
    std::unique_ptr<LoadedLogFile> firstLogFile_;
    QPointer<LogMainView> mainView_;
    QPointer<QTimer> reads_;
    ProcessWork::Moment indexingStarted_;
    std::vector<double> nbLine_;
    std::vector<double> lineString_;
    std::vector<double> expandedLines_;
    std::uint64_t screenReads_ = 0;
};

const ScenarioRegistration registration{
    "read-while-indexing",
    "Reads a Log File from the UI thread while it is indexed; reports each read and the "
    "indexing's parallelism",
    [] { return std::make_unique<ReadWhileIndexing>(); }
};

} // namespace

} // namespace logsquirl::benchmark
