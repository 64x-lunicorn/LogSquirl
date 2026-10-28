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

// A plugin reads the fields of the Log Lines of an open Log File by its
// path, as the Table View shows them, and hears when another Log Format
// applies (#867). The plugin is a real library built against the current
// header, loaded by the window's Plugin Host; the window's tabs are real
// Crawler Widgets whose Log Files Format Recognition recognizes against a
// Catalog of the test's own Log Formats, and the Log Files grow on disk
// through a File Watch Port the test drives.

#include "applicationplugins.h"
#include "crawlerwidget.h"
#include "fake_file_watch.h"
#include "logformatcatalog.h"
#include "logformattablemodel.h"
#include "logsquirl_plugin_api.h"
#include "logtableview.h"
#include "mainwindow.h"
#include "openlogfile.h"
#include "session.h"
#include "shown_widget.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

struct PluginLogFieldsTest {};

template <>
struct CrawlerWidget::access_by<PluginLogFieldsTest> {
    static LinesCount lineCount( const CrawlerWidget& crawler )
    {
        return crawler.openLogFile_->logData()->getNbLine();
    }

    // The name of the Log Format the Table View shows, empty for none.
    static QString logFormatName( const CrawlerWidget& crawler )
    {
        return crawler.recognizedFormat_ ? crawler.recognizedFormat_->name() : QString{};
    }

    static const LogFormatTableModel* tableModel( const CrawlerWidget& crawler )
    {
        return qobject_cast<const LogFormatTableModel*>( crawler.logTableView_->model() );
    }

    // The headers of the Table View's columns, but the elapsed time's.
    static QStringList fieldColumns( const CrawlerWidget& crawler )
    {
        const auto* model = tableModel( crawler );
        QStringList names;
        for ( int column = 0; model != nullptr && column < model->columnCount(); ++column ) {
            if ( !model->isElapsedColumn( column ) ) {
                names.append( model->columnName( column ) );
            }
        }
        return names;
    }

    // What the Table View's Rows show in those columns, by Log Line in order,
    // none for a Row whose Log Line the Log Format does not match.
    static std::vector<std::optional<QStringList>> rows( const CrawlerWidget& crawler )
    {
        const auto* model = tableModel( crawler );
        std::vector<std::optional<QStringList>> shown;
        for ( int row = 0; model != nullptr && row < model->rowCount(); ++row ) {
            if ( !model->rowMatchesFormat( row ) ) {
                shown.emplace_back();
                continue;
            }
            QStringList cells;
            for ( int column = 0; column < model->columnCount(); ++column ) {
                if ( !model->isElapsedColumn( column ) ) {
                    cells.append( model->data( model->index( row, column ) ).toString() );
                }
            }
            shown.emplace_back( std::move( cells ) );
        }
        return shown;
    }
};

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

namespace {

using CrawlerAccess = CrawlerWidget::access_by<PluginLogFieldsTest>;

const auto PluginId = QStringLiteral( "io.github.logsquirl.test.log-file-plugin" );

// A regex Log Format whose fields are a timestamp, a level, a component and
// a body; and a JSON one whose fields are a timestamp, a level, a nested path
// and a message.
const char* const RegexFormatJson = R"({
    "fields_test_log": {
        "title": "Fields Test",
        "regex": {
            "basic": {
                "pattern": "^FIELDS (?<timestamp>\\d{6}) (?<level>[A-Z]+) \\[(?<component>[a-z]+)\\] (?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "value": {
            "component": { "kind": "string", "identifier": true }
        },
        "sample": [{ "line": "FIELDS 000001 INFO [main] hello" }]
    }
})";

const char* const JsonFormatJson = R"({
    "fields_json_log": {
        "title": "Fields JSON",
        "file-type": "json",
        "timestamp-field": "ts",
        "level-field": "level",
        "body-field": "msg",
        "value": {
            "ts": { "kind": "string" },
            "level": { "kind": "string" },
            "src/file": { "kind": "string" },
            "msg": { "kind": "string" }
        }
    }
})";

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

void writeFile( const QString& path, const QByteArray& bytes )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
}

/// What get_log_format_fields answered the plugin, copied as it came.
struct LogFormatRead {
    int result = 0;
    QString name;
    QStringList fieldNames;
};

/// What get_log_line_fields answered the plugin, the values copied as they
/// came: none for a Log Line not parsed.
struct LogLineFieldsRead {
    int result = 0;
    size_t fieldCount = 0;
    std::vector<std::optional<QStringList>> lines;
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
        formatFn
            = reinterpret_cast<FormatFn>( library.resolve( "logsquirl_logfile_plugin_format" ) );
        fieldsFn
            = reinterpret_cast<FieldsFn>( library.resolve( "logsquirl_logfile_plugin_fields" ) );
        REQUIRE( formatFn != nullptr );
        REQUIRE( fieldsFn != nullptr );
    }
    ~PluginLibrary()
    {
        library.unload();
    }
    PluginLibrary( const PluginLibrary& ) = delete;
    PluginLibrary& operator=( const PluginLibrary& ) = delete;

    LogFormatRead format( const QString& path ) const
    {
        const auto pathUtf8 = path.toUtf8();
        const char* name = nullptr;
        const char* const* names = nullptr;
        size_t count = 0;
        LogFormatRead read;
        read.result = formatFn( pathUtf8.constData(), &name, &names, &count );
        read.name = QString::fromUtf8( name );
        for ( size_t field = 0; field < count; ++field ) {
            read.fieldNames.append( QString::fromUtf8( names[ field ] ) );
        }
        return read;
    }

    LogLineFieldsRead fields( const QString& path, uint64_t firstLine, size_t maxLines ) const
    {
        const auto pathUtf8 = path.toUtf8();
        const char* const* values = nullptr;
        const size_t* lengths = nullptr;
        const int* parsed = nullptr;
        size_t lineCount = 0;
        LogLineFieldsRead read;
        read.result = fieldsFn( pathUtf8.constData(), firstLine, maxLines, &values, &lengths,
                                &parsed, &read.fieldCount, &lineCount );
        for ( size_t line = 0; line < lineCount; ++line ) {
            if ( parsed[ line ] == 0 ) {
                read.lines.emplace_back();
                continue;
            }
            QStringList lineValues;
            for ( size_t field = 0; field < read.fieldCount; ++field ) {
                const auto at = line * read.fieldCount + field;
                lineValues.append(
                    QString::fromUtf8( values[ at ], static_cast<qsizetype>( lengths[ at ] ) ) );
            }
            read.lines.emplace_back( std::move( lineValues ) );
        }
        return read;
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

    using FormatFn = int ( * )( const char*, const char**, const char* const**, size_t* );
    using FieldsFn = int ( * )( const char*, uint64_t, size_t, const char* const**, const size_t**,
                                const int**, size_t*, size_t* );

    QLibrary library;
    FormatFn formatFn = nullptr;
    FieldsFn fieldsFn = nullptr;
};

/// A shown window whose plugin has loaded, whose Log Files are recognized
/// against the test's two Log Formats, a folder of Log Files and the File
/// Watch Port their changes are reported through.
struct LogFieldsWindow {
    LogFieldsWindow()
    {
        REQUIRE( pluginRoot.isValid() );
        REQUIRE( formatDir.isValid() );
        REQUIRE( fileDir.isValid() );
        installPlugin( pluginRoot.path() );
        writeFile( QDir( formatDir.path() ).filePath( "fields_test.json" ), RegexFormatJson );
        writeFile( QDir( formatDir.path() ).filePath( "fields_json.json" ), JsonFormatJson );
        // The test's Log Formats alone, none built in.
        auto catalog = std::make_shared<LogFormatCatalog>();
        catalog->loadFromDirectory( formatDir.path() );
        REQUIRE( catalog->formatCount() == 2 );

        auto policies = testSettingsPolicies();
        policies.recognition.enabled = true;
        plugins = std::make_shared<ApplicationPlugins>(
            [ this ]( PluginCatalog& pluginCatalog, PluginHost& host ) {
                pluginCatalog.discoverPlugins( { pluginRoot.path() } );
                loadErrors
                    = host.autoLoadPlugins( { .autoLoad = true, .enabled = { PluginId } } ).errors;
            } );
        window = std::make_unique<MainWindow>(
            WindowSession{ std::make_shared<Session>( policies, catalog, fileWatch ), "Main", 0 },
            plugins );
        showUntilExposed( *window );
        REQUIRE( waitUiState( [ this ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loadErrors.isEmpty() );
        tabs = window->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabs != nullptr );
    }

    ~LogFieldsWindow()
    {
        window.reset();
        plugins.reset();
        QTest::qWait( 50 );
    }

    LogFieldsWindow( const LogFieldsWindow& ) = delete;
    LogFieldsWindow& operator=( const LogFieldsWindow& ) = delete;

    /// Writes bytes into a new file named name in the folder, and returns its path.
    QString logFile( const QString& name, const QByteArray& bytes )
    {
        const auto path = fileDir.filePath( name );
        writeFile( path, bytes );
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
        // The Log Format is recognized, and the plugin told, once the tab
        // heard the load finished.
        QTest::qWait( 50 );
        return *crawler;
    }

    const StandardPathsInTestMode testPaths{};
    QTemporaryDir pluginRoot;
    QTemporaryDir formatDir;
    QTemporaryDir fileDir;
    QStringList loadErrors;
    std::shared_ptr<FakeFileWatch> fileWatch = std::make_shared<FakeFileWatch>();
    std::shared_ptr<ApplicationPlugins> plugins;
    std::unique_ptr<MainWindow> window;
    TabbedCrawlerWidget* tabs = nullptr;
};

/// Log Lines of the regex Log Format, from number on: "FIELDS 00000n INFO [main] line n".
QByteArray regexLogLines( int from, int count )
{
    QByteArray bytes;
    for ( int number = from; number < from + count; ++number ) {
        bytes += QStringLiteral( "FIELDS %1 INFO [main] line %2\n" )
                     .arg( number, 6, 10, QChar( '0' ) )
                     .arg( number )
                     .toUtf8();
    }
    return bytes;
}

} // namespace

SCENARIO( "A plugin reads the fields of Log Lines as the Table View shows them",
          "[ui][plugins][pluginlogfields]" )
{
    GIVEN( "A window with a plugin built against the current header, loaded" )
    {
        LogFieldsWindow window;
        PluginLibrary plugin;

        AND_GIVEN( "a Log File of the regex Log Format open, with a tab in a value, a Log Line "
                   "it does not parse and one in two-byte characters" )
        {
            const auto path
                = window.logFile( QStringLiteral( "regex.log" ),
                                  "FIELDS 000001 INFO [main] started\n"
                                  "FIELDS 000002 WARN [db] slow\tquery\n"
                                  "    at a continuation the Log Format does not parse\n"
                                  "FIELDS 000004 ERROR [net] gr\xc3\xbc\xc3\x9f "
                                  "\xc3\xa9t\xc3\xa9\n"
                                  "FIELDS 000005 DEBUG [main] \n" );
            auto& crawler = window.open( path, 5_lcount );
            REQUIRE( CrawlerAccess::logFormatName( crawler )
                     == QStringLiteral( "fields_test_log" ) );
            const auto shown = CrawlerAccess::rows( crawler );
            REQUIRE( shown.size() == 5 );

            THEN( "the plugin gets the Log Format's name and the fields of the Table View's "
                  "columns, in their order, without the elapsed time" )
            {
                const auto format = plugin.format( path );
                REQUIRE( format.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( format.name == QStringLiteral( "fields_test_log" ) );
                REQUIRE( format.fieldNames == CrawlerAccess::fieldColumns( crawler ) );
                REQUIRE( format.fieldNames
                         == QStringList{ "timestamp", "level", "component", "body" } );
            }

            THEN( "the plugin gets the values the Table View shows, the tab too, and no fields "
                  "for the Log Line it does not parse" )
            {
                const auto read = plugin.fields( path, 1, 10 );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.fieldCount == 4 );
                REQUIRE( read.lines == shown );
                REQUIRE(
                    read.lines[ 1 ]
                    == QStringList{ "000002", "WARN", "db", QStringLiteral( "slow\tquery" ) } );
                REQUIRE_FALSE( read.lines[ 2 ].has_value() );
                REQUIRE( read.lines[ 3 ]->back() == QStringLiteral( "grüß été" ) );
                REQUIRE( read.lines[ 4 ]->back().isEmpty() );
            }

            THEN( "a thread of the plugin's reads them all the same while the UI thread waits" )
            {
                LogLineFieldsRead read;
                std::thread( [ & ] { read = plugin.fields( path, 2, 2 ); } ).join();
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.lines
                         == std::vector<std::optional<QStringList>>{ shown[ 1 ], shown[ 2 ] } );
            }
        }

        AND_GIVEN( "a Log File of the JSON Log Format open, with a tab and a line feed in a "
                   "value, and a Log Line that is no JSON object" )
        {
            const auto path = window.logFile(
                QStringLiteral( "json.log" ),
                R"({"ts":"2026-10-26T10:00:00Z","level":"INFO","src":{"file":"a.c"},"msg":"started"})"
                "\n"
                R"({"ts":"2026-10-26T10:00:01Z","level":"WARN","msg":"tab\there\nand a line feed"})"
                "\n"
                "no JSON object\n"
                R"({"ts":"2026-10-26T10:00:03Z","level":"ERROR","src":{"file":"b.c"},"msg":"x"})"
                "\n" );
            auto& crawler = window.open( path, 4_lcount );
            REQUIRE( CrawlerAccess::logFormatName( crawler )
                     == QStringLiteral( "fields_json_log" ) );
            const auto shown = CrawlerAccess::rows( crawler );
            REQUIRE( shown.size() == 4 );

            THEN( "the plugin gets the fields and values the Table View shows, the tab and the "
                  "line feed too, and empty fields for the Log Line that is no JSON object" )
            {
                const auto format = plugin.format( path );
                REQUIRE( format.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( format.name == QStringLiteral( "fields_json_log" ) );
                REQUIRE( format.fieldNames == CrawlerAccess::fieldColumns( crawler ) );
                REQUIRE( format.fieldNames.contains( QStringLiteral( "src/file" ) ) );

                const auto read = plugin.fields( path, 1, 10 );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.lines == shown );
                const auto msg = format.fieldNames.indexOf( QStringLiteral( "msg" ) );
                REQUIRE( read.lines[ 1 ]->at( msg )
                         == QStringLiteral( "tab\there\nand a line feed" ) );
                // As the Table View shows it: a Row whose every cell is empty.
                REQUIRE( read.lines[ 2 ].has_value() );
                REQUIRE( read.lines[ 2 ]->join( QString() ).isEmpty() );
            }
        }

        AND_GIVEN( "a Log File no Log Format applies to open" )
        {
            const auto path = window.logFile( QStringLiteral( "plain.log" ),
                                              "just some text\nand more of it\n" );
            auto& crawler = window.open( path, 2_lcount );
            REQUIRE( CrawlerAccess::logFormatName( crawler ).isEmpty() );

            THEN( "the plugin is told there is no Log Format" )
            {
                REQUIRE( plugin.format( path ).result == LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT );
                REQUIRE( plugin.fields( path, 1, 2 ).result == LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT );
            }
        }
    }
}

SCENARIO( "A plugin hears when another Log Format applies to an open Log File",
          "[ui][plugins][pluginlogfields]" )
{
    GIVEN( "A window with the plugin loaded, and a Log File too short for a Log Format open and "
           "followed" )
    {
        LogFieldsWindow window;
        PluginLibrary plugin;
        const auto path
            = window.logFile( QStringLiteral( "growing.log" ), "starting up\nstill starting\n" );
        auto& crawler = window.open( path, 2_lcount );
        REQUIRE( waitUiState( [ & ] { return window.fileWatch->isWatched( path ); } ) );
        REQUIRE( CrawlerAccess::logFormatName( crawler ).isEmpty() );
        REQUIRE( plugin.format( path ).result == LOGSQUIRL_LOG_LINES_NO_LOG_FORMAT );
        crawler.followSet( true );
        const auto toldBefore = plugin.changes().size();

        WHEN( "Log Lines of the regex Log Format are appended, and Format Recognition finds it" )
        {
            REQUIRE( window.fileWatch->grow( path, regexLogLines( 3, 10 ) ) );
            REQUIRE( waitUiState(
                [ & ] { return CrawlerAccess::logFormatName( crawler ) == "fields_test_log"; },
                10'000 ) );

            THEN( "the plugin is told the Log Lines were replaced, not appended, and reads "
                  "their fields" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return plugin.toldOf( toldBefore, path, 12, LOGSQUIRL_LOG_LINES_REPLACED );
                    },
                    10'000 ) );
                REQUIRE_FALSE(
                    plugin.toldOf( toldBefore, path, 12, LOGSQUIRL_LOG_LINES_APPENDED ) );
                const auto format = plugin.format( path );
                REQUIRE( format.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( format.name == QStringLiteral( "fields_test_log" ) );
                const auto read = plugin.fields( path, 1, 20 );
                REQUIRE( read.result == LOGSQUIRL_LOG_LINES_OK );
                REQUIRE( read.lines == CrawlerAccess::rows( crawler ) );
                REQUIRE_FALSE( read.lines.front().has_value() );
                REQUIRE( read.lines.back() == QStringList{ "000012", "INFO", "main", "line 12" } );
            }

            AND_WHEN( "more Log Lines of it are appended" )
            {
                REQUIRE( waitUiState(
                    [ & ] {
                        return plugin.toldOf( toldBefore, path, 12, LOGSQUIRL_LOG_LINES_REPLACED );
                    },
                    10'000 ) );
                const auto toldSince = plugin.changes().size();
                REQUIRE( window.fileWatch->grow( path, regexLogLines( 13, 2 ) ) );

                THEN( "the plugin is told they were appended, the Log Format being the same" )
                {
                    REQUIRE( waitUiState(
                        [ & ] {
                            return plugin.toldOf( toldSince, path, 14,
                                                  LOGSQUIRL_LOG_LINES_APPENDED );
                        },
                        10'000 ) );
                }
            }
        }
    }
}
