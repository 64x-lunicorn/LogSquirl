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

// A plugin sets, removes and reads the Marks of an open Log File by its path
// (#866): it sets and removes them from any thread, and they are ordinary
// Marks, which the bullet margin shows and Next Mark visits. The plugin is a
// real library built against the current header, loaded by the window's
// Plugin Host, and the window's tabs are real Crawler Widgets reading real
// Log Files.

#include "abstractlogview.h"
#include "applicationplugins.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "fake_file_watch.h"
#include "logformatcatalog.h"
#include "logmainview.h"
#include "logsquirl_plugin_api.h"
#include "mainwindow.h"
#include "openlogfile.h"
#include "session.h"
#include "shortcuts.h"
#include "shown_widget.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QShortcut>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

struct PluginMarksTest {};

template <>
struct CrawlerWidget::access_by<PluginMarksTest> {
    static LinesCount lineCount( const CrawlerWidget& crawler )
    {
        return crawler.openLogFile_->logData()->getNbLine();
    }

    // Whether the bullet margin shows the Log Line, numbered from 0, as marked.
    static bool isMarked( const CrawlerWidget& crawler, LineNumber::UnderlyingType line )
    {
        return crawler.openLogFile_->lineType( LineNumber( line ) )
            .testFlag( AbstractLogData::LineTypeFlags::Mark );
    }

    // Marks Log Lines, numbered from 0, as the user does from the Text View.
    static void markByHand( CrawlerWidget& crawler, LineNumber::UnderlyingType line )
    {
        crawler.markLinesFromMain( { LineNumber( line ) } );
    }

    static AbstractLogView& textView( const CrawlerWidget& crawler )
    {
        return *crawler.logMainView_;
    }
};

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

namespace {

using CrawlerAccess = CrawlerWidget::access_by<PluginMarksTest>;

const auto PluginId = QStringLiteral( "io.github.logsquirl.test.marks-plugin" );

constexpr int LogLineCount = 100;

/// The plugin config directory the host creates goes to a test location, not the user's.
struct StandardPathsInTestMode {
    StandardPathsInTestMode()
    {
        QStandardPaths::setTestModeEnabled( true );
    }
    ~StandardPathsInTestMode()
    {
        QStandardPaths::setTestModeEnabled( false );
    }
    StandardPathsInTestMode( const StandardPathsInTestMode& ) = delete;
    StandardPathsInTestMode& operator=( const StandardPathsInTestMode& ) = delete;
};

/// Writes a plugin.json for the plugin into its own subdirectory of root.
void installPlugin( const QString& root )
{
    const auto pluginDir = QDir( root ).filePath( "marks-plugin" );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "Marks Plugin",
        "version": "1.0.0",
        "type": "ui",
        "library": "%2",
        "api_version": 1
    })" )
                        .arg( PluginId, QStringLiteral( LOGSQUIRL_TEST_MARKS_PLUGIN_PATH ) )
                        .toUtf8() );
}

/// What get_marks answered the plugin, the numbers copied as they came.
struct MarksRead {
    int result = 0;
    std::vector<uint64_t> lines;
};

/// The plugin's exports the tests call, which call the host in turn, on the
/// thread the test calls them on. The library stays loaded as long as this
/// does.
struct PluginLibrary {
    PluginLibrary()
        : library( QStringLiteral( LOGSQUIRL_TEST_MARKS_PLUGIN_PATH ) )
    {
        REQUIRE( library.load() );
        addFn = reinterpret_cast<ChangeFn>( library.resolve( "logsquirl_marks_plugin_add" ) );
        removeFn = reinterpret_cast<ChangeFn>( library.resolve( "logsquirl_marks_plugin_remove" ) );
        getFn = reinterpret_cast<GetFn>( library.resolve( "logsquirl_marks_plugin_get" ) );
        REQUIRE( addFn != nullptr );
        REQUIRE( removeFn != nullptr );
        REQUIRE( getFn != nullptr );
    }
    ~PluginLibrary()
    {
        library.unload();
    }
    PluginLibrary( const PluginLibrary& ) = delete;
    PluginLibrary& operator=( const PluginLibrary& ) = delete;

    int add( const QString& path, const std::vector<uint64_t>& lines ) const
    {
        return addFn( path.toUtf8().constData(), lines.data(), lines.size() );
    }

    int remove( const QString& path, const std::vector<uint64_t>& lines ) const
    {
        return removeFn( path.toUtf8().constData(), lines.data(), lines.size() );
    }

    MarksRead get( const QString& path ) const
    {
        const uint64_t* lines = nullptr;
        size_t count = 0;
        MarksRead read;
        read.result = getFn( path.toUtf8().constData(), &lines, &count );
        if ( lines != nullptr ) {
            read.lines.assign( lines, lines + count );
        }
        return read;
    }

    using ChangeFn = int ( * )( const char*, const uint64_t*, size_t );
    using GetFn = int ( * )( const char*, const uint64_t**, size_t* );

    QLibrary library;
    ChangeFn addFn = nullptr;
    ChangeFn removeFn = nullptr;
    GetFn getFn = nullptr;
};

/// A shown window whose plugin has loaded, and a folder of Log Files.
struct MarksWindow {
    MarksWindow()
    {
        REQUIRE( pluginRoot.isValid() );
        REQUIRE( fileDir.isValid() );
        installPlugin( pluginRoot.path() );

        plugins = std::make_shared<ApplicationPlugins>(
            [ this ]( PluginCatalog& catalog, PluginHost& host ) {
                catalog.discoverPlugins( { pluginRoot.path() } );
                loadErrors
                    = host.autoLoadPlugins( { .autoLoad = true, .enabled = { PluginId } } ).errors;
            } );
        window = std::make_unique<MainWindow>(
            WindowSession{ std::make_shared<Session>( testSettingsPolicies(),
                                                      std::make_shared<LogFormatCatalog>(),
                                                      fileWatch ),
                           "Main", 0 },
            plugins );
        showUntilExposed( *window );
        REQUIRE( waitUiState( [ this ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loadErrors.isEmpty() );
        tabs = window->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabs != nullptr );
    }

    ~MarksWindow()
    {
        window.reset();
        plugins.reset();
        QTest::qWait( 50 );
    }

    MarksWindow( const MarksWindow& ) = delete;
    MarksWindow& operator=( const MarksWindow& ) = delete;

    /// Writes count numbered Log Lines into a new file named name, and returns its path.
    QString writeFile( const QString& name, int count )
    {
        const auto path = fileDir.filePath( name );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        for ( int number = 1; number <= count; ++number ) {
            file.write( QStringLiteral( "line number %1\n" ).arg( number ).toUtf8() );
        }
        return path;
    }

    /// Opens the Log File in a tab of its own, in front, once it has loaded.
    CrawlerWidget& open( const QString& path, LinesCount lines )
    {
        window->loadFileNonInteractive( path );
        CrawlerWidget* crawler = nullptr;
        REQUIRE( waitUiState(
            [ & ] {
                crawler = qobject_cast<CrawlerWidget*>( tabs->currentWidget() );
                return crawler != nullptr
                       && crawler->state().loadStatus == LoadingStatus::Successful
                       && CrawlerAccess::lineCount( *crawler ) == lines;
            },
            10'000 ) );
        QTest::qWait( 50 );
        return *crawler;
    }

    const StandardPathsInTestMode testPaths{};
    QTemporaryDir pluginRoot;
    QTemporaryDir fileDir;
    QStringList loadErrors;
    std::shared_ptr<FakeFileWatch> fileWatch = std::make_shared<FakeFileWatch>();
    std::shared_ptr<ApplicationPlugins> plugins;
    std::unique_ptr<MainWindow> window;
    TabbedCrawlerWidget* tabs = nullptr;
};

// No Log Line selected: the view did not move.
constexpr int64_t NoLine = -1;

/// Selects from in the view, goes to the next Mark with its shortcut, and
/// returns the Log Line it selected, or NoLine.
int64_t nextMark( AbstractLogView& view, LineNumber from )
{
    view.selectAndDisplayLine( from );

    const auto keys = ShortcutAction::shortcutKeys( ShortcutAction::LogViewNextMark,
                                                    Configuration::get().shortcuts() );
    REQUIRE_FALSE( keys.isEmpty() );
    QShortcut* shortcut = nullptr;
    for ( auto* candidate : view.findChildren<QShortcut*>() ) {
        if ( candidate->key() == keys.first() ) {
            shortcut = candidate;
        }
    }
    REQUIRE( shortcut != nullptr );

    QSignalSpy selected( &view, &AbstractLogView::newSelection );
    Q_EMIT shortcut->activated();
    if ( selected.isEmpty() ) {
        return NoLine;
    }
    return static_cast<int64_t>( qvariant_cast<LineNumber>( selected.last().at( 0 ) ).get() );
}

} // namespace

SCENARIO( "A plugin sets, removes and reads the Marks of an open Log File",
          "[ui][plugins][pluginmarks]" )
{
    GIVEN( "A window with a plugin built against the current header, and a Log File of 100 Log "
           "Lines open" )
    {
        MarksWindow window;
        PluginLibrary plugin;
        const auto path = window.writeFile( QStringLiteral( "app.log" ), LogLineCount );
        auto& crawler = window.open( path, LinesCount( LogLineCount ) );

        WHEN( "the plugin sets Marks from a thread of its own, one past the end" )
        {
            int result = -100;
            std::thread( [ & ] { result = plugin.add( path, { 40, 10, 101 } ); } ).join();

            THEN( "it is told they are set, the bullet margin shows them once the event loop "
                  "turns, without the one past the end, and Next Mark visits them" )
            {
                REQUIRE( result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE(
                    waitUiState( [ & ] { return CrawlerAccess::isMarked( crawler, 39 ); }, 5000 ) );
                REQUIRE( CrawlerAccess::isMarked( crawler, 9 ) );
                REQUIRE( crawler.marks()
                         == QList<LineNumber>{ LineNumber( 9 ), LineNumber( 39 ) } );

                auto& view = CrawlerAccess::textView( crawler );
                REQUIRE( nextMark( view, LineNumber( 0 ) ) == 9 );
                REQUIRE( nextMark( view, LineNumber( 9 ) ) == 39 );
                REQUIRE( nextMark( view, LineNumber( 39 ) ) == NoLine );
            }

            AND_WHEN( "the user marks a Log Line by hand, and the plugin reads the Marks" )
            {
                REQUIRE(
                    waitUiState( [ & ] { return CrawlerAccess::isMarked( crawler, 39 ); }, 5000 ) );
                CrawlerAccess::markByHand( crawler, 24 );
                const auto read = plugin.get( path );

                THEN( "it gets the user's and its own, in the order of the Log File" )
                {
                    REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                    REQUIRE( read.lines == std::vector<uint64_t>{ 10, 25, 40 } );
                }

                AND_WHEN( "the plugin removes the user's Mark and one of its own, from a thread "
                          "of its own" )
                {
                    std::thread( [ & ] { result = plugin.remove( path, { 25, 10, 60 } ); } ).join();

                    THEN( "only the other one is left, once the event loop turns" )
                    {
                        REQUIRE( result == LOGSQUIRL_LOG_LINES_OK );
                        REQUIRE( waitUiState(
                            [ & ] { return !CrawlerAccess::isMarked( crawler, 9 ); }, 5000 ) );
                        REQUIRE_FALSE( CrawlerAccess::isMarked( crawler, 24 ) );
                        REQUIRE( plugin.get( path ).lines == std::vector<uint64_t>{ 40 } );
                        REQUIRE( crawler.marks() == QList<LineNumber>{ LineNumber( 39 ) } );
                    }
                }
            }
        }

        WHEN( "the plugin sets Marks on the UI thread, while another Log File is in front" )
        {
            window.open( window.writeFile( QStringLiteral( "front.log" ), 3 ), 3_lcount );
            const auto result = plugin.add( path, { 2, 3 } );

            THEN( "the tab behind marks them at once, and the one in front does not" )
            {
                REQUIRE( result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( plugin.get( path ).lines == std::vector<uint64_t>{ 2, 3 } );
                REQUIRE( crawler.marks() == QList<LineNumber>{ LineNumber( 1 ), LineNumber( 2 ) } );
                const auto* front = qobject_cast<CrawlerWidget*>( window.tabs->currentWidget() );
                REQUIRE( front != nullptr );
                REQUIRE( front->marks().isEmpty() );
            }
        }

        WHEN( "the plugin names a Log File no tab shows, or one whose tab closed" )
        {
            const auto nowhere = window.fileDir.filePath( "nowhere.log" );
            const auto addedNowhere = plugin.add( nowhere, { 1 } );
            const auto readNowhere = plugin.get( nowhere );
            Q_EMIT window.tabs->tabCloseRequested( window.tabs->indexOf( &crawler ) );
            const auto addedClosed = plugin.add( path, { 1 } );
            const auto removedClosed = plugin.remove( path, { 1 } );
            const auto readClosed = plugin.get( path );

            THEN( "it is told there is no Log File" )
            {
                REQUIRE( addedNowhere == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( readNowhere.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( addedClosed == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( removedClosed == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( readClosed.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
            }
        }
    }
}
