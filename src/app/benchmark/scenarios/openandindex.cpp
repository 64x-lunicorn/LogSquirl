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

// The scenario "open-and-index" (#666): opens one Log File as a user opening it
// from the command line does, and reports when its first Log Line is displayed
// and when its Index is finished.
//
// Events:
//   log_file_opened           the Log File's tab opened and its loading
//                             started; a Log File given on the command line
//                             waits for the window to be shown and the
//                             plugins to be loaded (none in a Benchmark Run)
//   first_log_line_displayed  the first paint of the Text View's Viewport that
//                             shows a Log Line has ended;
//                             data: log_line_count, the Log Lines the view had
//   index_finished            the Index is complete (the load finished);
//                             data: log_line_count
// Results:
//   log_line_count            Log Lines of the Log File
//   log_file_bytes            its size
//   index_mb_per_s            log_file_bytes in MB (10^6 bytes) over the time
//                             from the open to index_finished

#include <memory>

#include <QFileInfo>
#include <QJsonObject>
#include <QPointer>

#include "benchmarkscenario.h"
#include "crawlerwidget.h"
#include "loadingstatus.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "paintprobe.h"
#include "scenariorun.h"
#include "tabbedcrawlerwidget.h"

namespace logsquirl::benchmark {

namespace {

class OpenAndIndex : public Scenario {
public:
    void start( ScenarioRun& run ) override
    {
        run_ = &run;
        if ( run.logFiles().size() != 1 ) {
            run.fail( QStringLiteral( "open-and-index opens exactly one Log File, %1 given" )
                          .arg( run.logFiles().size() ) );
            return;
        }
        const auto& logFile = run.logFiles().front();

        // The window is not part of what is measured; opening the Log File is.
        auto* window = run.newWindow();
        auto* tabs = window->findChild<TabbedCrawlerWidget*>();
        if ( tabs == nullptr ) {
            run.fail( QStringLiteral( "the window has no tabs" ) );
            return;
        }
        // The tab opens once the window is shown and the plugins have
        // loaded, as for a Log File given on the command line: it is made
        // current as it opens.
        QObject::connect( tabs, &QTabWidget::currentChanged, run.context(), [ this, tabs ] {
            if ( auto* crawler = qobject_cast<CrawlerWidget*>( tabs->currentWidget() ) ) {
                opened( crawler );
            }
        } );

        run.report().markScenarioStart();
        window->loadInitialFile( logFile, false );
    }

private:
    void opened( CrawlerWidget* crawler )
    {
        if ( mainView_ || run_->report().hasEvent( "log_file_opened" ) ) {
            return;
        }
        run_->report().event( "log_file_opened" );

        auto* mainView = crawler->findChild<LogMainView*>();
        if ( mainView == nullptr ) {
            run_->fail( QStringLiteral( "the Log File's tab has no Text View" ) );
            return;
        }
        mainView_ = mainView;

        // Its loading has only started, and nothing is painted before the
        // event loop gets to it: no paint and no end of the load is missed.
        displayProbe_ = std::make_unique<PaintProbe>(
            mainView->viewport(),
            [ this ]( Clock::time_point, Clock::time_point ended ) { firstPaintEnded( ended ); } );

        QObject::connect( crawler, &CrawlerWidget::loadingFinished, run_->context(),
                          [ this ]( LoadingStatus status, const QString& failure ) {
                              loadingFinished( status, failure );
                          } );
    }

    LinesCount::UnderlyingType shownLineCount() const
    {
        return mainView_ ? mainView_->lineMapping().logLineCount().get() : 0;
    }

    void firstPaintEnded( Clock::time_point ended )
    {
        const auto lines = shownLineCount();
        if ( lines == 0 || run_->report().hasEvent( "first_log_line_displayed" ) ) {
            return;
        }
        run_->report().eventAt( "first_log_line_displayed", ended,
                                QJsonObject{ { "log_line_count", static_cast<qint64>( lines ) } } );
        // Not from within the paint it is called from.
        displayProbe_.release()->deleteLater();
        finishWhenDone();
    }

    void loadingFinished( LoadingStatus status, const QString& failure )
    {
        if ( run_->report().hasEvent( "index_finished" ) ) {
            return;
        }
        if ( status != LoadingStatus::Successful ) {
            run_->fail( QStringLiteral( "the Log File was not indexed: %1" )
                            .arg( failure.isEmpty() ? QStringLiteral( "interrupted" ) : failure ) );
            return;
        }

        const auto lines = static_cast<qint64>( shownLineCount() );
        run_->report().event( "index_finished", QJsonObject{ { "log_line_count", lines } } );

        const auto bytes = QFileInfo( run_->logFiles().front() ).size();
        run_->report().setResult( "log_line_count", lines );
        run_->report().setResult( "log_file_bytes", bytes );
        const auto indexMs = run_->report().millisecondsSinceScenarioStart( "index_finished" );
        if ( indexMs && *indexMs > 0.0 ) {
            run_->report().setResult( "index_mb_per_s",
                                      static_cast<double>( bytes ) / 1e6 / ( *indexMs / 1000.0 ) );
        }
        finishWhenDone();
    }

    void finishWhenDone()
    {
        if ( run_->report().hasEvent( "first_log_line_displayed" )
             && run_->report().hasEvent( "index_finished" ) ) {
            run_->finish();
        }
    }

    ScenarioRun* run_ = nullptr;
    QPointer<LogMainView> mainView_;
    std::unique_ptr<PaintProbe> displayProbe_;
};

const ScenarioRegistration registration{
    "open-and-index",
    "Opens one Log File; reports its first Log Line displayed and its Index finished",
    [] { return std::make_unique<OpenAndIndex>(); }
};

} // namespace

} // namespace logsquirl::benchmark
