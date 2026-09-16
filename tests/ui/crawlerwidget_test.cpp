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

#include <catch2/catch.hpp>

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
#include "test_policies.h"
#include "test_utils.h"

#include "logdata.h"
#include "logfiltereddata.h"

#include "configuration.h"
#include "crawlerwidget.h"
#include "filteredview.h"
#include "highlighterset.h"
#include "logformatdefinition.h"
#include "logtableview.h"

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
struct CrawlerWidget::access_by<CrawlerWidgetPrivate> {
    std::unique_ptr<CrawlerWidget> crawler;

    bool isLoadingFinished()
    {
        return !crawler->loadingInProgress_;
    }

    LinesCount getLogNbLines()
    {
        return crawler->logData_->getNbLine();
    }

    LinesCount getLogFilteredNbLines()
    {
        return crawler->logFilteredData_->getNbLine();
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
                                                  crawler->logData_.get() );
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
        return crawler->logFilteredData_->lineTypeByLine( line ).testFlag(
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
        return crawler->logData_->getLineString( line );
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
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

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

SCENARIO( "Selecting a Match in the Filtered View moves the main view only when it is off screen",
          "[ui][jump]" )
{
    QTemporaryFile file{ "crawler_test_XXXXXX" };
    REQUIRE( generateDataFiles( file ) );

    Session session{ testSettingsPolicies(), std::make_shared<LogFormatCatalog>() };
    session.savedSearches().clear();

    CrawlerWidgetVisitor crawlerVisitor;
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

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

    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );

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
            QSignalSpy policyChanged( crawlerVisitor.crawler.get(),
                                      &CrawlerWidget::quickFindPolicyChanged );

            policies.quickFind.mainRegexpType = SearchRegexpType::ExtendedRegexp;
            session.applyPolicies( policies );

            THEN( "the Log File already open holds it, and hands it on" )
            {
                REQUIRE( crawlerVisitor.crawler->quickFindPolicy().mainRegexpType
                         == SearchRegexpType::ExtendedRegexp );
                REQUIRE( policyChanged.count() == 1 );
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

// The Highlighter Sets, and which of them are active, restored when this
// object goes: nothing a test ticks leaks into the tests that run next.
class PinnedHighlighterSets {
public:
    PinnedHighlighterSets()
        : sets_( HighlighterSetCollection::get().highlighterSets() )
        , activeSetIds_( HighlighterSetCollection::get().activeSetIds() )
    {
    }

    ~PinnedHighlighterSets()
    {
        auto& collection = HighlighterSetCollection::get();
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
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );
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

        AND_WHEN( "the CrawlerWidget applies a Configuration that still shows them" )
        {
            auto& config = Configuration::get();
            const auto hideAnsiColorSequences = config.hideAnsiColorSequences();
            config.setHideAnsiColorSequences( false );
            crawlerVisitor.crawler->applyConfiguration();
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
    crawlerVisitor.crawler.reset( static_cast<CrawlerWidget*>(
        session.open( file.fileName(), []() { return new CrawlerWidget(); } ) ) );
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
// "line 000007" each match one Log Line. The match sits at the end of a long
// Log Line, so the view is made wide enough to show it whatever font it is
// drawn in.
void searchForOneLine( CrawlerWidgetVisitor& crawlerVisitor, const QString& pattern )
{
    crawlerVisitor.crawler->resize( 1600, 600 );
    crawlerVisitor.clearSearchPattern();
    crawlerVisitor.setSearchPattern( pattern );
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

        WHEN(
            "the CrawlerWidget applies a Configuration that colors main-search matches otherwise" )
        {
            auto& config = Configuration::get();
            const auto mainSearchHighlight = config.mainSearchHighlight();
            const auto mainSearchBackColor = config.mainSearchBackColor();
            config.setEnableMainSearchHighlight( true );
            config.setMainSearchBackColor( changedColor );
            first.crawler->applyConfiguration();
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
