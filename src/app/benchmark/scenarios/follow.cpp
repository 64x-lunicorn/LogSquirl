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

// The scenario "follow" (#670): a Log File grows while LogSquirl follows it,
// and the time from each Log Line's append to its display is reported, with
// whether a chart following the Log File keeps up.
//
// The scenario writes the Log File itself, in the run's own directory: first
// initial_lines Log Lines, which are opened, loaded and shown. It then follows
// the Log File as the window's follow action does, and with chart=true shows
// a chart of one series -- the value=<n> of every Log Line, X its line number
// -- as the chart toggle shows it. None of that is measured. The scenario
// starts once the Text View shows the end of the Log File and the chart its
// last Log Line.
//
// The writer is a thread of the scenario, not the e2e harness: it appends
// lines_per_second Log Lines a second for duration_ms, one whole Log Line per
// write, each due at a fixed moment from the writer's start, and reads the
// clock of the report just before it writes one. Its moments are as exact as
// the thread is scheduled, whatever the event loop does; how late it was
// against its schedule is reported. A Log Line reaches LogSquirl as any
// change on disk does: the file watch of the Watch Policy (native by default)
// tells the Open Log File, which indexes what was appended.
//
// An appended Log Line is displayed by the first paint of the Text View's
// Viewport that shows the Log File through it: following, the view stands at
// the end, so a paint shows every Log Line the Index had when it painted. A
// Log Line the view scrolled past between two paints was displayed by the
// paint that showed a later one. The same for the chart: an appended Log Line
// is charted by the first paint of the chart whose points reach it; the chart
// extracts appended Log Lines after its update delay (250 ms).
//
// Once the writer has written its last Log Line, the scenario waits up to
// settle_ms for every Log Line to be displayed and charted. A Log Line never
// displayed fails the run. The chart kept up when it charted every appended
// Log Line, none more than chart_budget_ms after the Text View displayed it:
// the file watch delays both alike, and what the chart adds -- its update
// delay and its extraction -- is what falls behind when it cannot keep up.
//
// Options:
//   lines_per_second  the rate of the writer; 10
//   duration_ms       how long it writes; 5000
//   initial_lines     Log Lines in the Log File before it grows; 1000
//   chart             true (default) or false: a chart follows the Log File
//   chart_budget_ms   how far behind the Text View the chart keeps up; 1000
//   settle_ms         the most to wait after the last append; 5000
//
// Events:
//   writer_finished          the last Log Line was appended;
//                            data: appended_count
//   last_log_line_displayed  the paint that displayed the last one ended
//   last_log_line_charted    the paint of the chart that charted it ended
// Results:
//   lines_per_second   the rate, as given
//   appended_count     the Log Lines appended
//   writer_lateness    how late each append was against its schedule:
//                      count, min_ms, p50_ms, p99_ms, max_ms, mean_ms
//   display_latency    the append-to-display latency of every appended Log
//                      Line: count, min_ms, p50_ms, p99_ms, max_ms, mean_ms
//   chart_latency      the append-to-chart latency (with chart=true),
//                      chart_behind_display, how much later than the Text
//                      View the chart showed each Log Line, and
//                      chart_charted_count, the appended Log Lines charted
//   chart_kept_up      true or false (with chart=true); chart_budget_ms
//   log_line_count     Log Lines of the Log File at the end
//   log_file_bytes     its size

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <QAction>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QScrollBar>
#include <QTimer>

#include "appendlatency.h"
#include "benchmarkscenario.h"
#include "chartpanel.h"
#include "chartseries.h"
#include "chartwidget.h"
#include "crawlerwidget.h"
#include "linemapping.h"
#include "loadedlogfile.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "paintprobe.h"
#include "scenariorun.h"

namespace logsquirl::benchmark {

namespace {

// Log Line `line` of the Log File, its line feed included: a timestamp, a
// level and the value the chart plots, all from the line number alone.
QByteArray logLine( std::uint64_t line )
{
    const auto seconds = line / 10;
    return QStringLiteral( "2026-10-01 %1:%2:%3.%4 INFO follow-writer line %5 value=%6\n" )
        .arg( seconds / 3600 % 24, 2, 10, QChar( '0' ) )
        .arg( seconds / 60 % 60, 2, 10, QChar( '0' ) )
        .arg( seconds % 60, 2, 10, QChar( '0' ) )
        .arg( line % 10 * 100, 3, 10, QChar( '0' ) )
        .arg( line )
        .arg( line % 97 )
        .toLatin1();
}

// Appends Log Lines to the end of a Log File at a fixed rate, from a thread of
// its own, and keeps the moment of each append for the event loop to take.
class LogWriter {
public:
    struct Append {
        std::uint64_t line;
        Clock::time_point moment;
        // How late it was against its schedule.
        Clock::duration lateness;
    };

    ~LogWriter()
    {
        stop_ = true;
        if ( thread_.joinable() ) {
            thread_.join();
        }
    }

    // Appends count Log Lines from Log Line first on, one every period, and
    // calls finished from the thread once the last is written, or failed when
    // the Log File cannot be written.
    void start( const QString& path, std::uint64_t first, std::uint64_t count,
                Clock::duration period, std::function<void( QString failure )> finished )
    {
        thread_ = std::thread{ [ this, file = QFile::encodeName( path ), first, count, period,
                                 finished = std::move( finished ) ] {
            finished( write( file, first, count, period ) );
        } };
    }

    // The appends since the last call.
    std::vector<Append> take()
    {
        const std::lock_guard lock{ mutex_ };
        return std::exchange( appended_, {} );
    }

private:
    QString write( const QByteArray& path, std::uint64_t first, std::uint64_t count,
                   Clock::duration period )
    {
        // Unbuffered: each Log Line is one write, whole, at its moment.
        std::FILE* file = std::fopen( path.constData(), "ab" );
        if ( file == nullptr ) {
            return QStringLiteral( "the writer cannot open the Log File" );
        }
        std::setvbuf( file, nullptr, _IONBF, 0 );

        const auto started = Clock::now();
        QString failure;
        for ( std::uint64_t index = 0; index < count; ++index ) {
            const auto due = started + period * static_cast<Clock::rep>( index );
            if ( !sleepUntil( due ) ) {
                break;
            }
            const auto line = logLine( first + index );
            const auto moment = Clock::now();
            if ( std::fwrite( line.constData(), 1, static_cast<std::size_t>( line.size() ), file )
                 != static_cast<std::size_t>( line.size() ) ) {
                failure = QStringLiteral( "the writer could not append Log Line %1" )
                              .arg( first + index );
                break;
            }
            const std::lock_guard lock{ mutex_ };
            appended_.push_back( { first + index, moment, moment - due } );
        }
        std::fclose( file );
        return failure;
    }

    // Sleeps until due, on the steady clock, looking at stop_ at least every
    // StopCheck. Returns false when stopped.
    bool sleepUntil( Clock::time_point due ) const
    {
        constexpr auto StopCheck = std::chrono::milliseconds{ 50 };
        while ( !stop_ ) {
            const auto now = Clock::now();
            if ( now >= due ) {
                return true;
            }
            std::this_thread::sleep_until( std::min( due, now + StopCheck ) );
        }
        return false;
    }

    std::thread thread_;
    std::atomic<bool> stop_ = false;
    std::mutex mutex_;
    std::vector<Append> appended_;
};

class Follow : public Scenario {
public:
    void prepare( ScenarioRun& run ) override
    {
        if ( !readNumber( run, "lines_per_second", 10, 1, linesPerSecond_ )
             || !readNumber( run, "duration_ms", 5000, 1, durationMs_ )
             || !readNumber( run, "initial_lines", 1000, 1, initialLines_ )
             || !readNumber( run, "chart_budget_ms", 1000, 1, chartBudgetMs_ )
             || !readNumber( run, "settle_ms", 5000, 0, settleMs_ ) ) {
            return;
        }
        const auto chart = run.option( "chart", "true" );
        if ( chart != "true" && chart != "false" ) {
            failure_ = QStringLiteral( "chart is true or false, not '%1'" ).arg( chart );
            return;
        }
        chart_ = chart == "true";
    }

    void start( ScenarioRun& run ) override
    {
        run_ = &run;
        if ( !failure_.isEmpty() ) {
            run.fail( failure_ );
            return;
        }
        if ( !run.logFiles().empty() ) {
            run.fail( QStringLiteral( "follow writes the Log File it follows; it takes none" ) );
            return;
        }

        path_ = QDir( run.dataDirectory() ).filePath( "follow.log" );
        QFile file( path_ );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
            run.fail( QStringLiteral( "cannot write the Log File: %1" ).arg( file.errorString() ) );
            return;
        }
        for ( std::uint64_t line = 0; line < static_cast<std::uint64_t>( initialLines_ ); ++line ) {
            file.write( logLine( line ) );
        }
        file.close();

        logFile_ = std::make_unique<LoadedLogFile>(
            run, QStringLiteral( "follow" ), path_,
            [ this ]( MainWindow& window, CrawlerWidget& crawler ) { loaded( window, crawler ); } );
        logFile_->open();
    }

private:
    bool readNumber( ScenarioRun& run, const char* name, int fallback, int least, int& value )
    {
        bool isNumber = false;
        value = run.option( name, QString::number( fallback ) ).toInt( &isNumber );
        if ( !isNumber || value < least ) {
            failure_ = QStringLiteral( "%1 takes a number from %2 on, not '%3'" )
                           .arg( name )
                           .arg( least )
                           .arg( run.option( name ) );
            return false;
        }
        return true;
    }

    void loaded( MainWindow& window, CrawlerWidget& crawler )
    {
        crawler_ = &crawler;
        mainView_ = crawler.findChild<LogMainView*>();
        auto* follow = window.findChild<QAction*>( "followAction" );
        if ( !mainView_ || follow == nullptr || !follow->isEnabled() ) {
            run_->fail( QStringLiteral( "the window has no Text View or cannot follow" ) );
            return;
        }

        displayProbe_ = std::make_unique<PaintProbe>(
            mainView_->viewport(),
            [ this ]( Clock::time_point, Clock::time_point ended ) { displayPainted( ended ); } );

        if ( chart_ ) {
            auto* panel = crawler.findChild<ChartPanel*>();
            auto* toggle = window.findChild<QAction*>( "toggleChartPanelAction" );
            chartWidget_ = panel != nullptr ? panel->findChild<ChartWidget*>() : nullptr;
            if ( panel == nullptr || toggle == nullptr || !chartWidget_ ) {
                run_->fail( QStringLiteral( "the Log File's tab has no chart" ) );
                return;
            }
            chartPanel_ = panel;
            ChartSeriesDefinition series;
            series.id = QStringLiteral( "follow-value" );
            series.name = QStringLiteral( "value" );
            series.color = QColor( "#2196F3" );
            series.pattern = QStringLiteral( "value=(\\d+)" );
            series.captureGroup = 1;
            panel->setSeriesDefinitions( { series } );
            chartProbe_ = std::make_unique<PaintProbe>(
                chartWidget_,
                [ this ]( Clock::time_point, Clock::time_point ended ) { chartPainted( ended ); } );
            // Shows the chart and extracts its points.
            toggle->trigger();
        }

        // As the user follows the Log File: the view goes to its end.
        if ( !follow->isChecked() ) {
            follow->trigger();
        }
        if ( !crawler.isFollowEnabled() ) {
            run_->fail( QStringLiteral( "the follow action did not follow the Log File" ) );
            return;
        }
        mainView_->viewport()->update();
    }

    // The last Log Line the Text View shows, when it follows the end.
    std::optional<std::uint64_t> displayedThrough() const
    {
        if ( !crawler_ || !mainView_ || !crawler_->isFollowEnabled() ) {
            return {};
        }
        const auto* bar = mainView_->verticalScrollBar();
        const auto count = mainView_->lineMapping().logLineCount().get();
        if ( bar->value() != bar->maximum() || count == 0 ) {
            return {};
        }
        return count - 1;
    }

    // The last Log Line the chart has a point for.
    std::optional<std::uint64_t> chartedThrough() const
    {
        if ( !chartPanel_ ) {
            return {};
        }
        const auto series = chartPanel_->seriesDefinitions();
        if ( series.isEmpty() || series.front().points.isEmpty() ) {
            return {};
        }
        return series.front().points.back().line.get();
    }

    void displayPainted( Clock::time_point ended )
    {
        const auto through = displayedThrough();
        if ( !through.has_value() ) {
            return;
        }
        if ( !writing_ ) {
            displayReady_ = *through + 1 >= static_cast<std::uint64_t>( initialLines_ );
            startWhenReady();
            return;
        }
        takeAppends();
        if ( !displayed_.shown( *through, ended ).empty() && settled() ) {
            done();
        }
    }

    void chartPainted( Clock::time_point ended )
    {
        const auto through = chartedThrough();
        if ( !through.has_value() ) {
            return;
        }
        if ( !writing_ ) {
            chartReady_ = *through + 1 >= static_cast<std::uint64_t>( initialLines_ );
            startWhenReady();
            return;
        }
        takeAppends();
        if ( !charted_.shown( *through, ended ).empty() && settled() ) {
            done();
        }
    }

    void startWhenReady()
    {
        if ( writing_ || !displayReady_ || ( chart_ && !chartReady_ ) ) {
            return;
        }
        writing_ = true;
        // Not from within the paint.
        QTimer::singleShot( 0, run_->context(), [ this ] {
            run_->report().markScenarioStart();
            const auto count = static_cast<std::uint64_t>( linesPerSecond_ )
                               * static_cast<std::uint64_t>( durationMs_ ) / 1000;
            appendCount_ = std::max<std::uint64_t>( count, 1 );
            const auto period = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>( 1.0 / linesPerSecond_ ) );
            // The context outlives the writer's thread: the scenario joins it.
            auto* context = run_->context();
            writer_.start(
                path_, static_cast<std::uint64_t>( initialLines_ ), appendCount_, period,
                [ context, this ]( QString failure ) {
                    QMetaObject::invokeMethod(
                        context,
                        [ this, failure = std::move( failure ) ] { writerFinished( failure ); },
                        Qt::QueuedConnection );
                } );
        } );
    }

    void writerFinished( const QString& failure )
    {
        if ( !failure.isEmpty() ) {
            run_->fail( failure );
            return;
        }
        takeAppends();
        writerFinished_ = true;
        run_->report().event( "writer_finished",
                              QJsonObject{ { "appended_count", static_cast<qint64>( taken_ ) } } );
        if ( taken_ != appendCount_ ) {
            run_->fail( QStringLiteral( "the writer appended %1 Log Lines, not %2" )
                            .arg( taken_ )
                            .arg( appendCount_ ) );
            return;
        }
        if ( settled() ) {
            done();
            return;
        }
        QTimer::singleShot( std::chrono::milliseconds{ settleMs_ }, run_->context(),
                            [ this ] { done(); } );
    }

    void takeAppends()
    {
        for ( const auto& append : writer_.take() ) {
            displayed_.appended( append.line, append.moment );
            if ( chart_ ) {
                charted_.appended( append.line, append.moment );
            }
            lateness_.push_back(
                std::chrono::duration<double, std::milli>( append.lateness ).count() );
            moments_.push_back( append.moment );
            ++taken_;
        }
    }

    // Every Log Line the writer appended was displayed, and charted.
    bool settled() const
    {
        return writerFinished_ && displayed_.waitingCount() == 0
               && ( !chart_ || charted_.waitingCount() == 0 );
    }

    void done()
    {
        if ( finished_ ) {
            return;
        }
        finished_ = true;
        // From the event loop: not from within the paint.
        QTimer::singleShot( 0, run_->context(), [ this ] { report(); } );
    }

    void report()
    {
        auto& report = run_->report();
        if ( displayed_.waitingCount() != 0 ) {
            run_->fail( QStringLiteral( "%1 of %2 appended Log Lines were never displayed" )
                            .arg( displayed_.waitingCount() )
                            .arg( taken_ ) );
            return;
        }

        report.setResult( "lines_per_second", linesPerSecond_ );
        report.setResult( "appended_count", static_cast<qint64>( taken_ ) );
        report.setResult( "writer_lateness", Distribution::of( lateness_ ).toJson() );
        report.setResult( "display_latency", displayed_.distribution().toJson() );
        const auto lastDisplayed = moments_.back() + toDuration( displayed_.latencies().back() );
        report.eventAt( "last_log_line_displayed", lastDisplayed );

        if ( chart_ ) {
            // How far the chart was behind the Text View for each Log Line:
            // the file watch's part of both latencies is not the chart's.
            const auto& charted = charted_.latencies();
            const auto& displayed = displayed_.latencies();
            std::vector<double> behind;
            for ( std::size_t line = 0; line < charted.size(); ++line ) {
                behind.push_back( std::max( 0.0, charted[ line ] - displayed[ line ] ) );
            }
            const auto behindDisplay = Distribution::of( behind );
            const auto keptUp = charted_.waitingCount() == 0
                                && behindDisplay.max <= static_cast<double>( chartBudgetMs_ );
            report.setResult( "chart_latency", charted_.distribution().toJson() );
            report.setResult( "chart_behind_display", behindDisplay.toJson() );
            report.setResult( "chart_charted_count", static_cast<qint64>( charted.size() ) );
            report.setResult( "chart_kept_up", keptUp );
            report.setResult( "chart_budget_ms", chartBudgetMs_ );
            if ( charted_.waitingCount() == 0 ) {
                report.eventAt( "last_log_line_charted",
                                moments_.back() + toDuration( charted.back() ) );
            }
        }

        report.setResult(
            "log_line_count",
            static_cast<qint64>( mainView_ ? mainView_->lineMapping().logLineCount().get() : 0 ) );
        report.setResult( "log_file_bytes", logFile_->bytes() );
        run_->finish();
    }

    static Clock::duration toDuration( double milliseconds )
    {
        return std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double, std::milli>( milliseconds ) );
    }

    QString failure_;
    int linesPerSecond_ = 0;
    int durationMs_ = 0;
    int initialLines_ = 0;
    int chartBudgetMs_ = 0;
    int settleMs_ = 0;
    bool chart_ = true;

    ScenarioRun* run_ = nullptr;
    QString path_;
    std::unique_ptr<LoadedLogFile> logFile_;
    QPointer<CrawlerWidget> crawler_;
    QPointer<LogMainView> mainView_;
    QPointer<ChartPanel> chartPanel_;
    QPointer<ChartWidget> chartWidget_;
    std::unique_ptr<PaintProbe> displayProbe_;
    std::unique_ptr<PaintProbe> chartProbe_;

    bool displayReady_ = false;
    bool chartReady_ = false;
    bool writing_ = false;
    bool writerFinished_ = false;
    bool finished_ = false;
    std::uint64_t appendCount_ = 0;
    std::uint64_t taken_ = 0;
    AppendLatency displayed_;
    AppendLatency charted_;
    std::vector<double> lateness_;
    std::vector<Clock::time_point> moments_;

    // Last: its thread stops, and is joined, before anything it reports to goes.
    LogWriter writer_;
};

const ScenarioRegistration registration{
    "follow",
    "Follows a Log File appended to at a fixed rate; reports each Log Line until displayed",
    [] { return std::make_unique<Follow>(); }
};

} // namespace

} // namespace logsquirl::benchmark
