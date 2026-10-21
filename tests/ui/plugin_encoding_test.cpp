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

// A plugin reads which Encoding a Log File is read in, and hears when it
// changes (#803). The plugin is a real library built against the current
// header, loaded by the window's Plugin Host, and the window's tabs are real
// Crawler Widgets reading real Log Files.

#include "applicationplugins.h"
#include "crawlerwidget.h"
#include "logformatcatalog.h"
#include "logsquirl_plugin_api.h"
#include "mainwindow.h"
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
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <utility>
#include <vector>

struct PluginEncodingTest {};

template <>
struct CrawlerWidget::access_by<PluginEncodingTest> {
    static LinesCount lineCount( const CrawlerWidget& crawler )
    {
        return crawler.openLogFile_->logData()->getNbLine();
    }
};

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

namespace {

const auto PluginId = QStringLiteral( "io.github.logsquirl.test.encoding-plugin" );

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
    const auto pluginDir = QDir( root ).filePath( "encoding-plugin" );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "Encoding Plugin",
        "version": "1.0.0",
        "type": "ui",
        "library": "%2",
        "api_version": 1
    })" )
                        .arg( PluginId, QStringLiteral( LOGSQUIRL_TEST_ENCODING_PLUGIN_PATH ) )
                        .toUtf8() );
}

/// What get_log_file_encoding answered the plugin.
struct EncodingRead {
    int result = 0;
    QString name;
};

/// The plugin's exports the tests call, which call the host in turn. The
/// library stays loaded as long as this does.
struct PluginLibrary {
    PluginLibrary()
        : library( QStringLiteral( LOGSQUIRL_TEST_ENCODING_PLUGIN_PATH ) )
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
            library.resolve( "logsquirl_encoding_plugin_host_api_size" ) )();
    }

    /// The Encoding of the Log File at path, or of the tab in front for none.
    EncodingRead read( const QString& path = {} )
    {
        using Fn = int ( * )( const char*, char*, size_t );
        const auto pathUtf8 = path.toUtf8();
        std::array<char, 64> name{};
        EncodingRead read;
        read.result = reinterpret_cast<Fn>( library.resolve( "logsquirl_encoding_plugin_read" ) )(
            path.isNull() ? nullptr : pathUtf8.constData(), name.data(), name.size() );
        read.name = QString::fromUtf8( name.data() );
        return read;
    }

    /// The Log Files and Encodings the plugin's encoding callback was told.
    std::vector<std::pair<QString, QString>> changes()
    {
        using CountFn = size_t ( * )();
        using ChangeFn = void ( * )( size_t, const char**, const char** );
        const auto count = reinterpret_cast<CountFn>(
            library.resolve( "logsquirl_encoding_plugin_change_count" ) )();
        auto* change
            = reinterpret_cast<ChangeFn>( library.resolve( "logsquirl_encoding_plugin_change" ) );
        std::vector<std::pair<QString, QString>> told;
        for ( size_t i = 0; i < count; ++i ) {
            const char* path = nullptr;
            const char* encoding = nullptr;
            change( i, &path, &encoding );
            told.emplace_back( QString::fromUtf8( path ), QString::fromUtf8( encoding ) );
        }
        return told;
    }

    QLibrary library;
};

/// A shown window whose encoding plugin has loaded, and a folder of Log Files.
struct EncodingWindow {
    EncodingWindow()
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
                                                      std::make_shared<LogFormatCatalog>() ),
                           "Main", 0 },
            plugins );
        showUntilExposed( *window );
        REQUIRE( waitUiState( [ this ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loadErrors.isEmpty() );
    }

    ~EncodingWindow()
    {
        window.reset();
        plugins.reset();
        QTest::qWait( 50 );
    }

    EncodingWindow( const EncodingWindow& ) = delete;
    EncodingWindow& operator=( const EncodingWindow& ) = delete;

    /// Writes bytes into a new file named name in the folder, and returns its path.
    QString writeFile( const QString& name, const QByteArray& bytes )
    {
        const auto path = fileDir.filePath( name );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( bytes ) == bytes.size() );
        return path;
    }

    /// Opens the Log File in a tab of its own, in front, once it has loaded.
    CrawlerWidget& open( const QString& path, LinesCount lines )
    {
        window->loadFileNonInteractive( path );
        auto* tabs = window->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabs != nullptr );
        CrawlerWidget* crawler = nullptr;
        REQUIRE( waitUiState(
            [ & ] {
                crawler = qobject_cast<CrawlerWidget*>( tabs->currentWidget() );
                return crawler != nullptr
                       && crawler->state().loadStatus == LoadingStatus::Successful
                       && CrawlerWidget::access_by<PluginEncodingTest>::lineCount( *crawler )
                              == lines;
            },
            10'000 ) );
        // The Encoding is settled, and the plugin told, once the tab heard
        // the load finished.
        QTest::qWait( 50 );
        return *crawler;
    }

    const StandardPathsInTestMode testPaths{};
    QTemporaryDir pluginRoot;
    QTemporaryDir fileDir;
    QStringList loadErrors;
    std::shared_ptr<ApplicationPlugins> plugins;
    std::unique_ptr<MainWindow> window;
};

/// Text in UTF-16LE with its byte order mark.
QByteArray utf16LeWithBom( const QString& text )
{
    return QByteArray( "\xFF\xFE", 2 ) + TextEncoding::forName( "UTF-16LE" )->fromUnicode( text );
}

} // namespace

SCENARIO( "A plugin reads which Encoding a Log File is read in, and hears when it changes",
          "[ui][plugins][pluginencoding]" )
{
    GIVEN( "A window with a plugin built against the current header, loaded" )
    {
        EncodingWindow encoding;
        PluginLibrary plugin;

        THEN( "the plugin sees both functions in this host's table" )
        {
            REQUIRE( plugin.hostApiSize() == sizeof( LogSquirlHostApi ) );
        }

        WHEN( "no Log File is open" )
        {
            const auto inFront = plugin.read();
            const auto named = plugin.read( encoding.fileDir.filePath( "nowhere.log" ) );

            THEN( "the plugin is told there is none, and gets no name" )
            {
                REQUIRE( inFront.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( inFront.name.isEmpty() );
                REQUIRE( named.result == LOGSQUIRL_LOG_LINES_NO_LOG_FILE );
                REQUIRE( named.name.isEmpty() );
            }
        }

        AND_GIVEN( "a UTF-16LE Log File with a byte order mark, open in the tab in front" )
        {
            const auto path
                = encoding.writeFile( QStringLiteral( "utf16.log" ),
                                      utf16LeWithBom( QStringLiteral( "first line\nVIN: X\n" ) ) );
            auto& crawler = encoding.open( path, 2_lcount );

            THEN( "the plugin reads UTF-16LE for the tab in front and for the Log File by path" )
            {
                REQUIRE( plugin.read().result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( plugin.read().name == QStringLiteral( "UTF-16LE" ) );
                REQUIRE( plugin.read( path ).result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( plugin.read( path ).name == QStringLiteral( "UTF-16LE" ) );
            }

            THEN( "the plugin was told the Encoding the load detected" )
            {
                const auto changes = plugin.changes();
                REQUIRE_FALSE( changes.empty() );
                REQUIRE( changes.back().second == QStringLiteral( "UTF-16LE" ) );
                REQUIRE( QFileInfo( changes.back().first ) == QFileInfo( path ) );
            }

            WHEN( "the user chooses ISO-8859-1 for the tab" )
            {
                const auto toldBefore = plugin.changes().size();
                // An Encoding whose line feed is another width reloads the
                // Log File. The user goes back only after it has loaded:
                // going back while that load starts is another story.
                QSignalSpy reloaded( &crawler, &CrawlerWidget::loadingFinished );
                crawler.setEncoding( TextEncoding::Latin1Mib );
                REQUIRE( waitUiState( [ &reloaded ] { return reloaded.count() > 0; }, 10'000 ) );

                THEN( "the plugin is told the Log File is read in ISO-8859-1, and reads it so" )
                {
                    const auto changes = plugin.changes();
                    REQUIRE( changes.size() == toldBefore + 1 );
                    REQUIRE( QFileInfo( changes.back().first ) == QFileInfo( path ) );
                    REQUIRE( changes.back().second == QStringLiteral( "ISO-8859-1" ) );
                    REQUIRE( plugin.read().name == QStringLiteral( "ISO-8859-1" ) );
                    REQUIRE( plugin.read( path ).name == QStringLiteral( "ISO-8859-1" ) );
                }

                AND_WHEN( "the user goes back to the detected Encoding" )
                {
                    crawler.setEncoding( std::nullopt );

                    THEN( "the plugin is told UTF-16LE again, once the load has found the byte "
                          "order mark again, and reads it" )
                    {
                        // The guess the ISO-8859-1 Index left is settled
                        // first; the load it starts finds the byte order
                        // mark again.
                        REQUIRE( waitUiState(
                            [ & ] {
                                return plugin.changes().back().second
                                       == QStringLiteral( "UTF-16LE" );
                            },
                            10'000 ) );
                        REQUIRE( plugin.read().name == QStringLiteral( "UTF-16LE" ) );
                        REQUIRE( plugin.read( path ).name == QStringLiteral( "UTF-16LE" ) );
                    }
                }
            }

            AND_WHEN( "a single-byte Log File opens in front of it" )
            {
                const auto other = encoding.writeFile( QStringLiteral( "plain.log" ),
                                                       QByteArray( "plain text\n" ) );
                auto& inFront = encoding.open( other, 1_lcount );

                THEN( "the plugin reads that tab's Encoding in front, and UTF-16LE for the first "
                      "by path" )
                {
                    REQUIRE( inFront.encodingName() != QStringLiteral( "UTF-16LE" ) );
                    REQUIRE( plugin.read().name == inFront.encodingName() );
                    REQUIRE( plugin.read( other ).name == inFront.encodingName() );
                    REQUIRE( plugin.read( path ).name == QStringLiteral( "UTF-16LE" ) );
                }
            }
        }
    }
}
