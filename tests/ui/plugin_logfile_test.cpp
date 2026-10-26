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

// A plugin reads the Log Lines of an open Log File by its path, on any
// thread, and hears when they change (#858). The plugin is a real library
// built against the current header, loaded by the window's Plugin Host, and
// the window's tabs are real Crawler Widgets reading real Log Files; the
// Log Files change on disk through a File Watch Port the test drives.

#include "applicationplugins.h"
#include "crawlerwidget.h"
#include "fake_file_watch.h"
#include "logformatcatalog.h"
#include "logsquirl_plugin_api.h"
#include "mainwindow.h"
#include "modal_answers.h"
#include "openlogfile.h"
#include "session.h"
#include "shown_widget.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"
#include "textencoding.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMessageBox>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <catch2/catch_test_macros.hpp>
#include <kcompressiondevice.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

struct PluginLogFileTest {};

template <>
struct CrawlerWidget::access_by<PluginLogFileTest> {
    static LinesCount lineCount( const CrawlerWidget& crawler )
    {
        return crawler.openLogFile_->logData()->getNbLine();
    }

    // The path the tab's Log File was opened under: a temporary file for one
    // decompressed from an archive.
    static QString pathOf( const CrawlerWidget& crawler )
    {
        return crawler.openLogFile_->fileName();
    }

    // The Log Lines first up to last, numbered as the views number them, as
    // the views read them, joined as get_log_lines joins them.
    static QString textOf( const CrawlerWidget& crawler, int first, int last )
    {
        QStringList lines;
        for ( int number = first; number <= last; ++number ) {
            lines.append( crawler.openLogFile_->logData()->getLineString(
                LineNumber( static_cast<LineNumber::UnderlyingType>( number - 1 ) ) ) );
        }
        return lines.join( '\n' );
    }
};

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

namespace {

using CrawlerAccess = CrawlerWidget::access_by<PluginLogFileTest>;

const auto PluginId = QStringLiteral( "io.github.logsquirl.test.log-file-plugin" );

constexpr int LogLineCount = 3000;

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
    const auto pluginDir = QDir( root ).filePath( "log-file-plugin" );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "Log File Plugin",
        "version": "1.0.0",
        "type": "ui",
        "library": "%2",
        "api_version": 1
    })" )
                        .arg( PluginId, QStringLiteral( LOGSQUIRL_TEST_LOG_FILE_PLUGIN_PATH ) )
                        .toUtf8() );
}

/// What get_log_lines answered the plugin, the text copied as it came.
struct LogLinesRead {
    int result = 0;
    QString text;
    size_t lineCount = 0;
    bool hasText = false;
};

/// A change of Log Lines the plugin was told.
struct LogLinesChange {
    QString path;
    uint64_t lineCount = 0;
    int change = -1;
};

/// The plugin's exports the tests call, which call the host in turn, on the
/// thread the test calls them on. The library stays loaded as long as this
/// does.
struct PluginLibrary {
    PluginLibrary()
        : library( QStringLiteral( LOGSQUIRL_TEST_LOG_FILE_PLUGIN_PATH ) )
    {
        REQUIRE( library.load() );
        countFn = reinterpret_cast<CountFn>( library.resolve( "logsquirl_logfile_plugin_count" ) );
        readFn = reinterpret_cast<ReadFn>( library.resolve( "logsquirl_logfile_plugin_read" ) );
        REQUIRE( countFn != nullptr );
        REQUIRE( readFn != nullptr );
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
            library.resolve( "logsquirl_logfile_plugin_host_api_size" ) )();
    }

    std::pair<int, uint64_t> count( const QString& path ) const
    {
        const auto pathUtf8 = path.toUtf8();
        uint64_t counted = 0;
        const auto result = countFn( pathUtf8.constData(), &counted );
        return { result, counted };
    }

    LogLinesRead read( const QString& path, uint64_t firstLine, size_t maxLines ) const
    {
        const auto pathUtf8 = path.toUtf8();
        const char* text = nullptr;
        size_t length = 0;
        LogLinesRead read;
        read.result
            = readFn( pathUtf8.constData(), firstLine, maxLines, &text, &length, &read.lineCount );
        read.hasText = text != nullptr;
        if ( text != nullptr ) {
            read.text = QString::fromUtf8( text, static_cast<qsizetype>( length ) );
        }
        return read;
    }

    /// The changes the plugin's log lines callback was told, in order.
    std::vector<LogLinesChange> changes()
    {
        using CountChangesFn = size_t ( * )();
        using ChangeFn = void ( * )( size_t, const char**, uint64_t*, int* );
        const auto changeCount = reinterpret_cast<CountChangesFn>(
            library.resolve( "logsquirl_logfile_plugin_change_count" ) )();
        auto* change
            = reinterpret_cast<ChangeFn>( library.resolve( "logsquirl_logfile_plugin_change" ) );
        std::vector<LogLinesChange> told;
        for ( size_t i = 0; i < changeCount; ++i ) {
            const char* path = nullptr;
            LogLinesChange one;
            change( i, &path, &one.lineCount, &one.change );
            one.path = QString::fromUtf8( path );
            told.push_back( one );
        }
        return told;
    }

    /// Whether the plugin was told, after the first `after` changes, that the
    /// Log File at path changed so and now has lineCount Log Lines.
    bool toldOf( size_t after, const QString& path, uint64_t lineCount, int change )
    {
        const auto told = changes();
        for ( auto i = after; i < told.size(); ++i ) {
            if ( QFileInfo( told[ i ].path ) == QFileInfo( path )
                 && told[ i ].lineCount == lineCount && told[ i ].change == change ) {
                return true;
            }
        }
        return false;
    }

    using CountFn = int ( * )( const char*, uint64_t* );
    using ReadFn = int ( * )( const char*, uint64_t, size_t, const char**, size_t*, size_t* );

    QLibrary library;
    CountFn countFn = nullptr;
    ReadFn readFn = nullptr;
};

/// Answers the decompression's progress dialog by waiting for it to close.
std::function<void( QDialog& )> answerProgress()
{
    return
        []( QDialog& modal ) { waitUiState( [ &modal ] { return !modal.isVisible(); }, 30'000 ); };
}

/// A shown window whose plugin has loaded, a folder of Log Files and the
/// File Watch Port their changes are reported through.
struct LogFileWindow {
    LogFileWindow()
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
        session = std::make_shared<Session>( policies, std::make_shared<LogFormatCatalog>(),
                                             fileWatch );
        window = std::make_unique<MainWindow>( WindowSession{ session, "Main", 0 }, plugins );
        showUntilExposed( *window );
        REQUIRE( waitUiState( [ this ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loadErrors.isEmpty() );
        tabs = window->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabs != nullptr );
    }

    ~LogFileWindow()
    {
        window.reset();
        plugins.reset();
        QTest::qWait( 50 );
    }

    LogFileWindow( const LogFileWindow& ) = delete;
    LogFileWindow& operator=( const LogFileWindow& ) = delete;

    /// Writes bytes into a new file named name in the folder, and returns its path.
    QString writeFile( const QString& name, const QByteArray& bytes )
    {
        const auto path = fileDir.filePath( name );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( bytes ) == bytes.size() );
        return path;
    }

    /// The tab in front once it has loaded lines Log Lines.
    CrawlerWidget& loadedInFront( LinesCount lines )
    {
        CrawlerWidget* crawler = nullptr;
        REQUIRE( waitUiState(
            [ & ] {
                crawler = qobject_cast<CrawlerWidget*>( tabs->currentWidget() );
                return crawler != nullptr
                       && crawler->state().loadStatus == LoadingStatus::Successful
                       && CrawlerAccess::lineCount( *crawler ) == lines;
            },
            10'000 ) );
        // The Encoding is settled, and the plugin told, once the tab heard
        // the load finished.
        QTest::qWait( 50 );
        return *crawler;
    }

    /// Opens the Log File in a tab of its own, in front, once it has loaded.
    CrawlerWidget& open( const QString& path, LinesCount lines )
    {
        window->loadFileNonInteractive( path );
        return loadedInFront( lines );
    }

    const StandardPathsInTestMode testPaths{};
    QTemporaryDir pluginRoot;
    QTemporaryDir fileDir;
    QStringList loadErrors;
    std::shared_ptr<FakeFileWatch> fileWatch = std::make_shared<FakeFileWatch>();
    // As the window's Session applies them; a test changes and applies them again.
    SettingsPolicies policies = testSettingsPolicies();
    std::shared_ptr<Session> session;
    std::shared_ptr<ApplicationPlugins> plugins;
    std::unique_ptr<MainWindow> window;
    TabbedCrawlerWidget* tabs = nullptr;
};

/// The Log Line of number n says n, with a character UTF-8 takes two bytes for.
QByteArray numberedLogLines( int from, int count )
{
    QByteArray bytes;
    for ( int number = from; number < from + count; ++number ) {
        bytes += QStringLiteral( "line number %1 ä\n" ).arg( number ).toUtf8();
    }
    return bytes;
}

/// Text in UTF-16LE with its byte order mark.
QByteArray utf16LeWithBom( const QString& text )
{
    return QByteArray( "\xFF\xFE", 2 ) + TextEncoding::forName( "UTF-16LE" )->fromUnicode( text );
}

} // namespace

SCENARIO( "A plugin reads the Log Lines of an open Log File by path, on any thread",
          "[ui][plugins][pluginlogfile]" )
{
    GIVEN( "A window with a plugin built against the current header, loaded" )
    {
        LogFileWindow window;
        PluginLibrary plugin;

        THEN( "the plugin sees the functions in this host's table" )
        {
            REQUIRE( plugin.hostApiSize() == sizeof( LogSquirlHostApi ) );
        }

        WHEN( "no Log File is open" )
        {
            const auto nowhere = window.fileDir.filePath( "nowhere.log" );

            THEN( "the plugin is told there is none" )
            {
                REQUIRE( plugin.count( nowhere ).first == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( plugin.read( nowhere, 1, 1 ).result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
            }
        }

        AND_GIVEN( "a UTF-8 Log File of 3000 Log Lines open, and another Log File in front of it" )
        {
            const auto path = window.writeFile( QStringLiteral( "app.log" ),
                                                numberedLogLines( 1, LogLineCount ) );
            auto& crawler = window.open( path, LinesCount( LogLineCount ) );
            window.open( window.writeFile( QStringLiteral( "front.log" ), "in front\n" ),
                         1_lcount );

            THEN( "the plugin counts its Log Lines and reads ranges of them as the Text View "
                  "shows them" )
            {
                REQUIRE( plugin.count( path )
                         == std::pair<int, uint64_t>{ LOGSQUIRL_LOG_LINES_OK, LogLineCount } );

                const auto first = plugin.read( path, 1, 3 );
                REQUIRE( first.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( first.lineCount == 3 );
                REQUIRE( first.text == CrawlerAccess::textOf( crawler, 1, 3 ) );
                REQUIRE( first.text.startsWith( QStringLiteral( "line number 1 ä\n" ) ) );

                const auto middle = plugin.read( path, 1500, 4 );
                REQUIRE( middle.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( middle.text == CrawlerAccess::textOf( crawler, 1500, 1503 ) );

                const auto last = plugin.read( path, 2999, 10 );
                REQUIRE( last.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( last.lineCount == 2 );
                REQUIRE( last.text == CrawlerAccess::textOf( crawler, 2999, 3000 ) );

                const auto pastEnd = plugin.read( path, 3001, 1 );
                REQUIRE( pastEnd.result == LOGSQUIRL_LOG_LINES_OUT_OF_RANGE );
                REQUIRE_FALSE( pastEnd.hasText );
            }

            THEN( "a range past the bounds of one call reads the first 1000, whole, truncated" )
            {
                const auto many = plugin.read( path, 1, 5000 );
                REQUIRE( many.result == LOGSQUIRL_LOG_LINES_TRUNCATED );
                REQUIRE( many.lineCount == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                REQUIRE( many.text == CrawlerAccess::textOf( crawler, 1, 1000 ) );
            }

            WHEN( "the plugin reads on a thread of its own while the UI thread is busy" )
            {
                // The UI thread waits for the thread and turns no event loop.
                LogLinesRead read;
                std::pair<int, uint64_t> counted;
                std::thread( [ & ] {
                    counted = plugin.count( path );
                    read = plugin.read( path, 2000, 2 );
                } ).join();

                THEN( "it reads all the same" )
                {
                    REQUIRE( counted
                             == std::pair<int, uint64_t>{ LOGSQUIRL_LOG_LINES_OK, LogLineCount } );
                    REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                    REQUIRE( read.text == CrawlerAccess::textOf( crawler, 2000, 2001 ) );
                }
            }

            WHEN( "the tab closes while a thread of the plugin keeps reading it" )
            {
                std::atomic<int> reads{ 0 };
                std::atomic<bool> done{ false };
                std::vector<int> results;
                std::thread reader( [ & ] {
                    const auto until
                        = std::chrono::steady_clock::now() + std::chrono::seconds( 20 );
                    while ( std::chrono::steady_clock::now() < until ) {
                        const auto read = plugin.read( path, 1, 1000 );
                        results.push_back( read.result );
                        reads.fetch_add( 1 );
                        if ( read.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE ) {
                            break;
                        }
                    }
                    done.store( true );
                } );
                REQUIRE( waitUiState( [ & ] { return reads.load() > 2; }, 10'000 ) );
                Q_EMIT window.tabs->tabCloseRequested( window.tabs->indexOf( &crawler ) );
                // The tab's widget goes from the event loop, while the reads go on.
                const auto finished = waitUiState( [ & ] { return done.load(); }, 30'000 );
                reader.join();

                THEN( "the reads went on until there was no Log File, and none failed otherwise" )
                {
                    REQUIRE( finished );
                    REQUIRE( results.back() == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                    results.pop_back();
                    REQUIRE( std::ranges::all_of(
                        results, []( int result ) { return result == LOGSQUIRL_LOG_LINES_OK; } ) );
                    REQUIRE( plugin.count( path ).first == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                }
            }
        }

        AND_GIVEN( "a UTF-16LE Log File with a byte order mark open" )
        {
            const auto path
                = window.writeFile( QStringLiteral( "utf16.log" ),
                                    utf16LeWithBom( QStringLiteral( "first line ä\nVIN: X\n" ) ) );
            auto& crawler = window.open( path, 2_lcount );

            THEN( "the plugin reads its text decoded, as the Text View shows it" )
            {
                const auto read = plugin.read( path, 1, 10 );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.text == QStringLiteral( "first line ä\nVIN: X" ) );
                REQUIRE( read.text == CrawlerAccess::textOf( crawler, 1, 2 ) );
            }
        }

        AND_GIVEN( "a compressed Log File, opened decompressed" )
        {
            const auto archivePath = window.fileDir.filePath( "archived.log.gz" );
            {
                KCompressionDevice archive( archivePath, KCompressionDevice::GZip );
                REQUIRE( archive.open( QIODevice::WriteOnly ) );
                REQUIRE( archive.write( numberedLogLines( 1, 5 ) ) > 0 );
            }
            ModalAnswers modals;
            modals.click( QMessageBox::Yes ).answerWith( answerProgress() );
            window.window->loadFileNonInteractive( archivePath );
            auto& crawler = window.loadedInFront( 5_lcount );
            const auto path = CrawlerAccess::pathOf( crawler );

            THEN( "the plugin reads the decompressed text by the path of the tab's Log File, and "
                  "nothing by the archive's" )
            {
                REQUIRE( QFileInfo( path ) != QFileInfo( archivePath ) );
                const auto read = plugin.read( path, 1, 10 );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.text == QString::fromUtf8( numberedLogLines( 1, 5 ) ).chopped( 1 ) );
                REQUIRE( read.text == CrawlerAccess::textOf( crawler, 1, 5 ) );
                REQUIRE( plugin.read( archivePath, 1, 10 ).result
                         == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
            }
        }
    }
}

SCENARIO( "A plugin hears when the Log Lines of an open Log File change",
          "[ui][plugins][pluginlogfile]" )
{
    GIVEN( "A window with the plugin loaded, and a UTF-8 Log File of 3000 Log Lines open and "
           "watched" )
    {
        LogFileWindow window;
        PluginLibrary plugin;
        const auto path
            = window.writeFile( QStringLiteral( "app.log" ), numberedLogLines( 1, LogLineCount ) );
        auto& crawler = window.open( path, LinesCount( LogLineCount ) );
        REQUIRE( waitUiState( [ & ] { return window.fileWatch->isWatched( path ); } ) );
        const auto toldBefore = plugin.changes().size();

        THEN( "the plugin was told when the first load finished, as a replacement" )
        {
            REQUIRE( plugin.toldOf( 0, path, LogLineCount, LOGSQUIRL_LOG_LINES_REPLACED ) );
        }

        WHEN( "Log Lines are appended to it, followed" )
        {
            crawler.followSet( true );
            REQUIRE( window.fileWatch->grow( path, numberedLogLines( LogLineCount + 1, 2 ) ) );

            THEN( "the plugin is told they were appended, with the new count, and reads them" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return plugin.toldOf( toldBefore, path, LogLineCount + 2,
                                              LOGSQUIRL_LOG_LINES_APPENDED );
                    },
                    10'000 ) );
                const auto read = plugin.read( path, LogLineCount + 1, 5 );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE(
                    read.text
                    == QString::fromUtf8( numberedLogLines( LogLineCount + 1, 2 ) ).chopped( 1 ) );
            }
        }

        WHEN( "it is truncated on disk" )
        {
            REQUIRE( window.fileWatch->truncate( path, numberedLogLines( 1, 2 ) ) );

            THEN( "the plugin is told it was replaced, with the new count" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return plugin.toldOf( toldBefore, path, 2, LOGSQUIRL_LOG_LINES_REPLACED );
                    },
                    10'000 ) );
                REQUIRE( plugin.count( path )
                         == std::pair<int, uint64_t>{ LOGSQUIRL_LOG_LINES_OK, 2 } );
            }
        }

        WHEN( "the user reloads it" )
        {
            crawler.reload();

            THEN( "the plugin is told it was replaced" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return plugin.toldOf( toldBefore, path, LogLineCount,
                                              LOGSQUIRL_LOG_LINES_REPLACED );
                    },
                    10'000 ) );
            }
        }

        WHEN( "the user chooses ISO-8859-1 for its tab" )
        {
            crawler.setEncoding( TextEncoding::Latin1Mib );

            THEN( "the plugin is told it was replaced, and reads it in ISO-8859-1" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return plugin.toldOf( toldBefore, path, LogLineCount,
                                              LOGSQUIRL_LOG_LINES_REPLACED );
                    },
                    10'000 ) );
                const auto read = plugin.read( path, 1, 1 );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.text == QStringLiteral( "line number 1 Ã¤" ) );
                REQUIRE( read.text == CrawlerAccess::textOf( crawler, 1, 1 ) );
            }
        }
    }
}

SCENARIO( "A plugin hears when an open Log File reads under another Decoding Policy",
          "[ui][plugins][pluginlogfile]" )
{
    GIVEN( "A window with the plugin loaded, and a Log File with ANSI color sequences open "
           "with its sequences shown" )
    {
        LogFileWindow window;
        PluginLibrary plugin;
        REQUIRE_FALSE( window.policies.decoding.hideAnsiColorSequences );
        const auto path = window.writeFile( QStringLiteral( "ansi.log" ),
                                            "\x1B[31mERROR\x1B[0m: disk full\nall well\n" );
        auto& crawler = window.open( path, 2_lcount );
        REQUIRE( plugin.read( path, 1, 1 ).text.contains( QChar( 0x1B ) ) );
        const auto toldBefore = plugin.changes().size();

        WHEN( "the Policies after hiding ANSI color sequences was ticked are applied" )
        {
            window.policies.decoding.hideAnsiColorSequences = true;
            window.session->applyPolicies( window.policies );

            THEN( "the plugin is told it was replaced, with its count, and reads it without the "
                  "sequences, as the Text View shows it" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return plugin.toldOf( toldBefore, path, 2, LOGSQUIRL_LOG_LINES_REPLACED );
                    },
                    10'000 ) );
                const auto read = plugin.read( path, 1, 1 );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.text == QStringLiteral( "ERROR: disk full" ) );
                REQUIRE( read.text == CrawlerAccess::textOf( crawler, 1, 1 ) );
            }
        }
    }
}
