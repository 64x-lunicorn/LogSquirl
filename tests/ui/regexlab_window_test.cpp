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

// The Regex Lab (#659): it opens from the Tools menu for the tab in front,
// takes its sample from the tab or from pasted text, marks the Matches and
// lists the capture groups of a line, says what is wrong with a pattern, and
// counts exactly the Log Lines a Search with the same pattern and options
// selects.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTest>
#include <QTextBlock>

#include <memory>

#include "abstractlogview.h"
#include "applicationplugins.h"
#include "crawlerwidget.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "logmainview.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "openlogfile.h"
#include "regexlabwindow.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"

struct RegexLabTest {};

template <>
struct CrawlerWidget::access_by<RegexLabTest> {
    static OpenLogFile& openLogFile( CrawlerWidget& crawler )
    {
        return *crawler.openLogFile_;
    }

    static void selectInMainView( CrawlerWidget& crawler, LineNumber line )
    {
        crawler.logMainView_->setFocus();
        crawler.logMainView_->selectAndDisplayLine( line );
    }
};

namespace {

using CrawlerAccess = CrawlerWidget::access_by<RegexLabTest>;

constexpr int LogLineCount = 3000;

QString logLine( int index )
{
    const auto* level = index % 7 == 0 ? "ERROR" : ( index % 5 == 0 ? "Warn" : "info" );
    return QStringLiteral( "2026-09-30 10:%1 %2 request id=%3 user=%4" )
        .arg( index % 60, 2, 10, QChar( '0' ) )
        .arg( QString::fromLatin1( level ) )
        .arg( index )
        .arg( index % 3 == 0 ? QStringLiteral( "alice" ) : QStringLiteral( "bob" ) );
}

bool writeLogFile( QTemporaryFile& file, int lines )
{
    if ( !file.open() ) {
        return false;
    }
    for ( int i = 0; i < lines; ++i ) {
        file.write( logLine( i ).toUtf8() + '\n' );
    }
    file.flush();
    return true;
}

// A Log File of LogLineCount lines, open in a Crawler Widget of its own Session.
struct OpenCrawler {
    OpenCrawler()
        : session( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() )
    {
        REQUIRE( writeLogFile( file, LogLineCount ) );
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

    RegexLabSampleSource source()
    {
        RegexLabSampleSource tab;
        tab.name = QStringLiteral( "test.log" );
        tab.selectedLines
            = [ this ]( LinesCount count ) { return crawler->selectedLogLineTexts( count ); };
        tab.linesAroundCurrentLine = [ this ]( LinesCount count ) {
            return crawler->logLineTextsAroundCurrentLine( count );
        };
        return tab;
    }

    QTemporaryFile file;
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

// Types the pattern into the Lab as the user does and waits for what it
// shows of it.
void typePattern( RegexLabWindow& lab, const QString& pattern )
{
    QSignalSpy evaluated( &lab, &RegexLabWindow::evaluated );
    auto* edit = part<QLineEdit>( lab, "pattern" );
    edit->clear();
    QTest::keyClicks( edit, pattern );
    REQUIRE( waitUiState( [ & ] { return !evaluated.isEmpty(); }, 10'000 ) );
}

// Sets the pattern and its options at once and waits for what the Lab shows.
void setPattern( RegexLabWindow& lab, const RegularExpressionPattern& pattern )
{
    QSignalSpy evaluated( &lab, &RegexLabWindow::evaluated );
    lab.setPattern( pattern );
    REQUIRE( waitUiState( [ & ] { return !evaluated.isEmpty(); }, 10'000 ) );
}

void pasteSample( RegexLabWindow& lab, const QString& text )
{
    QSignalSpy evaluated( &lab, &RegexLabWindow::evaluated );
    lab.setSample( RegexLabWindow::Sample::PastedText );
    auto* sample = part<QPlainTextEdit>( lab, "sampleText" );
    sample->setPlainText( text );
    REQUIRE( waitUiState( [ & ] { return evaluated.size() >= 2; }, 10'000 ) );
}

// The text the Lab marks as matched in its sample, in order.
QStringList markedTexts( const RegexLabWindow& lab )
{
    const auto* sample = lab.findChild<QPlainTextEdit*>( QStringLiteral( "sampleText" ) );
    QStringList marked;
    for ( const auto& selection : sample->extraSelections() ) {
        if ( selection.cursor.hasSelection() ) {
            marked.append( selection.cursor.selectedText() );
        }
    }
    return marked;
}

// The lines the Lab marks as matching, by number in its sample.
QList<int> markedLines( const RegexLabWindow& lab )
{
    const auto* sample = lab.findChild<QPlainTextEdit*>( QStringLiteral( "sampleText" ) );
    QList<int> lines;
    for ( const auto& selection : sample->extraSelections() ) {
        if ( selection.format.boolProperty( QTextFormat::FullWidthSelection ) ) {
            lines.append( selection.cursor.blockNumber() );
        }
    }
    return lines;
}

void putCursorOnLine( RegexLabWindow& lab, int line )
{
    auto* sample = part<QPlainTextEdit>( lab, "sampleText" );
    QTextCursor cursor( sample->document()->findBlockByNumber( line ) );
    sample->setTextCursor( cursor );
}

QAction* regexLabAction( const MainWindow& window )
{
    const auto text = QApplication::translate( "logsquirl::mainwindow::action",
                                               logsquirl::mainwindow::action::regexLabText );
    for ( auto* action : window.findChildren<QAction*>() ) {
        if ( action->text() == text ) {
            return action;
        }
    }
    return nullptr;
}

bool isChoiceEnabled( const RegexLabWindow& lab, RegexLabWindow::Sample sample )
{
    const auto* choice = lab.findChild<QComboBox*>( QStringLiteral( "sampleChoice" ) );
    const auto* model = qobject_cast<QStandardItemModel*>( choice->model() );
    return model->item( choice->findData( static_cast<int>( sample ) ) )->isEnabled();
}

} // namespace

SCENARIO( "The Regex Lab opens from the Tools menu for the tab in front", "[ui][regexlab]" )
{
    auto session
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();
    auto window = std::make_unique<MainWindow>( WindowSession{ session, "Main", 0 }, plugins );
    window->show();

    auto* action = regexLabAction( *window );
    REQUIRE( action != nullptr );

    GIVEN( "no open Log File" )
    {
        action->trigger();
        auto* lab = window->findChild<RegexLabWindow*>();

        THEN( "a window that is not modal opens, with pasted text as its only sample" )
        {
            REQUIRE( lab != nullptr );
            CHECK( lab->isVisible() );
            CHECK( lab->isWindow() );
            CHECK_FALSE( lab->isModal() );
            CHECK( lab->sample() == RegexLabWindow::Sample::PastedText );
            CHECK_FALSE( isChoiceEnabled( *lab, RegexLabWindow::Sample::SelectedLines ) );
            CHECK_FALSE( isChoiceEnabled( *lab, RegexLabWindow::Sample::LinesAroundCurrentLine ) );
            CHECK_FALSE( part<QPushButton>( *lab, "refreshSample" )->isEnabled() );
            // Opened from the menu, it offers no Apply.
            CHECK( part<QDialogButtonBox>( *lab, "buttons" )->button( QDialogButtonBox::Apply )
                   == nullptr );
        }

        AND_WHEN( "it is chosen again" )
        {
            action->trigger();

            THEN( "the same window comes to the front" )
            {
                CHECK( window->findChildren<RegexLabWindow*>().size() == 1 );
            }
        }
    }

    GIVEN( "an open Log File in the tab in front" )
    {
        QTemporaryFile file;
        REQUIRE( writeLogFile( file, 50 ) );
        window->loadFileNonInteractive( file.fileName() );
        auto* tabs = window->findChild<TabbedCrawlerWidget*>();
        REQUIRE( waitUiState(
            [ & ] { return qobject_cast<CrawlerWidget*>( tabs->currentWidget() ) != nullptr; },
            10'000 ) );
        auto* crawler = qobject_cast<CrawlerWidget*>( tabs->currentWidget() );
        REQUIRE( waitUiState(
            [ & ] {
                return CrawlerAccess::openLogFile( *crawler ).logData()->getNbLine().get() == 50;
            },
            10'000 ) );

        action->trigger();
        auto* lab = window->findChild<RegexLabWindow*>();
        REQUIRE( lab != nullptr );

        THEN( "it is tied to the tab and takes its sample from the Log File" )
        {
            CHECK( lab->windowTitle().contains( tabs->tabText( tabs->currentIndex() ) ) );
            CHECK( isChoiceEnabled( *lab, RegexLabWindow::Sample::SelectedLines ) );
            CHECK( isChoiceEnabled( *lab, RegexLabWindow::Sample::LinesAroundCurrentLine ) );
            CHECK( lab->sample() == RegexLabWindow::Sample::LinesAroundCurrentLine );
            CHECK( part<QPlainTextEdit>( *lab, "sampleText" )->document()->blockCount() == 50 );
            CHECK( part<QPlainTextEdit>( *lab, "sampleText" )->isReadOnly() );
        }
    }

    window.reset();
    QTest::qWait( 50 );
}

SCENARIO( "The Regex Lab takes its sample from the tab or from pasted text", "[ui][regexlab]" )
{
    OpenCrawler open;
    CrawlerAccess::selectInMainView( *open.crawler, LineNumber( 2000 ) );

    RegexLabWindow lab( RegexpEngine::Vectorscan );
    lab.setSampleSource( open.source() );
    lab.show();
    auto* sample = part<QPlainTextEdit>( lab, "sampleText" );

    THEN( "with a Log Line selected, the sample is the selected Log Line" )
    {
        CHECK( lab.sample() == RegexLabWindow::Sample::SelectedLines );
        CHECK( sample->toPlainText() == logLine( 2000 ) );
    }

    WHEN( "the lines around the current line are chosen" )
    {
        lab.setSample( RegexLabWindow::Sample::LinesAroundCurrentLine );

        THEN( "the sample is a bounded window of Log Lines around it" )
        {
            const auto lines = sample->toPlainText().split( QChar::LineFeed );
            REQUIRE( lines.size() == 1000 );
            CHECK( lines.front() == logLine( 1500 ) );
            CHECK( lines.back() == logLine( 2499 ) );
        }
    }

    WHEN( "another line is selected in the tab" )
    {
        CrawlerAccess::selectInMainView( *open.crawler, LineNumber( 10 ) );

        THEN( "the sample stays as it was until it is refreshed" )
        {
            CHECK( sample->toPlainText() == logLine( 2000 ) );
            part<QPushButton>( lab, "refreshSample" )->click();
            CHECK( sample->toPlainText() == logLine( 10 ) );
        }
    }

    WHEN( "text is pasted" )
    {
        pasteSample( lab, "first pasted line\nsecond pasted line" );

        THEN( "it is the sample, and it stays when the Log File's sample is shown in between" )
        {
            CHECK( !sample->isReadOnly() );
            lab.setSample( RegexLabWindow::Sample::SelectedLines );
            CHECK( sample->toPlainText() == logLine( 2000 ) );
            lab.setSample( RegexLabWindow::Sample::PastedText );
            CHECK( sample->toPlainText() == "first pasted line\nsecond pasted line" );
        }
    }
}

SCENARIO( "The Regex Lab marks the Matches and lists the capture groups of a line",
          "[ui][regexlab]" )
{
    RegexLabWindow lab( RegexpEngine::Vectorscan );
    lab.show();
    pasteSample( lab, "GET /index.html 200\nPOST /login 403\nnothing here" );

    WHEN( "a pattern with a named and a numbered group is typed" )
    {
        typePattern( lab, R"((?<method>GET|POST) (/\w+))" );

        THEN( "the matching lines and the matched text are marked" )
        {
            CHECK( markedLines( lab ) == QList<int>{ 0, 1 } );
            CHECK( markedTexts( lab ) == QStringList{ "GET /index", "POST /login" } );
            CHECK( part<QLabel>( lab, "status" )->text().contains( "2 of 3" ) );
            CHECK( part<QLabel>( lab, "error" )->isHidden() );
        }

        AND_WHEN( "the cursor is on the second line" )
        {
            putCursorOnLine( lab, 1 );
            auto* groups = part<QTableWidget>( lab, "captureGroups" );

            THEN( "its groups are listed, numbered and named, with their text" )
            {
                REQUIRE( groups->rowCount() == 3 );
                CHECK( groups->item( 0, 0 )->text() == "0" );
                CHECK( groups->item( 0, 2 )->text() == "POST /login" );
                CHECK( groups->item( 1, 0 )->text() == "1" );
                CHECK( groups->item( 1, 1 )->text() == "method" );
                CHECK( groups->item( 1, 2 )->text() == "POST" );
                CHECK( groups->item( 2, 0 )->text() == "2" );
                CHECK( groups->item( 2, 1 )->text().isEmpty() );
                CHECK( groups->item( 2, 2 )->text() == "/login" );
            }
        }

        AND_WHEN( "the cursor is on a line that does not match" )
        {
            putCursorOnLine( lab, 2 );

            THEN( "no group is listed" )
            {
                CHECK( part<QTableWidget>( lab, "captureGroups" )->rowCount() == 0 );
            }
        }
    }

    WHEN( "the case of the pattern differs from the text" )
    {
        typePattern( lab, "get" );

        THEN( "it matches only once Match case is off" )
        {
            CHECK( markedLines( lab ).isEmpty() );
            QSignalSpy evaluated( &lab, &RegexLabWindow::evaluated );
            part<QCheckBox>( lab, "matchCase" )->setChecked( false );
            REQUIRE( waitUiState( [ & ] { return !evaluated.isEmpty(); }, 10'000 ) );
            CHECK( markedLines( lab ) == QList<int>{ 0 } );
        }
    }

    WHEN( "Copy pattern is clicked" )
    {
        typePattern( lab, "POST (/\\w+)" );
        part<QPushButton>( lab, "copyPattern" )->click();

        THEN( "the pattern is on the clipboard" )
        {
            CHECK( QGuiApplication::clipboard()->text() == "POST (/\\w+)" );
        }
    }
}

SCENARIO( "The Regex Lab says what is wrong with an invalid pattern and where", "[ui][regexlab]" )
{
    RegexLabWindow lab( RegexpEngine::Vectorscan );
    lab.show();
    pasteSample( lab, "GET /index.html 200" );

    typePattern( lab, "GET (/index" );

    THEN( "the error and its place in the pattern are shown, and nothing is marked" )
    {
        auto* error = part<QLabel>( lab, "error" );
        CHECK( error->isVisible() );
        CHECK( error->text().contains( "character" ) );
        REQUIRE( lab.result().error.has_value() );
        CHECK( lab.result().error->position >= 4 );
        CHECK( markedTexts( lab ).isEmpty() );
        CHECK( markedLines( lab ).isEmpty() );
    }

    AND_WHEN( "the pattern is corrected" )
    {
        typePattern( lab, "GET (/index)" );

        THEN( "the error goes" )
        {
            CHECK( part<QLabel>( lab, "error" )->isHidden() );
            CHECK( markedLines( lab ) == QList<int>{ 0 } );
        }
    }
}

SCENARIO( "The Regex Lab counts the Log Lines a Search with the same pattern and options selects",
          "[ui][regexlab]" )
{
    OpenCrawler open;
    auto& openLogFile = CrawlerAccess::openLogFile( *open.crawler );

    const auto searched = GENERATE(
        RegularExpressionPattern( "ERROR", true, false, false, false ),
        RegularExpressionPattern( "warn", false, false, false, true ),
        RegularExpressionPattern( R"(id=\d*7 )", true, false, false, false ),
        RegularExpressionPattern( "alice", true, true, false, false ),
        RegularExpressionPattern( "\"ERROR\" and not(\"alice\")", true, false, true, true ),
        RegularExpressionPattern( "\"warn\" or \"id=1\\d\\d \"", false, false, true, false ) );

    GIVEN( "a Search for '" + searched.pattern.toStdString() + "'" )
    {
        openLogFile.requestSearch( searched );
        REQUIRE( waitUiState(
            [ & ] { return openLogFile.searchState().phase == SearchSessionPhase::Complete; } ) );
        const auto& search = *openLogFile.filteredData();

        // The Lab's sample: the first 1000 Log Lines, around the first one.
        CrawlerAccess::selectInMainView( *open.crawler, LineNumber( 0 ) );
        RegexLabWindow lab( testSettingsPolicies().search.regexpEngine );
        lab.setSampleSource( open.source() );
        lab.setSample( RegexLabWindow::Sample::LinesAroundCurrentLine );
        setPattern( lab, searched );

        THEN( "the Lab's matching lines are exactly the Search's Matches" )
        {
            const auto& result = lab.result();
            REQUIRE_FALSE( result.error.has_value() );
            REQUIRE( result.lines.size() == 1000 );
            std::size_t matches = 0;
            for ( std::size_t line = 0; line < result.lines.size(); ++line ) {
                const auto isMatch = search.lineTypeByLine( LineNumber( line ) )
                                         .testFlag( AbstractLogData::LineTypeFlags::Match );
                INFO( "Log Line " << line );
                REQUIRE( result.lines[ line ].isMatch == isMatch );
                matches += isMatch ? 1 : 0;
            }
            CHECK( result.matchingLines == matches );
            CHECK( markedLines( lab ).size() == static_cast<qsizetype>( matches ) );
        }
    }
}

SCENARIO( "A newer pattern supersedes the Regex Lab's evaluation, and closing stops it",
          "[ui][regexlab]" )
{
    auto lab = std::make_unique<RegexLabWindow>( RegexpEngine::QRegularExpression );
    lab->show();
    // Long lines on which a backtracking pattern takes its time.
    QStringList lines;
    for ( int i = 0; i < 200; ++i ) {
        lines.append( QString( 3000, QChar( 'a' ) ) + QStringLiteral( "!" ) );
    }
    lines.append( QStringLiteral( "the end" ) );
    pasteSample( *lab, lines.join( QChar::LineFeed ) );

    WHEN( "a slow pattern is replaced by a quick one while it is evaluated" )
    {
        QSignalSpy evaluated( lab.get(), &RegexLabWindow::evaluated );
        lab->setPattern( RegularExpressionPattern( "(a|aa)+$" ) );
        lab->setPattern( RegularExpressionPattern( "end" ) );
        REQUIRE( waitUiState( [ & ] { return !evaluated.isEmpty(); }, 10'000 ) );
        QTest::qWait( 500 );

        THEN( "only the newer pattern's result is shown" )
        {
            CHECK( evaluated.size() == 1 );
            CHECK( lab->result().matchingLines == 1 );
            CHECK( lab->result().stop == regexlab::Stop::None );
            CHECK( markedTexts( *lab ) == QStringList{ "end" } );
        }
    }

    WHEN( "the slow pattern runs to its time limit" )
    {
        setPattern( *lab, RegularExpressionPattern( "(a|aa)+$" ) );

        THEN( "the Lab says that it stopped, and warns of the pattern" )
        {
            CHECK( lab->result().stop == regexlab::Stop::TimeLimit );
            CHECK( part<QLabel>( *lab, "status" )->text().contains( "stopped" ) );
            CHECK( part<QLabel>( *lab, "warning" )->isVisible() );
        }
    }

    WHEN( "the window is closed while the slow pattern is evaluated" )
    {
        QSignalSpy evaluated( lab.get(), &RegexLabWindow::evaluated );
        lab->setPattern( RegularExpressionPattern( "(a|aa)+$" ) );
        QElapsedTimer closing;
        closing.start();
        lab->close();
        lab.reset();

        THEN( "the evaluation stops with it, well before its time limit" )
        {
            CHECK( closing.elapsed() < RegexLabWindow::bounds().timeLimit.count() );
            CHECK( evaluated.isEmpty() );
        }
    }
}

SCENARIO( "A Regex Lab opened to edit a pattern answers Apply or Cancel once", "[ui][regexlab]" )
{
    auto lab = std::make_unique<RegexLabWindow>( RegexpEngine::Vectorscan );
    lab->offerApply( true );
    lab->show();
    setPattern( *lab, RegularExpressionPattern( "old", false, false, false, true ) );

    QSignalSpy applied( lab.get(), &RegexLabWindow::applied );
    QSignalSpy cancelled( lab.get(), &RegexLabWindow::cancelled );
    auto* buttons = part<QDialogButtonBox>( *lab, "buttons" );
    REQUIRE( buttons->button( QDialogButtonBox::Apply ) != nullptr );

    WHEN( "the pattern is edited and applied" )
    {
        typePattern( *lab, "new" );
        part<QCheckBox>( *lab, "useRegexp" )->setChecked( true );
        buttons->button( QDialogButtonBox::Apply )->click();

        THEN( "the edited pattern with its options is handed back, and the Lab closes" )
        {
            REQUIRE( applied.size() == 1 );
            const auto pattern = applied.front().front().value<RegularExpressionPattern>();
            CHECK( pattern.pattern == "new" );
            CHECK_FALSE( pattern.isCaseSensitive );
            CHECK_FALSE( pattern.isPlainText );
            CHECK( cancelled.isEmpty() );
            CHECK_FALSE( lab->isVisible() );
        }
    }

    WHEN( "the Lab is closed" )
    {
        lab->close();

        THEN( "it is cancelled, once" )
        {
            CHECK( applied.isEmpty() );
            CHECK( cancelled.size() == 1 );
        }
    }
}
