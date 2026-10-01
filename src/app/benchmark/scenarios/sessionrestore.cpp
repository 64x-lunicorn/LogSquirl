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

// The scenario "session-restore" (#670): restores a Session of several tabs,
// as a start of the application restores the last one, and reports when the
// tab in front is usable and when every tab is indexed.
//
// The Session is generated from the Log Files of the run, one tab each in
// their order, and written into the run's own Session before the application
// reads it -- never the user's (a Benchmark Run, CONTEXT.md). Each tab is
// saved with the view state a user leaves: marks Marks (on Log Lines 0, 10,
// 20 ...), applied once its Log File has loaded. The tab in front is the one
// at current.
//
// The Session does not keep Kept Searches yet (#704): a tab is restored with
// its Search flags, never with a Search, so none runs.
//
// What is measured starts as the restore does: the windows of the Session are
// built, the Log File of the tab in front loads first and the others after it,
// one after another (#300). The tab in front is usable at the end of the first
// paint of its Text View's Viewport that starts after its Index finished and
// shows Log Lines. Every tab is indexed when the last of their Indexes has
// finished.
//
// Options:
//   current  the tab in front, from 0; 0
//   marks    Marks saved with each tab; 10
//
// Events:
//   tab_indexed         the Index of a tab finished; data: tab, from 0, and
//                       log_line_count
//   current_tab_usable  the paint that made the tab in front usable ended
//   all_tabs_indexed    the Index of the last tab finished
// Results:
//   tab_count       tabs restored
//   current_tab     the tab in front
//   marks_per_tab   Marks saved with each tab
//   log_line_count  Log Lines of all the Log Files
//   log_file_bytes  their size

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include <QFileInfo>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

#include "benchmarkscenario.h"
#include "crawlerwidget.h"
#include "linemapping.h"
#include "loadingstatus.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "paintprobe.h"
#include "scenariorun.h"
#include "sessioninfo.h"
#include "tabbedcrawlerwidget.h"
#include "viewstatecodec.h"

namespace logsquirl::benchmark {

namespace {

// The window of the generated Session.
constexpr const char SessionWindowId[] = "benchmark-session";

class SessionRestore : public Scenario {
public:
    void prepare( ScenarioRun& run ) override
    {
        const auto& logFiles = run.logFiles();
        if ( logFiles.size() < 2 ) {
            failure_ = QStringLiteral( "session-restore restores several tabs: give it at least "
                                       "two Log Files, not %1" )
                           .arg( logFiles.size() );
            return;
        }
        if ( !run.numberOption( "current", 0, 0, current_, failure_ )
             || !run.numberOption( "marks", 10, 0, marks_, failure_ ) ) {
            return;
        }
        if ( current_ >= static_cast<int>( logFiles.size() ) ) {
            failure_ = QStringLiteral( "current is a tab from 0 to %1, not %2" )
                           .arg( logFiles.size() - 1 )
                           .arg( current_ );
            return;
        }

        ViewState state;
        for ( auto mark = 0; mark < marks_; ++mark ) {
            state.marks.append( static_cast<LineNumber::UnderlyingType>( mark ) * 10 );
        }
        const auto viewContext = encodeViewState( state );

        std::vector<SessionInfo::OpenFile> openFiles;
        for ( const auto& logFile : logFiles ) {
            openFiles.emplace_back( logFile, viewContext );
        }

        // Into the run's own Session: DataLocation isolated it before this.
        auto& session = SessionInfo::getSynced();
        session.add( SessionWindowId );
        session.setOpenFiles( SessionWindowId, openFiles, current_ );
        session.save();
    }

    void start( ScenarioRun& run ) override
    {
        run_ = &run;
        if ( !failure_.isEmpty() ) {
            run.fail( failure_ );
            return;
        }

        // The scenario started just before: the restore is measured.
        auto* window = run.restoreSession();
        auto* tabs = window != nullptr ? window->findChild<TabbedCrawlerWidget*>() : nullptr;
        if ( tabs == nullptr ) {
            run.fail( QStringLiteral( "the Session restored no window with tabs" ) );
            return;
        }
        for ( const auto index : tabs->logFileTabs() ) {
            if ( auto* crawler = qobject_cast<CrawlerWidget*>( tabs->widget( index ) ) ) {
                tabs_.push_back( { crawler } );
            }
        }
        if ( tabs_.size() != run.logFiles().size() ) {
            run.fail( QStringLiteral( "the Session restored %1 tabs, not %2" )
                          .arg( tabs_.size() )
                          .arg( run.logFiles().size() ) );
            return;
        }
        if ( tabs->currentWidget() != tabs_[ static_cast<std::size_t>( current_ ) ].crawler ) {
            run.fail( QStringLiteral( "tab %1 is not in front" ).arg( current_ ) );
            return;
        }

        currentView_
            = tabs_[ static_cast<std::size_t>( current_ ) ].crawler->findChild<LogMainView*>();
        if ( !currentView_ ) {
            run.fail( QStringLiteral( "the tab in front has no Text View" ) );
            return;
        }
        probe_ = std::make_unique<PaintProbe>(
            currentView_->viewport(),
            [ this ]( Clock::time_point started, Clock::time_point ended ) {
                painted( started, ended );
            } );

        for ( std::size_t tab = 0; tab < tabs_.size(); ++tab ) {
            QObject::connect( tabs_[ tab ].crawler, &CrawlerWidget::loadingFinished, run.context(),
                              [ this, tab ]( LoadingStatus status, const QString& failure ) {
                                  indexed( tab, status, failure );
                              } );
        }
    }

private:
    struct Tab {
        QPointer<CrawlerWidget> crawler;
        bool indexed = false;
        qint64 logLineCount = 0;
    };

    void indexed( std::size_t tab, LoadingStatus status, const QString& failure )
    {
        auto& restored = tabs_[ tab ];
        if ( restored.indexed ) {
            return;
        }
        if ( status != LoadingStatus::Successful ) {
            run_->fail( QStringLiteral( "the Log File of tab %1 was not loaded: %2" )
                            .arg( tab )
                            .arg( failure.isEmpty() ? QStringLiteral( "interrupted" ) : failure ) );
            return;
        }
        const auto moment = Clock::now();
        restored.indexed = true;
        const auto* view = restored.crawler ? restored.crawler->findChild<LogMainView*>() : nullptr;
        restored.logLineCount
            = view != nullptr ? static_cast<qint64>( view->lineMapping().logLineCount().get() ) : 0;
        run_->report().eventAt( "tab_indexed", moment,
                                QJsonObject{ { "tab", static_cast<qint64>( tab ) },
                                             { "log_line_count", restored.logLineCount } } );

        if ( tab == static_cast<std::size_t>( current_ ) ) {
            currentIndexed_ = moment;
            // Usable with the paint that follows.
            if ( currentView_ ) {
                currentView_->viewport()->update();
            }
        }

        if ( ++indexedCount_ == tabs_.size() ) {
            run_->report().eventAt( "all_tabs_indexed", moment );
            finishWhenDone();
        }
    }

    void painted( Clock::time_point started, Clock::time_point ended )
    {
        if ( usable_ || !currentIndexed_.has_value() || started < *currentIndexed_ || !currentView_
             || currentView_->lineMapping().logLineCount().get() == 0 ) {
            return;
        }
        usable_ = true;
        run_->report().eventAt( "current_tab_usable", ended, QJsonObject{ { "tab", current_ } } );
        // Not from within the paint.
        QTimer::singleShot( 0, run_->context(), [ this ] { finishWhenDone(); } );
    }

    void finishWhenDone()
    {
        if ( !usable_ || indexedCount_ != tabs_.size() || finished_ ) {
            return;
        }
        finished_ = true;
        probe_.reset();

        qint64 logLines = 0;
        qint64 bytes = 0;
        for ( std::size_t tab = 0; tab < tabs_.size(); ++tab ) {
            logLines += tabs_[ tab ].logLineCount;
            bytes += QFileInfo( run_->logFiles()[ tab ] ).size();
        }
        auto& report = run_->report();
        report.setResult( "tab_count", static_cast<qint64>( tabs_.size() ) );
        report.setResult( "current_tab", current_ );
        report.setResult( "marks_per_tab", marks_ );
        report.setResult( "log_line_count", logLines );
        report.setResult( "log_file_bytes", bytes );
        run_->finish();
    }

    QString failure_;
    int current_ = 0;
    int marks_ = 0;

    ScenarioRun* run_ = nullptr;
    std::vector<Tab> tabs_;
    QPointer<LogMainView> currentView_;
    std::unique_ptr<PaintProbe> probe_;
    std::optional<Clock::time_point> currentIndexed_;
    std::size_t indexedCount_ = 0;
    bool usable_ = false;
    bool finished_ = false;
};

const ScenarioRegistration registration{
    "session-restore",
    "Restores a Session of several tabs; reports the tab in front usable and every tab indexed",
    [] { return std::make_unique<SessionRestore>(); }
};

} // namespace

} // namespace logsquirl::benchmark
