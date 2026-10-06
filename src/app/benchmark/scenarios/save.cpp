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

// The scenario "save" (#730): marks Log Lines of a loaded and searched Log
// File, saves what the Filtered View displays to a file, and removes the
// Marks again, as a user marking Log Lines in the Text View, choosing Select
// All and Save selected to file in the Filtered View, and unmarking them does.
//
// The Log File is opened and loaded, and the Search run, first; none of that
// is measured. The scenario starts once the Search is complete. A Mark is set
// as the Mark shortcut sets it: its Log Line is selected in the Text View and
// marked. The Marks are spread evenly over the Log File. A Mark set on a Log
// Line the Filtered View already displays as a Match adds nothing to the save.
//
// It is also what trains a profile-guided build on Marks and on saving
// displayed lines, which no other scenario runs (#730).
//
// Options:
//   pattern  what is searched for, as plain text (required)
//   marks    the Log Lines marked; 200
//
// Events:
//   marks_added      every Mark is set
//   saved            the file is written
//   marks_removed    every Mark is removed again
// Results:
//   match_count       the Matches of the Search
//   mark_count        the Marks set
//   saved_line_count  the Log Lines in the saved file
//   saved_bytes       its size
//   log_line_count    Log Lines of the Log File
//   log_file_bytes    its size

#include <memory>

#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include "benchmarkscenario.h"
#include "crawlerwidget.h"
#include "filteredview.h"
#include "loadedlogfile.h"
#include "logmainview.h"
#include "regularexpressionpattern.h"
#include "scenariorun.h"
#include "searchlinewidget.h"

namespace logsquirl::benchmark {

namespace {

class Save : public Scenario {
public:
    void start( ScenarioRun& run ) override
    {
        run_ = &run;
        const auto pattern = run.option( "pattern" );
        if ( pattern.isEmpty() ) {
            run.fail( QStringLiteral( "save needs the option pattern=<what is searched for>" ) );
            return;
        }
        QString failure;
        if ( !run.numberOption( "marks", 200, 1, marks_, failure ) ) {
            run.fail( failure );
            return;
        }
        savedFile_ = QDir( run.dataDirectory() ).filePath( "saved.log" );

        logFile_ = std::make_unique<LoadedLogFile>(
            run, QStringLiteral( "save" ),
            [ this, pattern ]( MainWindow&, CrawlerWidget& crawler ) {
                search( crawler, RegularExpressionPattern( pattern, true, false, false, true ) );
            } );
        logFile_->open();
    }

private:
    void search( CrawlerWidget& crawler, const RegularExpressionPattern& pattern )
    {
        auto* searchLine = crawler.findChild<SearchLineWidget*>();
        mainView_ = crawler.findChild<LogMainView*>();
        filteredView_ = crawler.findChild<FilteredView*>();
        if ( searchLine == nullptr || !mainView_ || !filteredView_ ) {
            run_->fail( QStringLiteral(
                "the Log File's tab has no Search Line, Text View or Filtered View" ) );
            return;
        }

        QObject::connect( &crawler, &CrawlerWidget::searchProgressed, run_->context(),
                          [ this ]( const SearchSession::State& state ) { progressed( state ); } );
        searchLine->apply( pattern );
        searchLine->requestSearch();
    }

    void progressed( const SearchSession::State& state )
    {
        if ( searched_ ) {
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

        searched_ = true;
        matchCount_ = static_cast<qint64>( state.matchCount.get() );
        // From the event loop, not from within the Search's progress; the
        // save below runs a modal dialog of its own.
        QTimer::singleShot( 0, run_->context(), [ this ] { markSaveAndUnmark(); } );
    }

    void markSaveAndUnmark()
    {
        if ( !mainView_ || !filteredView_ ) {
            run_->fail( QStringLiteral( "the Log File's tab went" ) );
            return;
        }
        auto& report = run_->report();
        report.markScenarioStart();

        if ( !toggleMarks() ) {
            return;
        }
        report.event( "marks_added" );

        // Blocks behind its progress dialog until the file is written.
        filteredView_->selectAll();
        filteredView_->saveSelectedTo( savedFile_ );
        report.event( "saved" );

        // Marking a marked Log Line again removes its Mark.
        if ( !toggleMarks() ) {
            return;
        }
        report.event( "marks_removed" );

        QFile saved{ savedFile_ };
        if ( !saved.open( QIODevice::ReadOnly ) ) {
            run_->fail( QStringLiteral( "nothing was saved to %1" ).arg( savedFile_ ) );
            return;
        }
        const auto text = saved.readAll();
        report.setResult( "match_count", matchCount_ );
        report.setResult( "mark_count", static_cast<qint64>( marks_ ) );
        report.setResult( "saved_line_count", static_cast<qint64>( text.count( '\n' ) ) );
        report.setResult( "saved_bytes", static_cast<qint64>( text.size() ) );
        report.setResult( "log_line_count", logFile_->logLineCount() );
        report.setResult( "log_file_bytes", logFile_->bytes() );
        run_->finish();
    }

    // Marks each of the Log Lines spread evenly over the Log File, or removes
    // its Mark, with the Text View's Mark action on it selected.
    bool toggleMarks()
    {
        const auto lineCount = logFile_->logLineCount();
        for ( qint64 mark = 0; mark < marks_; ++mark ) {
            mainView_->selectAndDisplayLine( LineNumber(
                static_cast<LineNumber::UnderlyingType>( mark * lineCount / marks_ ) ) );
            if ( !QMetaObject::invokeMethod( mainView_, "markSelected", Qt::DirectConnection ) ) {
                run_->fail( QStringLiteral( "the Text View has no Mark action" ) );
                return false;
            }
        }
        return true;
    }

    ScenarioRun* run_ = nullptr;
    int marks_ = 200;
    QString savedFile_;
    std::unique_ptr<LoadedLogFile> logFile_;
    QPointer<LogMainView> mainView_;
    QPointer<FilteredView> filteredView_;
    bool searched_ = false;
    qint64 matchCount_ = 0;
};

const ScenarioRegistration registration{
    "save",
    "Marks Log Lines of a searched Log File, saves the Filtered View's Log Lines and unmarks them",
    [] { return std::make_unique<Save>(); }
};

} // namespace

} // namespace logsquirl::benchmark
