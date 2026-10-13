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

// A plugin ships Log Formats named in its manifest, read by the Log Format
// Catalog while the plugin is enabled (#793): after the plugins were
// discovered and whenever one is enabled or disabled in Plugin Management.

#include <catch2/catch_test_macros.hpp>

#include "applicationplugins.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "logdata.h"
#include "logformatcatalog.h"
#include "plugindialog.h"
#include "pluginlogformats.h"
#include "savedsearches.h"
#include "session.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

struct PluginLogFormatsAccess {};

template <>
struct CrawlerWidget::access_by<PluginLogFormatsAccess> {
    std::unique_ptr<CrawlerWidget> crawler;

    explicit access_by( ViewInterface* view )
        : crawler( static_cast<CrawlerWidget*>( view ) )
    {
    }

    const LogFormatDefinition* logFormat() const
    {
        return crawler->recognizedFormat_.get();
    }

    bool isTableViewToggled() const
    {
        return crawler->tableViewToggle_->isChecked();
    }

    bool waitLoaded( LinesCount lines )
    {
        return waitUiState( [ this, lines ] {
            return crawler->openLogFile_->logData()->getNbLine() == lines
                   && crawler->lastLoadStatus_.has_value();
        } );
    }
};

using CrawlerAccess = CrawlerWidget::access_by<PluginLogFormatsAccess>;

namespace {

constexpr int LineCount = 60;

const auto FormatPluginId = QStringLiteral( "io.github.logsquirl.test.format-plugin" );
const auto FormatPluginName = QStringLiteral( "Format Plugin" );
const auto FormatName = QStringLiteral( "plugin_test_log" );

const char* PluginFormatJson = R"({
    "plugin_test_log": {
        "title": "Plugin Test",
        "regex": {
            "basic": {
                "pattern": "^PLUGIN_TEST (?<timestamp>\\d{6}) (?<level>[A-Z]+) (?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "sample": [{ "line": "PLUGIN_TEST 000001 INFO hello" }]
    }
})";

void writeFile( const QString& path, const QByteArray& content )
{
    REQUIRE( QDir().mkpath( QFileInfo( path ).absolutePath() ) );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    file.write( content );
}

/// Installs the format plugin into root: the Log Lines plugin's library, whose
/// manifest names the given Log Format files, relative to the plugin directory.
QString installFormatPlugin( const QString& root, const QStringList& formats )
{
    const auto pluginDir = QDir( root ).filePath( "format-plugin" );
    QStringList quoted;
    for ( const auto& format : formats ) {
        quoted << QStringLiteral( "\"%1\"" ).arg( format );
    }
    writeFile( QDir( pluginDir ).filePath( "plugin.json" ),
               QStringLiteral( R"({
        "id": "%1",
        "name": "%2",
        "version": "1.0.0",
        "type": "ui",
        "library": "%3",
        "api_version": 1,
        "formats": [ %4 ]
    })" )
                   .arg( FormatPluginId, FormatPluginName,
                         QStringLiteral( LOGSQUIRL_TEST_LOG_LINES_PLUGIN_PATH ),
                         quoted.join( ", " ) )
                   .toUtf8() );
    return pluginDir;
}

void writeLogFile( const QString& path )
{
    QByteArray lines;
    for ( int i = 0; i < LineCount; ++i ) {
        lines += QString( "PLUGIN_TEST %1 INFO this is line %2\n" )
                     .arg( i, 6, 10, QChar( '0' ) )
                     .arg( i )
                     .toUtf8();
    }
    writeFile( path, lines );
}

/// The enabled plugins stand in the configuration, where Plugin Management
/// writes them and the application reads them for the Catalog.
struct EnabledPluginsInConfiguration {
    explicit EnabledPluginsInConfiguration( const QStringList& enabled )
        : before( Configuration::get().enabledPlugins() )
    {
        Configuration::get().setEnabledPlugins( enabled );
    }
    ~EnabledPluginsInConfiguration()
    {
        Configuration::get().setEnabledPlugins( before );
    }
    EnabledPluginsInConfiguration( const EnabledPluginsInConfiguration& ) = delete;
    EnabledPluginsInConfiguration& operator=( const EnabledPluginsInConfiguration& ) = delete;

    QStringList before;
};

QStringList enabledPlugins()
{
    return Configuration::get().enabledPlugins();
}

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

/// Application Plugins that discover the plugins in root and load the enabled
/// ones, as the application's load step does.
std::shared_ptr<ApplicationPlugins> pluginsIn( const QString& root, bool autoLoad = true )
{
    return std::make_shared<ApplicationPlugins>( [ root, autoLoad ]( PluginCatalog& catalog,
                                                                     PluginHost& host ) {
        catalog.discoverPlugins( { root } );
        const auto errors
            = host.autoLoadPlugins( { .autoLoad = autoLoad, .enabled = enabledPlugins() } ).errors;
        REQUIRE( errors.isEmpty() );
    } );
}

SettingsPolicies recognitionShownAsTable()
{
    auto policies = testSettingsPolicies();
    policies.recognition.enabled = true;
    policies.presentation.autoShowTableView = true;
    return policies;
}

std::unique_ptr<CrawlerAccess> openLogFile( Session& session, const QString& path )
{
    return std::make_unique<CrawlerAccess>(
        session.open( path, []( const ViewBuild& build ) { return new CrawlerWidget( build ); } ) );
}

/// The Enable or Disable button of the only plugin card in Plugin Management.
QPushButton* toggleButton( const PluginDialog& dialog )
{
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    const auto buttons = dialog.findChildren<QPushButton*>( "toggleButton" );
    REQUIRE( buttons.size() == 1 );
    return buttons.front();
}

/// The Log Format warning of the only plugin card in Plugin Management.
QLabel* formatWarning( const PluginDialog& dialog )
{
    // The cards replaced by the last rebuild are gone once their deferred
    // deletion ran.
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    const auto labels = dialog.findChildren<QLabel*>( "formatWarningLabel" );
    REQUIRE( labels.size() == 1 );
    return labels.front();
}

} // namespace

SCENARIO( "A plugin's Log Format is in the Catalog while the plugin is enabled",
          "[ui][plugins][logformat][pluginlogformats]" )
{
    const StandardPathsInTestMode testPaths;
    QTemporaryDir root;
    REQUIRE( root.isValid() );
    const auto pluginDir = installFormatPlugin( root.path(), { "formats/plugin_test.json" } );
    writeFile( QDir( pluginDir ).filePath( "formats/plugin_test.json" ), PluginFormatJson );
    const auto logPath = root.filePath( "capture.log" );
    writeLogFile( logPath );

    GIVEN( "the plugin enabled, and the Catalog fed by the enabled plugins' Log Formats" )
    {
        const EnabledPluginsInConfiguration enabled( { FormatPluginId } );
        auto plugins = pluginsIn( root.path() );
        auto catalog = std::make_shared<LogFormatCatalog>();
        catalog->rebuild();
        PluginLogFormats pluginLogFormats( *plugins, catalog, &enabledPlugins );
        Session session{ recognitionShownAsTable(), catalog };
        session.savedSearches().clear();

        REQUIRE( catalog->formatByName( FormatName ) == nullptr );

        WHEN( "the plugins are loaded" )
        {
            plugins->loadSoon();
            REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );

            THEN( "the plugin's Log Format is in the Catalog, with the plugin as its Source" )
            {
                const auto format = catalog->formatByName( FormatName );
                REQUIRE( format != nullptr );
                CHECK( format->title() == "Plugin Test" );
                CHECK( format->source() == LogFormatSource::plugin( FormatPluginName ) );
            }

            AND_WHEN( "a Log File matching it is opened" )
            {
                auto logFile = openLogFile( session, logPath );
                REQUIRE( logFile->waitLoaded( LinesCount( LineCount ) ) );

                THEN( "it is recognized with the plugin's Log Format and shown as a Table View" )
                {
                    REQUIRE( logFile->logFormat() == catalog->formatByName( FormatName ).get() );
                    REQUIRE( waitUiState( [ & ] { return logFile->isTableViewToggled(); } ) );
                }

                AND_WHEN( "the plugin is disabled in Plugin Management" )
                {
                    const auto* held = logFile->logFormat();
                    REQUIRE( held != nullptr );
                    PluginDialog dialog( *plugins );
                    REQUIRE( toggleButton( dialog )->text() == "Disable" );
                    toggleButton( dialog )->click();

                    THEN( "its Log Format is gone from the Catalog" )
                    {
                        REQUIRE_FALSE( enabledPlugins().contains( FormatPluginId ) );
                        CHECK( catalog->formatByName( FormatName ) == nullptr );
                        // The built-in Log Formats are read again, as every rebuild does.
                        CHECK( catalog->formatByName( "syslog_log" ) != nullptr );
                    }

                    THEN( "the open Log File keeps the Log Format it was recognized with" )
                    {
                        CHECK( logFile->logFormat() == held );
                        CHECK( logFile->logFormat()->title() == "Plugin Test" );
                    }

                    AND_WHEN( "it is enabled again" )
                    {
                        REQUIRE( toggleButton( dialog )->text() == "Enable" );
                        toggleButton( dialog )->click();

                        THEN( "its Log Format is back in the Catalog, as a new object" )
                        {
                            REQUIRE( enabledPlugins().contains( FormatPluginId ) );
                            const auto format = catalog->formatByName( FormatName );
                            REQUIRE( format != nullptr );
                            CHECK( format.get() != held );
                            CHECK( format->source()
                                   == LogFormatSource::plugin( FormatPluginName ) );
                            CHECK( logFile->logFormat() == held );
                        }
                    }
                }
            }
        }
    }

    GIVEN( "the plugin enabled but not loaded, with auto-load off" )
    {
        const EnabledPluginsInConfiguration enabled( { FormatPluginId } );
        auto plugins = pluginsIn( root.path(), false );
        auto catalog = std::make_shared<LogFormatCatalog>();
        catalog->rebuild();
        PluginLogFormats pluginLogFormats( *plugins, catalog, &enabledPlugins );

        WHEN( "the plugins are discovered" )
        {
            plugins->loadSoon();
            REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );
            REQUIRE_FALSE( plugins->host().isLoaded( FormatPluginId ) );

            THEN( "the plugin's Log Format is in the Catalog all the same" )
            {
                const auto format = catalog->formatByName( FormatName );
                REQUIRE( format != nullptr );
                CHECK( format->source() == LogFormatSource::plugin( FormatPluginName ) );
            }
        }
    }

    GIVEN( "the plugin disabled" )
    {
        const EnabledPluginsInConfiguration enabled( {} );
        auto plugins = pluginsIn( root.path() );
        auto catalog = std::make_shared<LogFormatCatalog>();
        catalog->rebuild();
        PluginLogFormats pluginLogFormats( *plugins, catalog, &enabledPlugins );

        WHEN( "the plugins are discovered" )
        {
            plugins->loadSoon();
            REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );

            THEN( "its Log Format is not in the Catalog" )
            {
                CHECK( catalog->formatByName( FormatName ) == nullptr );
            }
        }
    }
}

SCENARIO(
    "A plugin's Log Format file that cannot be read is skipped, and Plugin Management says so",
    "[ui][plugins][logformat][pluginlogformats]" )
{
    const StandardPathsInTestMode testPaths;
    QTemporaryDir root;
    REQUIRE( root.isValid() );

    GIVEN( "an enabled plugin naming a fine, a missing and a broken Log Format file" )
    {
        const auto pluginDir
            = installFormatPlugin( root.path(), { "formats/plugin_test.json",
                                                  "formats/missing.json", "formats/broken.json" } );
        writeFile( QDir( pluginDir ).filePath( "formats/plugin_test.json" ), PluginFormatJson );
        writeFile( QDir( pluginDir ).filePath( "formats/broken.json" ), "{ \"broken\": " );

        const EnabledPluginsInConfiguration enabled( { FormatPluginId } );
        auto plugins = pluginsIn( root.path() );
        auto catalog = std::make_shared<LogFormatCatalog>();
        catalog->rebuild();
        PluginLogFormats pluginLogFormats( *plugins, catalog, &enabledPlugins );

        WHEN( "the plugins are loaded" )
        {
            plugins->loadSoon();
            REQUIRE( waitUiState( [ & ] { return plugins->isLoaded(); }, 5000 ) );

            THEN( "the plugin is loaded and its fine Log Format is in the Catalog" )
            {
                CHECK( plugins->host().isLoaded( FormatPluginId ) );
                CHECK( catalog->formatByName( FormatName ) != nullptr );
            }

            THEN( "the Catalog warns about the two files, naming the plugin" )
            {
                const auto warnings = catalog->pluginFormatWarnings( FormatPluginId );
                REQUIRE( warnings.size() == 2 );
                CHECK( warnings[ 0 ].contains( "missing.json" ) );
                CHECK( warnings[ 1 ].contains( "broken.json" ) );
            }

            THEN( "Plugin Management shows the warnings in the plugin's details" )
            {
                PluginDialog dialog( *plugins );
                dialog.showLogFormatWarnings( catalog );
                const auto* label = formatWarning( dialog );
                CHECK( label->isVisibleTo( &dialog ) );
                CHECK( label->text().contains( "missing.json" ) );
                CHECK( label->text().contains( "broken.json" ) );
            }
        }
    }
}
