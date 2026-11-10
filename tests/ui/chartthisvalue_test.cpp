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

// Chart this value... (#898): the context menu of the Text View and the
// Filtered View offers it, under More, when the selection is a number; the
// Chart Series dialog it opens proposes a pattern capturing the number and a
// series name from the text before it, and OK adds the series to the Chart
// Panel as any other series.

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>
#include <memory>

#include "abstractlogview.h"
#include "applicationplugins.h"
#include "chartpanel.h"
#include "chartseriesdialog.h"
#include "context_menu_entries.h"
#include "crawlerwidget.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "openlogfile.h"
#include "searchlinewidget_access.h"
#include "session.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

struct ChartThisValueTest {};

template <>
struct AbstractLogView::access_by<ChartThisValueTest> {
    static std::unique_ptr<QMenu> contextMenu( AbstractLogView& view )
    {
        return view.createContextMenu( QPoint( view.viewport()->width() / 2, 1 ) );
    }

    static void selectPortion( AbstractLogView& view, LineNumber line, LineColumn startColumn,
                               LineColumn endColumn )
    {
        view.selection_.selectPortion( line, startColumn, endColumn );
        view.update();
    }
};

template <>
struct CrawlerWidget::access_by<ChartThisValueTest> {
    static AbstractLogView& textView( const CrawlerWidget& crawler )
    {
        return *crawler.logMainView_;
    }

    static AbstractLogView& filteredView( const CrawlerWidget& crawler )
    {
        return *qobject_cast<AbstractLogView*>(
            crawler.tabbedFilteredView_->widget( crawler.state().frontFilteredView ) );
    }

    static ChartPanel& chartPanel( const CrawlerWidget& crawler )
    {
        return *crawler.chartPanel_;
    }

    static bool searchStopped( const CrawlerWidget& crawler )
    {
        return SearchLineAccess::stopButton( *crawler.searchLine_ )->isHidden();
    }

    static void search( CrawlerWidget& crawler, const QString& pattern )
    {
        auto* edit = SearchLineAccess::patternEdit( *crawler.searchLine_ );
        edit->clearEditText();
        QTest::keyClicks( edit, pattern );
        QTest::mouseClick( SearchLineAccess::searchButton( *crawler.searchLine_ ), Qt::LeftButton );
        QTest::qWait( 100 );
        REQUIRE( waitUiState( [ & ] { return searchStopped( crawler ); } ) );
    }
};

namespace {

using ViewAccess = AbstractLogView::access_by<ChartThisValueTest>;
using CrawlerAccess = CrawlerWidget::access_by<ChartThisValueTest>;

const auto ChartThisValue = QStringLiteral( "Chart this value..." );
const auto TheLogLine = QStringLiteral( "12:00:01 GET /api took 125 ms" );
const auto LikeLogLine = QStringLiteral( "12:00:02 GET /api took 980 ms" );
const auto Number = QStringLiteral( "125" );

template <class Widget>
Widget* childOf( QWidget& parent, const QString& name )
{
    auto* child = parent.findChild<Widget*>( name );
    REQUIRE( child != nullptr );
    return child;
}

// A window showing the Log File.
struct ChartWindow {
    ChartWindow()
    {
        REQUIRE( dir.isValid() );
        const auto path = dir.filePath( QStringLiteral( "chart.log" ) );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write(
            ( TheLogLine + u'\n' + LikeLogLine + QStringLiteral( "\nother line\n" ) ).toUtf8() );
        file.close();

        const auto session = std::make_shared<Session>( testSettingsPolicies(),
                                                        std::make_shared<LogFormatCatalog>() );
        window = std::make_unique<MainWindow>(
            WindowSession{ session, QStringLiteral( "Main" ), 0 },
            std::make_shared<logsquirl::plugins::ApplicationPlugins>() );
        showUntilExposed( *window );
        window->loadFileNonInteractive( path );
        REQUIRE( waitUiState( [ this ] {
            crawler = window->findChild<CrawlerWidget*>();
            return crawler != nullptr && crawler->state().loadStatus == LoadingStatus::Successful;
        } ) );
        QTest::qWait( 50 );
    }

    ~ChartWindow()
    {
        window.reset();
        QTest::qWait( 50 );
    }

    ChartWindow( const ChartWindow& ) = delete;
    ChartWindow& operator=( const ChartWindow& ) = delete;

    QTemporaryDir dir;
    std::unique_ptr<MainWindow> window;
    CrawlerWidget* crawler = nullptr;
};

// Selects the text of TheLogLine, shown as line of the view.
void selectText( AbstractLogView& view, const QString& text, LineNumber line = 0_lnum )
{
    const auto start = TheLogLine.indexOf( text );
    REQUIRE( start >= 0 );
    ViewAccess::selectPortion( view, line, LineColumn{ start },
                               LineColumn{ start + text.size() - 1 } );
}

// What the Chart Series dialog proposed when it opened.
struct Proposed {
    QString name;
    QString pattern;
    int captureGroup = 0;
};

// Chooses Chart this value... of the menu, and in the dialog it opens notes
// what it proposes, then accepts or cancels it.
Proposed chartInDialog( QMenu& menu, bool accept )
{
    auto* entry = contextMenuEntry( menu, ChartThisValue );
    REQUIRE( entry != nullptr );
    REQUIRE( entry->isEnabled() );
    bool opened = false;
    Proposed proposed;
    QTimer::singleShot( 0, [ & ] {
        auto* dialog = qobject_cast<ChartSeriesDialog*>( QApplication::activeModalWidget() );
        REQUIRE( dialog != nullptr );
        opened = true;
        proposed.name = childOf<QLineEdit>( *dialog, QStringLiteral( "seriesName" ) )->text();
        proposed.pattern = childOf<QLineEdit>( *dialog, QStringLiteral( "seriesPattern" ) )->text();
        proposed.captureGroup
            = childOf<QSpinBox>( *dialog, QStringLiteral( "seriesCaptureGroup" ) )->value();
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        buttons->button( accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel )->click();
    } );
    entry->trigger();
    REQUIRE( opened );
    return proposed;
}

// The values of the points of the chart's series.
QList<double> pointValues( const ChartSeriesDefinition& series )
{
    QList<double> values;
    for ( const auto& point : series.points ) {
        values.append( point.value );
    }
    return values;
}

} // namespace

SCENARIO( "The context menu of the Text View and the Filtered View offers Chart this value... "
          "for a number",
          "[ui][chart][chartthisvalue]" )
{
    const ChartWindow shown;
    auto& textView = CrawlerAccess::textView( *shown.crawler );

    GIVEN( "the Text View" )
    {
        WHEN( "a number is selected within a Log Line" )
        {
            selectText( textView, Number );
            const auto menu = ViewAccess::contextMenu( textView );

            THEN( "the entry is offered under More, first, enabled, the top level kept short" )
            {
                REQUIRE( moreEntriesOf( *menu ).value( 0 ) == ChartThisValue );
                REQUIRE( contextMenuEntry( *menu, ChartThisValue )->isEnabled() );
                REQUIRE( entriesOf( *menu ).size() <= MaxTopLevelEntries );
            }
        }

        WHEN( "a word is selected" )
        {
            selectText( textView, QStringLiteral( "took" ) );
            const auto menu = ViewAccess::contextMenu( textView );

            THEN( "the entry is disabled" )
            {
                REQUIRE( contextMenuEntry( *menu, ChartThisValue ) != nullptr );
                REQUIRE_FALSE( contextMenuEntry( *menu, ChartThisValue )->isEnabled() );
            }
        }

        WHEN( "a number is selected with more text" )
        {
            selectText( textView, QStringLiteral( "took 125" ) );
            const auto menu = ViewAccess::contextMenu( textView );

            THEN( "the entry is disabled" )
            {
                REQUIRE_FALSE( contextMenuEntry( *menu, ChartThisValue )->isEnabled() );
            }
        }

        WHEN( "a whole Log Line is selected" )
        {
            textView.selectAndDisplayLine( 0_lnum );
            const auto menu = ViewAccess::contextMenu( textView );

            THEN( "the entry is disabled" )
            {
                REQUIRE_FALSE( contextMenuEntry( *menu, ChartThisValue )->isEnabled() );
            }
        }
    }

    GIVEN( "the Filtered View of a Search for the Log Line" )
    {
        CrawlerAccess::search( *shown.crawler, QStringLiteral( "125" ) );
        auto& filteredView = CrawlerAccess::filteredView( *shown.crawler );

        WHEN( "the number is selected within its Log Line" )
        {
            selectText( filteredView, Number );
            const auto menu = ViewAccess::contextMenu( filteredView );

            THEN( "it offers the entry, enabled" )
            {
                REQUIRE( contextMenuEntry( *menu, ChartThisValue ) != nullptr );
                REQUIRE( contextMenuEntry( *menu, ChartThisValue )->isEnabled() );
            }
        }
    }
}

SCENARIO( "Chart this value... opens the Chart Series dialog with the pattern and the series "
          "name proposed, and OK adds the series to the Chart Panel",
          "[ui][chart][chartthisvalue]" )
{
    const ChartWindow shown;
    auto& chartPanel = CrawlerAccess::chartPanel( *shown.crawler );
    REQUIRE( chartPanel.seriesDefinitions().isEmpty() );

    WHEN( "the number selected in the Text View is charted" )
    {
        auto& textView = CrawlerAccess::textView( *shown.crawler );
        selectText( textView, Number );
        const auto menu = ViewAccess::contextMenu( textView );
        const auto proposed = chartInDialog( *menu, true );

        THEN( "the dialog proposed the word before the number as the name, and a pattern whose "
              "capture group 1 captures the number in the clicked Log Line" )
        {
            REQUIRE( proposed.name == QStringLiteral( "took" ) );
            REQUIRE( proposed.captureGroup == 1 );
            const auto match = QRegularExpression( proposed.pattern ).match( TheLogLine );
            REQUIRE( match.hasMatch() );
            REQUIRE( match.capturedStart( 1 ) == TheLogLine.indexOf( Number ) );
            REQUIRE( match.captured( 1 ) == Number );
        }

        THEN( "the Chart Panel shows the series, with a point for each Log Line like the clicked "
              "one" )
        {
            REQUIRE( chartPanel.isVisible() );
            const auto series = chartPanel.seriesDefinitions();
            REQUIRE( series.size() == 1 );
            REQUIRE( series.front().name == proposed.name );
            REQUIRE( series.front().pattern == proposed.pattern );
            REQUIRE( series.front().captureGroup == 1 );
            REQUIRE( waitUiState( [ & ] {
                const auto now = chartPanel.seriesDefinitions();
                return now.size() == 1
                       && pointValues( now.front() ) == QList<double>{ 125.0, 980.0 };
            } ) );
        }
    }

    WHEN( "the number selected in the Filtered View is charted" )
    {
        CrawlerAccess::search( *shown.crawler, QStringLiteral( "125" ) );
        auto& filteredView = CrawlerAccess::filteredView( *shown.crawler );
        selectText( filteredView, Number );
        const auto menu = ViewAccess::contextMenu( filteredView );
        const auto proposed = chartInDialog( *menu, true );

        THEN( "the series is added from the Filtered View's Log Line too" )
        {
            REQUIRE( proposed.name == QStringLiteral( "took" ) );
            const auto series = chartPanel.seriesDefinitions();
            REQUIRE( series.size() == 1 );
            REQUIRE( QRegularExpression( series.front().pattern ).match( TheLogLine ).captured( 1 )
                     == Number );
        }
    }

    WHEN( "the dialog is cancelled" )
    {
        REQUIRE_FALSE( chartPanel.isVisible() );
        auto& textView = CrawlerAccess::textView( *shown.crawler );
        selectText( textView, Number );
        const auto menu = ViewAccess::contextMenu( textView );
        chartInDialog( *menu, false );

        THEN( "no series is added, and the Chart Panel stays hidden" )
        {
            REQUIRE( chartPanel.seriesDefinitions().isEmpty() );
            REQUIRE_FALSE( chartPanel.isVisible() );
        }
    }

    WHEN( "the dialog is cancelled with the Chart Panel shown" )
    {
        shown.crawler->toggleChartPanel();
        REQUIRE( chartPanel.isVisible() );
        auto& textView = CrawlerAccess::textView( *shown.crawler );
        selectText( textView, Number );
        const auto menu = ViewAccess::contextMenu( textView );
        chartInDialog( *menu, false );

        THEN( "the Chart Panel stays shown" )
        {
            REQUIRE( chartPanel.seriesDefinitions().isEmpty() );
            REQUIRE( chartPanel.isVisible() );
        }
    }
}
