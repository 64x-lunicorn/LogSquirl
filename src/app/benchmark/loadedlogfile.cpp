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

#include "loadedlogfile.h"

#include <utility>

#include <QFileInfo>
#include <QTimer>

#include "crawlerwidget.h"
#include "loadingstatus.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "paintprobe.h"
#include "scenariorun.h"
#include "tabbedcrawlerwidget.h"

namespace logsquirl::benchmark {

LoadedLogFile::LoadedLogFile( ScenarioRun& run, QString scenarioName, Loaded loaded )
    : run_( run )
    , scenarioName_( std::move( scenarioName ) )
    , loaded_( std::move( loaded ) )
{
}

LoadedLogFile::~LoadedLogFile() = default;

void LoadedLogFile::open()
{
    if ( run_.logFiles().size() != 1 ) {
        run_.fail( QStringLiteral( "%1 opens exactly one Log File, %2 given" )
                       .arg( scenarioName_ )
                       .arg( run_.logFiles().size() ) );
        return;
    }

    window_ = run_.newWindow();
    auto* tabs = window_->findChild<TabbedCrawlerWidget*>();
    if ( tabs == nullptr ) {
        run_.fail( QStringLiteral( "the window has no tabs" ) );
        return;
    }
    // The tab opens once the window is shown, as for a Log File given on the
    // command line, and is made current as it opens (open-and-index).
    QObject::connect( tabs, &QTabWidget::currentChanged, run_.context(), [ this, tabs ] {
        if ( auto* crawler = qobject_cast<CrawlerWidget*>( tabs->currentWidget() ) ) {
            opened( crawler );
        }
    } );
    window_->loadInitialFile( run_.logFiles().front(), false );
}

qint64 LoadedLogFile::bytes() const
{
    return QFileInfo( run_.logFiles().front() ).size();
}

qint64 LoadedLogFile::logLineCount() const
{
    return logLineCount_;
}

void LoadedLogFile::opened( CrawlerWidget* crawler )
{
    if ( crawler_ ) {
        return;
    }
    crawler_ = crawler;
    mainView_ = crawler->findChild<LogMainView*>();
    if ( !mainView_ ) {
        run_.fail( QStringLiteral( "the Log File's tab has no Text View" ) );
        return;
    }

    probe_ = std::make_unique<PaintProbe>(
        mainView_->viewport(), [ this ]( Clock::time_point, Clock::time_point ) { painted(); } );

    QObject::connect( crawler, &CrawlerWidget::loadingFinished, run_.context(),
                      [ this ]( LoadingStatus status, const QString& failure ) {
                          if ( indexed_ ) {
                              return;
                          }
                          if ( status != LoadingStatus::Successful ) {
                              run_.fail( QStringLiteral( "the Log File was not loaded: %1" )
                                             .arg( failure.isEmpty()
                                                       ? QStringLiteral( "interrupted" )
                                                       : failure ) );
                              return;
                          }
                          indexed_ = true;
                          logLineCount_ = static_cast<qint64>(
                              mainView_ ? mainView_->lineMapping().logLineCount().get() : 0 );
                          // Shown by the paint that follows the load.
                          mainView_->viewport()->update();
                      } );
}

void LoadedLogFile::painted()
{
    if ( called_ || !indexed_ || !mainView_
         || mainView_->lineMapping().logLineCount().get() == 0 ) {
        return;
    }
    called_ = true;
    // Not from within the paint: the probe goes, and the scenario starts from
    // the event loop.
    probe_.release()->deleteLater();
    QTimer::singleShot( 0, run_.context(), [ this ] {
        if ( window_ && crawler_ ) {
            loaded_( *window_, *crawler_ );
        }
        else {
            run_.fail( QStringLiteral( "the Log File's window closed" ) );
        }
    } );
}

} // namespace logsquirl::benchmark
