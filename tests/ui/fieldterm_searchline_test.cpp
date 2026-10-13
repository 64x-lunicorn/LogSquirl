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

// A Search with Field Terms in a tab (#798): the Search Line's logical
// combination reads a Field Term through the Log Format recognized for the
// Log File; a field the Log Format lacks, or no Log Format, is a pattern
// error in the Search Line; the Regex Lab, Predefined Filters and the Table
// View's cell menu take part; and a reload recognizing another Log Format
// reads the Search anew.

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <set>

#include "crawlerwidget.h"
#include "fake_file_watch.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "logmainview.h"
#include "logtableview.h"
#include "openlogfile.h"
#include "regexlabwindow.h"
#include "savedsearches.h"
#include "searchlinewidget_access.h"
#include "session.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

struct FieldTermSearchLineTest {};

template <>
struct CrawlerWidget::access_by<FieldTermSearchLineTest> {
    static OpenLogFile& openLogFile( CrawlerWidget& crawler )
    {
        return *crawler.openLogFile_;
    }

    static SearchLineWidget& searchLine( CrawlerWidget& crawler )
    {
        return *crawler.searchLine_;
    }

    static LogTableView& tableView( CrawlerWidget& crawler )
    {
        return *crawler.logTableView_;
    }

    static void focusMainView( CrawlerWidget& crawler )
    {
        crawler.logMainView_->setFocus();
        QCoreApplication::processEvents();
    }

    static int recognitionCount( CrawlerWidget& crawler )
    {
        return crawler.openLogFile_->formatRecognitionCount();
    }

    static bool isLoadingFinished( CrawlerWidget& crawler )
    {
        return crawler.lastLoadStatus_.has_value();
    }
};

namespace {

using CrawlerAccess = CrawlerWidget::access_by<FieldTermSearchLineTest>;

constexpr int LogLineCount = 40;

// "10:00:NN LEVEL request NN from db-X": every fourth Log Line an ERROR,
// every fifth from db-2.
QString logLine( int index )
{
    const auto* level = index % 4 == 0 ? "ERROR" : ( index % 3 == 0 ? "WARN" : "INFO" );
    return QStringLiteral( "10:00:%1 %2 request %3 from %4" )
        .arg( index, 2, 10, QChar( '0' ) )
        .arg( QString::fromLatin1( level ) )
        .arg( index )
        .arg( index % 5 == 0 ? QStringLiteral( "db-2" ) : QStringLiteral( "db-1" ) );
}

// A Log Format that parses logLine(), with the level field named as given.
const char* FormatJson = R"({
    "fieldterm_test_log": {
        "title": "Field Term test",
        "regex": {
            "basic": {
                "pattern": "^(?<timestamp>\\d{2}:\\d{2}:\\d{2}) (?<%1>[A-Z]+) request (?<id>\\d+) from (?<host>\\S+)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "%1",
        "value": {
            "host": { "kind": "string", "identifier": true }
        },
        "sample": [{ "line": "10:00:00 ERROR request 0 from db-2" }]
    }
})";

void writeUserFormat( const QString& directory, const QString& levelField )
{
    QFile file( QDir( directory ).filePath( "fieldterm_test.json" ) );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) );
    file.write( QString::fromUtf8( FormatJson ).arg( levelField ).toUtf8() );
}

const SearchLine::Flags LogicalPlain{ .matchCase = true,
                                      .useRegexp = false,
                                      .inverse = false,
                                      .booleanCombination = true,
                                      .autoRefresh = false };

// A Log File open in a Crawler Widget of its own Session, recognized by the
// Log Format of the Session's Catalog -- or by none, without one.
struct OpenCrawler {
    explicit OpenCrawler( bool withLogFormat = true )
    {
        REQUIRE( directory.isValid() );
        userFormats = directory.filePath( "formats" );
        REQUIRE( QDir().mkpath( userFormats ) );
        if ( withLogFormat ) {
            writeUserFormat( userFormats, "level" );
        }
        catalog = std::make_shared<LogFormatCatalog>( userFormats );
        catalog->rebuild();

        auto policies = testSettingsPolicies();
        policies.recognition.enabled = true;
        policies.quickFind.mainRegexpType = SearchRegexpType::FixedString;
        // The Log File hears of each change at once, when the test reports it.
        fileWatch = std::make_shared<FakeFileWatch>();
        session = std::make_unique<Session>( policies, catalog, fileWatch );
        session->savedSearches().clear();

        logFilePath = directory.filePath( "fieldterm.log" );
        QFile file( logFilePath );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        for ( int i = 0; i < LogLineCount; ++i ) {
            file.write( logLine( i ).toUtf8() + '\n' );
        }
        file.close();

        crawler.reset( static_cast<CrawlerWidget*>( session->open(
            logFilePath, []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) ) );
        crawler->resize( 800, 600 );
        showUntilExposed( *crawler );
        REQUIRE( waitUiState( [ this ] {
            return CrawlerAccess::isLoadingFinished( *crawler )
                   && CrawlerAccess::openLogFile( *crawler ).logData()->getNbLine().get()
                          == static_cast<uint64_t>( LogLineCount );
        } ) );
        if ( withLogFormat ) {
            REQUIRE( CrawlerAccess::openLogFile( *crawler ).logFormat() != nullptr );
        }
        QTest::qWait( 100 );
    }

    ~OpenCrawler()
    {
        crawler.reset();
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    }

    OpenCrawler( const OpenCrawler& ) = delete;
    OpenCrawler& operator=( const OpenCrawler& ) = delete;

    SearchLineWidget& searchLine()
    {
        return CrawlerAccess::searchLine( *crawler );
    }

    void setSearch( const QString& pattern, const SearchLine::Flags& flags )
    {
        searchLine().setFlags( flags );
        auto* edit = SearchLineAccess::patternEdit( searchLine() );
        edit->clearEditText();
        QTest::keyClicks( edit, pattern );
        REQUIRE( searchLine().pattern() == pattern );
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

    std::set<int> matchingLines()
    {
        const auto filtered = CrawlerAccess::openLogFile( *crawler ).filteredData();
        std::set<int> lines;
        for ( const auto logLine : filtered->matches() ) {
            lines.insert( static_cast<int>( logLine ) );
        }
        return lines;
    }

    QString infoText()
    {
        return SearchLineAccess::infoLine( searchLine() )->text();
    }

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
        lab->setSample( RegexLabWindow::Sample::LinesAroundCurrentLine );
        REQUIRE( waitUiState(
            [ lab ] {
                return lab->result().lines.size() == static_cast<std::size_t>( LogLineCount )
                       || lab->result().error.has_value();
            },
            10'000 ) );
        return *lab;
    }

    QTemporaryDir directory;
    QString userFormats;
    QString logFilePath;
    std::shared_ptr<LogFormatCatalog> catalog;
    std::shared_ptr<FakeFileWatch> fileWatch;
    std::unique_ptr<Session> session;
    std::unique_ptr<CrawlerWidget> crawler;
};

std::set<int> linesWhere( bool ( *predicate )( int ) )
{
    std::set<int> lines;
    for ( int i = 0; i < LogLineCount; ++i ) {
        if ( predicate( i ) ) {
            lines.insert( i );
        }
    }
    return lines;
}

} // namespace

SCENARIO( "A Search Line's Field Term reads the field through the tab's Log Format",
          "[ui][fieldterm]" )
{
    GIVEN( "a tab whose Log File a Log Format with a level and a host field parses" )
    {
        OpenCrawler open;

        WHEN( "the Search is a Field Term on the level" )
        {
            open.setSearch( R"(level:"ERROR")", LogicalPlain );
            open.runSearch();

            THEN( "the Log Lines whose level is ERROR match, and no other" )
            {
                REQUIRE( open.matchingLines() == linesWhere( []( int i ) { return i % 4 == 0; } ) );
            }
        }

        WHEN( "two Field Terms are combined" )
        {
            open.setSearch( R"(level:"ERROR" and host:"db-2")", LogicalPlain );
            open.runSearch();

            THEN( "both fields must match" )
            {
                REQUIRE( open.matchingLines()
                         == linesWhere( []( int i ) { return i % 4 == 0 && i % 5 == 0; } ) );
            }
        }

        WHEN( "a Field Term names a field the Log Format lacks" )
        {
            open.setSearch( R"(port:"53")", LogicalPlain );
            open.runSearch();

            THEN( "the Search Line shows a pattern error naming the field, and nothing matches" )
            {
                REQUIRE( open.infoText().startsWith( "Error in expression" ) );
                REQUIRE( open.infoText().contains( QStringLiteral( "\"port\"" ) ) );
                REQUIRE( open.matchingLines().empty() );
            }
        }

        WHEN( "a Predefined Filter with a Field Term is applied" )
        {
            open.crawler->setSearchPatternFromPredefinedFilters(
                { { "errors", R"(level:"ERROR")", true } } );
            // Without auto-run, the Search runs as the user asks for it.
            open.runSearch();

            THEN( "the Search runs as a logical combination and finds the ERROR Log Lines" )
            {
                REQUIRE( open.searchLine().flags().booleanCombination );
                REQUIRE( open.searchLine().pattern() == R"((level:"ERROR"))" );
                REQUIRE( open.matchingLines() == linesWhere( []( int i ) { return i % 4 == 0; } ) );
            }
        }

        WHEN( "the Table View's cell menu asks to search a value in a column" )
        {
            open.setSearch( R"(level:"ERROR")", LogicalPlain );
            open.runSearch();
            Q_EMIT CrawlerAccess::tableView( *open.crawler )
                .searchValueInColumn( QStringLiteral( "host" ), QStringLiteral( "db-2" ) );
            QTest::qWait( 100 );
            REQUIRE( waitUiState( [ &open ] { return !open.isSearchRunning(); } ) );

            THEN( "the Field Term joins the pattern and the Search ran" )
            {
                REQUIRE( open.searchLine().pattern() == R"(level:"ERROR" and host:"db-2")" );
                REQUIRE( open.matchingLines()
                         == linesWhere( []( int i ) { return i % 4 == 0 && i % 5 == 0; } ) );
            }
        }

        WHEN( "a Field Term Search is opened in the Regex Lab" )
        {
            open.setSearch( R"(level:"ERROR" or host:"db-2")", LogicalPlain );
            open.runSearch();
            auto& lab = open.openLab();
            const auto& result = lab.result();

            THEN( "the Lab lists the Field Terms with their fields and says per line which match" )
            {
                REQUIRE_FALSE( result.error.has_value() );
                REQUIRE( result.subPatternFields
                         == QStringList{ QStringLiteral( "level" ), QStringLiteral( "host" ) } );
                REQUIRE( result.lines.size() == static_cast<std::size_t>( LogLineCount ) );
                const auto* legend = lab.findChild<QLabel*>( QStringLiteral( "subPatterns" ) );
                REQUIRE( legend != nullptr );
                REQUIRE( legend->text().contains( QStringLiteral( "level:" ) ) );
                for ( int line = 0; line < LogLineCount; ++line ) {
                    INFO( "line " << line );
                    QList<int> expected;
                    if ( line % 4 == 0 ) {
                        expected.append( 1 );
                    }
                    if ( line % 5 == 0 ) {
                        expected.append( 2 );
                    }
                    CHECK( lab.subPatternsShown( line ) == expected );
                }
            }
        }

        WHEN( "the Log Format is edited on disk to name the level field otherwise, the Log "
              "File is reloaded and the Search run again" )
        {
            open.setSearch( R"(level:"ERROR")", LogicalPlain );
            open.runSearch();
            REQUIRE( open.matchingLines().size() == 10 );

            // A reload by hand drops the Search and recognizes the Log Format
            // anew; the Search is run again as the user runs it.
            writeUserFormat( open.userFormats, "severity" );
            open.catalog->rebuild();
            open.crawler->reload();
            REQUIRE( waitUiState(
                [ &open ] { return CrawlerAccess::recognitionCount( *open.crawler ) == 2; } ) );
            REQUIRE( waitUiState(
                [ &open ] { return CrawlerAccess::isLoadingFinished( *open.crawler ); } ) );
            QTest::qWait( 100 );
            open.runSearch();

            THEN( "the Search is read under the new Log Format, which lacks the field: a pattern "
                  "error, not the cached Matches of the Log Format before" )
            {
                REQUIRE( open.infoText().startsWith( "Error in expression" ) );
                REQUIRE( open.infoText().contains( QStringLiteral( "\"level\"" ) ) );
                REQUIRE( open.matchingLines().empty() );
            }

            AND_WHEN( "the Search names the field as the new Log Format does" )
            {
                open.setSearch( R"(severity:"ERROR")", LogicalPlain );
                open.runSearch();

                THEN( "it finds the ERROR Log Lines again" )
                {
                    REQUIRE( open.matchingLines()
                             == linesWhere( []( int i ) { return i % 4 == 0; } ) );
                }
            }
        }
    }

    GIVEN( "a tab whose Log File is truncated under an auto-refreshed Field Term Search" )
    {
        OpenCrawler open;
        auto refreshed = LogicalPlain;
        refreshed.autoRefresh = true;
        open.setSearch( R"(level:"ERROR")", refreshed );
        open.runSearch();
        REQUIRE( open.matchingLines().size() == 10 );

        WHEN( "the Log File is truncated, which forgets the Log Format, and written again with "
              "half its Log Lines" )
        {
            {
                QFile file( open.logFilePath );
                REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
            }
            REQUIRE( open.fileWatch->reportChange( open.logFilePath ) );
            REQUIRE( waitUiState( [ &open ] {
                return CrawlerAccess::openLogFile( *open.crawler ).logData()->getNbLine().get()
                       == 0;
            } ) );
            REQUIRE( CrawlerAccess::openLogFile( *open.crawler ).logFormat() == nullptr );
            {
                QFile file( open.logFilePath );
                REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Append ) );
                for ( int i = 0; i < LogLineCount / 2; ++i ) {
                    file.write( logLine( i ).toUtf8() + '\n' );
                }
            }
            REQUIRE( open.fileWatch->reportChange( open.logFilePath ) );
            REQUIRE( waitUiState(
                [ &open ] { return CrawlerAccess::recognitionCount( *open.crawler ) >= 2; } ) );
            REQUIRE( waitUiState( [ &open ] {
                const auto state = CrawlerAccess::openLogFile( *open.crawler ).searchState();
                return state.phase == SearchSession::Phase::Complete && !open.isSearchRunning();
            } ) );

            THEN( "the Search was read anew under the Log Format recognized again, and finds "
                  "the ERROR Log Lines of the truncated Log File" )
            {
                REQUIRE( open.matchingLines() == linesWhere( []( int i ) {
                             return i < LogLineCount / 2 && i % 4 == 0;
                         } ) );
                REQUIRE_FALSE( open.infoText().startsWith( "Error in expression" ) );
            }
        }
    }

    GIVEN( "a tab without a Log Format" )
    {
        OpenCrawler open( false );

        WHEN( "the Search is a Field Term" )
        {
            open.setSearch( R"(level:"ERROR")", LogicalPlain );
            open.runSearch();

            THEN( "the Search Line shows a pattern error naming the field and the Log Format" )
            {
                REQUIRE( open.infoText().startsWith( "Error in expression" ) );
                REQUIRE( open.infoText().contains( QStringLiteral( "\"level\"" ) ) );
                REQUIRE( open.infoText().contains( QStringLiteral( "Log Format" ) ) );
                REQUIRE( open.matchingLines().empty() );
            }
        }

        WHEN( "the Search is 00:05, not a logical combination" )
        {
            auto plain = LogicalPlain;
            plain.booleanCombination = false;
            open.setSearch( "00:05", plain );
            open.runSearch();

            THEN( "the colon is text: the Log Line with that time matches" )
            {
                REQUIRE( open.matchingLines() == std::set<int>{ 5 } );
            }
        }
    }
}
