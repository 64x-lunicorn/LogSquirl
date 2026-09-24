/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "textencoding.h"
#include <QHeaderView>
#include <QImage>
#include <QPointer>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <qglobal.h>
#include <qnamespace.h>
#include <qtestmouse.h>

#include "logformatcatalog.h"
#include "savedsearches.h"
#include "session.h"
#include "sessioninfo.h"
#include "test_policies.h"
#include "test_utils.h"

#include "logdata.h"
#include "logfiltereddata.h"

#include "abstractlogview.h"
#include "chartpanel.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "fake_file_watch.h"
#include "filteredview.h"
#include "fontutils.h"
#include "highlighterset.h"
#include "infoline.h"
#include "logformatdefinition.h"
#include "logtableview.h"
#include "shortcuts.h"
#include "textviewscrolling.h"

#include "theme.h"
#include "theme_lists.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

static const qint64 SL_NB_LINES = 100LL;

namespace {
bool generateDataFiles( QTemporaryFile& file )
{
    char newLine[ 90 ];

    if ( file.open() ) {
        for ( int i = 0; i < SL_NB_LINES; i++ ) {
            snprintf( newLine, 89,
                      "LOGDATA \t is a part of glogg, we are going to test it thoroughly, this is "
                      "line %06d",
                      i );
            file.write( newLine, static_cast<qint64>( qstrlen( newLine ) ) );
#ifdef Q_OS_WIN
            file.write( "\r\n", 2 );
#else
            file.write( "\n", 1 );
#endif
        }
        file.flush();
    }

    return true;
}

} // namespace

struct CrawlerWidgetPrivate {};

template <>
struct TextViewScrolling::access_by<CrawlerWidgetPrivate> {
    // Whether the Visual Lines of the bottom lines are kept from the last
    // count, to be extended when lines are appended.
    static bool bottomLinesKept( const TextViewScrolling& scrolling )
    {
        return scrolling.bottomLines_.has_value();
    }
};

template <>
struct AbstractLogView::access_by<CrawlerWidgetPrivate> {
    static bool bottomLinesKept( const AbstractLogView& view )
    {
        return TextViewScrolling::access_by<CrawlerWidgetPrivate>::bottomLinesKept(
            view.scrolling_ );
    }
};

template <>
struct CrawlerWidget::access_by<CrawlerWidgetPrivate> {
    std::unique_ptr<CrawlerWidget> crawler;

    bool isLoadingFinished()
    {
        return !crawler->loadingInProgress_;
    }

    LinesCount getLogNbLines()
    {
        return crawler->openLogFile_->logData()->getNbLine();
    }

    LinesCount getLogFilteredNbLines()
    {
        return crawler->openLogFile_->filteredData()->getNbLine();
    }

    void selectAllInMainView()
    {
        crawler->logMainView_->selectAll();
    }

    void selectAllInFilteredView()
    {
        crawler->filteredView_->selectAll();
    }

    QString mainViewSelectedText()
    {
        return crawler->logMainView_->getSelectedText();
    }

    QString filteredViewSelectedText()
    {
        return crawler->filteredView_->getSelectedText();
    }

    void setSearchPattern( const QString& pattern )
    {
        QTest::keyClicks( crawler->searchLineEdit_, pattern );
    }

    void enableCaseSensitiveSearch()
    {
        if ( !crawler->matchCaseButton_->isChecked() ) {
            QTest::mouseClick( crawler->matchCaseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void disableCaseSensitiveSearch()
    {
        if ( crawler->matchCaseButton_->isChecked() ) {
            QTest::mouseClick( crawler->matchCaseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableInverseMatch()
    {
        if ( !crawler->inverseButton_->isChecked() ) {
            QTest::mouseClick( crawler->inverseButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void enableBooleanCombinationMode()
    {
        if ( !crawler->booleanButton_->isChecked() ) {
            QTest::mouseClick( crawler->booleanButton_, Qt::LeftButton );
            QTest::qWait( 100 );
        }
    }

    void runSearch()
    {
        QTest::mouseClick( crawler->searchButton_, Qt::LeftButton );

        QTest::qWait( 100 );

        waitUiState( [ & ]() { return crawler->stopButton_->isHidden(); } );
    }

    void render()
    {
        crawler->grab();
    }

    void showSized()
    {
        crawler->resize( 800, 600 );
        crawler->show();
        QCoreApplication::processEvents();
    }

    ScrollPosition mainViewScrollPosition()
    {
        return crawler->logMainView_->scrollPosition();
    }

    // Rows of the main view, a partly visible one included.
    int mainViewRows()
    {
        return crawler->logMainView_->verticalScrollBar()->pageStep();
    }

    void selectInFilteredView( LineNumber line )
    {
        crawler->filteredView_->selectAndDisplayLine( line );
    }

    // Recognizes the Log File with a Log Format of its own, and shows the
    // Table View or the Text View.
    void showTableView( bool tableView )
    {
        if ( !crawler->recognizedFormat_ ) {
            LogFormatDefinition format;
            format.setName( "crawlerwidget_test_presentations" );
            format.setTitle( "Presentations test" );
            QHash<QString, QString> regex;
            regex[ "basic" ] = R"(^(?<source>\w+)\s+(?<body>.*)$)";
            format.setRegexPatterns( regex );
            format.setBodyField( "body" );

            crawler->recognizedFormat_ = std::make_shared<const LogFormatDefinition>( format );
            crawler->logTableView_->setLogFormat( crawler->recognizedFormat_.get(),
                                                  crawler->openLogFile_->logData().get() );
            crawler->tableViewToggle_->setVisible( true );
        }

        crawler->tableViewToggle_->setChecked( tableView );
        QTest::qWait( 50 );
    }

    LogMainView* textView()
    {
        return crawler->logMainView_;
    }

    LogTableView* tableView()
    {
        return crawler->logTableView_;
    }

    LogPresentation* presentation()
    {
        return crawler->presentation_;
    }

    bool isMarked( LineNumber line )
    {
        return crawler->openLogFile_->filteredData()->lineTypeByLine( line ).testFlag(
            AbstractLogData::LineTypeFlags::Mark );
    }

    QString searchText()
    {
        return crawler->searchLineEdit_->currentText();
    }

    FilteredView* filteredView()
    {
        return crawler->filteredView_;
    }

    QString logLineString( LineNumber line )
    {
        return crawler->openLogFile_->logData()->getLineString( line );
    }

    void clearSearchPattern()
    {
        crawler->searchLineEdit_->clearEditText();
    }

    // What the Keep Results button does: the next Search opens a tab of its
    // own, and the current one keeps its results.
    void keepSearchResults()
    {
        crawler->keepSearchResultsButton_->setChecked( true );
    }

    // The Filtered View in tab index, current or not.
    FilteredView* filteredViewInTab( int index )
    {
        return qobject_cast<FilteredView*>( crawler->tabbedFilteredView_->widget( index ) );
    }

    int currentFilteredViewTab()
    {
        return crawler->tabbedFilteredView_->currentIndex();
    }

    // What the Color Label shortcuts and menus do: the text selected in the
    // main view, here the whole of the given Log Line, gets the label.
    void addColorLabelToLogLine( LineNumber line, size_t label )
    {
        crawler->logMainView_->selectAndDisplayLine( line );
        crawler->addColorLabelToSelection( label );
        QCoreApplication::processEvents();
    }

    // What "clear all Color Labels" does in any view of the Log File.
    void clearColorLabels()
    {
        crawler->clearColorLabels();
        QCoreApplication::processEvents();
    }

    // What the Search Limits context menu entries do in any view.
    void setSearchLimits( LineNumber startLine, LineNumber endLine )
    {
        crawler->setSearchLimits( startLine, endLine );
        QCoreApplication::processEvents();
    }

    void clearSearchLimits()
    {
        crawler->clearSearchLimits();
        QCoreApplication::processEvents();
    }

    // The Search Limits every view of the Log File was handed last.
    std::optional<std::pair<LineNumber, LineNumber>> searchLimits() const
    {
        return crawler->viewSet_.searchLimits();
    }

    // The words of every Color Label every view of the Log File was handed.
    const ViewSet::ColorLabels& colorLabels() const
    {
        return crawler->viewSet_.colorLabels();
    }

    // What a Log File growing in the background does: its tab shows new data.
    void showNewData()
    {
        crawler->changeDataStatus( DataStatus::NEW_DATA );
    }

    // What marking a Log Line in the main view does.
    void markLogLine( LineNumber line )
    {
        crawler->markLinesFromMain( { line } );
        QCoreApplication::processEvents();
    }

    // What the close button of a Filtered View's tab does.
    void closeFilteredViewTab( int index )
    {
        crawler->closeFilteredView( index );
    }

    // The Filtered View of the current Search.
    FilteredView* filteredView() const
    {
        return crawler->filteredView_;
    }

    // Whether the overview of this Log File is shown beside the main view.
    // Asked of the widget, not of whether it is on screen, so that it answers
    // for a Log File whose tab is not the current one as well.
    bool overviewShown() const
    {
        return crawler->overview_.isVisible() && !crawler->overviewWidget_->isHidden();
    }

    // Whether the Search line reads its pattern as a regexp.
    bool useRegexpChecked() const
    {
        return crawler->useRegexpButton_->isChecked();
    }

    // Whether the Search matches case.
    bool matchCaseChecked() const
    {
        return crawler->matchCaseButton_->isChecked();
    }

    // Whether the Search refreshes as the Log File grows.
    bool autoRefreshChecked() const
    {
        return crawler->searchRefreshButton_->isChecked();
    }

    // Whether the Search pattern is read as a logical combination.
    bool booleanCombiningChecked() const
    {
        return crawler->booleanButton_->isChecked();
    }

    // What the user does by hand: clicks the match case, auto-refresh and
    // logical combining buttons of the search button row.
    // What the font-size shortcuts and Ctrl+wheel do in any view of the Log File.
    void zoom( bool increase )
    {
        crawler->changeFontSize( increase );
        QCoreApplication::processEvents();
    }

    // What the user does by hand: asks for the Search to follow the Log File.
    void enableAutoRefresh()
    {
        if ( !crawler->searchRefreshButton_->isChecked() ) {
            QTest::mouseClick( crawler->searchRefreshButton_, Qt::LeftButton );
            QCoreApplication::processEvents();
        }
    }

    QString searchInfoText() const
    {
        return crawler->searchInfoLine_->text();
    }

    bool isSearchRunning() const
    {
        return !crawler->stopButton_->isHidden();
    }

    bool mainViewKeepsBottomLines() const
    {
        return AbstractLogView::access_by<CrawlerWidgetPrivate>::bottomLinesKept(
            *crawler->logMainView_ );
    }

    // What the user does by hand: shows the chart and adds a series counting
    // the Log Lines that match pattern.
    void showChartCounting( const QString& pattern )
    {
        crawler->chartPanel_->show();
        crawler->chartPanel_->addFilterFrequencySeries( { pattern }, true );
    }

    // What the user does by hand: asks for the frequency of the Search's
    // patterns.
    void showFilterFrequency()
    {
        crawler->showFilterFrequency();
        QCoreApplication::processEvents();
    }

    // The points the chart shows for each of its series, in their order.
    QList<qsizetype> chartPointsPerSeries() const
    {
        QList<qsizetype> points;
        for ( const auto& series : crawler->chartPanel_->seriesDefinitions() ) {
            points.append( series.points.size() );
        }
        return points;
    }

    // The points the chart shows for its first series.
    qsizetype chartPoints() const
    {
        const auto series = crawler->chartPanel_->seriesDefinitions();
        return series.isEmpty() ? 0 : series.front().points.size();
    }

    void clickSearchDefaultButtons()
    {
        QTest::mouseClick( crawler->matchCaseButton_, Qt::LeftButton );
        QTest::mouseClick( crawler->searchRefreshButton_, Qt::LeftButton );
        QTest::mouseClick( crawler->booleanButton_, Qt::LeftButton );
        QCoreApplication::processEvents();
    }

    // What the user does by hand: asks for the Search pattern to be read as a
    // regexp.
    void enableRegexpSearch()
    {
        if ( !crawler->useRegexpButton_->isChecked() ) {
            QTest::mouseClick( crawler->useRegexpButton_, Qt::LeftButton );
            QCoreApplication::processEvents();
        }
    }

    // What the user does by hand: asks for the Search pattern to be read as a
    // fixed string.
    void disableRegexpSearch()
    {
        if ( crawler->useRegexpButton_->isChecked() ) {
            QTest::mouseClick( crawler->useRegexpButton_, Qt::LeftButton );
            QCoreApplication::processEvents();
        }
    }

    // The Search info line, as painted.
    QImage searchInfoImage() const
    {
        return crawler->searchInfoLine_->grab().toImage().convertToFormat( QImage::Format_RGB32 );
    }
};

using CrawlerWidgetVisitor = CrawlerWidget::access_by<CrawlerWidgetPrivate>;

SCENARIO( "Crawler widget search", "[ui]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    session.savedSearches().clear();

    REQUIRE( session.savedSearches().recentSearches().empty() );

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );

    waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } );
    waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );

    crawlerVisitor.render();

    REQUIRE( crawlerVisitor.getLogNbLines().get() == SL_NB_LINES );

    GIVEN( "loaded log data" )
    {
        THEN( "Has no lines in log view" )
        {
            REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
        }

        WHEN( "search for lines" )
        {
            crawlerVisitor.setSearchPattern( "this is line" );
            crawlerVisitor.runSearch();

            REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                return crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES;
            } ) );

            THEN( "all lines are matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
            }

            AND_WHEN( "copy all from main view" )
            {
                crawlerVisitor.selectAllInMainView();
                auto text = crawlerVisitor.mainViewSelectedText();
                THEN( "text has same number of lines" )
                {
                    REQUIRE( text.split( QChar::LineFeed ).size() == SL_NB_LINES );
                }
            }

            AND_WHEN( "copy all from filtered view" )
            {
                crawlerVisitor.selectAllInFilteredView();
                auto text = crawlerVisitor.filteredViewSelectedText();
                THEN( "text has same number of lines" )
                {
                    REQUIRE( text.split( QChar::LineFeed ).size() == SL_NB_LINES );
                }
            }
        }

        WHEN( "search for 10" )
        {
            crawlerVisitor.setSearchPattern( "10" );

            crawlerVisitor.runSearch();

            waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 1; } );

            THEN( "single line match" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 1 );
            }
        }

        WHEN( "case sensitive search" )
        {
            crawlerVisitor.setSearchPattern( "THIS" );
            crawlerVisitor.enableCaseSensitiveSearch();
            crawlerVisitor.runSearch();

            THEN( "no lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 0 );
            }
        }

        WHEN( "inverse match search" )
        {
            crawlerVisitor.setSearchPattern( "not match" );
            crawlerVisitor.enableInverseMatch();
            crawlerVisitor.runSearch();

            THEN( "all lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES );
            }
        }

        WHEN( "boolean search" )
        {
            crawlerVisitor.setSearchPattern( "\"glogg\" or \"logsquirl\"" );
            crawlerVisitor.enableBooleanCombinationMode();
            crawlerVisitor.runSearch();

            THEN( "has lines matched" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() >= 2 );
            }
        }
    }
}

SCENARIO( "An auto-refreshed Search follows a Log File truncated on disk", "[ui][autorefresh]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "truncated.log" );
    const auto writeLogLines = [ &path ]( int count ) {
        QFile file( path );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
            return false;
        }
        for ( int i = 0; i < count; i++ ) {
            file.write( QString( "LOGDATA is a part of logsquirl, this is line %1\n" )
                            .arg( i, 6, 10, QChar( '0' ) )
                            .toUtf8() );
        }
        return true;
    };
    REQUIRE( writeLogLines( SL_NB_LINES ) );

    // The Log File hears of the truncation at once, when the test reports it.
    const auto fileWatch = std::make_shared<FakeFileWatch>();
    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>(), fileWatch };
    session.savedSearches().clear();

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        path, []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ & ]() {
        return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES
               && crawlerVisitor.isLoadingFinished();
    } ) );

    GIVEN( "an auto-refreshed Search with its matches and a Mark" )
    {
        crawlerVisitor.enableAutoRefresh();
        // Log Lines 10 to 19.
        crawlerVisitor.setSearchPattern( "line 00001" );
        crawlerVisitor.runSearch();
        REQUIRE(
            waitUiState( [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 10; } ) );
        crawlerVisitor.markLogLine( 50_lnum );
        REQUIRE( crawlerVisitor.isMarked( 50_lnum ) );

        WHEN( "the Log File is truncated to fewer Log Lines" )
        {
            REQUIRE( writeLogLines( 15 ) );
            REQUIRE( fileWatch->reportChange( path ) );

            REQUIRE( waitUiState( [ & ]() {
                return crawlerVisitor.getLogNbLines().get() == 15
                       && crawlerVisitor.getLogFilteredNbLines().get() == 5
                       && !crawlerVisitor.isSearchRunning();
            } ) );

            THEN( "the Search started again and shows the matches of the truncated Log File" )
            {
                REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 5 );
                REQUIRE( crawlerVisitor.searchInfoText() == "5 matches found" );
            }

            THEN( "the Marks are gone" )
            {
                REQUIRE_FALSE( crawlerVisitor.isMarked( 50_lnum ) );
            }
        }
    }
}

SCENARIO( "A Log File growing under an unchanged Encoding keeps what scrolling counted",
          "[ui][encoding]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "growing.log" );
    QByteArray content;
    for ( int i = 0; i < SL_NB_LINES; i++ ) {
        content += QString( "LOGDATA is a part of logsquirl, this is line %1\n" )
                       .arg( i, 6, 10, QChar( '0' ) )
                       .toUtf8();
    }
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( content ) == content.size() );
    }

    const auto fileWatch = std::make_shared<FakeFileWatch>();
    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>(), fileWatch };
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        path, []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ & ]() {
        return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES
               && crawlerVisitor.isLoadingFinished();
    } ) );
    crawlerVisitor.showSized();

    WHEN( "Log Lines are appended to the Log File" )
    {
        REQUIRE( fileWatch->grow( path, "one more Log Line\nand another\n" ) );
        REQUIRE( waitUiState( [ & ]() {
            return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES + 2
                   && crawlerVisitor.isLoadingFinished();
        } ) );
        QCoreApplication::processEvents();

        THEN( "the main view keeps the Visual Lines counted for its bottom, as nothing was "
              "decoded differently" )
        {
            REQUIRE( crawlerVisitor.mainViewKeepsBottomLines() );
        }
    }
}

SCENARIO( "The chart extracts its points again under a changed Encoding", "[ui][encoding][chart]" )
{
    QTemporaryDir directory;
    REQUIRE( directory.isValid() );
    const auto path = directory.filePath( "encoded.log" );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        for ( int i = 0; i < SL_NB_LINES; i++ ) {
            file.write( QByteArray( "caf\xC3\xA9 order " ) + QByteArray::number( i ) + "\n" );
        }
    }

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        path, []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ & ]() {
        return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES
               && crawlerVisitor.isLoadingFinished();
    } ) );
    crawlerVisitor.showSized();

    GIVEN( "a chart counting the Log Lines that read an accented word decoded as UTF-8" )
    {
        crawlerVisitor.crawler->setEncoding( TextEncoding::forName( "UTF-8" )->mibEnum() );
        crawlerVisitor.showChartCounting( QString::fromUtf8( "caf\xC3\xA9" ) );
        REQUIRE(
            waitUiState( [ & ]() { return crawlerVisitor.chartPoints() == SL_NB_LINES; }, 20000 ) );

        WHEN( "the Log File is displayed as ISO-8859-1, where none reads so" )
        {
            crawlerVisitor.crawler->setEncoding( TextEncoding::forName( "ISO-8859-1" )->mibEnum() );

            THEN( "the chart drops the points extracted under the old Encoding" )
            {
                REQUIRE( waitUiState(
                    [ & ]() {
                        return crawlerVisitor.isLoadingFinished()
                               && crawlerVisitor.chartPoints() == 0;
                    },
                    20000 ) );
            }
        }
    }
}

SCENARIO( "Selecting a Match in the Filtered View moves the main view only when it is off screen",
          "[ui][jump]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    session.savedSearches().clear();

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );

    waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } );
    waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );
    crawlerVisitor.showSized();

    crawlerVisitor.setSearchPattern( "this is line" );
    crawlerVisitor.runSearch();
    REQUIRE( waitUiState( [ &crawlerVisitor ]() {
        return crawlerVisitor.getLogFilteredNbLines().get() == SL_NB_LINES;
    } ) );

    // Line 50 is off screen, and not so near the end that the bottom stops it.
    REQUIRE( crawlerVisitor.mainViewScrollPosition().lineNumber < 10_lnum );
    REQUIRE( crawlerVisitor.mainViewRows() < 40 );

    WHEN( "a Match off screen in the main view is selected" )
    {
        crawlerVisitor.selectInFilteredView( 50_lnum );

        THEN( "its Log Line is put on the main view's top row" )
        {
            REQUIRE( crawlerVisitor.mainViewScrollPosition() == ScrollPosition{ 50_lnum, 0 } );
        }

        AND_WHEN( "a Match already wholly visible in the main view is selected" )
        {
            crawlerVisitor.selectInFilteredView( 51_lnum );

            THEN( "the main view does not scroll" )
            {
                REQUIRE( crawlerVisitor.mainViewScrollPosition() == ScrollPosition{ 50_lnum, 0 } );
            }
        }
    }
}

namespace {

QStringList savedLines( const QString& fileName )
{
    QFile file{ fileName };
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return QString::fromUtf8( file.readAll() ).split( '\n', Qt::SkipEmptyParts );
}

// Opens a Log File of SL_NB_LINES Log Lines in a shown CrawlerWidget.
void openCrawler( Session& session, QTemporaryFile& file, CrawlerWidgetVisitor& crawlerVisitor )
{
    REQUIRE( generateDataFiles( file ) );
    session.savedSearches().clear();

    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );

    waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } );
    waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );
    crawlerVisitor.showSized();
}

} // namespace

// The CrawlerWidget holds the Presentation Policy and the QuickFind Policy of
// its Log File and reads no setting on those two axes for itself (#185).
// Every Policy below says something other than the shipped default, so a
// widget that still reached for the settings store would fail these.
SCENARIO( "The Crawler Widget shows and searches under the Policies it was handed",
          "[ui][settings]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };

    GIVEN( "a Policy wrapping text, and one reading the Search line as a fixed string" )
    {
        auto policies = testSettingsPolicies();
        policies.presentation.useTextWrap = true;
        policies.quickFind.mainRegexpType = SearchRegexpType::FixedString;

        Session session{ policies, std::make_shared<LogFormatCatalog>() };
        CrawlerWidgetVisitor crawlerVisitor;
        openCrawler( session, file, crawlerVisitor );

        THEN( "every text view of the Log File wraps" )
        {
            REQUIRE( crawlerVisitor.crawler->isTextWrapEnabled() );
            REQUIRE( crawlerVisitor.filteredView()->isTextWrapEnabled() );
        }

        THEN( "the Search line does not read its pattern as a regexp" )
        {
            REQUIRE( !crawlerVisitor.useRegexpChecked() );
        }

        WHEN( "a QuickFind Policy reading it as an extended regexp arrives" )
        {
            policies.quickFind.mainRegexpType = SearchRegexpType::ExtendedRegexp;
            session.applyPolicies( policies );

            // Nothing is handed on to the window: its QuickFind bar takes the
            // Policy from its session (#231).
            THEN( "the Log File already open holds it" )
            {
                REQUIRE( crawlerVisitor.crawler->quickFindPolicy().mainRegexpType
                         == SearchRegexpType::ExtendedRegexp );
            }
        }
    }

    GIVEN( "a Policy that does not wrap text" )
    {
        auto policies = testSettingsPolicies();
        policies.presentation.useTextWrap = false;

        Session session{ policies, std::make_shared<LogFormatCatalog>() };
        CrawlerWidgetVisitor crawlerVisitor;
        openCrawler( session, file, crawlerVisitor );

        THEN( "no text view of the Log File wraps" )
        {
            REQUIRE( !crawlerVisitor.crawler->isTextWrapEnabled() );
            REQUIRE( !crawlerVisitor.filteredView()->isTextWrapEnabled() );
        }
    }
}

SCENARIO( "Save selected to file writes the selection of the Presentation shown",
          "[ui][presentation]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    CrawlerWidgetVisitor crawlerVisitor;
    openCrawler( session, file, crawlerVisitor );

    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto fileName = dir.filePath( "crawlerwidget_save_selected_test.log" );

    GIVEN( "Log Line 3 selected in the Table View and Log Line 10 in the Text View" )
    {
        crawlerVisitor.showTableView( true );
        REQUIRE( crawlerVisitor.tableView()->model() != nullptr );
        crawlerVisitor.tableView()->selectRow( 3 );
        crawlerVisitor.textView()->selectAndDisplayLine( 10_lnum );

        WHEN( "the Table View is shown and the selection is saved" )
        {
            REQUIRE( crawlerVisitor.presentation() == crawlerVisitor.tableView() );
            crawlerVisitor.presentation()->saveSelectedTo( fileName );

            THEN( "the file holds the Table View's Log Line" )
            {
                const auto lines = savedLines( fileName );
                REQUIRE( lines.size() == 1 );
                REQUIRE( lines[ 0 ].contains( "line 000003" ) );
            }
        }

        WHEN( "the Text View is shown and the selection is saved" )
        {
            crawlerVisitor.showTableView( false );
            REQUIRE( crawlerVisitor.presentation() == crawlerVisitor.textView() );
            crawlerVisitor.presentation()->saveSelectedTo( fileName );

            THEN( "the file holds the Text View's Log Line" )
            {
                const auto lines = savedLines( fileName );
                REQUIRE( lines.size() == 1 );
                REQUIRE( lines[ 0 ].contains( "line 000010" ) );
            }
        }
    }
}

SCENARIO( "Both Presentations report to the CrawlerWidget alike", "[ui][presentation]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    CrawlerWidgetVisitor crawlerVisitor;
    openCrawler( session, file, crawlerVisitor );

    // Sends the reports through whichever Presentation is shown, and checks
    // what the CrawlerWidget made of them.
    const auto reportThroughShownPresentation = [ & ]( QObject* shown ) {
        QSignalSpy newSelection( crawlerVisitor.crawler.get(), &CrawlerWidget::newSelection );
        QSignalSpy scratchpad( crawlerVisitor.crawler.get(), &CrawlerWidget::sendToScratchpad );

        if ( auto* textView = qobject_cast<LogMainView*>( shown ) ) {
            Q_EMIT textView->newSelection( 7_lnum, 1_lcount, 0_lcol, 0_length );
            Q_EMIT textView->markLines( { 5_lnum } );
            Q_EMIT textView->addToSearch( "needle" );
            Q_EMIT textView->sendSelectionToScratchpad();
        }
        else if ( auto* tableView = qobject_cast<LogTableView*>( shown ) ) {
            Q_EMIT tableView->newSelection( 7_lnum, 1_lcount, 0_lcol, 0_length );
            Q_EMIT tableView->markLines( { 5_lnum } );
            Q_EMIT tableView->addToSearch( "needle" );
            Q_EMIT tableView->sendSelectionToScratchpad();
        }

        REQUIRE( newSelection.size() == 1 );
        REQUIRE( newSelection.first().at( 0 ).value<LineNumber>() == 7_lnum );
        REQUIRE( crawlerVisitor.isMarked( 5_lnum ) );
        REQUIRE( crawlerVisitor.searchText().contains( "needle" ) );
        REQUIRE( scratchpad.size() == 1 );
        REQUIRE( scratchpad.first().at( 0 ).toString()
                 == crawlerVisitor.presentation()->selectedText() );
    };

    WHEN( "the Text View is shown" )
    {
        crawlerVisitor.showTableView( false );

        THEN( "its selection, Marks, Search and scratchpad reports reach the CrawlerWidget" )
        {
            reportThroughShownPresentation( crawlerVisitor.textView() );
        }
    }

    WHEN( "the Table View is shown" )
    {
        crawlerVisitor.showTableView( true );

        THEN( "its selection, Marks, Search and scratchpad reports reach the CrawlerWidget" )
        {
            reportThroughShownPresentation( crawlerVisitor.tableView() );
        }
    }
}

namespace {

// Any view of a Log File that emits the signals every view shares.
using SharedSignalView = std::variant<LogMainView*, LogTableView*, FilteredView*>;

// What a Crawler Widget made of a shared signal, watched from before it was
// emitted.
struct SharedSignalArrivals {
    explicit SharedSignalArrivals( CrawlerWidget* crawler )
        : dataStatus( crawler, &CrawlerWidget::dataStatusChanged )
        , sentToScratchpad( crawler, &CrawlerWidget::sendToScratchpad )
        , replacedInScratchpad( crawler, &CrawlerWidget::replaceDataInScratchpad )
    {
    }

    QSignalSpy dataStatus;
    QSignalSpy sentToScratchpad;
    QSignalSpy replacedInScratchpad;
    // The changes the views reported for every open Log File.
    std::vector<Changed> reported;
};

// One signal of the set every view shares (ADR 0003): how a view emits it,
// and whether the Crawler Widget did what it asks.
struct SharedSignal {
    const char* name;
    std::function<void( SharedSignalView )> emit;
    std::function<bool( CrawlerWidgetVisitor&, const SharedSignalArrivals& )> arrived;
};

// Emits through whichever view is handed.
template <class Emit>
std::function<void( SharedSignalView )> emitting( Emit emit )
{
    return [ emit ]( SharedSignalView view ) { std::visit( emit, view ); };
}

// The Search line reads "haystack", the Search Limits are Log Lines 3 to 8,
// Color Label 1 holds Log Line 10, which is selected in the main view, and
// the tab shows new data, before any of these is emitted.
std::vector<SharedSignal> sharedSignals()
{
    return {
        { "markLines", emitting( []( auto* view ) { Q_EMIT view->markLines( { 5_lnum } ); } ),
          []( CrawlerWidgetVisitor& crawler, const auto& ) { return crawler.isMarked( 5_lnum ); } },
        { "highlightersChange", emitting( []( auto* view ) { Q_EMIT view->highlightersChange(); } ),
          []( CrawlerWidgetVisitor&, const auto& arrivals ) {
              return arrivals.reported == std::vector<Changed>{ Changed::HighlighterSets };
          } },
        { "addToSearch", emitting( []( auto* view ) { Q_EMIT view->addToSearch( "needle" ); } ),
          []( CrawlerWidgetVisitor& crawler, const auto& ) {
              return crawler.searchText().contains( "haystack" )
                     && crawler.searchText().contains( "needle" );
          } },
        { "excludeFromSearch",
          emitting( []( auto* view ) { Q_EMIT view->excludeFromSearch( "needle" ); } ),
          []( CrawlerWidgetVisitor& crawler, const auto& ) {
              return crawler.searchText().contains( "haystack" )
                     && crawler.searchText().contains( "not(" )
                     && crawler.searchText().contains( "needle" );
          } },
        { "replaceSearch", emitting( []( auto* view ) { Q_EMIT view->replaceSearch( "needle" ); } ),
          []( CrawlerWidgetVisitor& crawler, const auto& ) {
              return crawler.searchText() == "needle";
          } },
        { "activity", emitting( []( auto* view ) { Q_EMIT view->activity(); } ),
          []( CrawlerWidgetVisitor&, const auto& arrivals ) {
              return arrivals.dataStatus.size() == 1
                     && arrivals.dataStatus.first().at( 0 ).template value<DataStatus>()
                            == DataStatus::OLD_DATA;
          } },
        { "changeSearchLimits",
          emitting( []( auto* view ) { Q_EMIT view->changeSearchLimits( 4_lnum, 9_lnum ); } ),
          []( CrawlerWidgetVisitor& crawler, const auto& ) {
              return crawler.searchLimits() == std::make_pair( 4_lnum, 9_lnum );
          } },
        { "clearSearchLimits", emitting( []( auto* view ) { Q_EMIT view->clearSearchLimits(); } ),
          []( CrawlerWidgetVisitor& crawler, const auto& ) {
              return crawler.searchLimits() == std::make_pair( 0_lnum, LineNumber( SL_NB_LINES ) );
          } },
        { "saveDefaultSplitterSizes",
          emitting( []( auto* view ) { Q_EMIT view->saveDefaultSplitterSizes(); } ),
          []( CrawlerWidgetVisitor& crawler, const auto& ) {
              return Configuration::get().splitterSizes() == crawler.crawler->sizes();
          } },
        { "addColorLabel", emitting( []( auto* view ) { Q_EMIT view->addColorLabel( 0 ); } ),
          []( CrawlerWidgetVisitor& crawler, const auto& ) {
              return !crawler.colorLabels()[ 0 ].isEmpty();
          } },
        { "clearColorLabels", emitting( []( auto* view ) { Q_EMIT view->clearColorLabels(); } ),
          []( CrawlerWidgetVisitor& crawler, const auto& ) {
              const auto& labels = crawler.colorLabels();
              return std::all_of( labels.cbegin(), labels.cend(),
                                  []( const auto& words ) { return words.isEmpty(); } );
          } },
        { "sendSelectionToScratchpad",
          emitting( []( auto* view ) { Q_EMIT view->sendSelectionToScratchpad(); } ),
          []( CrawlerWidgetVisitor&, const auto& arrivals ) {
              return arrivals.sentToScratchpad.size() == 1;
          } },
        { "replaceScratchpadWithSelection",
          emitting( []( auto* view ) { Q_EMIT view->replaceScratchpadWithSelection(); } ),
          []( CrawlerWidgetVisitor&, const auto& arrivals ) {
              return arrivals.replacedInScratchpad.size() == 1;
          } },
    };
}

// The splitter sizes saved in the settings, restored when this object goes.
class PinnedSplitterSizes {
public:
    PinnedSplitterSizes()
        : sizes_( Configuration::get().splitterSizes() )
    {
    }

    ~PinnedSplitterSizes()
    {
        auto& config = Configuration::get();
        config.setSplitterSizes( sizes_ );
        config.save();
    }

    PinnedSplitterSizes( const PinnedSplitterSizes& ) = delete;
    PinnedSplitterSizes& operator=( const PinnedSplitterSizes& ) = delete;

private:
    QList<int> sizes_;
};

} // namespace

// The views of a Log File share one set of signals (ADR 0003), and the
// Crawler Widget connects that set in one place for the Presentations and
// the Filtered Views alike (#391).
SCENARIO( "Every shared signal reaches the Crawler Widget from a Presentation and from a "
          "Filtered View",
          "[ui][presentation]" )
{
    const auto table = sharedSignals();
    const auto& shared = table.at( GENERATE( range( std::size_t{ 0 }, sharedSignals().size() ) ) );
    const auto viewName = GENERATE( as<std::string>{}, "Text View", "Table View", "Filtered View" );

    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );
    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    session.savedSearches().clear();
    const PinnedSplitterSizes pinnedSplitterSizes;

    std::vector<Changed> reported;
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), [ & ]( const ViewBuild& build ) {
            auto recording = build;
            recording.changeReport
                = [ &reported ]( Changed change ) { reported.push_back( change ); };
            return new CrawlerWidget( recording );
        } ) ) );
    waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );
    crawlerVisitor.showSized();

    crawlerVisitor.setSearchPattern( "haystack" );
    crawlerVisitor.setSearchLimits( 3_lnum, 8_lnum );
    crawlerVisitor.addColorLabelToLogLine( 10_lnum, 1 );
    crawlerVisitor.showNewData();
    Configuration::get().setSplitterSizes( { 1, 2 } );

    const auto view = [ & ]() -> SharedSignalView {
        if ( viewName == "Text View" ) {
            return crawlerVisitor.textView();
        }
        if ( viewName == "Table View" ) {
            return crawlerVisitor.tableView();
        }
        return crawlerVisitor.filteredView();
    }();

    WHEN( std::string{ "the " } + viewName + " emits " + shared.name )
    {
        SharedSignalArrivals arrivals{ crawlerVisitor.crawler.get() };
        shared.emit( view );
        QCoreApplication::processEvents();
        arrivals.reported = reported;

        THEN( "the Crawler Widget does what it asks" )
        {
            INFO( "Search line: " << crawlerVisitor.searchText().toStdString() );
            REQUIRE( shared.arrived( crawlerVisitor, arrivals ) );
        }
    }
}

SCENARIO( "A Row selected in the Table View is selected in the Filtered View too",
          "[ui][presentation]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    CrawlerWidgetVisitor crawlerVisitor;
    openCrawler( session, file, crawlerVisitor );

    GIVEN( "the Table View shown and Log Lines 10 to 19 matching the search" )
    {
        crawlerVisitor.showTableView( true );
        REQUIRE( crawlerVisitor.presentation() == crawlerVisitor.tableView() );

        crawlerVisitor.setSearchPattern( "this is line 00001" );
        crawlerVisitor.runSearch();
        REQUIRE( waitUiState( [ &crawlerVisitor ]() {
            return crawlerVisitor.getLogFilteredNbLines().get() == 10;
        } ) );

        WHEN( "the Row of the matching Log Line 15 is selected" )
        {
            crawlerVisitor.tableView()->selectRow( 15 );

            THEN( "the Filtered View selects Log Line 15" )
            {
                REQUIRE( crawlerVisitor.filteredViewSelectedText().contains( "line 000015" ) );
                REQUIRE( crawlerVisitor.tableView()->selectedLogLines()
                         == logsquirl::vector<LineNumber>{ 15_lnum } );
            }
        }

        WHEN( "the Row of Log Line 30, which does not match, is selected" )
        {
            crawlerVisitor.tableView()->selectRow( 30 );

            THEN( "the Filtered View selects the Match before it, and the Table View keeps "
                  "its selection" )
            {
                REQUIRE( crawlerVisitor.filteredViewSelectedText().contains( "line 000019" ) );
                REQUIRE( crawlerVisitor.tableView()->selectedLogLines()
                         == logsquirl::vector<LineNumber>{ 30_lnum } );
            }
        }
    }
}

namespace {

// The Highlighter Sets, which of them are active, and the colors of the Color
// Labels, restored when this object goes: nothing a test ticks leaks into the
// tests that run next.
class PinnedHighlighterSets {
public:
    PinnedHighlighterSets()
        : sets_( HighlighterSetCollection::get().highlighterSets() )
        , activeSetIds_( HighlighterSetCollection::get().activeSetIds() )
        , colorLabels_( HighlighterSetCollection::get().quickHighlighters() )
    {
    }

    ~PinnedHighlighterSets()
    {
        auto& collection = HighlighterSetCollection::get();
        collection.setQuickHighlighters( colorLabels_ );
        collection.setHighlighterSets( sets_ );
        collection.deactivateAll();
        for ( const auto& setId : activeSetIds_ ) {
            collection.activateSet( setId );
        }
    }

    PinnedHighlighterSets( const PinnedHighlighterSets& ) = delete;
    PinnedHighlighterSets& operator=( const PinnedHighlighterSets& ) = delete;

private:
    QList<HighlighterSet> sets_;
    QStringList activeSetIds_;
    QList<QuickHighlighter> colorLabels_;
};

bool showsColor( QWidget* view, const QColor& color )
{
    const auto image = view->grab().toImage();
    for ( auto y = 0; y < image.height(); ++y ) {
        for ( auto x = 0; x < image.width(); ++x ) {
            if ( image.pixelColor( x, y ).rgb() == color.rgb() ) {
                return true;
            }
        }
    }
    return false;
}

std::vector<QPointer<QShortcut>> shortcutsOf( const QObject& crawler )
{
    std::vector<QPointer<QShortcut>> shortcuts;
    for ( auto* shortcut : crawler.findChildren<QShortcut*>() ) {
        shortcuts.emplace_back( shortcut );
    }
    return shortcuts;
}

bool allAlive( const std::vector<QPointer<QShortcut>>& shortcuts )
{
    return std::all_of( shortcuts.cbegin(), shortcuts.cend(),
                        []( const auto& shortcut ) { return !shortcut.isNull(); } );
}

} // namespace

SCENARIO( "A Highlighter Set change re-highlights the views and nothing else",
          "[ui][highlighters]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    CrawlerWidgetVisitor crawlerVisitor;
    openCrawler( session, file, crawlerVisitor );

    const PinnedHighlighterSets pinnedSets;
    auto& collection = HighlighterSetCollection::get();
    collection.deactivateAll();

    // A color nothing else in the views is painted with.
    const QColor highlightColor{ 0x12, 0x34, 0x56 };
    auto set = HighlighterSet::createNewSet( "crawlerwidget_test_highlighters" );
    set.addHighlighter( Highlighter{ "line 000003", false, false, Qt::white, highlightColor } );
    auto sets = collection.highlighterSets();
    sets.append( set );
    collection.setHighlighterSets( sets );

    GIVEN( "Log Line 3 shown in the main and the Filtered View, with no Highlighter Set active" )
    {
        crawlerVisitor.setSearchPattern( "this is line 000003" );
        crawlerVisitor.runSearch();
        REQUIRE( waitUiState(
            [ &crawlerVisitor ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 1; } ) );
        QTest::qWait( 50 );

        REQUIRE_FALSE( showsColor( crawlerVisitor.textView(), highlightColor ) );
        REQUIRE_FALSE( showsColor( crawlerVisitor.filteredView(), highlightColor ) );

        // What a view's Highlighters menu does when a set is ticked: the set
        // is activated, then the view reports the change.
        collection.activateSet( set.id() );
        const auto shortcuts = shortcutsOf( *crawlerVisitor.crawler );
        REQUIRE_FALSE( shortcuts.empty() );

        WHEN( "the set is ticked in the Filtered View" )
        {
            Q_EMIT crawlerVisitor.filteredView()->highlightersChange();
            QTest::qWait( 50 );

            THEN( "the main and the Filtered View are painted with it" )
            {
                REQUIRE( showsColor( crawlerVisitor.textView(), highlightColor ) );
                REQUIRE( showsColor( crawlerVisitor.filteredView(), highlightColor ) );
            }

            THEN( "the Configuration is not applied again: no shortcut is registered anew" )
            {
                REQUIRE( allAlive( shortcuts ) );
            }
        }

        WHEN( "the set is ticked in the Text View" )
        {
            Q_EMIT crawlerVisitor.textView()->highlightersChange();
            QTest::qWait( 50 );

            THEN( "the main and the Filtered View are painted with it" )
            {
                REQUIRE( showsColor( crawlerVisitor.textView(), highlightColor ) );
                REQUIRE( showsColor( crawlerVisitor.filteredView(), highlightColor ) );
            }

            THEN( "the Configuration is not applied again: no shortcut is registered anew" )
            {
                REQUIRE( allAlive( shortcuts ) );
            }
        }

        WHEN( "the set is ticked in the Table View" )
        {
            Q_EMIT crawlerVisitor.tableView()->highlightersChange();
            QTest::qWait( 50 );

            THEN( "the Configuration is not applied again: no shortcut is registered anew" )
            {
                REQUIRE( allAlive( shortcuts ) );
            }
        }
    }
}

SCENARIO( "Hiding ANSI color sequences reaches an open Log File through its Decoding Policy",
          "[ui][settings]" )
{
    QTemporaryFile file{ "crawler_ansi_test_XXXXXX" };
    REQUIRE( file.open() );
    file.write( "plain line\n" );
    file.write( "\x1B[31mERROR\x1B[0m: disk full\n" );
    file.write( "another plain line\n" );
    file.flush();

    auto policies = testSettingsPolicies();
    policies.decoding.hideAnsiColorSequences = false;
    Session session{ policies, std::make_shared<LogFormatCatalog>() };
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ &crawlerVisitor ]() {
        return crawlerVisitor.getLogNbLines().get() == 3 && crawlerVisitor.isLoadingFinished();
    } ) );

    REQUIRE( crawlerVisitor.logLineString( 1_lnum ).contains( "\x1B[31m" ) );

    WHEN( "the Policies re-derived after the setting was ticked are applied" )
    {
        policies.decoding.hideAnsiColorSequences = true;
        session.applyPolicies( policies );

        THEN( "the Log Line reads without them" )
        {
            REQUIRE( crawlerVisitor.logLineString( 1_lnum ) == "ERROR: disk full" );
        }

        AND_WHEN( "the CrawlerWidget reads the settings without a Policy again, while the store "
                  "still shows them" )
        {
            auto& config = Configuration::get();
            const auto hideAnsiColorSequences = config.hideAnsiColorSequences();
            config.setHideAnsiColorSequences( false );
            crawlerVisitor.crawler->applyChange(
                ViewChange{ .rereadSettingsWithoutPolicy = true } );
            config.setHideAnsiColorSequences( hideAnsiColorSequences );

            THEN( "the Log Line still reads without them: only the Policy decides" )
            {
                REQUIRE( crawlerVisitor.logLineString( 1_lnum ) == "ERROR: disk full" );
            }
        }
    }
}

namespace {

void writeAnsiLogFile( QTemporaryFile& file )
{
    REQUIRE( file.open() );
    file.write( "plain line\n" );
    file.write( "\x1B[31mERROR\x1B[0m: disk full\n" );
    file.write( "another plain line\n" );
    file.flush();
}

void openAnsiCrawler( Session& session, QTemporaryFile& file, CrawlerWidgetVisitor& crawlerVisitor )
{
    writeAnsiLogFile( file );
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ &crawlerVisitor ]() {
        return crawlerVisitor.getLogNbLines().get() == 3 && crawlerVisitor.isLoadingFinished();
    } ) );
    crawlerVisitor.showSized();
}

void searchFor( CrawlerWidgetVisitor& crawlerVisitor, const QString& pattern )
{
    crawlerVisitor.clearSearchPattern();
    crawlerVisitor.setSearchPattern( pattern );
    crawlerVisitor.runSearch();
    REQUIRE( waitUiState(
        [ &crawlerVisitor ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 1; } ) );
    QTest::qWait( 50 );
}

} // namespace

SCENARIO( "Every view of every open Log File shows its Log Lines under a changed Decoding Policy",
          "[ui][settings]" )
{
    QTemporaryFile firstFile{ "crawler_ansi_first_XXXXXX" };
    QTemporaryFile secondFile{ "crawler_ansi_second_XXXXXX" };

    auto policies = testSettingsPolicies();
    policies.decoding.hideAnsiColorSequences = false;
    Session session{ policies, std::make_shared<LogFormatCatalog>() };
    session.savedSearches().clear();

    const PinnedHighlighterSets pinnedSets;
    auto& collection = HighlighterSetCollection::get();
    collection.deactivateAll();

    // The ANSI color sequences split this text in the Log File, so a view
    // is painted with the color only once it shows the Log Line without them.
    const QColor highlightColor{ 0x65, 0x43, 0x21 };
    auto set = HighlighterSet::createNewSet( "crawlerwidget_test_decoding" );
    set.addHighlighter(
        Highlighter{ "ERROR: disk full", false, false, Qt::white, highlightColor } );
    auto sets = collection.highlighterSets();
    sets.append( set );
    collection.setHighlighterSets( sets );
    collection.activateSet( set.id() );

    CrawlerWidgetVisitor first;
    CrawlerWidgetVisitor second;
    openAnsiCrawler( session, firstFile, first );
    openAnsiCrawler( session, secondFile, second );

    GIVEN( "two Log Files shown with their sequences, one with a kept Search in a tab not current" )
    {
        searchFor( first, "disk full" );
        first.keepSearchResults();
        searchFor( first, "ERROR" );
        REQUIRE( first.currentFilteredViewTab() == 1 );
        REQUIRE( first.filteredViewInTab( 0 ) != nullptr );
        REQUIRE( first.filteredViewInTab( 1 ) != nullptr );

        searchFor( second, "disk full" );

        // Every view has painted the Log Lines as they read now.
        REQUIRE( first.logLineString( 1_lnum ).contains( "\x1B[31m" ) );
        REQUIRE_FALSE( showsColor( first.textView(), highlightColor ) );
        REQUIRE_FALSE( showsColor( first.filteredViewInTab( 0 ), highlightColor ) );
        REQUIRE_FALSE( showsColor( first.filteredViewInTab( 1 ), highlightColor ) );
        REQUIRE_FALSE( showsColor( second.textView(), highlightColor ) );
        REQUIRE_FALSE( showsColor( second.filteredView(), highlightColor ) );

        // As in a window's tabs: the second Log File is not the current one.
        second.crawler->hide();

        WHEN( "the Policies re-derived after hiding ANSI color sequences was ticked are applied" )
        {
            policies.decoding.hideAnsiColorSequences = true;
            session.applyPolicies( policies );
            QTest::qWait( 50 );

            THEN( "the main view and both Filtered Views of the first Log File show them hidden" )
            {
                REQUIRE( showsColor( first.textView(), highlightColor ) );
                REQUIRE( showsColor( first.filteredViewInTab( 0 ), highlightColor ) );
                REQUIRE( showsColor( first.filteredViewInTab( 1 ), highlightColor ) );
            }

            THEN( "the views of the Log File in the tab not current show them hidden too" )
            {
                second.showSized();
                REQUIRE( showsColor( second.textView(), highlightColor ) );
                REQUIRE( showsColor( second.filteredView(), highlightColor ) );
            }
        }
    }
}

namespace {

// The Log Files of generateDataFiles(), in which "line 000003" and
// "line 000007" each end one Log Line. A main-search match is colored only
// where it matches, and the end of the Log Line lies past the right edge of
// the view in a wider font -- Windows' offscreen platform draws in one -- so
// the Search matches from the first column on, which every view shows
// whatever the font.
void searchForOneLine( CrawlerWidgetVisitor& crawlerVisitor, const QString& lineEnd )
{
    crawlerVisitor.clearSearchPattern();
    crawlerVisitor.setSearchPattern( "LOGDATA.*" + lineEnd );
    crawlerVisitor.runSearch();
    REQUIRE( waitUiState(
        [ &crawlerVisitor ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 1; } ) );
    QTest::qWait( 50 );
}

} // namespace

// The Decoration Policy travels as an Axis like the others (#190): the
// Session hands it to every open Log File, and the CrawlerWidget derives
// none from the settings store. The store's shipped default colors no
// main-search match at all, so a view painted in the Policy's color was
// painted under the Policy.
SCENARIO( "A changed Decoration Policy reaches every view of every open Log File",
          "[ui][settings]" )
{
    QTemporaryFile firstFile{ "crawler_decoration_first_XXXXXX" };
    QTemporaryFile secondFile{ "crawler_decoration_second_XXXXXX" };

    // Colors nothing else in the views is painted with.
    const QColor openedColor{ 0x21, 0x43, 0x65 };
    const QColor changedColor{ 0x56, 0x34, 0x12 };

    auto policies = testSettingsPolicies();
    policies.decoration.mainSearchHighlight = true;
    policies.decoration.variateMainSearchHighlight = false;
    policies.decoration.mainSearchBackColor = openedColor;
    Session session{ policies, std::make_shared<LogFormatCatalog>() };

    const PinnedHighlighterSets pinnedSets;
    HighlighterSetCollection::get().deactivateAll();

    CrawlerWidgetVisitor first;
    CrawlerWidgetVisitor second;
    openCrawler( session, firstFile, first );
    openCrawler( session, secondFile, second );

    GIVEN( "two Log Files, one with a kept Search in a tab not current" )
    {
        searchForOneLine( first, "line 000003" );
        first.keepSearchResults();
        searchForOneLine( first, "line 000007" );
        REQUIRE( first.currentFilteredViewTab() == 1 );
        REQUIRE( first.filteredViewInTab( 0 ) != nullptr );
        REQUIRE( first.filteredViewInTab( 1 ) != nullptr );

        searchForOneLine( second, "line 000003" );

        THEN( "every view was painted in the colors of the Policy it was opened under" )
        {
            REQUIRE( first.crawler->decorationPolicy() == policies.decoration );
            REQUIRE( showsColor( first.textView(), openedColor ) );
            REQUIRE( showsColor( first.filteredViewInTab( 0 ), openedColor ) );
            REQUIRE( showsColor( first.filteredViewInTab( 1 ), openedColor ) );
            REQUIRE( showsColor( second.textView(), openedColor ) );
            REQUIRE( showsColor( second.filteredView(), openedColor ) );
        }

        // As in a window's tabs: the second Log File is not the current one.
        second.crawler->hide();

        WHEN( "the Policies re-derived after the main-search color was changed are applied" )
        {
            auto changed = policies;
            changed.decoration.mainSearchBackColor = changedColor;
            session.applyPolicies( changed );
            QTest::qWait( 50 );

            THEN( "the main view and both Filtered Views of the first Log File take the new color" )
            {
                REQUIRE( first.crawler->decorationPolicy() == changed.decoration );
                REQUIRE( showsColor( first.textView(), changedColor ) );
                REQUIRE( showsColor( first.filteredViewInTab( 0 ), changedColor ) );
                REQUIRE( showsColor( first.filteredViewInTab( 1 ), changedColor ) );
                REQUIRE_FALSE( showsColor( first.textView(), openedColor ) );
            }

            THEN(
                "the views of the Log File in the tab not current take it too, with no new Search" )
            {
                second.crawler->show();
                QCoreApplication::processEvents();
                REQUIRE( second.crawler->decorationPolicy() == changed.decoration );
                REQUIRE( showsColor( second.textView(), changedColor ) );
                REQUIRE( showsColor( second.filteredView(), changedColor ) );
                REQUIRE_FALSE( showsColor( second.textView(), openedColor ) );
            }
        }

        WHEN( "a Policy that changes some other axis arrives" )
        {
            auto changed = policies;
            changed.watch.pollingEnabled = false;
            session.applyPolicies( changed );
            QTest::qWait( 50 );

            THEN( "the Decoration Policy the views hold is left exactly as it was" )
            {
                REQUIRE( first.crawler->decorationPolicy() == policies.decoration );
                REQUIRE( second.crawler->decorationPolicy() == policies.decoration );
                REQUIRE( showsColor( first.textView(), openedColor ) );
                REQUIRE( showsColor( first.filteredViewInTab( 0 ), openedColor ) );
            }
        }

        WHEN( "the CrawlerWidget reads the settings without a Policy again, while the store "
              "colors main-search matches otherwise" )
        {
            auto& config = Configuration::get();
            const auto mainSearchHighlight = config.mainSearchHighlight();
            const auto mainSearchBackColor = config.mainSearchBackColor();
            config.setEnableMainSearchHighlight( true );
            config.setMainSearchBackColor( changedColor );
            first.crawler->applyChange( ViewChange{ .rereadSettingsWithoutPolicy = true } );
            config.setEnableMainSearchHighlight( mainSearchHighlight );
            config.setMainSearchBackColor( mainSearchBackColor );
            QTest::qWait( 50 );

            THEN( "the views still show the Policy's color: only the Policy decides" )
            {
                REQUIRE( showsColor( first.textView(), openedColor ) );
                REQUIRE_FALSE( showsColor( first.textView(), changedColor ) );
                REQUIRE( showsColor( first.filteredViewInTab( 1 ), openedColor ) );
            }
        }

        WHEN( "the kept Search's Filtered View is destroyed and a Policy arrives afterwards" )
        {
            const QPointer<FilteredView> kept{ first.filteredViewInTab( 0 ) };
            first.closeFilteredViewTab( 0 );
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
            REQUIRE( kept.isNull() );

            auto changed = policies;
            changed.decoration.mainSearchBackColor = changedColor;
            session.applyPolicies( changed );
            QTest::qWait( 50 );

            THEN( "the surviving one is still reached, and nothing dangles" )
            {
                REQUIRE( first.filteredViewInTab( 0 ) != nullptr );
                REQUIRE( showsColor( first.filteredViewInTab( 0 ), changedColor ) );
                REQUIRE( showsColor( first.textView(), changedColor ) );
            }
        }
    }
}

namespace {

bool showsLineNumbers( const AbstractLogView* view )
{
    return view->viewportLayout().input().lineNumbersVisible;
}

} // namespace

// Whether line numbers are drawn and whether the overview is shown ride the
// Presentation Policy (#192): the CrawlerWidget reads neither from the
// settings store, so a change reaches every open Log File the way every other
// Presentation setting does, not only the one in the active tab.
SCENARIO( "Line numbers and the overview follow the Presentation Policy in every open Log File",
          "[ui][settings]" )
{
    QTemporaryFile firstFile{ "crawler_line_numbers_first_XXXXXX" };
    QTemporaryFile secondFile{ "crawler_line_numbers_second_XXXXXX" };

    // The other way round from the shipped defaults, so a widget that still
    // read the settings store would show the defaults instead.
    auto policies = testSettingsPolicies();
    policies.presentation.mainLineNumbersVisible = true;
    policies.presentation.filteredLineNumbersVisible = false;
    policies.presentation.overviewVisible = false;
    Session session{ policies, std::make_shared<LogFormatCatalog>() };

    CrawlerWidgetVisitor first;
    CrawlerWidgetVisitor second;
    openCrawler( session, firstFile, first );
    openCrawler( session, secondFile, second );

    GIVEN( "two Log Files, one with a kept Search in a tab not current" )
    {
        searchForOneLine( first, "line 000003" );
        first.keepSearchResults();
        searchForOneLine( first, "line 000007" );
        REQUIRE( first.currentFilteredViewTab() == 1 );

        searchForOneLine( second, "line 000003" );

        THEN( "every view shows what the Policy it was opened under says" )
        {
            REQUIRE( showsLineNumbers( first.textView() ) );
            REQUIRE_FALSE( showsLineNumbers( first.filteredViewInTab( 0 ) ) );
            REQUIRE_FALSE( showsLineNumbers( first.filteredViewInTab( 1 ) ) );
            REQUIRE_FALSE( first.overviewShown() );
            REQUIRE( showsLineNumbers( second.textView() ) );
            REQUIRE_FALSE( showsLineNumbers( second.filteredView() ) );
            REQUIRE_FALSE( second.overviewShown() );
        }

        // As in a window's tabs: the second Log File is not the current one.
        second.crawler->hide();

        WHEN( "the Policies re-derived after line numbers in the main view were switched off "
              "are applied" )
        {
            auto changed = policies;
            changed.presentation.mainLineNumbersVisible = false;
            session.applyPolicies( changed );

            THEN( "the main view of either Log File draws none, the tab not current included" )
            {
                REQUIRE_FALSE( showsLineNumbers( first.textView() ) );
                REQUIRE_FALSE( showsLineNumbers( second.textView() ) );
                REQUIRE( second.crawler->presentationPolicy() == changed.presentation );
            }

            THEN( "the Filtered Views are left as they were" )
            {
                REQUIRE_FALSE( showsLineNumbers( first.filteredViewInTab( 0 ) ) );
                REQUIRE_FALSE( showsLineNumbers( first.filteredViewInTab( 1 ) ) );
                REQUIRE_FALSE( showsLineNumbers( second.filteredView() ) );
            }
        }

        WHEN( "the Policies re-derived after line numbers in the Filtered View were switched on "
              "are applied" )
        {
            auto changed = policies;
            changed.presentation.filteredLineNumbersVisible = true;
            session.applyPolicies( changed );

            THEN( "every Filtered View of either Log File draws them, the kept Search's included" )
            {
                REQUIRE( showsLineNumbers( first.filteredViewInTab( 0 ) ) );
                REQUIRE( showsLineNumbers( first.filteredViewInTab( 1 ) ) );
                REQUIRE( showsLineNumbers( second.filteredView() ) );
            }

            THEN( "a Filtered View built by a Search kept afterwards draws them too" )
            {
                first.keepSearchResults();
                searchForOneLine( first, "line 000005" );
                REQUIRE( first.currentFilteredViewTab() == 2 );
                REQUIRE( showsLineNumbers( first.filteredViewInTab( 2 ) ) );
            }
        }

        WHEN( "the Policies re-derived after the overview was switched on are applied" )
        {
            auto changed = policies;
            changed.presentation.overviewVisible = true;
            session.applyPolicies( changed );

            THEN( "the overview of either Log File is shown, the tab not current included" )
            {
                REQUIRE( first.overviewShown() );
                REQUIRE( second.overviewShown() );
            }
        }

        WHEN( "a Policy that changes some other axis arrives" )
        {
            // Set on the views behind the Policy's back: were the Presentation
            // Policy handed down again, they would be put back as it says.
            first.textView()->setLineNumbersVisible( false );
            second.filteredView()->setLineNumbersVisible( true );

            auto changed = policies;
            changed.watch.pollingEnabled = false;
            session.applyPolicies( changed );

            THEN( "the Presentation Policy is handed to no view of either Log File" )
            {
                REQUIRE_FALSE( showsLineNumbers( first.textView() ) );
                REQUIRE( showsLineNumbers( second.filteredView() ) );
            }
        }
    }
}

// The search defaults ride the QuickFind Policy (#193): the whole search button
// row starts in the state that Policy says, and none of it is read from the
// settings store. They are a starting state, not a live one: a user who then
// sets a button by hand is not editing a setting, and a Policy arriving later
// leaves that button alone.
SCENARIO( "The search button row starts in the state the QuickFind Policy says", "[ui][settings]" )
{
    QTemporaryFile firstFile{ "crawler_search_defaults_first_XXXXXX" };
    QTemporaryFile secondFile{ "crawler_search_defaults_second_XXXXXX" };

    // Every one the other way round from the shipped defaults, so a widget that
    // still read the settings store would start in the shipped state instead.
    auto policies = testSettingsPolicies();
    policies.quickFind.searchIgnoreCaseDefault = true;
    policies.quickFind.searchAutoRefreshDefault = true;
    policies.quickFind.searchLogicalCombiningDefault = true;
    Session session{ policies, std::make_shared<LogFormatCatalog>() };

    GIVEN( "a Log File opened under that Policy" )
    {
        CrawlerWidgetVisitor first;
        openCrawler( session, firstFile, first );

        THEN( "the buttons start as the Policy says" )
        {
            REQUIRE_FALSE( first.matchCaseChecked() );
            REQUIRE( first.autoRefreshChecked() );
            REQUIRE( first.booleanCombiningChecked() );
        }

        WHEN( "the user sets the buttons by hand and a changed QuickFind Policy arrives" )
        {
            first.clickSearchDefaultButtons();
            REQUIRE( first.matchCaseChecked() );
            REQUIRE_FALSE( first.autoRefreshChecked() );
            REQUIRE_FALSE( first.booleanCombiningChecked() );

            // Changed on the same axis, yet still saying the defaults the user
            // clicked away from: were the buttons seeded again from it, they
            // would go back to the state they started in.
            auto changed = policies;
            changed.quickFind.incremental = !policies.quickFind.incremental;
            session.applyPolicies( changed );

            THEN( "the Log File holds the new Policy" )
            {
                REQUIRE( first.crawler->quickFindPolicy() == changed.quickFind );
            }

            THEN( "the buttons keep what the user set" )
            {
                REQUIRE( first.matchCaseChecked() );
                REQUIRE_FALSE( first.autoRefreshChecked() );
                REQUIRE_FALSE( first.booleanCombiningChecked() );
            }
        }

        WHEN( "a Policy with other defaults arrives and another Log File is opened" )
        {
            auto changed = policies;
            changed.quickFind.searchIgnoreCaseDefault = false;
            changed.quickFind.searchAutoRefreshDefault = false;
            changed.quickFind.searchLogicalCombiningDefault = false;
            session.applyPolicies( changed );

            CrawlerWidgetVisitor second;
            openCrawler( session, secondFile, second );

            THEN( "the new Log File starts as the new Policy says" )
            {
                REQUIRE( second.matchCaseChecked() );
                REQUIRE_FALSE( second.autoRefreshChecked() );
                REQUIRE_FALSE( second.booleanCombiningChecked() );
            }

            THEN( "the Log File already open keeps the state it started in" )
            {
                REQUIRE_FALSE( first.matchCaseChecked() );
                REQUIRE( first.autoRefreshChecked() );
                REQUIRE( first.booleanCombiningChecked() );
            }
        }
    }
}

namespace {

// Holds the three settings that make the font Log Lines are drawn in, and puts
// back what they were: they live in the settings singleton the other tests
// share.
class ConfiguredFont {
public:
    ConfiguredFont( const QFont& font, bool bold, bool forceAntialiasing )
        : font_( Configuration::get().mainFont() )
        , bold_( Configuration::get().useBoldFont() )
        , forceAntialiasing_( Configuration::get().forceFontAntialiasing() )
    {
        auto& config = Configuration::get();
        config.setMainFont( font );
        config.setUseBoldFont( bold );
        config.setForceFontAntialiasing( forceAntialiasing );
    }

    ~ConfiguredFont()
    {
        auto& config = Configuration::get();
        config.setMainFont( font_ );
        config.setUseBoldFont( bold_ );
        config.setForceFontAntialiasing( forceAntialiasing_ );
    }

    ConfiguredFont( const ConfiguredFont& ) = delete;
    ConfiguredFont& operator=( const ConfiguredFont& ) = delete;

private:
    QFont font_;
    bool bold_;
    bool forceAntialiasing_;
};

// Whether a view draws in the font assembled from the settings: no kerning,
// fixed pitch, bold and forced antialiasing, at pointSize.
bool drawsInAssembledFont( const QWidget* view, int pointSize )
{
    const auto font = view->font();
    return !font.kerning() && font.fixedPitch() && font.bold()
           && ( font.styleStrategy() & QFont::PreferAntialias ) != 0
           && font.pointSize() == pointSize;
}

} // namespace

// The font Log Lines are drawn in is assembled from the settings in one place,
// by the Crawler Widget, and handed to its views: the Table View reads no
// setting for it (#194). Every view is handed it before it is first painted,
// and again on every zoom.
SCENARIO( "Every view of a Log File draws in the configured font from its first frame",
          "[ui][settings]" )
{
    QTemporaryFile file{ "crawler_font_XXXXXX" };

    // Bold and forced antialiasing are the other way round from the shipped
    // defaults, and the size is not the shipped one either.
    const auto shippedFont = Configuration{}.mainFont();
    const auto configuredSize = shippedFont.pointSize() + 4;
    const ConfiguredFont configured{ QFont{ shippedFont.family(), configuredSize }, true, true };

    auto policies = testSettingsPolicies();
    Session session{ policies, std::make_shared<LogFormatCatalog>() };

    GIVEN( "a Log File just opened, not yet painted" )
    {
        REQUIRE( generateDataFiles( file ) );
        session.savedSearches().clear();

        CrawlerWidgetVisitor crawlerVisitor;
        crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
            session.open( file.fileName(),
                          []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );

        THEN( "every view already holds the assembled font" )
        {
            REQUIRE( drawsInAssembledFont( crawlerVisitor.tableView(), configuredSize ) );
            REQUIRE( drawsInAssembledFont( crawlerVisitor.textView(), configuredSize ) );
            REQUIRE( drawsInAssembledFont( crawlerVisitor.filteredView(), configuredSize ) );
        }

        WHEN( "it is shown and the settings without a Policy are read again afterwards" )
        {
            waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );
            crawlerVisitor.showSized();
            crawlerVisitor.showTableView( true );
            const auto rowHeight
                = crawlerVisitor.tableView()->verticalHeader()->defaultSectionSize();
            const auto tableFont = crawlerVisitor.tableView()->font();

            crawlerVisitor.crawler->applyChange(
                ViewChange{ .rereadSettingsWithoutPolicy = true } );
            QCoreApplication::processEvents();

            THEN( "the Table View neither changes its font nor resizes its rows" )
            {
                REQUIRE( crawlerVisitor.tableView()->font() == tableFont );
                REQUIRE( crawlerVisitor.tableView()->verticalHeader()->defaultSectionSize()
                         == rowHeight );
            }
        }

        WHEN( "the user zooms in with a kept Search in a tab not current" )
        {
            waitUiState( [ & ]() { return crawlerVisitor.getLogNbLines().get() == SL_NB_LINES; } );
            waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );
            crawlerVisitor.showSized();
            searchForOneLine( crawlerVisitor, "line 000003" );
            crawlerVisitor.keepSearchResults();
            searchForOneLine( crawlerVisitor, "line 000007" );
            REQUIRE( crawlerVisitor.currentFilteredViewTab() == 1 );

            crawlerVisitor.zoom( true );
            const auto zoomedSize = Configuration::get().mainFont().pointSize();
            REQUIRE( zoomedSize > configuredSize );

            THEN( "both Presentations and every Filtered View draw in the assembled, larger font" )
            {
                REQUIRE( drawsInAssembledFont( crawlerVisitor.textView(), zoomedSize ) );
                REQUIRE( drawsInAssembledFont( crawlerVisitor.tableView(), zoomedSize ) );
                REQUIRE(
                    drawsInAssembledFont( crawlerVisitor.filteredViewInTab( 0 ), zoomedSize ) );
                REQUIRE(
                    drawsInAssembledFont( crawlerVisitor.filteredViewInTab( 1 ), zoomedSize ) );
            }
        }
    }
}

// A zoom steps from the configured size to the next size offered for the
// family, whether or not the configured size is one of them. It looked the
// size up in the offered ones and stepped past the end of the list when it was
// not there, reading whatever lay behind it: on Windows' offscreen platform,
// which had no fonts to resolve the configured one, a zoom in from 14pt made
// the font 12pt (#220).
SCENARIO( "A zoom steps to the next offered font size from any configured size", "[ui][settings]" )
{
    QTemporaryFile file{ "crawler_zoom_XXXXXX" };

    const auto family = Configuration{}.mainFont().family();
    const auto offeredSizes = FontUtils::availableFontSizes( family );

    // Two neighbouring offered sizes with a size between them that is not
    // offered itself. Where every size up to the largest is offered, the size
    // is one above the largest, and a zoom in keeps it.
    auto below = offeredSizes.last();
    auto above = offeredSizes.last() + 1;
    for ( auto i = 1; i < offeredSizes.size(); ++i ) {
        if ( offeredSizes[ i ] - offeredSizes[ i - 1 ] > 1 ) {
            below = offeredSizes[ i - 1 ];
            above = offeredSizes[ i ];
            break;
        }
    }
    const auto between = below + 1;

    const ConfiguredFont configured{ QFont{ family, between }, true, true };

    auto policies = testSettingsPolicies();
    Session session{ policies, std::make_shared<LogFormatCatalog>() };

    GIVEN( "a Log File opened with a font size that is not offered" )
    {
        REQUIRE( generateDataFiles( file ) );

        CrawlerWidgetVisitor crawlerVisitor;
        crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
            session.open( file.fileName(),
                          []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
        waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } );

        WHEN( "the user zooms in" )
        {
            crawlerVisitor.zoom( true );

            THEN( "the font takes the next offered size above it, if there is one" )
            {
                REQUIRE( Configuration::get().mainFont().pointSize() == above );
            }
        }

        WHEN( "the user zooms out" )
        {
            crawlerVisitor.zoom( false );

            THEN( "the font takes the next offered size below it" )
            {
                REQUIRE( Configuration::get().mainFont().pointSize() == below );
            }
        }
    }
}

namespace {

// How many pixels of the view are painted in the color a Log Line outside the
// Search Limits is subdued in. The separator beside the bullets is drawn in
// it too, so only a change of the count within one view says something.
//
// The color is read off the view rather than set on it (#373). A Line
// Decorator subdues with QPalette::Disabled's text color
// (linedecorator.cpp:46), and a Theme applies a stylesheet to the whole
// application, which decides that color over any palette a test hands the
// widget. This scenario used to set its own palette and look for the color it
// had chosen: that worked only while no Theme had been applied, so it passed
// alone and failed after any scenario that applies one.
int subduedPixels( QWidget* view )
{
    const auto subdued = view->palette().color( QPalette::Disabled, QPalette::Text ).rgb();
    const auto image = view->grab().toImage();
    auto count = 0;
    for ( auto y = 0; y < image.height(); ++y ) {
        for ( auto x = 0; x < image.width(); ++x ) {
            if ( image.pixelColor( x, y ).rgb() == subdued ) {
                ++count;
            }
        }
    }
    return count;
}

// The Color Labels of the Highlighter Set Collection, the first two in colors
// nothing else in the views is painted with.
void setColorLabelColors( const QColor& first, const QColor& second )
{
    auto& collection = HighlighterSetCollection::get();
    auto labels = collection.quickHighlighters();
    while ( labels.size() < 2 ) {
        labels.append( QuickHighlighter{ "label", HighlightColor{ Qt::black, Qt::white }, true } );
    }
    labels[ 0 ].color = HighlightColor{ Qt::white, first };
    labels[ 1 ].color = HighlightColor{ Qt::white, second };
    collection.setQuickHighlighters( labels );
}

} // namespace

// Color Labels and Search Limits belong to the Log File, not to one of its
// Filtered Views (#234): a change is painted in every Filtered View the Log
// File has, the ones of kept Searches in tabs not current included, and a
// Filtered View built afterwards starts with them.
SCENARIO( "Color Labels and Search Limits reach every Filtered View of the Log File",
          "[ui][decoration]" )
{
    QTemporaryFile file{ "crawler_labels_limits_XXXXXX" };

    // Colors nothing else in the views is painted with.
    const QColor firstLabelColor{ 0x13, 0x57, 0x9b };
    const QColor secondLabelColor{ 0x9b, 0x57, 0x13 };

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };

    const PinnedHighlighterSets pinnedSets;
    HighlighterSetCollection::get().deactivateAll();
    setColorLabelColors( firstLabelColor, secondLabelColor );

    // A Theme of its own, so the colors the views paint with are this
    // scenario's and not whichever Theme ran before it (#373).
    Theme::apply( Theme::defaultTheme() );

    CrawlerWidgetVisitor crawlerVisitor;
    openCrawler( session, file, crawlerVisitor );

    GIVEN( "a kept Search in a tab not current, both Searches showing Log Line 3" )
    {
        searchForOneLine( crawlerVisitor, "line 000003" );
        crawlerVisitor.keepSearchResults();
        searchForOneLine( crawlerVisitor, "line 000003" );
        REQUIRE( crawlerVisitor.currentFilteredViewTab() == 1 );
        REQUIRE( crawlerVisitor.filteredViewInTab( 0 ) != nullptr );
        REQUIRE( crawlerVisitor.filteredViewInTab( 1 ) != nullptr );

        REQUIRE_FALSE( showsColor( crawlerVisitor.filteredViewInTab( 0 ), firstLabelColor ) );
        REQUIRE_FALSE( showsColor( crawlerVisitor.filteredViewInTab( 1 ), firstLabelColor ) );

        WHEN( "Log Line 3 gets a Color Label" )
        {
            crawlerVisitor.addColorLabelToLogLine( 3_lnum, 0 );

            THEN( "every Filtered View colors it, the kept Search's included" )
            {
                REQUIRE( showsColor( crawlerVisitor.textView(), firstLabelColor ) );
                REQUIRE( showsColor( crawlerVisitor.filteredViewInTab( 1 ), firstLabelColor ) );
                REQUIRE( showsColor( crawlerVisitor.filteredViewInTab( 0 ), firstLabelColor ) );
            }

            AND_WHEN( "the text is given another Color Label" )
            {
                crawlerVisitor.addColorLabelToLogLine( 3_lnum, 1 );

                THEN( "every Filtered View colors it in the other label's color" )
                {
                    for ( const auto tab : { 0, 1 } ) {
                        CAPTURE( tab );
                        auto* view = crawlerVisitor.filteredViewInTab( tab );
                        REQUIRE( showsColor( view, secondLabelColor ) );
                        REQUIRE_FALSE( showsColor( view, firstLabelColor ) );
                    }
                }
            }

            AND_WHEN( "the Color Labels are cleared" )
            {
                crawlerVisitor.clearColorLabels();

                THEN( "no Filtered View colors it any more, the kept Search's included" )
                {
                    REQUIRE_FALSE(
                        showsColor( crawlerVisitor.filteredViewInTab( 0 ), firstLabelColor ) );
                    REQUIRE_FALSE(
                        showsColor( crawlerVisitor.filteredViewInTab( 1 ), firstLabelColor ) );
                }
            }
        }

        WHEN( "Search Limits are set that end before Log Line 3" )
        {
            const auto keptBefore = subduedPixels( crawlerVisitor.filteredViewInTab( 0 ) );
            const auto currentBefore = subduedPixels( crawlerVisitor.filteredViewInTab( 1 ) );

            crawlerVisitor.setSearchLimits( 0_lnum, 3_lnum );

            THEN( "every Filtered View subdues it, the kept Search's included" )
            {
                REQUIRE( subduedPixels( crawlerVisitor.filteredViewInTab( 1 ) ) > currentBefore );
                REQUIRE( subduedPixels( crawlerVisitor.filteredViewInTab( 0 ) ) > keptBefore );
            }

            AND_WHEN( "the Search Limits are cleared" )
            {
                crawlerVisitor.clearSearchLimits();

                THEN( "no Filtered View subdues it any more, the kept Search's included" )
                {
                    REQUIRE( subduedPixels( crawlerVisitor.filteredViewInTab( 1 ) )
                             == currentBefore );
                    REQUIRE( subduedPixels( crawlerVisitor.filteredViewInTab( 0 ) ) == keptBefore );
                }
            }
        }

        // The Search Limits reach the Line Decorator as Log Lines: a Log Line
        // shown before a start the Filtered View doesn't show is outside them
        // (#243).
        WHEN( "a Search shows Log Lines 3 and 7, and Search Limits start between them" )
        {
            crawlerVisitor.clearSearchPattern();
            crawlerVisitor.setSearchPattern( "LOGDATA.*line 00000[37]" );
            crawlerVisitor.runSearch();
            REQUIRE( waitUiState( [ &crawlerVisitor ]() {
                return crawlerVisitor.getLogFilteredNbLines().get() == 2;
            } ) );
            QTest::qWait( 50 );

            auto* view = crawlerVisitor.filteredView();
            const auto inside = subduedPixels( view );

            crawlerVisitor.setSearchLimits( 5_lnum, 10_lnum );
            const auto firstOutside = subduedPixels( view );

            crawlerVisitor.setSearchLimits( 8_lnum, 10_lnum );
            const auto bothOutside = subduedPixels( view );

            THEN( "Log Line 3 is subdued, and Log Line 7 is not" )
            {
                REQUIRE( firstOutside > inside );
                REQUIRE( bothOutside > firstOutside );
            }
        }

        WHEN( "with a Color Label on Log Line 7 and Search Limits that end after it, "
              "a Search is kept and a new one shows Log Line 7 and the marked 9" )
        {
            crawlerVisitor.addColorLabelToLogLine( 7_lnum, 0 );
            crawlerVisitor.setSearchLimits( 0_lnum, 8_lnum );

            crawlerVisitor.keepSearchResults();
            searchForOneLine( crawlerVisitor, "line 000007" );
            REQUIRE( crawlerVisitor.currentFilteredViewTab() == 2 );
            crawlerVisitor.markLogLine( 9_lnum );
            REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 2 );

            THEN( "the new Filtered View starts with the Color Label" )
            {
                REQUIRE( showsColor( crawlerVisitor.filteredViewInTab( 2 ), firstLabelColor ) );
            }

            THEN( "the new Filtered View starts with the Search Limits: Log Line 9 is subdued" )
            {
                const auto limited = subduedPixels( crawlerVisitor.filteredViewInTab( 2 ) );
                crawlerVisitor.clearSearchLimits();
                REQUIRE( limited > subduedPixels( crawlerVisitor.filteredViewInTab( 2 ) ) );
            }
        }
    }
}

namespace {

// What the application does once a Highlighter Set, or the color of a Color
// Label, has been changed -- in the Highlighters dialog, the Highlighters
// menu or by an import: it tells the Session, whatever window it happened in.
void changeHighlighterSets( Session& session )
{
    session.applyChange( Changed::HighlighterSets );
    QCoreApplication::processEvents();
}

} // namespace

// A Color Label caches its color alongside its words in each Log File, so a
// change of the Highlighter Set Collection has to reach every open Log File,
// not only the one the current tab shows (#237).
SCENARIO( "A Highlighter Set change re-colors Color Labels in every open Log File",
          "[ui][highlighters]" )
{
    QTemporaryFile currentFile{ "crawler_labels_current_XXXXXX" };
    QTemporaryFile backgroundFile{ "crawler_labels_background_XXXXXX" };

    // Colors nothing else in the views is painted with.
    const QColor oldLabelColor{ 0x13, 0x57, 0x9b };
    const QColor newLabelColor{ 0x57, 0x9b, 0x13 };
    const QColor otherLabelColor{ 0x9b, 0x57, 0x13 };

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };

    const PinnedHighlighterSets pinnedSets;
    HighlighterSetCollection::get().deactivateAll();
    setColorLabelColors( oldLabelColor, otherLabelColor );

    // Destroyed before the Session they were opened from.
    CrawlerWidgetVisitor current;
    openCrawler( session, currentFile, current );
    CrawlerWidgetVisitor background;
    openCrawler( session, backgroundFile, background );

    GIVEN( "two open Log Files, both with a Color Label on Log Line 3, one in the background" )
    {
        current.addColorLabelToLogLine( 3_lnum, 0 );
        background.addColorLabelToLogLine( 3_lnum, 0 );

        REQUIRE( showsColor( current.textView(), oldLabelColor ) );
        REQUIRE( showsColor( background.textView(), oldLabelColor ) );

        // As a tab not current is.
        background.crawler->hide();
        QCoreApplication::processEvents();

        WHEN( "the Color Label is given another color in the Highlighter Set Collection" )
        {
            setColorLabelColors( newLabelColor, otherLabelColor );
            changeHighlighterSets( session );

            THEN( "the current Log File shows the new color" )
            {
                REQUIRE( showsColor( current.textView(), newLabelColor ) );
                REQUIRE_FALSE( showsColor( current.textView(), oldLabelColor ) );
            }

            THEN( "the Log File in the background shows it too" )
            {
                REQUIRE( showsColor( background.textView(), newLabelColor ) );
                REQUIRE_FALSE( showsColor( background.textView(), oldLabelColor ) );
            }

            AND_WHEN( "the Log File in the background is brought to the front" )
            {
                background.showSized();

                THEN( "it shows the new color without any further action" )
                {
                    REQUIRE( showsColor( background.textView(), newLabelColor ) );
                    REQUIRE_FALSE( showsColor( background.textView(), oldLabelColor ) );
                }
            }
        }
    }
}

namespace {

// The keys of the shortcuts a Crawler Widget has registered and not yet let go.
QStringList shortcutKeysOf( const QObject& crawler )
{
    QStringList keys;
    for ( const auto& shortcut : shortcutsOf( crawler ) ) {
        if ( !shortcut.isNull() ) {
            keys.append( shortcut->key().toString() );
        }
    }
    return keys;
}

// Holds the configured shortcuts, and puts back what they were.
class ConfiguredShortcuts {
public:
    ConfiguredShortcuts()
        : shortcuts_( Configuration::get().shortcuts() )
    {
    }

    ~ConfiguredShortcuts()
    {
        Configuration::get().setShortcuts( shortcuts_ );
    }

    ConfiguredShortcuts( const ConfiguredShortcuts& ) = delete;
    ConfiguredShortcuts& operator=( const ConfiguredShortcuts& ) = delete;

private:
    std::map<std::string, QStringList> shortcuts_;
};

} // namespace

// The font and the shortcuts have no Policy, but a change to them travels the
// way a Policy does: the Session tells every open Log File to read them again,
// not only the one the current tab shows, and a tab brought to the front
// applies nothing (#245).
SCENARIO( "A changed font or shortcut reaches every open Log File", "[ui][settings]" )
{
    QTemporaryFile currentFile{ "crawler_settings_current_XXXXXX" };
    QTemporaryFile backgroundFile{ "crawler_settings_background_XXXXXX" };

    const auto shippedFont = Configuration{}.mainFont();
    const auto openedSize = shippedFont.pointSize() + 2;
    const ConfiguredFont configured{ QFont{ shippedFont.family(), openedSize }, true, true };
    const ConfiguredShortcuts configuredShortcuts;

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };

    // Destroyed before the Session they were opened from.
    CrawlerWidgetVisitor current;
    openCrawler( session, currentFile, current );
    CrawlerWidgetVisitor background;
    openCrawler( session, backgroundFile, background );

    const QString changedKey = QStringLiteral( "Ctrl+Alt+Shift+F11" );

    GIVEN( "two open Log Files, one in the background" )
    {
        // As a tab not current is.
        background.crawler->hide();
        QCoreApplication::processEvents();

        REQUIRE( drawsInAssembledFont( background.textView(), openedSize ) );
        REQUIRE_FALSE( shortcutKeysOf( *background.crawler ).contains( changedKey ) );

        const auto changedSize = openedSize + 3;
        auto& config = Configuration::get();

        WHEN( "the font and a shortcut are changed in the settings and the Session is told" )
        {
            config.setMainFont( QFont{ shippedFont.family(), changedSize } );
            auto shortcuts = config.shortcuts();
            shortcuts[ ShortcutAction::LogViewClearColorLabels ] = QStringList{ changedKey };
            config.setShortcuts( shortcuts );

            session.applyChange( Changed::Settings );
            QCoreApplication::processEvents();

            THEN( "the Log File in the background draws in the new font, without being brought "
                  "to the front" )
            {
                REQUIRE( drawsInAssembledFont( background.textView(), changedSize ) );
                REQUIRE( drawsInAssembledFont( background.filteredView(), changedSize ) );
            }

            THEN( "the Log File in the background answers to the new shortcut" )
            {
                REQUIRE( shortcutKeysOf( *background.crawler ).contains( changedKey ) );
            }

            THEN( "the current Log File takes both too" )
            {
                REQUIRE( drawsInAssembledFont( current.textView(), changedSize ) );
                REQUIRE( shortcutKeysOf( *current.crawler ).contains( changedKey ) );
            }
        }

        WHEN( "a Search is kept, a view's shortcut is changed in the settings and the Session is "
              "told" )
        {
            searchForOneLine( current, "line 000003" );
            current.keepSearchResults();
            searchForOneLine( current, "line 000007" );
            REQUIRE( current.currentFilteredViewTab() == 1 );

            auto shortcuts = config.shortcuts();
            shortcuts[ ShortcutAction::LogViewJumpToTop ] = QStringList{ changedKey };
            config.setShortcuts( shortcuts );

            session.applyChange( Changed::Settings );
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );

            THEN( "the kept Search's Filtered View answers to it, as the current one does" )
            {
                REQUIRE( shortcutKeysOf( *current.filteredViewInTab( 0 ) ).contains( changedKey ) );
                REQUIRE( shortcutKeysOf( *current.filteredViewInTab( 1 ) ).contains( changedKey ) );
                REQUIRE( shortcutKeysOf( *current.textView() ).contains( changedKey ) );
            }
        }

        WHEN( "the user zooms in the current Log File" )
        {
            // A key listed twice for a view -- the platform's standard
            // bindings can repeat one the defaults list, as on Linux --
            // released a shortcut, deleted only later: it is gone before the
            // shortcuts are counted.
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
            const auto shortcutsBefore = shortcutsOf( *background.crawler );
            current.zoom( true );
            // Shortcuts registered anew delete the old ones later.
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
            const auto zoomedSize = Configuration::get().mainFont().pointSize();
            REQUIRE( zoomedSize > openedSize );

            THEN( "the Log File in the background draws in the zoomed font too" )
            {
                REQUIRE( drawsInAssembledFont( background.textView(), zoomedSize ) );
                REQUIRE( drawsInAssembledFont( background.filteredView(), zoomedSize ) );
            }

            THEN( "only the font is taken again: the shortcuts are not rebuilt" )
            {
                REQUIRE( allAlive( shortcutsBefore ) );
            }
        }

        WHEN( "the font is changed in the settings, nobody is told, and the Log File in the "
              "background is brought to the front" )
        {
            const auto shortcutsBefore = shortcutsOf( *background.crawler );
            config.setMainFont( QFont{ shippedFont.family(), changedSize } );

            background.crawler->broughtToFront();
            background.showSized();

            THEN( "it applies no configuration: neither the font nor its shortcuts are taken "
                  "again" )
            {
                REQUIRE( drawsInAssembledFont( background.textView(), openedSize ) );
                REQUIRE( allAlive( shortcutsBefore ) );
            }
        }
    }
}

namespace {

// The tabs of one window restored from the Session info, as the main window
// restores them at startup: a Crawler Widget per Log File, in tab order.
struct RestoredWindow {
    // Before the Crawler Widgets, which it outlives.
    std::shared_ptr<Session> appSession;
    std::unique_ptr<WindowSession> window;
    std::vector<std::unique_ptr<CrawlerWidget>> tabs;

    // Restores the window with these Log Files and view contexts, the last one
    // its current tab. Building a Session reads the settings store again, so
    // the Session info is written after it.
    RestoredWindow( const QString& windowId,
                    const std::vector<std::pair<QString, QString>>& openFiles )
        : appSession( std::make_shared<Session>( testSettingsPolicies(),
                                                 std::make_shared<LogFormatCatalog>() ) )
    {
        std::vector<SessionInfo::OpenFile> saved;
        for ( const auto& [ fileName, viewContext ] : openFiles ) {
            saved.emplace_back( fileName, 0, viewContext );
        }
        auto& readAtStartup = SessionInfo::get();
        readAtStartup.add( windowId );
        readAtStartup.setOpenFiles( windowId, saved );

        window = std::make_unique<WindowSession>( appSession, windowId, 0 );
        int currentFileIndex = -1;
        window->restore(
            [ this ]( const ViewBuild& build ) {
                tabs.emplace_back( new CrawlerWidget( build ) );
                return tabs.back().get();
            },
            &currentFileIndex );
        REQUIRE( tabs.size() == openFiles.size() );
    }

    ~RestoredWindow()
    {
        tabs.clear();
        // Leave the in-memory Session info as the settings store has it.
        SessionInfo::getSynced();
    }

    RestoredWindow( const RestoredWindow& ) = delete;
    RestoredWindow& operator=( const RestoredWindow& ) = delete;
};

} // namespace

SCENARIO( "A restored tab whose Log File loads after the current one shows what was saved for it",
          "[ui][session]" )
{
    const auto windowId = QStringLiteral( "crawlerwidget_test_window_300" );
    QTemporaryFile marked{ "crawler_test_marked_XXXXXX" };
    QTemporaryFile current{ "crawler_test_current_XXXXXX" };
    REQUIRE( generateDataFiles( marked ) );
    REQUIRE( generateDataFiles( current ) );

    // What was saved for the Log File in an earlier run: two Marks and a Search
    // that matches case.
    const auto savedContext = [ & ] {
        Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
        CrawlerWidgetVisitor earlier;
        earlier.crawler.reset( static_cast<CrawlerWidget*>(
            session.open( marked.fileName(),
                          []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
        REQUIRE( waitUiState( [ & ] { return earlier.isLoadingFinished(); } ) );
        earlier.markLogLine( 3_lnum );
        earlier.markLogLine( 7_lnum );
        earlier.enableCaseSensitiveSearch();
        REQUIRE( earlier.isMarked( 3_lnum ) );
        return earlier.crawler->context()->toString();
    }();

    // The tab of the marked Log File, not the current one.
    const auto activate = []( RestoredWindow& restored ) {
        CrawlerWidgetVisitor tab;
        tab.crawler = std::move( restored.tabs.front() );
        restored.window->startLoading( tab.crawler.get() );
        return tab;
    };

    GIVEN( "a Session restored with that Log File in a tab that is not the current one" )
    {
        RestoredWindow restored{
            windowId, { { marked.fileName(), savedContext }, { current.fileName(), {} } }
        };

        WHEN( "its tab is activated" )
        {
            auto tab = activate( restored );
            REQUIRE( waitUiState( [ & ] {
                return tab.isLoadingFinished() && tab.getLogNbLines().get() == SL_NB_LINES;
            } ) );

            THEN( "it shows the Log File with its Marks and its Search settings" )
            {
                REQUIRE( tab.isMarked( 3_lnum ) );
                REQUIRE( tab.isMarked( 7_lnum ) );
                REQUIRE_FALSE( tab.isMarked( 5_lnum ) );
                REQUIRE( tab.matchCaseChecked() );
            }

            THEN( "a Search runs over the whole Log File" )
            {
                tab.render();
                tab.clearSearchPattern();
                tab.setSearchPattern( "this is line" );
                tab.runSearch();
                REQUIRE( waitUiState(
                    [ & ] { return tab.getLogFilteredNbLines().get() == SL_NB_LINES; } ) );
            }
        }

        WHEN( "the Session is saved and restored again before that Log File has loaded" )
        {
            const auto savedAgain = restored.tabs.front()->context()->toString();
            restored.tabs.clear();
            RestoredWindow restoredAgain{
                windowId, { { marked.fileName(), savedAgain }, { current.fileName(), {} } }
            };
            auto tab = activate( restoredAgain );
            REQUIRE( waitUiState( [ & ] {
                return tab.isLoadingFinished() && tab.getLogNbLines().get() == SL_NB_LINES;
            } ) );

            THEN( "its Marks were kept" )
            {
                REQUIRE( tab.isMarked( 3_lnum ) );
                REQUIRE( tab.isMarked( 7_lnum ) );
                REQUIRE( tab.matchCaseChecked() );
            }
        }

        WHEN( "it is reloaded before its Log File has been attached" )
        {
            // The user interface does not reach this today -- activating a
            // tab starts its load first -- but reloading such a tab loads it
            // instead of reaching for log data that is not there (#332).
            CrawlerWidgetVisitor tab;
            tab.crawler = std::move( restored.tabs.front() );
            tab.crawler->reload();
            REQUIRE( waitUiState( [ & ] {
                return tab.isLoadingFinished() && tab.getLogNbLines().get() == SL_NB_LINES;
            } ) );

            THEN( "its Log File loads with what was saved for it, dropping nothing" )
            {
                REQUIRE( tab.isMarked( 3_lnum ) );
                REQUIRE( tab.isMarked( 7_lnum ) );
                REQUIRE( tab.matchCaseChecked() );
            }
        }
    }
}

SCENARIO( "An invalid Search pattern is shown in the Theme's error colors", "[ui][theme]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    CrawlerWidgetVisitor crawlerVisitor;
    Theme::apply( Theme::LightKey );
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ & ]() { return crawlerVisitor.isLoadingFinished(); } ) );
    crawlerVisitor.showSized();

    GIVEN( "a Search for a pattern that is no regexp" )
    {
        crawlerVisitor.enableRegexpSearch();
        crawlerVisitor.setSearchPattern( "(unclosed" );
        crawlerVisitor.runSearch();
        QCoreApplication::processEvents();
        REQUIRE( crawlerVisitor.searchInfoText().startsWith( "Error in expression" ) );

        // The error background fills the line behind its text.
        const auto background = []( const QImage& image ) {
            return image.pixelColor( image.width() - 4, image.height() / 2 ).rgb();
        };

        THEN( "the Search info line shows the error in the Light Theme's error background" )
        {
            REQUIRE( background( crawlerVisitor.searchInfoImage() )
                     == Theme::active().color( ColorToken::ErrorBackground ).rgb() );
        }

        for ( const auto& name : themeSwitchesFrom( Theme::LightKey ) ) {
            WHEN( "the " << name.toStdString() << " Theme is applied" )
            {
                Theme::apply( name );
                QCoreApplication::processEvents();

                THEN( "the error is shown in that Theme's error background" )
                {
                    REQUIRE( background( crawlerVisitor.searchInfoImage() )
                             == Theme::active().color( ColorToken::ErrorBackground ).rgb() );
                }
            }
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

namespace {

// Log Lines holding a word with quotes in it: 5 read alpha say "hi", 5 read
// beta say "hi", and 10 read alpha say hi, without the quotes.
bool generateQuotedWordFile( QTemporaryFile& file )
{
    if ( !file.open() ) {
        return false;
    }
    const auto writeLines = [ & ]( const char* line, int count ) {
        for ( int i = 0; i < count; ++i ) {
            file.write( line );
            file.write( "\n" );
        }
    };
    writeLines( R"(alpha say "hi")", 5 );
    writeLines( R"(beta say "hi")", 5 );
    writeLines( "alpha say hi", 10 );
    file.flush();
    return true;
}

} // namespace

// In the boolean combination mode every sub-pattern is enclosed in quotes and
// a quote inside it is written \" (#398).
SCENARIO( "A word with quotes added to a Search keeps its pattern valid", "[ui][search]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateQuotedWordFile( file ) );

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    session.savedSearches().clear();
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ & ]() {
        return crawlerVisitor.isLoadingFinished() && crawlerVisitor.getLogNbLines().get() == 20;
    } ) );
    crawlerVisitor.showSized();

    const QString quotedWord = R"(say "hi")";
    auto* view = crawlerVisitor.textView();

    // Runs the Search on the Search line and tells whether it ran without an
    // error in its pattern.
    const auto searchRuns = [ & ] {
        crawlerVisitor.runSearch();
        QCoreApplication::processEvents();
        UNSCOPED_INFO( "pattern: " << crawlerVisitor.searchText().toStdString() << ", search info: "
                                   << crawlerVisitor.searchInfoText().toStdString() );
        return !crawlerVisitor.searchInfoText().startsWith( "Error" );
    };

    // Read as a regexp, the word is escaped and its quotes with it; read as a
    // fixed string, it is not.
    for ( const auto useRegexp : { false, true } ) {
        if ( useRegexp ) {
            crawlerVisitor.enableRegexpSearch();
        }
        else {
            crawlerVisitor.disableRegexpSearch();
        }
        const std::string reading = useRegexp ? " read as a regexp" : " read as a fixed string";

        GIVEN( "a Search in the boolean combination mode," << reading )
        {
            crawlerVisitor.enableBooleanCombinationMode();

            WHEN( "the word is added to a Search for nothing" )
            {
                crawlerVisitor.setSearchPattern( R"("nothing")" );
                Q_EMIT view->addToSearch( quotedWord );

                THEN( "the Search runs and matches the Log Lines with the word and its quotes" )
                {
                    REQUIRE( searchRuns() );
                    REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 10 );
                }
            }

            WHEN( "the word is excluded from a Search for alpha" )
            {
                crawlerVisitor.setSearchPattern( R"("alpha")" );
                Q_EMIT view->excludeFromSearch( quotedWord );

                THEN( "the Search runs and matches the alpha Log Lines without the quotes" )
                {
                    REQUIRE( searchRuns() );
                    REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 10 );
                }
            }

            WHEN( "the Search is replaced with the word" )
            {
                crawlerVisitor.setSearchPattern( R"("alpha")" );
                Q_EMIT view->replaceSearch( quotedWord );

                THEN( "the Search runs and matches the Log Lines with the word and its quotes" )
                {
                    REQUIRE( searchRuns() );
                    REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 10 );
                }
            }

            WHEN( "Predefined Filters for the word and for beta are combined" )
            {
                crawlerVisitor.crawler->setSearchPatternFromPredefinedFilters(
                    { { "word", quotedWord, false }, { "beta", "beta", false } } );

                THEN( "the Search runs and matches the Log Lines with either" )
                {
                    REQUIRE( searchRuns() );
                    REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 10 );
                }
            }
        }

        GIVEN( "a Search for the word, not in the boolean combination mode," << reading )
        {
            crawlerVisitor.setSearchPattern( quotedWord );

            WHEN( "beta is excluded from it" )
            {
                Q_EMIT view->excludeFromSearch( "beta" );

                THEN( "the Search runs in the boolean combination mode and matches the alpha Log "
                      "Lines with the word and its quotes" )
                {
                    REQUIRE( crawlerVisitor.booleanCombiningChecked() );
                    REQUIRE( searchRuns() );
                    REQUIRE( crawlerVisitor.getLogFilteredNbLines().get() == 5 );
                }
            }
        }
    }
}

// A fixed string has no alternatives: adding a word to a plain Search or
// combining Predefined Filters switches the logical combination on (#408).
SCENARIO( "Adding a word to a plain Search keeps both words searchable", "[ui][search]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateQuotedWordFile( file ) );

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    session.savedSearches().clear();
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ & ]() {
        return crawlerVisitor.isLoadingFinished() && crawlerVisitor.getLogNbLines().get() == 20;
    } ) );
    crawlerVisitor.showSized();
    crawlerVisitor.disableRegexpSearch();
    REQUIRE_FALSE( crawlerVisitor.booleanCombiningChecked() );

    const auto searchMatches = [ & ] {
        crawlerVisitor.runSearch();
        QCoreApplication::processEvents();
        UNSCOPED_INFO( "pattern: " << crawlerVisitor.searchText().toStdString() << ", search info: "
                                   << crawlerVisitor.searchInfoText().toStdString() );
        return crawlerVisitor.getLogFilteredNbLines().get();
    };

    WHEN( "a word is added to a plain Search for beta" )
    {
        crawlerVisitor.setSearchPattern( "beta" );
        Q_EMIT crawlerVisitor.textView()->addToSearch( "alpha say hi" );

        THEN( "the logical combination is on and the Search matches the Log Lines of either" )
        {
            REQUIRE( crawlerVisitor.booleanCombiningChecked() );
            REQUIRE( searchMatches() == 15 );
        }
    }

    WHEN( "Predefined Filters for both are combined" )
    {
        crawlerVisitor.crawler->setSearchPatternFromPredefinedFilters(
            { { "beta", "beta", false }, { "alpha", "alpha say hi", false } } );

        THEN( "the logical combination is on and the Search matches the Log Lines of either" )
        {
            REQUIRE( crawlerVisitor.booleanCombiningChecked() );
            REQUIRE( searchMatches() == 15 );
        }
    }
}

// The Search Line holds the pattern the Search runs with; the line starts
// with the latest Search of the history in it (#399).
SCENARIO( "A Search started without typing runs the pattern the Search line shows", "[ui][search]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    session.savedSearches().clear();
    session.savedSearches().addRecent( "10" );

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ & ]() {
        return crawlerVisitor.isLoadingFinished()
               && crawlerVisitor.getLogNbLines().get() == SL_NB_LINES;
    } ) );

    GIVEN( "a Search history whose latest Search is 10" )
    {
        REQUIRE( crawlerVisitor.searchText() == "10" );

        WHEN( "the Search is started" )
        {
            crawlerVisitor.runSearch();

            THEN( "it runs for 10" )
            {
                REQUIRE( waitUiState(
                    [ & ]() { return crawlerVisitor.getLogFilteredNbLines().get() == 1; } ) );
            }
        }
    }

    session.savedSearches().clear();
}

namespace {
bool generateFilterFrequencyFile( QTemporaryFile& file )
{
    if ( !file.open() ) {
        return false;
    }
    const auto writeLines = [ & ]( const char* line, int count ) {
        for ( int i = 0; i < count; ++i ) {
            file.write( line );
            file.write( "\n" );
        }
    };
    writeLines( R"(alpha say "hi")", 3 );
    writeLines( "beta a or b", 4 );
    writeLines( "gamma x.y", 2 );
    writeLines( "delta xzy", 1 );
    writeLines( "epsilon say hi", 2 );
    file.flush();
    return true;
}
} // namespace

// Filter frequency charts each sub-pattern of a logical Search on its own,
// read as the Search reads it (#410).
SCENARIO( "Filter frequency counts the patterns the Search matches", "[ui][search][chart]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateFilterFrequencyFile( file ) );

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    session.savedSearches().clear();
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ & ]() {
        return crawlerVisitor.isLoadingFinished() && crawlerVisitor.getLogNbLines().get() == 12;
    } ) );
    crawlerVisitor.showSized();

    // Shows the filter frequency and waits for the chart to count as expected.
    const auto chartCounts = [ & ]( const QList<qsizetype>& expected ) {
        crawlerVisitor.showFilterFrequency();
        waitUiState( [ & ]() { return crawlerVisitor.chartPointsPerSeries() == expected; }, 10000 );
        UNSCOPED_INFO( "pattern: " << crawlerVisitor.searchText().toStdString() );
        return crawlerVisitor.chartPointsPerSeries();
    };

    GIVEN( "a logical Search for a quoted word, a phrase with or and a dotted word" )
    {
        crawlerVisitor.enableBooleanCombinationMode();
        crawlerVisitor.setSearchPattern( R"("say \"hi\"" or "a or b" or "x.y")" );

        WHEN( "its patterns are fixed strings" )
        {
            crawlerVisitor.disableRegexpSearch();

            THEN( "the chart counts the Log Lines with each, as written" )
            {
                REQUIRE( chartCounts( { 3, 4, 2 } ) == QList<qsizetype>{ 3, 4, 2 } );
            }
        }

        WHEN( "its patterns are regexps" )
        {
            crawlerVisitor.enableRegexpSearch();

            THEN( "the chart counts the Log Lines each matches" )
            {
                REQUIRE( chartCounts( { 3, 4, 3 } ) == QList<qsizetype>{ 3, 4, 3 } );
            }
        }
    }

    GIVEN( "a logical Search excluding a word" )
    {
        crawlerVisitor.enableBooleanCombinationMode();
        crawlerVisitor.disableRegexpSearch();
        crawlerVisitor.setSearchPattern( R"("say" and not("hi"))" );

        WHEN( "its filter frequency is shown" )
        {
            THEN( "the chart counts the Log Lines with each word, the excluded one too" )
            {
                REQUIRE( chartCounts( { 5, 5 } ) == QList<qsizetype>{ 5, 5 } );
            }
        }
    }

    GIVEN( "a Search for a dotted word, not logical" )
    {
        crawlerVisitor.setSearchPattern( "x.y" );

        WHEN( "it is a fixed string" )
        {
            crawlerVisitor.disableRegexpSearch();

            THEN( "the chart counts the Log Lines with it, as written" )
            {
                REQUIRE( chartCounts( { 2 } ) == QList<qsizetype>{ 2 } );
            }
        }

        WHEN( "it is a regexp" )
        {
            crawlerVisitor.enableRegexpSearch();

            THEN( "the chart counts the Log Lines it matches" )
            {
                REQUIRE( chartCounts( { 3 } ) == QList<qsizetype>{ 3 } );
            }
        }
    }

    GIVEN( "a regexp Search with alternatives, not logical" )
    {
        crawlerVisitor.enableRegexpSearch();
        crawlerVisitor.setSearchPattern( "x.y|beta" );

        WHEN( "its filter frequency is shown" )
        {
            THEN( "the chart counts the Log Lines each alternative matches" )
            {
                REQUIRE( chartCounts( { 3, 4 } ) == QList<qsizetype>{ 3, 4 } );
            }
        }
    }
}

namespace {
bool generateCaseAndGroupsFile( QTemporaryFile& file )
{
    if ( !file.open() ) {
        return false;
    }
    const auto writeLines = [ & ]( const char* line, int count ) {
        for ( int i = 0; i < count; ++i ) {
            file.write( line );
            file.write( "\n" );
        }
    };
    writeLines( "alpha error", 2 );
    writeLines( "beta Error", 1 );
    writeLines( "gamma ERROR", 1 );
    writeLines( "ac", 2 );
    writeLines( "bc", 3 );
    writeLines( "xoo", 1 );
    writeLines( "yoo", 2 );
    file.flush();
    return true;
}
} // namespace

// Filter frequency charts with the Search's Match case, and splits a regexp
// Search only into its top-level alternatives (#411).
SCENARIO( "Filter frequency follows the Search's Match case and regexp groups",
          "[ui][search][chart]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateCaseAndGroupsFile( file ) );

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    session.savedSearches().clear();
    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>( session.open(
        file.fileName(), []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
    REQUIRE( waitUiState( [ & ]() {
        return crawlerVisitor.isLoadingFinished() && crawlerVisitor.getLogNbLines().get() == 12;
    } ) );
    crawlerVisitor.showSized();

    // Shows the filter frequency and waits for the chart to count as expected.
    const auto chartCounts = [ & ]( const QList<qsizetype>& expected ) {
        crawlerVisitor.showFilterFrequency();
        waitUiState( [ & ]() { return crawlerVisitor.chartPointsPerSeries() == expected; }, 10000 );
        UNSCOPED_INFO( "pattern: " << crawlerVisitor.searchText().toStdString() );
        return crawlerVisitor.chartPointsPerSeries();
    };

    GIVEN( "a Search for a word in lower case" )
    {
        crawlerVisitor.disableRegexpSearch();
        crawlerVisitor.setSearchPattern( "error" );

        WHEN( "Match case is off" )
        {
            crawlerVisitor.disableCaseSensitiveSearch();

            THEN( "the chart counts the Log Lines with the word in any case" )
            {
                REQUIRE( chartCounts( { 4 } ) == QList<qsizetype>{ 4 } );
            }
        }

        WHEN( "Match case is on" )
        {
            crawlerVisitor.enableCaseSensitiveSearch();

            THEN( "the chart counts the Log Lines with the word as written" )
            {
                REQUIRE( chartCounts( { 2 } ) == QList<qsizetype>{ 2 } );
            }
        }
    }

    GIVEN( "a logical Search for a word in lower case, Match case off" )
    {
        crawlerVisitor.enableBooleanCombinationMode();
        crawlerVisitor.disableRegexpSearch();
        crawlerVisitor.disableCaseSensitiveSearch();
        crawlerVisitor.setSearchPattern( R"("error" or "xoo")" );

        WHEN( "its filter frequency is shown" )
        {
            THEN( "the chart counts the Log Lines with each word in any case" )
            {
                REQUIRE( chartCounts( { 4, 1 } ) == QList<qsizetype>{ 4, 1 } );
            }
        }
    }

    GIVEN( "a regexp Search with alternatives inside a group" )
    {
        crawlerVisitor.enableRegexpSearch();
        crawlerVisitor.setSearchPattern( "(a|b)c" );

        WHEN( "its filter frequency is shown" )
        {
            THEN( "the chart counts the Log Lines the whole regexp matches, as one series" )
            {
                REQUIRE( chartCounts( { 5 } ) == QList<qsizetype>{ 5 } );
            }
        }
    }

    GIVEN( "a regexp Search with top-level alternatives" )
    {
        crawlerVisitor.enableRegexpSearch();
        crawlerVisitor.setSearchPattern( "x|y" );

        WHEN( "its filter frequency is shown" )
        {
            THEN( "the chart counts the Log Lines each alternative matches" )
            {
                REQUIRE( chartCounts( { 1, 2 } ) == QList<qsizetype>{ 1, 2 } );
            }
        }
    }
}
