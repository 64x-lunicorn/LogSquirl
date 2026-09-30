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

// A plugin goes to a Log Line of the tab in front and reads the Log Lines
// selected in it (#663). The plugin is a real library built against the
// current header, loaded by the Plugin Host, and the window's side of the
// Plugin UI Port reaches a Crawler Widget with an open Log File.

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QMainWindow>
#include <QMenu>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>

#include <memory>
#include <thread>

#include "abstractlogview.h"
#include "applicationplugins.h"
#include "crawlerwidget.h"
#include "logformatcatalog.h"
#include "logmainview.h"
#include "logsquirl_plugin_api.h"
#include "mainwindow.h"
#include "openlogfile.h"
#include "plugincatalog.h"
#include "pluginhost.h"
#include "pluginuiadapter.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"

struct PluginLogLinesTest {};

template <>
struct AbstractLogView::access_by<PluginLogLinesTest> {
    static void selectWord( AbstractLogView& view, LineNumber line, LineColumn column )
    {
        view.selectWordAtPosition( FilePosition{ line, column } );
    }
};

template <>
struct CrawlerWidget::access_by<PluginLogLinesTest> {
    static OpenLogFile& openLogFile( CrawlerWidget& crawler )
    {
        return *crawler.openLogFile_;
    }

    static LogMainView& textView( CrawlerWidget& crawler )
    {
        return *crawler.logMainView_;
    }

    // What clicking a line of the text view does.
    static void selectLine( CrawlerWidget& crawler, LineNumber line )
    {
        crawler.logMainView_->setFocus();
        crawler.logMainView_->selectAndDisplayLine( line );
    }

    // What double-clicking a word of a line does.
    static void selectWord( CrawlerWidget& crawler, LineNumber line, LineColumn column );

    static void selectAll( CrawlerWidget& crawler )
    {
        crawler.logMainView_->setFocus();
        crawler.logMainView_->selectAll();
    }
};

void CrawlerWidget::access_by<PluginLogLinesTest>::selectWord( CrawlerWidget& crawler,
                                                               LineNumber line, LineColumn column )
{
    crawler.logMainView_->setFocus();
    AbstractLogView::access_by<PluginLogLinesTest>::selectWord( *crawler.logMainView_, line,
                                                                column );
}

namespace {

using CrawlerAccess = CrawlerWidget::access_by<PluginLogLinesTest>;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;
using logsquirl::plugins::PluginLogLineJump;

const auto PluginId = QStringLiteral( "io.github.logsquirl.test.log-lines-plugin" );

constexpr int LogLineCount = 3000;

// Line numbers count from 1, Log Lines from 0: the Log Line of number n says n.
QString logLine( int index )
{
    return QStringLiteral( "line number %1 ä" ).arg( index + 1 );
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

    QTemporaryFile file;
    Session session;
    std::unique_ptr<CrawlerWidget> crawler;
};

/// Writes a plugin.json for the plugin into its own subdirectory of root.
void installPlugin( const QString& root )
{
    const auto pluginDir = QDir( root ).filePath( "log-lines-plugin" );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "Log Lines Plugin",
        "version": "1.0.0",
        "type": "ui",
        "library": "%2",
        "api_version": 1
    })" )
                        .arg( PluginId, QStringLiteral( LOGSQUIRL_TEST_LOG_LINES_PLUGIN_PATH ) )
                        .toUtf8() );
}

/// What get_selected_log_lines handed the plugin.
struct PluginSelection {
    int result = 0;
    const char* text = nullptr;
    size_t length = 0;
    size_t lineCount = 0;

    QString textAsString() const
    {
        return QString::fromUtf8( text, static_cast<qsizetype>( length ) );
    }
};

/// The plugin's exports the tests call, which call the host in turn. The
/// library stays loaded as long as this does.
struct PluginLibrary {
    PluginLibrary()
        : library( QStringLiteral( LOGSQUIRL_TEST_LOG_LINES_PLUGIN_PATH ) )
    {
        REQUIRE( library.load() );
    }
    ~PluginLibrary()
    {
        library.unload();
    }
    PluginLibrary( const PluginLibrary& ) = delete;
    PluginLibrary& operator=( const PluginLibrary& ) = delete;

    size_t hostApiSize()
    {
        using Fn = size_t ( * )();
        return reinterpret_cast<Fn>(
            library.resolve( "logsquirl_log_lines_plugin_host_api_size" ) )();
    }

    int goTo( uint64_t lineNumber )
    {
        using Fn = int ( * )( uint64_t );
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_log_lines_plugin_go_to" ) )(
            lineNumber );
    }

    PluginSelection readSelected()
    {
        using Fn = int ( * )( const char**, size_t*, size_t* );
        PluginSelection selection;
        selection.result
            = reinterpret_cast<Fn>( library.resolve( "logsquirl_log_lines_plugin_read_selected" ) )(
                &selection.text, &selection.length, &selection.lineCount );
        return selection;
    }

    QLibrary library;
};

/// The action of the menu with the given text, or nullptr.
QAction* menuAction( const QMenu& menu, const QString& text )
{
    for ( auto* action : menu.actions() ) {
        if ( action->text() == text ) {
            return action;
        }
    }
    return nullptr;
}

} // namespace

SCENARIO( "A plugin goes to a Log Line and reads the selected Log Lines of the tab in front",
          "[ui][plugins][pluginloglines]" )
{
    GIVEN( "A window whose tab in front shows a Log File, and a plugin built against the current "
           "header, loaded" )
    {
        OpenCrawler open;
        auto& crawler = *open.crawler;
        auto& textView = CrawlerAccess::textView( crawler );

        QTemporaryDir pluginRoot;
        REQUIRE( pluginRoot.isValid() );
        installPlugin( pluginRoot.path() );
        PluginCatalog catalog;
        catalog.discoverPluginsIn( pluginRoot.path() );

        PluginLibrary plugin;

        QMainWindow window;
        QMenu pluginsMenu;
        auto* separator = pluginsMenu.addSeparator();
        QTabWidget sidebarTabs;
        PluginUiAdapter adapter( window, pluginsMenu, separator, sidebarTabs );
        CrawlerWidget* tabInFront = &crawler;
        adapter.setTabInFront( [ &tabInFront ]() { return tabInFront; } );

        PluginHost host( catalog );
        host.setUiPort( &adapter );
        QStringList notifications;
        QObject::connect(
            &host, &PluginHost::notificationRequested,
            [ &notifications ]( const QString& message ) { notifications.append( message ); } );
        REQUIRE( host.loadPlugin( PluginId ).isEmpty() );

        THEN( "The plugin sees both functions in this host's table, and offers its actions" )
        {
            REQUIRE( plugin.hostApiSize() == sizeof( LogSquirlHostApi ) );
            REQUIRE( menuAction( pluginsMenu, QStringLiteral( "Show Selection" ) ) != nullptr );
            REQUIRE( menuAction( pluginsMenu, QStringLiteral( "Go to Line 10" ) ) != nullptr );
        }

        WHEN( "The plugin goes to line number 2000" )
        {
            const auto result = plugin.goTo( 2000 );

            THEN( "That Log Line is selected and scrolled into view in the Text View" )
            {
                REQUIRE( result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( textView.selectedLogLines()
                         == logsquirl::vector<LineNumber>{ 1999_lnum } );
                REQUIRE( textView.getSelectedText() == logLine( 1999 ) );
                REQUIRE( textView.getTopLine() <= 1999_lnum );
                REQUIRE( textView.getTopLine().get() + 20 > 1999 );
            }
        }

        WHEN( "The plugin goes to the last line number" )
        {
            const auto result = plugin.goTo( LogLineCount );

            THEN( "The last Log Line is selected" )
            {
                REQUIRE( result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( textView.getSelectedText() == logLine( LogLineCount - 1 ) );
            }
        }

        WHEN( "The plugin goes to a line number the Log File does not have, or to 0" )
        {
            CrawlerAccess::selectLine( crawler, 5_lnum );
            const auto pastTheEnd = plugin.goTo( LogLineCount + 1 );
            const auto zero = plugin.goTo( 0 );

            THEN( "It is told the line is out of range, and the selection stays" )
            {
                REQUIRE( pastTheEnd == LOGSQUIRL_LOG_LINES_OUT_OF_RANGE );
                REQUIRE( zero == LOGSQUIRL_LOG_LINES_OUT_OF_RANGE );
                REQUIRE( textView.selectedLogLines() == logsquirl::vector<LineNumber>{ 5_lnum } );
            }
        }

        WHEN( "The plugin's menu action goes to line 10" )
        {
            auto* goTo = menuAction( pluginsMenu, QStringLiteral( "Go to Line 10" ) );
            REQUIRE( goTo != nullptr );
            goTo->trigger();

            THEN( "The Log Line of number 10 is selected" )
            {
                REQUIRE( textView.getSelectedText() == logLine( 9 ) );
            }
        }

        WHEN( "Nothing is selected and the plugin reads the selected Log Lines" )
        {
            const auto selection = plugin.readSelected();

            THEN( "It is told there is no selection, and gets no text" )
            {
                REQUIRE( selection.result == LOGSQUIRL_LOG_LINES_NO_SELECTION );
                REQUIRE( selection.text == nullptr );
                REQUIRE( selection.length == 0 );
                REQUIRE( selection.lineCount == 0 );
            }
        }

        WHEN( "One Log Line is selected and the plugin reads the selected Log Lines" )
        {
            CrawlerAccess::selectLine( crawler, 41_lnum );
            const auto selection = plugin.readSelected();

            THEN( "It gets that Log Line's text in UTF-8" )
            {
                REQUIRE( selection.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( selection.textAsString() == logLine( 41 ) );
                REQUIRE( selection.text[ selection.length ] == '\0' );
                REQUIRE( selection.lineCount == 1 );
            }
        }

        WHEN( "Characters within a Log Line are selected and the plugin reads the selected "
              "Log Lines" )
        {
            CrawlerAccess::selectWord( crawler, 7_lnum, 7_lcol );
            REQUIRE( textView.getSelectedText() == QStringLiteral( "number" ) );
            const auto selection = plugin.readSelected();

            THEN( "It gets the whole Log Line" )
            {
                REQUIRE( selection.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( selection.textAsString() == logLine( 7 ) );
            }
        }

        WHEN( "More Log Lines are selected than the plugin gets, and it reads them" )
        {
            CrawlerAccess::selectAll( crawler );
            const auto selection = plugin.readSelected();

            THEN( "It gets the first ones, joined by line feeds, and is told there are more" )
            {
                REQUIRE( selection.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( selection.lineCount == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                const auto lines = selection.textAsString().split( QChar::LineFeed );
                REQUIRE( lines.size() == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                REQUIRE( lines.front() == logLine( 0 ) );
                REQUIRE( lines.back() == logLine( LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES - 1 ) );
            }
        }

        WHEN( "The plugin's menu action shows the selection" )
        {
            CrawlerAccess::selectLine( crawler, 2_lnum );
            auto* show = menuAction( pluginsMenu, QStringLiteral( "Show Selection" ) );
            REQUIRE( show != nullptr );
            show->trigger();

            THEN( "It shows the selected Log Line" )
            {
                REQUIRE( notifications == QStringList{ logLine( 2 ) } );
            }
        }

        WHEN( "The plugin calls both off the UI thread" )
        {
            CrawlerAccess::selectLine( crawler, 5_lnum );
            int goToResult = 0;
            PluginSelection selection;
            std::thread( [ & ] {
                goToResult = plugin.goTo( 100 );
                selection = plugin.readSelected();
            } ).join();

            THEN( "Neither does anything, and both say why" )
            {
                REQUIRE( goToResult == LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD );
                REQUIRE( selection.result == LOGSQUIRL_LOG_LINES_NOT_ON_UI_THREAD );
                REQUIRE( selection.text == nullptr );
                REQUIRE( textView.selectedLogLines() == logsquirl::vector<LineNumber>{ 5_lnum } );
            }
        }

        WHEN( "The tab in front shows no Log File" )
        {
            tabInFront = nullptr;

            THEN( "The plugin is told so by both" )
            {
                REQUIRE( plugin.goTo( 1 ) == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( plugin.readSelected().result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
            }
        }

        host.unloadAll();
    }
}

SCENARIO( "A plugin reaches the tab in front of the most recently active window",
          "[ui][plugins][pluginloglines]" )
{
    auto session
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    auto plugins = std::make_shared<logsquirl::plugins::ApplicationPlugins>();
    auto window = std::make_unique<MainWindow>( WindowSession{ session, "Main", 0 }, plugins );
    window->show();

    GIVEN( "no open Log File" )
    {
        THEN( "there is no Log Line to go to and none to read" )
        {
            REQUIRE( plugins->uiPort().goToLogLine( 0 ) == PluginLogLineJump::NoLogFile );
            REQUIRE_FALSE( plugins->uiPort().selectedLogLines( 10 ).has_value() );
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

        WHEN( "a plugin goes to a Log Line of it and reads the selection" )
        {
            const auto jump = plugins->uiPort().goToLogLine( 20 );
            const auto selected = plugins->uiPort().selectedLogLines( 10 );

            THEN( "that tab's Log Line is selected, and is what is read" )
            {
                REQUIRE( jump == PluginLogLineJump::Shown );
                REQUIRE( CrawlerAccess::textView( *crawler ).getSelectedText() == logLine( 20 ) );
                REQUIRE( selected == QStringList{ logLine( 20 ) } );
            }
        }

        WHEN( "a plugin goes to a Log Line past its end" )
        {
            THEN( "it is out of range" )
            {
                REQUIRE( plugins->uiPort().goToLogLine( 50 ) == PluginLogLineJump::OutOfRange );
            }
        }
    }

    window.reset();
    QTest::qWait( 50 );
}
