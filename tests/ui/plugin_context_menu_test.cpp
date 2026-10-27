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

// A plugin adds items to the context menu of the Log Lines (#859): that of the
// Text View, the Table View and the Filtered View. The plugin is a real
// library built against the current header, loaded by the Application
// Plugins, and the menus are those of the views of real tabs of two real
// windows.

#include "abstractlogview.h"
#include "applicationplugins.h"
#include "crawlerwidget.h"
#include "logformatcatalog.h"
#include "logformatdefinition.h"
#include "logsquirl_plugin_api.h"
#include "logtableview.h"
#include "mainwindow.h"
#include "searchlinewidget_access.h"
#include "session.h"
#include "shown_widget.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMenu>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <vector>

struct PluginContextMenuTest {};

template <>
struct AbstractLogView::access_by<PluginContextMenuTest> {
    static std::unique_ptr<QMenu> contextMenu( AbstractLogView& view, const QPoint& pos )
    {
        return view.createContextMenu( pos );
    }

    // What dragging over the characters startColumn to endColumn of a Log
    // Line does: they are selected.
    static void selectPortion( AbstractLogView& view, LineNumber line, LineColumn startColumn,
                               LineColumn endColumn )
    {
        view.selection_.selectPortion( line, startColumn, endColumn );
        view.update();
    }

    // A point, in viewport coordinates, on the text of the Log Line shown at
    // row, counted from the top of the view.
    static QPoint pointOnRow( const AbstractLogView& view, int row )
    {
        return { view.viewportGeometry().leftMarginPx() + 40,
                 row * view.charHeight_ + view.charHeight_ / 2 };
    }
};

template <>
struct LogTableView::access_by<PluginContextMenuTest> {
    static std::unique_ptr<QMenu> contextMenu( LogTableView& view, const QPoint& pos )
    {
        return view.createContextMenu( pos );
    }

    // What dragging over characters inside a cell does: they are selected.
    static void selectInCell( LogTableView& view, int row, int column, int startChar, int endChar )
    {
        view.state_.selection.selectInCell( row, column, startChar, endChar );
        view.showInCellSelection();
    }
};

template <>
struct CrawlerWidget::access_by<PluginContextMenuTest> {
    static AbstractLogView& textView( const CrawlerWidget& crawler )
    {
        return *crawler.logMainView_;
    }

    static AbstractLogView& filteredView( const CrawlerWidget& crawler )
    {
        return *crawler.currentFilteredView();
    }

    static LogTableView& tableView( const CrawlerWidget& crawler )
    {
        return *crawler.logTableView_;
    }

    static LinesCount lineCount( const CrawlerWidget& crawler )
    {
        return crawler.openLogFile_->logData()->getNbLine();
    }

    // Runs a Search for pattern, as the Search Line does, until it is done.
    static void search( CrawlerWidget& crawler, const QString& pattern )
    {
        auto* edit = SearchLineAccess::patternEdit( *crawler.searchLine_ );
        edit->clearEditText();
        QTest::keyClicks( edit, pattern );
        QTest::mouseClick( SearchLineAccess::searchButton( *crawler.searchLine_ ), Qt::LeftButton );
        QTest::qWait( 100 );
        REQUIRE( waitUiState(
            [ & ] { return SearchLineAccess::stopButton( *crawler.searchLine_ )->isHidden(); } ) );
    }

    // Recognizes the Log File with a Log Format of its own, the first word
    // a field and the rest the body, and shows the Table View.
    static void showTableView( CrawlerWidget& crawler )
    {
        LogFormatDefinition format;
        format.setName( "plugin_context_menu_test" );
        format.setTitle( "Plugin context menu test" );
        QHash<QString, QString> regex;
        regex[ "basic" ] = R"(^(?<source>\w+)\s+(?<body>.*)$)";
        format.setRegexPatterns( regex );
        format.setBodyField( "body" );

        crawler.recognizedFormat_ = std::make_shared<const LogFormatDefinition>( format );
        crawler.logTableView_->setLogFormat( crawler.recognizedFormat_.get(),
                                             crawler.openLogFile_->logData().get() );
        crawler.tableViewToggle_->setVisible( true );
        crawler.tableViewToggle_->setChecked( true );
        QTest::qWait( 50 );
    }
};

using logsquirl::plugins::ApplicationPlugins;
using logsquirl::plugins::PluginCatalog;
using logsquirl::plugins::PluginHost;

namespace {

using CrawlerAccess = CrawlerWidget::access_by<PluginContextMenuTest>;
using TextViewAccess = AbstractLogView::access_by<PluginContextMenuTest>;
using TableViewAccess = LogTableView::access_by<PluginContextMenuTest>;

const auto PluginId = QStringLiteral( "io.github.logsquirl.test.context-menu-plugin" );
const auto ProbeItem = QStringLiteral( "Probe Item" );

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
    const auto pluginDir = QDir( root ).filePath( "context-menu-plugin" );
    REQUIRE( QDir().mkpath( pluginDir ) );

    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "%1",
        "name": "Context Menu Plugin",
        "version": "1.0.0",
        "type": "ui",
        "library": "%2",
        "api_version": 1
    })" )
                        .arg( PluginId, QStringLiteral( LOGSQUIRL_TEST_CONTEXT_MENU_PLUGIN_PATH ) )
                        .toUtf8() );
}

/// What the plugin's last choice was handed.
struct Choice {
    size_t size = 0;
    int view = -1;
    QString filePath;
    std::vector<uint64_t> lineNumbers;
    uint64_t selectedLineCount = 0;
    std::optional<QString> selectedText;
    bool ranHere = false;
};

/// The plugin's exports the tests call. The library stays loaded as long as this does.
struct PluginLibrary {
    PluginLibrary()
        : library( QStringLiteral( LOGSQUIRL_TEST_CONTEXT_MENU_PLUGIN_PATH ) )
    {
        REQUIRE( library.load() );
    }
    ~PluginLibrary()
    {
        library.unload();
    }
    PluginLibrary( const PluginLibrary& ) = delete;
    PluginLibrary& operator=( const PluginLibrary& ) = delete;

    static constexpr int Item = 0;
    static constexpr int Nested = 1;

    int id( int item )
    {
        using Fn = int ( * )( int );
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_context_menu_plugin_id" ) )(
            item );
    }

    int runs( int item )
    {
        using Fn = int ( * )( int );
        return reinterpret_cast<Fn>( library.resolve( "logsquirl_context_menu_plugin_runs" ) )(
            item );
    }

    void unregister( int id )
    {
        using Fn = void ( * )( int );
        reinterpret_cast<Fn>( library.resolve( "logsquirl_context_menu_plugin_unregister" ) )( id );
    }

    Choice last()
    {
        Choice choice;
        using LastFn = void ( * )( size_t*, int*, uint64_t* );
        reinterpret_cast<LastFn>( library.resolve( "logsquirl_context_menu_plugin_last" ) )(
            &choice.size, &choice.view, &choice.selectedLineCount );
        using TextFn = const char* ( * )();
        choice.filePath = QString::fromUtf8( reinterpret_cast<TextFn>(
            library.resolve( "logsquirl_context_menu_plugin_last_file_path" ) )() );
        using LinesFn = const uint64_t* ( * )( size_t* );
        size_t count = 0;
        const auto* lines = reinterpret_cast<LinesFn>(
            library.resolve( "logsquirl_context_menu_plugin_last_lines" ) )( &count );
        choice.lineNumbers.assign( lines, lines + count );
        if ( const auto* text = reinterpret_cast<TextFn>(
                 library.resolve( "logsquirl_context_menu_plugin_last_selected_text" ) )() ) {
            choice.selectedText = QString::fromUtf8( text );
        }
        using HereFn = int ( * )();
        choice.ranHere = reinterpret_cast<HereFn>(
                             library.resolve( "logsquirl_context_menu_plugin_last_ran_here" ) )()
                         != 0;
        return choice;
    }

    QLibrary library;
};

/// The entry of the menu named path, through its submenus, or nullptr.
QAction* entryAt( const QMenu& menu, const QStringList& path )
{
    const auto* current = &menu;
    for ( qsizetype i = 0; i < path.size(); ++i ) {
        QAction* found = nullptr;
        for ( auto* action : current->actions() ) {
            if ( action->text() == path[ i ] ) {
                found = action;
                break;
            }
        }
        if ( found == nullptr || i + 1 == path.size() ) {
            return found;
        }
        current = found->menu();
        if ( current == nullptr ) {
            return nullptr;
        }
    }
    return nullptr;
}

/// The texts of the entries after the last separator of the menu, or none
/// when the menu does not end with plugin entries.
QStringList pluginSectionOf( const QMenu& menu )
{
    QStringList section;
    const auto actions = menu.actions();
    for ( auto it = actions.rbegin(); it != actions.rend(); ++it ) {
        if ( ( *it )->isSeparator() ) {
            break;
        }
        section.prepend( ( *it )->text() );
    }
    return section;
}

const QStringList PluginSection = { ProbeItem, QStringLiteral( "Context Probe" ) };

/// The submenu Context Probe > Sub of the plugin section holds Nested.
bool hasNested( const QMenu& menu )
{
    return entryAt( menu, { QStringLiteral( "Context Probe" ), QStringLiteral( "Sub" ),
                            QStringLiteral( "Nested" ) } )
           != nullptr;
}

/// Right-clicks pos of the view's viewport and chooses the entry of the
/// context menu that opens, as the user does.
void rightClickAndChoose( QWidget& viewport, const QPoint& pos, const QString& entry )
{
    bool chosen = false;
    QTimer timer;
    timer.setInterval( 10 );
    QObject::connect( &timer, &QTimer::timeout, [ & ] {
        auto* menu = qobject_cast<QMenu*>( QApplication::activePopupWidget() );
        if ( menu == nullptr ) {
            return;
        }
        timer.stop();
        if ( auto* action = entryAt( *menu, { entry } ) ) {
            action->trigger();
            chosen = true;
        }
        menu->close();
    } );
    timer.start();
    QTest::mousePress( &viewport, Qt::RightButton, {}, pos );
    QTest::mouseRelease( &viewport, Qt::RightButton, {}, pos );
    timer.stop();
    REQUIRE( chosen );
}

/// Chooses the entry of a context menu that was built and not shown.
void choose( const QMenu& menu, const QStringList& path )
{
    auto* action = entryAt( menu, path );
    REQUIRE( action != nullptr );
    REQUIRE( action->isEnabled() );
    action->trigger();
}

/// "line N word" for N from 1 to count; every fourth one, from the third,
/// says ERROR.
QByteArray numberedLogLines( int count )
{
    QByteArray bytes;
    for ( int number = 1; number <= count; ++number ) {
        bytes += QStringLiteral( "line%1 %2 token%1\n" )
                     .arg( number )
                     .arg( number % 4 == 3 ? QStringLiteral( "ERROR" ) : QStringLiteral( "info" ) )
                     .toUtf8();
    }
    return bytes;
}

/// Two shown windows whose plugin has loaded, each with a Log File open.
struct ContextMenuWindows {
    ContextMenuWindows()
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
        const auto session = std::make_shared<Session>( testSettingsPolicies(),
                                                        std::make_shared<LogFormatCatalog>() );
        first = std::make_unique<MainWindow>( WindowSession{ session, "First", 0 }, plugins );
        showUntilExposed( *first );
        REQUIRE( waitUiState( [ this ] { return plugins->isLoaded(); }, 5000 ) );
        REQUIRE( loadErrors.isEmpty() );
        second = std::make_unique<MainWindow>( WindowSession{ session, "Second", 1 }, plugins );
        showUntilExposed( *second );

        path = writeFile( QStringLiteral( "app.log" ), numberedLogLines( LineCount ) );
        crawler = &open( *first, path );
        secondCrawler
            = &open( *second, writeFile( QStringLiteral( "other.log" ), numberedLogLines( 10 ) ) );
    }

    ~ContextMenuWindows()
    {
        second.reset();
        first.reset();
        plugins.reset();
        QTest::qWait( 50 );
    }

    ContextMenuWindows( const ContextMenuWindows& ) = delete;
    ContextMenuWindows& operator=( const ContextMenuWindows& ) = delete;

    static constexpr int LineCount = 1200;

    QString writeFile( const QString& name, const QByteArray& bytes )
    {
        const auto filePath = fileDir.filePath( name );
        QFile file( filePath );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        REQUIRE( file.write( bytes ) == bytes.size() );
        return filePath;
    }

    /// Opens the Log File in a tab of the window, in front, once it has loaded.
    static CrawlerWidget& open( MainWindow& window, const QString& filePath )
    {
        window.loadFileNonInteractive( filePath );
        auto* tabs = window.findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabs != nullptr );
        CrawlerWidget* opened = nullptr;
        REQUIRE( waitUiState(
            [ & ] {
                opened = qobject_cast<CrawlerWidget*>( tabs->currentWidget() );
                return opened != nullptr && opened->state().loadStatus == LoadingStatus::Successful
                       && CrawlerAccess::lineCount( *opened ).get() > 0;
            },
            10'000 ) );
        QTest::qWait( 50 );
        return *opened;
    }

    const StandardPathsInTestMode testPaths{};
    QTemporaryDir pluginRoot;
    QTemporaryDir fileDir;
    QStringList loadErrors;
    std::shared_ptr<ApplicationPlugins> plugins;
    std::unique_ptr<MainWindow> first;
    std::unique_ptr<MainWindow> second;
    QString path;
    CrawlerWidget* crawler = nullptr;
    CrawlerWidget* secondCrawler = nullptr;
};

} // namespace

SCENARIO( "A plugin adds items to the context menu of the Log Lines",
          "[ui][plugins][plugincontextmenu]" )
{
    GIVEN( "Two shown windows with a Log File each, and a plugin that added two context menu "
           "items, one of them in submenus" )
    {
        ContextMenuWindows windows;
        PluginLibrary plugin;
        auto& textView = CrawlerAccess::textView( *windows.crawler );
        const auto onRow = [ & ]( int row ) { return TextViewAccess::pointOnRow( textView, row ); };

        THEN( "both get an id, and the Text View of each window ends its context menu with "
              "them, below a separator" )
        {
            REQUIRE( plugin.id( PluginLibrary::Item ) > 0 );
            REQUIRE( plugin.id( PluginLibrary::Nested ) > plugin.id( PluginLibrary::Item ) );
            for ( auto* crawler : { windows.crawler, windows.secondCrawler } ) {
                const auto menu = TextViewAccess::contextMenu( CrawlerAccess::textView( *crawler ),
                                                               onRow( 0 ) );
                REQUIRE( pluginSectionOf( *menu ) == PluginSection );
                REQUIRE( menu->actions().size() > PluginSection.size() );
                REQUIRE( hasNested( *menu ) );
            }
        }

        WHEN( "the user right-clicks a Log Line that is not selected and chooses an item" )
        {
            textView.selectAndDisplayLine( 0_lnum );
            QTest::qWait( 20 );
            rightClickAndChoose( *textView.viewport(), onRow( 2 ), ProbeItem );

            THEN( "the item is not called while the menu delivers the choice" )
            {
                REQUIRE( plugin.runs( PluginLibrary::Item ) == 0 );
            }

            AND_WHEN( "the event loop turns" )
            {
                QTest::qWait( 50 );

                THEN( "the plugin is handed that Log Line alone, numbered from 1, in the Text "
                      "View of the Log File, on the UI thread, with no selected text" )
                {
                    REQUIRE( plugin.runs( PluginLibrary::Item ) == 1 );
                    REQUIRE( plugin.runs( PluginLibrary::Nested ) == 0 );
                    const auto choice = plugin.last();
                    REQUIRE( choice.size == sizeof( LogSquirlContextMenuContext ) );
                    REQUIRE( choice.view == LOGSQUIRL_LOG_VIEW_TEXT );
                    REQUIRE( QFileInfo( choice.filePath ) == QFileInfo( windows.path ) );
                    REQUIRE( choice.lineNumbers == std::vector<uint64_t>{ 3 } );
                    REQUIRE( choice.selectedLineCount == 1 );
                    REQUIRE_FALSE( choice.selectedText.has_value() );
                    REQUIRE( choice.ranHere );
                }
            }
        }

        WHEN( "the user selects four Log Lines, right-clicks within them and chooses an item" )
        {
            QTest::mouseClick( textView.viewport(), Qt::LeftButton, {}, onRow( 1 ) );
            QTest::mouseClick( textView.viewport(), Qt::LeftButton, Qt::ShiftModifier, onRow( 4 ) );
            rightClickAndChoose( *textView.viewport(), onRow( 2 ), ProbeItem );
            QTest::qWait( 50 );

            THEN( "the plugin is handed all four, in the order of the Log File" )
            {
                REQUIRE( plugin.runs( PluginLibrary::Item ) == 1 );
                const auto choice = plugin.last();
                REQUIRE( choice.lineNumbers == std::vector<uint64_t>{ 2, 3, 4, 5 } );
                REQUIRE( choice.selectedLineCount == 4 );
                REQUIRE_FALSE( choice.selectedText.has_value() );
            }
        }

        WHEN( "a word is selected within a Log Line, and the user chooses the item in the "
              "submenus" )
        {
            // "line2 info token2": "info".
            TextViewAccess::selectPortion( textView, 1_lnum, LineColumn( 6 ), LineColumn( 9 ) );
            REQUIRE( textView.getSelectedText() == QStringLiteral( "info" ) );
            choose( *TextViewAccess::contextMenu( textView, onRow( 1 ) ),
                    { QStringLiteral( "Context Probe" ), QStringLiteral( "Sub" ),
                      QStringLiteral( "Nested" ) } );
            QTest::qWait( 50 );

            THEN( "the plugin is handed the word, and its Log Line" )
            {
                REQUIRE( plugin.runs( PluginLibrary::Nested ) == 1 );
                const auto choice = plugin.last();
                REQUIRE( choice.selectedText == QStringLiteral( "info" ) );
                REQUIRE( choice.lineNumbers == std::vector<uint64_t>{ 2 } );
            }
        }

        WHEN( "every Log Line is selected and the user chooses an item" )
        {
            textView.selectAll();
            choose( *TextViewAccess::contextMenu( textView, onRow( 0 ) ), { ProbeItem } );
            QTest::qWait( 50 );

            THEN( "the plugin is handed the first 1000, and told how many were selected" )
            {
                const auto choice = plugin.last();
                REQUIRE( choice.lineNumbers.size() == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                REQUIRE( choice.lineNumbers.front() == 1 );
                REQUIRE( choice.lineNumbers.back() == LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES );
                REQUIRE( choice.selectedLineCount == ContextMenuWindows::LineCount );
            }
        }

        WHEN( "the user searches, right-clicks the second Log Line of the Filtered View and "
              "chooses an item" )
        {
            CrawlerAccess::search( *windows.crawler, QStringLiteral( "ERROR" ) );
            auto& filteredView = CrawlerAccess::filteredView( *windows.crawler );
            const auto menu = TextViewAccess::contextMenu(
                filteredView, TextViewAccess::pointOnRow( filteredView, 0 ) );
            REQUIRE( pluginSectionOf( *menu ) == PluginSection );
            rightClickAndChoose( *filteredView.viewport(),
                                 TextViewAccess::pointOnRow( filteredView, 1 ), ProbeItem );
            QTest::qWait( 50 );

            THEN( "the plugin is handed the number the Log File gives that Log Line, in the "
                  "Filtered View" )
            {
                REQUIRE( plugin.runs( PluginLibrary::Item ) == 1 );
                const auto choice = plugin.last();
                REQUIRE( choice.view == LOGSQUIRL_LOG_VIEW_FILTERED );
                // The ERROR Log Lines are 3, 7, 11 ...
                REQUIRE( choice.lineNumbers == std::vector<uint64_t>{ 7 } );
            }
        }

        WHEN( "the Log File is shown in the Table View, a word of a cell is selected and the "
              "user chooses an item" )
        {
            CrawlerAccess::showTableView( *windows.crawler );
            auto& tableView = CrawlerAccess::tableView( *windows.crawler );
            REQUIRE( waitUiState( [ & ] {
                return tableView.model() != nullptr && tableView.model()->rowCount() > 2;
            } ) );
            int bodyColumn = -1;
            for ( int column = 0; column < tableView.model()->columnCount(); ++column ) {
                if ( tableView.model()->index( 2, column ).data().toString()
                     == QStringLiteral( "ERROR token3" ) ) {
                    bodyColumn = column;
                }
            }
            REQUIRE( bodyColumn >= 0 );
            // As pressing the cell does, the Row is selected first.
            tableView.selectRow( 2 );
            TableViewAccess::selectInCell( tableView, 2, bodyColumn, 6, 12 );
            const auto cell = tableView.visualRect( tableView.model()->index( 2, bodyColumn ) );
            const auto menu = TableViewAccess::contextMenu( tableView, cell.center() );
            REQUIRE( pluginSectionOf( *menu ) == PluginSection );
            REQUIRE( hasNested( *menu ) );
            choose( *menu, { ProbeItem } );
            QTest::qWait( 50 );

            THEN( "the plugin is handed the word, its Log Line, in the Table View" )
            {
                REQUIRE( plugin.runs( PluginLibrary::Item ) == 1 );
                const auto choice = plugin.last();
                REQUIRE( choice.view == LOGSQUIRL_LOG_VIEW_TABLE );
                REQUIRE( choice.selectedText == QStringLiteral( "token3" ) );
                REQUIRE( choice.lineNumbers == std::vector<uint64_t>{ 3 } );
            }
        }

        WHEN( "the plugin unregisters the item in the submenus" )
        {
            plugin.unregister( plugin.id( PluginLibrary::Nested ) );

            THEN( "no context menu shows it, nor its submenus, in either window" )
            {
                for ( auto* crawler : { windows.crawler, windows.secondCrawler } ) {
                    const auto menu = TextViewAccess::contextMenu(
                        CrawlerAccess::textView( *crawler ), onRow( 0 ) );
                    REQUIRE( pluginSectionOf( *menu ) == QStringList{ ProbeItem } );
                }
            }

            AND_WHEN( "it unregisters the other one too" )
            {
                plugin.unregister( plugin.id( PluginLibrary::Item ) );

                THEN( "the menu has no plugin section, and no separator at its end" )
                {
                    const auto menu = TextViewAccess::contextMenu( textView, onRow( 0 ) );
                    REQUIRE( entryAt( *menu, { ProbeItem } ) == nullptr );
                    REQUIRE_FALSE( menu->actions().back()->isSeparator() );
                }
            }
        }

        WHEN( "the user chooses an item, and the plugin unregisters it before the event loop "
              "turns" )
        {
            choose( *TextViewAccess::contextMenu( textView, onRow( 0 ) ), { ProbeItem } );
            plugin.unregister( plugin.id( PluginLibrary::Item ) );
            QTest::qWait( 50 );

            THEN( "the choice does not reach the plugin" )
            {
                REQUIRE( plugin.runs( PluginLibrary::Item ) == 0 );
            }
        }

        WHEN( "the user chooses an item, and the tab closes before the event loop turns" )
        {
            choose( *TextViewAccess::contextMenu( textView, onRow( 0 ) ), { ProbeItem } );
            auto* tabs = windows.first->findChild<TabbedCrawlerWidget*>();
            REQUIRE( tabs != nullptr );
            Q_EMIT tabs->tabCloseRequested( tabs->indexOf( windows.crawler ) );
            QTest::qWait( 50 );

            THEN( "the choice does not reach the plugin" )
            {
                REQUIRE( plugin.runs( PluginLibrary::Item ) == 0 );
            }
        }

        WHEN( "the plugin is unloaded" )
        {
            windows.plugins->host().unloadPlugin( PluginId );

            THEN( "the context menus show none of its items" )
            {
                for ( auto* crawler : { windows.crawler, windows.secondCrawler } ) {
                    const auto menu = TextViewAccess::contextMenu(
                        CrawlerAccess::textView( *crawler ), onRow( 0 ) );
                    REQUIRE( entryAt( *menu, { ProbeItem } ) == nullptr );
                    REQUIRE_FALSE( menu->actions().back()->isSeparator() );
                }
            }
        }
    }
}
