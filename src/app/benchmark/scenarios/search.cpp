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

// The scenario "search" (#668): runs one Search on a loaded Log File, as a
// user typing the pattern into the Search Line and pressing Return does, and
// reports when its first Match is displayed and when it is finished.
//
// The Log File is opened and loaded first; that is not measured. The scenario
// starts with the request of the Search.
//
// Options:
//   pattern     what is searched for (required)
//   regex       "true": the pattern is a regular expression; plain text
//               otherwise (the Search Line's regular expression button)
//   match_case  "false": case is ignored; it is matched otherwise
//   logical     "true": the pattern is a logical combination of quoted
//               sub-patterns, Field Terms among them (#798); plain otherwise
//   format      a Log Format file copied into the run's own formats folder
//               before the Catalog reads it, where it replaces a Log Format
//               of the same name, and Log Formats are recognized: for a
//               Search with Field Terms, which reads the fields of the Log
//               Format recognized for the Log File
//
// Events:
//   first_match_displayed  the first paint of the Filtered View's Viewport
//                          that shows a Match has ended; none when the
//                          Search has no Match
//   search_finished        the Search is complete, as the Search Line is told;
//                          data: match_count
// Results:
//   match_count            the Matches of the Search
//   logical                whether the pattern was a logical combination
//                          (#798). A build from before that option ran the
//                          pattern as text and reports no such result, which
//                          the performance suite reads to skip its Field Term
//                          cases on that build, the before side of the
//                          Benchmarks workflow.
//   undecided_count        the Log Lines whose match the regex engine could
//                          not decide (#689)
//   log_line_count         Log Lines of the Log File
//   log_file_bytes         its size
//   search_gb_per_s        log_file_bytes in GB (10^9 bytes) over the time from
//                          the request to search_finished

#include <memory>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QPointer>

#include "benchmarkscenario.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "filteredview.h"
#include "loadedlogfile.h"
#include "logformatcatalog.h"
#include "paintprobe.h"
#include "regularexpressionpattern.h"
#include "scenariorun.h"
#include "searchlinewidget.h"

namespace logsquirl::benchmark {

namespace {

class Search : public Scenario {
public:
    void prepare( ScenarioRun& run ) override
    {
        // A Log Format of the scenario's own, in the run's formats folder
        // before the Catalog reads it; the run's folder goes with the run.
        // Only then are Log Formats recognized: the Searches without one run
        // as they always did.
        const auto format = run.option( "format" );
        if ( format.isEmpty() ) {
            return;
        }
        const auto folder = LogFormatCatalog::defaultUserFormatsDirectory();
        if ( folder.isEmpty() || !QDir().mkpath( folder )
             || !QFile::copy( format,
                              QDir( folder ).filePath( QFileInfo( format ).fileName() ) ) ) {
            failure_ = QStringLiteral( "the Log Format %1 could not be copied into %2" )
                           .arg( format, folder );
            return;
        }
        auto& config = Configuration::getSynced();
        config.setAutoDetectLogFormats( true );
        config.setAutoShowTableView( false );
        config.save();
    }

    void start( ScenarioRun& run ) override
    {
        run_ = &run;
        if ( !failure_.isEmpty() ) {
            run.fail( failure_ );
            return;
        }
        const auto pattern = run.option( "pattern" );
        if ( pattern.isEmpty() ) {
            run.fail( QStringLiteral( "search needs the option pattern=<what is searched for>" ) );
            return;
        }
        const auto useRegexp = run.option( "regex", "false" ) == "true";
        const auto matchCase = run.option( "match_case", "true" ) != "false";
        const auto logical = run.option( "logical", "false" ) == "true";
        logical_ = logical;

        logFile_ = std::make_unique<LoadedLogFile>(
            run, QStringLiteral( "search" ),
            [ this, pattern, useRegexp, matchCase, logical ]( MainWindow&,
                                                              CrawlerWidget& crawler ) {
                search( crawler, RegularExpressionPattern( pattern, matchCase, false, logical,
                                                           !useRegexp ) );
            } );
        logFile_->open();
    }

private:
    void search( CrawlerWidget& crawler, const RegularExpressionPattern& pattern )
    {
        auto* searchLine = crawler.findChild<SearchLineWidget*>();
        filteredView_ = crawler.findChild<FilteredView*>();
        if ( searchLine == nullptr || !filteredView_ ) {
            run_->fail(
                QStringLiteral( "the Log File's tab has no Search Line or Filtered View" ) );
            return;
        }

        displayProbe_ = std::make_unique<PaintProbe>(
            filteredView_->viewport(),
            [ this ]( Clock::time_point, Clock::time_point ended ) { painted( ended ); } );
        QObject::connect( &crawler, &CrawlerWidget::searchProgressed, run_->context(),
                          [ this ]( const SearchSession::State& state ) { progressed( state ); } );

        // The pattern and the buttons as the user types and sets them; the
        // Search runs when asked, as Return asks.
        searchLine->apply( pattern );
        run_->report().markScenarioStart();
        searchLine->requestSearch();
    }

    void painted( Clock::time_point ended )
    {
        if ( !filteredView_ || run_->report().hasEvent( "first_match_displayed" )
             || !filteredView_->lineMapping().logLineAt( 0_lnum ).has_value() ) {
            return;
        }
        run_->report().eventAt( "first_match_displayed", ended );
        // Not from within the paint it is called from.
        displayProbe_.release()->deleteLater();
        finishWhenDone();
    }

    void progressed( const SearchSession::State& state )
    {
        if ( run_->report().hasEvent( "search_finished" ) ) {
            return;
        }
        switch ( state.phase ) {
        case SearchSession::Phase::Complete:
            break;
        case SearchSession::Phase::InvalidPattern:
        case SearchSession::Phase::Failed:
            run_->fail( QStringLiteral( "the Search failed: %1" ).arg( state.errorString ) );
            return;
        case SearchSession::Phase::Interrupted:
            run_->fail( QStringLiteral( "the Search was interrupted" ) );
            return;
        default:
            return;
        }

        matchCount_ = static_cast<qint64>( state.matchCount.get() );
        run_->report().event( "search_finished", QJsonObject{ { "match_count", matchCount_ } } );

        auto& report = run_->report();
        report.setResult( "match_count", matchCount_ );
        report.setResult( "logical", logical_ );
        report.setResult( "undecided_count", static_cast<qint64>( state.undecidedCount.get() ) );
        report.setResult( "log_line_count", logFile_->logLineCount() );
        report.setResult( "log_file_bytes", logFile_->bytes() );
        const auto searchMs = report.millisecondsSinceScenarioStart( "search_finished" );
        if ( searchMs && *searchMs > 0.0 ) {
            report.setResult( "search_gb_per_s", static_cast<double>( logFile_->bytes() ) / 1e9
                                                     / ( *searchMs / 1000.0 ) );
        }
        finishWhenDone();
    }

    void finishWhenDone()
    {
        const auto& report = run_->report();
        if ( report.hasEvent( "search_finished" )
             && ( matchCount_ == 0 || report.hasEvent( "first_match_displayed" ) ) ) {
            run_->finish();
        }
    }

    ScenarioRun* run_ = nullptr;
    QString failure_;
    bool logical_ = false;
    std::unique_ptr<LoadedLogFile> logFile_;
    QPointer<FilteredView> filteredView_;
    std::unique_ptr<PaintProbe> displayProbe_;
    qint64 matchCount_ = 0;
};

const ScenarioRegistration registration{
    "search", "Runs a Search on a loaded Log File; reports its first Match displayed and its end",
    [] { return std::make_unique<Search>(); }
};

} // namespace

} // namespace logsquirl::benchmark
