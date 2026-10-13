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

// The scenario "sort" (#797): sorts the Table View of a loaded Log File by
// one of its columns, as a click on the column's header does, and reports
// when the Rows have switched to the Sort Order.
//
// The Log File is opened, loaded and shown in the Table View first, as its
// toggle shows it; none of that is measured. The scenario starts with the
// sort, which reads the column's keys of every Log Line off the UI thread
// and then sorts them: what a user waits for.
//
// Options:
//   column     the header of the column to sort by (required); "Δt" is the
//              elapsed-time column
//   direction  asc (default) or desc
//   format     a Log Format file copied into the run's own formats folder
//              before the Catalog reads it, where it replaces a Log Format of
//              the same name: for a Log File that needs a column of a kind no
//              Log Format of the Catalog gives it
//
// Events:
//   sorted         the Rows are in the Sort Order; data: row_count
// Results:
//   column         the column sorted by, direction its direction
//   row_count      the Rows of the Table View
//   log_line_count Log Lines of the Log File
//   log_file_bytes its size

#include <memory>

#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QToolButton>

#include "benchmarkscenario.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "loadedlogfile.h"
#include "logformatcatalog.h"
#include "logtableview.h"
#include "paintprobe.h"
#include "scenariorun.h"

namespace logsquirl::benchmark {

namespace {

class Sort : public Scenario {
public:
    void prepare( ScenarioRun& run ) override
    {
        column_ = run.option( "column" );
        if ( column_.isEmpty() ) {
            failure_ = QStringLiteral( "sort needs the option column=<header of the column>" );
            return;
        }
        const auto direction = run.option( "direction", "asc" );
        if ( direction != "asc" && direction != "desc" ) {
            failure_ = QStringLiteral( "direction is asc or desc, not '%1'" ).arg( direction );
            return;
        }
        descending_ = direction == "desc";

        // A Log Format of the scenario's own, in the run's formats folder
        // before the Catalog reads it; the run's folder goes with the run.
        const auto format = run.option( "format" );
        if ( !format.isEmpty() ) {
            const auto folder = LogFormatCatalog::defaultUserFormatsDirectory();
            if ( folder.isEmpty() || !QDir().mkpath( folder )
                 || !QFile::copy( format,
                                  QDir( folder ).filePath( QFileInfo( format ).fileName() ) ) ) {
                failure_ = QStringLiteral( "the Log Format %1 could not be copied into %2" )
                               .arg( format, folder );
                return;
            }
        }

        // The run's own settings: the Table View shows the Log Format
        // recognized.
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
        logFile_ = std::make_unique<LoadedLogFile>(
            run, QStringLiteral( "sort" ),
            [ this ]( MainWindow&, CrawlerWidget& crawler ) { loaded( crawler ); } );
        logFile_->open();
    }

private:
    void loaded( CrawlerWidget& crawler )
    {
        table_ = crawler.findChild<LogTableView*>();
        auto* toggle = crawler.findChild<QToolButton*>( "tableViewToggle" );
        if ( !table_ || toggle == nullptr ) {
            run_->fail( QStringLiteral( "the Log File's tab has no Table View" ) );
            return;
        }
        if ( toggle->isHidden() ) {
            run_->fail( QStringLiteral(
                "no Log Format was recognized for the Log File: the Table View shows none" ) );
            return;
        }
        // Shown once a paint of the Table View shows Rows, which the toggle
        // reads in from the event loop.
        probe_ = std::make_unique<PaintProbe>(
            table_->viewport(), [ this ]( Clock::time_point, Clock::time_point ) { painted(); } );
        toggle->click();
    }

    void painted()
    {
        const auto* model = table_ ? table_->model() : nullptr;
        if ( shown_ || model == nullptr || model->rowCount() == 0 ) {
            return;
        }
        shown_ = true;
        // Not from within the paint.
        QTimer::singleShot( 0, run_->context(), [ this ] { sort(); } );
    }

    void sort()
    {
        probe_.reset();
        if ( !table_ ) {
            run_->fail( QStringLiteral( "the Table View went" ) );
            return;
        }
        const auto* model = table_->model();
        int column = -1;
        for ( int candidate = 0; candidate < model->columnCount(); ++candidate ) {
            if ( model->headerData( candidate, Qt::Horizontal ).toString() == column_ ) {
                column = candidate;
                break;
            }
        }
        if ( column < 0 ) {
            QStringList columns;
            for ( int candidate = 0; candidate < model->columnCount(); ++candidate ) {
                columns << model->headerData( candidate, Qt::Horizontal ).toString();
            }
            run_->fail( QStringLiteral( "the Table View has no column '%1', only %2" )
                            .arg( column_, columns.join( ", " ) ) );
            return;
        }

        QObject::connect( table_, &LogTableView::sortOrderChanged, run_->context(),
                          [ this ] { sorted(); } );
        run_->report().markScenarioStart();
        table_->sortBy( LogTableView::SortOrder{ column, descending_ ? Qt::DescendingOrder
                                                                     : Qt::AscendingOrder } );
    }

    void sorted()
    {
        if ( !table_ || !table_->sortOrder() ) {
            run_->fail( QStringLiteral( "the Rows are not in a Sort Order" ) );
            return;
        }
        auto& report = run_->report();
        const auto rowCount = static_cast<qint64>( table_->model()->rowCount() );
        report.event( "sorted", QJsonObject{ { "row_count", rowCount } } );
        report.setResult( "column", column_ );
        report.setResult( "direction", descending_ ? "desc" : "asc" );
        report.setResult( "row_count", rowCount );
        report.setResult( "log_line_count", logFile_->logLineCount() );
        report.setResult( "log_file_bytes", logFile_->bytes() );
        run_->finish();
    }

    ScenarioRun* run_ = nullptr;
    QString failure_;
    QString column_;
    bool descending_ = false;
    std::unique_ptr<LoadedLogFile> logFile_;
    QPointer<LogTableView> table_;
    std::unique_ptr<PaintProbe> probe_;
    bool shown_ = false;
};

const ScenarioRegistration registration{
    "sort", "Sorts the Table View of a loaded Log File by a column; reports when the Rows switched",
    [] { return std::make_unique<Sort>(); }
};

} // namespace

} // namespace logsquirl::benchmark
