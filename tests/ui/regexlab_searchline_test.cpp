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

// The Search Line opens its pattern in the Regex Lab (#661): with the options
// it reads the pattern with, Apply writes both back without running a Search
// unless auto-refresh is on, Cancel changes nothing, and a logical
// combination shows which of its sub-patterns match each line, the verdict
// being the Search's.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include <memory>
#include <set>

#include "crawlerwidget.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "logmainview.h"
#include "openlogfile.h"
#include "regexlabwindow.h"
#include "savedsearches.h"
#include "searchlinewidget_access.h"
#include "session.h"
#include "test_policies.h"
#include "test_utils.h"

struct RegexLabSearchLineTest {};

template <>
struct CrawlerWidget::access_by<RegexLabSearchLineTest> {
    static OpenLogFile& openLogFile( CrawlerWidget& crawler )
    {
        return *crawler.openLogFile_;
    }

    static SearchLineWidget& searchLine( CrawlerWidget& crawler )
    {
        return *crawler.searchLine_;
    }

    // The main view, which shows every Log Line, is the one last in focus.
    static void focusMainView( CrawlerWidget& crawler )
    {
        crawler.logMainView_->setFocus();
        QCoreApplication::processEvents();
    }
};

namespace {

using CrawlerAccess = CrawlerWidget::access_by<RegexLabSearchLineTest>;

constexpr int LogLineCount = 40;

QString logLine( int index )
{
    const auto* level = index % 4 == 0 ? "ERROR" : ( index % 3 == 0 ? "WARN" : "INFO" );
    return QStringLiteral( "10:00:%1 %2 request %3 from %4" )
        .arg( index, 2, 10, QChar( '0' ) )
        .arg( QString::fromLatin1( level ) )
        .arg( index )
        .arg( index % 5 == 0 ? QStringLiteral( "db-2" ) : QStringLiteral( "db-1" ) );
}

// A Log File open in a Crawler Widget of its own Session, whose Search Line
// starts as the QuickFind Policy says.
struct OpenCrawler {
    explicit OpenCrawler( SearchRegexpType mainRegexpType = SearchRegexpType::ExtendedRegexp )
        : session( policies( mainRegexpType ), std::make_shared<LogFormatCatalog>() )
    {
        REQUIRE( file.open() );
        for ( int i = 0; i < LogLineCount; ++i ) {
            file.write( logLine( i ).toUtf8() + '\n' );
        }
        file.flush();
        session.savedSearches().clear();

        crawler.reset( static_cast<CrawlerWidget*>(
            session.open( file.fileName(),
                          []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
        crawler->resize( 800, 600 );
        crawler->show();
        REQUIRE( waitUiState( [ this ] {
            return CrawlerAccess::openLogFile( *crawler ).logData()->getNbLine().get()
                   == static_cast<uint64_t>( LogLineCount );
        } ) );
        QTest::qWait( 100 );
    }

    ~OpenCrawler()
    {
        // A Lab left open goes with the tab, before the next test's windows.
        crawler.reset();
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    }

    OpenCrawler( const OpenCrawler& ) = delete;
    OpenCrawler& operator=( const OpenCrawler& ) = delete;

    static SettingsPolicies policies( SearchRegexpType mainRegexpType )
    {
        auto settings = testSettingsPolicies();
        settings.quickFind.mainRegexpType = mainRegexpType;
        return settings;
    }

    SearchLineWidget& searchLine()
    {
        return CrawlerAccess::searchLine( *crawler );
    }

    // Types the pattern into the Search Line and sets its buttons, as the
    // user does.
    void setSearch( const QString& pattern, const SearchLine::Flags& flags )
    {
        searchLine().setFlags( flags );
        auto* edit = SearchLineAccess::patternEdit( searchLine() );
        edit->clearEditText();
        QTest::keyClicks( edit, pattern );
        REQUIRE( searchLine().pattern() == pattern );
    }

    LinesCount matchCount()
    {
        return CrawlerAccess::openLogFile( *crawler ).filteredData()->getNbMatches();
    }

    // The Log Lines the Search found.
    std::set<int> matchingLines()
    {
        const auto filtered = CrawlerAccess::openLogFile( *crawler ).filteredData();
        std::set<int> lines;
        for ( std::uint64_t i = 0; i < filtered->getNbMatches().get(); ++i ) {
            lines.insert(
                static_cast<int>( filtered->getMatchingLineNumber( LineNumber( i ) ).get() ) );
        }
        return lines;
    }

    bool isSearchRunning()
    {
        return SearchLineAccess::stopButton( searchLine() )->isVisible();
    }

    void runSearch()
    {
        QTest::mouseClick( SearchLineAccess::searchButton( searchLine() ), Qt::LeftButton );
        QTest::qWait( 100 );
        REQUIRE( waitUiState( [ this ] { return !isSearchRunning(); } ) );
    }

    // Opens the Lab as the Search Line's context menu does, and waits for
    // what it shows of the sample from the tab: every Log Line, around the
    // current line of the main view.
    RegexLabWindow& openLab()
    {
        CrawlerAccess::focusMainView( *crawler );
        const auto text = QCoreApplication::translate( "CrawlerWidget", "Open in Regex Lab..." );
        QAction* open = nullptr;
        for ( auto* action : crawler->findChildren<QAction*>() ) {
            if ( action->text() == text ) {
                open = action;
            }
        }
        REQUIRE( open != nullptr );
        open->trigger();

        auto* lab = crawler->findChild<RegexLabWindow*>();
        REQUIRE( lab != nullptr );
        // A Search selects its first Match: the sample is every Log Line
        // around it, not the one selected.
        lab->setSample( RegexLabWindow::Sample::LinesAroundCurrentLine );
        REQUIRE( waitUiState(
            [ lab ] {
                return lab->result().lines.size() == static_cast<std::size_t>( LogLineCount );
            },
            10'000 ) );
        return *lab;
    }

    QTemporaryFile file{ "regexlab_searchline_XXXXXX" };
    Session session;
    std::unique_ptr<CrawlerWidget> crawler;
};

template <typename Widget>
Widget* part( const RegexLabWindow& lab, const char* name )
{
    auto* widget = lab.findChild<Widget*>( QString::fromLatin1( name ) );
    REQUIRE( widget != nullptr );
    return widget;
}

QAbstractButton* button( const RegexLabWindow& lab, QDialogButtonBox::StandardButton which )
{
    auto* found = part<QDialogButtonBox>( lab, "buttons" )->button( which );
    REQUIRE( found != nullptr );
    return found;
}

// Edits the pattern in the Lab as the user types it, with the options.
void editInLab( RegexLabWindow& lab, const QString& pattern, bool matchCase, bool useRegexp,
                bool inverse, bool logicalCombination )
{
    auto* edit = part<QLineEdit>( lab, "pattern" );
    edit->clear();
    QTest::keyClicks( edit, pattern );
    part<QCheckBox>( lab, "matchCase" )->setChecked( matchCase );
    part<QCheckBox>( lab, "useRegexp" )->setChecked( useRegexp );
    part<QCheckBox>( lab, "inverse" )->setChecked( inverse );
    part<QCheckBox>( lab, "logicalCombination" )->setChecked( logicalCombination );
}

const SearchLine::Flags PlainRegexp{ .matchCase = false,
                                     .useRegexp = true,
                                     .inverse = false,
                                     .booleanCombination = false,
                                     .autoRefresh = false };

} // namespace

SCENARIO( "The Search Line opens its pattern and options in the Regex Lab", "[ui][regexlab]" )
{
    GIVEN( "a Search Line with an inverted regexp that matches case" )
    {
        OpenCrawler open;
        auto flags = PlainRegexp;
        flags.matchCase = true;
        flags.inverse = true;
        open.setSearch( "ERROR|WARN", flags );

        WHEN( "it is opened in the Regex Lab" )
        {
            auto& lab = open.openLab();

            THEN( "the Lab has the pattern exactly as the Search Line asks for it" )
            {
                CHECK( lab.pattern() == open.searchLine().request() );
                CHECK( lab.isVisible() );
            }

            THEN( "it offers Apply and Cancel, on Log Lines of the tab" )
            {
                CHECK( button( lab, QDialogButtonBox::Apply )->isVisible() );
                CHECK( button( lab, QDialogButtonBox::Cancel )->isVisible() );
                CHECK( lab.sample() == RegexLabWindow::Sample::LinesAroundCurrentLine );
                CHECK( lab.windowTitle().contains( QFileInfo( open.file.fileName() ).fileName() ) );
            }

            THEN( "it matches with the engine the Searches of the tab run on, and follows it" )
            {
                CHECK( lab.engine() == RegexpEngine::Vectorscan );
                auto policies = testSettingsPolicies();
                policies.search.regexpEngine = RegexpEngine::QRegularExpression;
                open.session.applyPolicies( policies );
                CHECK( lab.engine() == RegexpEngine::QRegularExpression );
            }

            AND_WHEN( "the Search Line changes and the Lab, its pattern unedited, is asked for "
                      "again" )
            {
                open.setSearch( "INFO", PlainRegexp );
                auto& again = open.openLab();

                THEN( "the one Lab takes the Search Line's pattern of now" )
                {
                    CHECK( open.crawler->findChildren<RegexLabWindow*>().size() == 1 );
                    CHECK( &again == &lab );
                    CHECK( lab.pattern() == open.searchLine().request() );
                }
            }

            AND_WHEN( "the pattern is edited in the Lab, the Search Line changes and the Lab is "
                      "asked for again" )
            {
                editInLab( lab, "db-1", false, true, false, false );
                const auto edited = lab.pattern();
                open.setSearch( "INFO", PlainRegexp );
                open.openLab();

                THEN( "the one Lab keeps the pattern edited in it" )
                {
                    CHECK( open.crawler->findChildren<RegexLabWindow*>().size() == 1 );
                    CHECK( lab.pattern() == edited );
                }
            }
        }
    }

    GIVEN( "a Search Line that reads its pattern as Wildcard or Fixed String does" )
    {
        OpenCrawler open( GENERATE( SearchRegexpType::Wildcard, SearchRegexpType::FixedString ) );
        REQUIRE_FALSE( open.searchLine().flags().useRegexp );
        auto flags = open.searchLine().flags();
        open.setSearch( "request 1.", flags );

        WHEN( "it is opened in the Regex Lab" )
        {
            auto& lab = open.openLab();

            THEN( "the Lab reads the pattern as plain text, as the Search does" )
            {
                CHECK( lab.pattern() == open.searchLine().request() );
                CHECK_FALSE( part<QCheckBox>( lab, "useRegexp" )->isChecked() );
                // "request 1." taken literally matches no line.
                CHECK( lab.result().matchingLines == 0 );
            }
        }
    }
}

SCENARIO( "Apply writes the Regex Lab's pattern and options into the Search Line, Cancel does not",
          "[ui][regexlab]" )
{
    const auto autoRefresh = GENERATE( false, true );

    GIVEN( "a Search Line with a Search run, auto-refresh " << ( autoRefresh ? "on" : "off" ) )
    {
        OpenCrawler open;
        auto flags = PlainRegexp;
        flags.autoRefresh = autoRefresh;
        open.setSearch( "ERROR", flags );
        open.runSearch();
        REQUIRE( open.matchCount() == 10_lcount );

        auto& lab = open.openLab();
        QPointer<RegexLabWindow> labPointer( &lab );
        QSignalSpy edited( &open.searchLine(), &SearchLineWidget::patternEdited );

        WHEN( "a logical combination is edited in the Lab and applied" )
        {
            editInLab( lab, R"("WARN" or "db-2")", true, false, false, true );
            button( lab, QDialogButtonBox::Apply )->click();

            THEN( "the Search Line has the pattern and the options, as though typed and set" )
            {
                const RegularExpressionPattern expected( R"("WARN" or "db-2")", true, false, true,
                                                         true );
                CHECK( open.searchLine().request() == expected );
                CHECK( SearchLineAccess::patternEdit( open.searchLine() )->currentText()
                       == expected.pattern );
                CHECK( SearchLineAccess::booleanButton( open.searchLine() )->isChecked() );
                CHECK( SearchLineAccess::matchCaseButton( open.searchLine() )->isChecked() );
                CHECK_FALSE( SearchLineAccess::useRegexpButton( open.searchLine() )->isChecked() );
                CHECK( open.searchLine().flags().autoRefresh == autoRefresh );
                CHECK( edited.size() == 1 );
                CHECK( ( labPointer.isNull() || !labPointer->isVisible() ) );
            }

            THEN( "the Search runs only when auto-refresh is on" )
            {
                // WARN: 10 Log Lines, db-2: 8, 2 of them both.
                const auto expected = autoRefresh ? 16_lcount : 10_lcount;
                waitUiState( [ & ] { return open.matchCount() == expected; }, 2000 );
                REQUIRE( waitUiState( [ & ] { return !open.isSearchRunning(); } ) );
                CHECK( open.matchCount() == expected );
            }
        }

        WHEN( "the pattern is edited in the Lab and cancelled" )
        {
            const auto before = open.searchLine().request();
            const auto flagsBefore = open.searchLine().flags();
            editInLab( lab, "WARN", true, true, true, false );
            button( lab, QDialogButtonBox::Cancel )->click();

            THEN( "the Search Line and its Search stay as they were" )
            {
                CHECK( open.searchLine().request() == before );
                CHECK( open.searchLine().flags() == flagsBefore );
                CHECK( edited.isEmpty() );
                QTest::qWait( 200 );
                CHECK( open.matchCount() == 10_lcount );
                CHECK( ( labPointer.isNull() || !labPointer->isVisible() ) );
            }
        }
    }
}

SCENARIO( "A logical combination in the Regex Lab shows which sub-patterns match each line",
          "[ui][regexlab]" )
{
    const auto inverse = GENERATE( false, true );

    GIVEN( "a Search Line with a logical combination, "
           << ( inverse ? "inverted" : "not inverted" ) )
    {
        OpenCrawler open;
        auto flags = PlainRegexp;
        flags.booleanCombination = true;
        flags.inverse = inverse;
        open.setSearch( R"("ERROR" and not("db-2") or "WARN")", flags );
        open.runSearch();
        const auto searched = open.matchingLines();

        WHEN( "it is opened in the Regex Lab" )
        {
            auto& lab = open.openLab();
            const auto& result = lab.result();

            THEN( "the sub-patterns are listed, numbered as written" )
            {
                CHECK( result.subPatterns
                       == QStringList{ QStringLiteral( "ERROR" ), QStringLiteral( "db-2" ),
                                       QStringLiteral( "WARN" ) } );
                const auto* legend = part<QLabel>( lab, "subPatterns" );
                CHECK( legend->isVisible() );
                CHECK( legend->text().contains( QStringLiteral( "db-2" ) ) );
                CHECK( part<QWidget>( lab, "subPatternColumn" )->isVisible() );
            }

            THEN( "each line's verdict is the Search's, and each line shows the sub-patterns "
                  "that match it" )
            {
                REQUIRE( result.lines.size() == static_cast<std::size_t>( LogLineCount ) );
                // Shown beside the lines as they are drawn.
                CHECK_FALSE( lab.grab().isNull() );
                for ( int line = 0; line < LogLineCount; ++line ) {
                    INFO( "line " << line << ": " << logLine( line ).toStdString() );
                    CHECK( result.lines[ static_cast<std::size_t>( line ) ].isMatch
                           == searched.contains( line ) );

                    QList<int> expected;
                    if ( line % 4 == 0 ) {
                        expected.append( 1 );
                    }
                    if ( line % 5 == 0 ) {
                        expected.append( 2 );
                    }
                    if ( line % 4 != 0 && line % 3 == 0 ) {
                        expected.append( 3 );
                    }
                    CHECK( lab.subPatternsShown( line ) == expected );
                }
                CHECK( result.matchingLines == searched.size() );
            }

            THEN( "the numbers beside a line are said in words too" )
            {
                // Line 20: ERROR, db-2.
                CHECK( lab.subPatternsDescription( 20 ).contains( QStringLiteral( "1, 2" ) ) );
                CHECK( lab.subPatternsDescription( 1 ).contains( QStringLiteral( "no" ) ) );
            }
        }
    }

    GIVEN( "a Search Line with a pattern that is no logical combination" )
    {
        OpenCrawler open;
        open.setSearch( "ERROR", PlainRegexp );

        WHEN( "it is opened in the Regex Lab" )
        {
            const auto& lab = open.openLab();

            THEN( "no sub-patterns are shown" )
            {
                CHECK( lab.result().subPatterns.isEmpty() );
                CHECK_FALSE( part<QLabel>( lab, "subPatterns" )->isVisible() );
                CHECK_FALSE( part<QWidget>( lab, "subPatternColumn" )->isVisible() );
                CHECK( lab.subPatternsShown( 0 ).isEmpty() );
            }
        }
    }
}

SCENARIO( "A tab closed with the Regex Lab open takes the Lab along", "[ui][regexlab]" )
{
    GIVEN( "the Search Line's pattern open in the Regex Lab, edited there" )
    {
        OpenCrawler open;
        open.setSearch( "ERROR", PlainRegexp );
        auto& lab = open.openLab();
        editInLab( lab, "WARN", true, true, false, false );

        QPointer<RegexLabWindow> labPointer( &lab );
        QSignalSpy applied( &lab, &RegexLabWindow::applied );
        QSignalSpy edited( &open.searchLine(), &SearchLineWidget::patternEdited );
        QSignalSpy flagsChanged( &open.searchLine(), &SearchLineWidget::flagsChanged );

        WHEN( "the tab is closed" )
        {
            open.crawler.reset();
            QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
            QTest::qWait( 300 );

            THEN( "the Lab is gone, and nothing was written into the Search Line" )
            {
                CHECK( labPointer.isNull() );
                CHECK( applied.isEmpty() );
                CHECK( edited.isEmpty() );
                CHECK( flagsChanged.isEmpty() );
            }
        }
    }
}
