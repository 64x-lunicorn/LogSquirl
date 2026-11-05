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

// The menu bar is ordered by task: seven menus instead of ten (#892). The
// actions of the ten menus it had before are listed here with the shortcut
// each had: none is lost on the way, and each keeps its shortcut and its
// entry in the Shortcuts list.

#include "applicationplugins.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "csvexportdialog.h"
#include "filteredview.h"
#include "loadingstatus.h"
#include "logformatcatalog.h"
#include "logmainview.h"
#include "logtableview.h"
#include "mainwindow.h"
#include "session.h"
#include "shortcuts.h"
#include "shown_widget.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace {

/// An action of the menu bar, where it is and the shortcut it answers to.
struct MenuEntry {
    QString path;
    QString text;
    QAction* action = nullptr;
};

QString plain( QString text )
{
    return text.remove( u'&' );
}

/// Every action of the menu, the submenus and their actions too, with the
/// path of titles to it.
void collect( const QMenu& menu, const QString& path, std::vector<MenuEntry>& entries )
{
    for ( auto* action : menu.actions() ) {
        if ( action->isSeparator() ) {
            continue;
        }
        if ( auto* submenu = action->menu() ) {
            entries.push_back( { path, plain( submenu->title() ), action } );
            collect( *submenu, path + u'/' + plain( submenu->title() ), entries );
            continue;
        }
        entries.push_back( { path, plain( action->text() ), action } );
    }
}

std::vector<MenuEntry> menuBarEntries( MainWindow& window )
{
    std::vector<MenuEntry> entries;
    for ( auto* top : window.menuBar()->actions() ) {
        if ( auto* menu = top->menu() ) {
            collect( *menu, plain( menu->title() ), entries );
        }
    }
    return entries;
}

QStringList topLevelTitles( MainWindow& window )
{
    QStringList titles;
    for ( auto* top : window.menuBar()->actions() ) {
        if ( top->menu() ) {
            titles.append( plain( top->menu()->title() ) );
        }
    }
    return titles;
}

/// The entries of the menu bar with the text.
std::vector<MenuEntry> entriesNamed( const std::vector<MenuEntry>& entries, const QString& text )
{
    std::vector<MenuEntry> found;
    for ( const auto& entry : entries ) {
        if ( entry.text == text ) {
            found.push_back( entry );
        }
    }
    return found;
}

/// An action of the menu bar before #892, and the Shortcuts list entry that
/// gives it its keys, or nullptr for an action without one.
struct ListedAction {
    const char* text;
    const char* shortcutId;
};

// Every action of the ten menus before #892 (File, Edit, View, Tools,
// Highlighters, Encoding, Favorites, Plugins, Sources, Help), in the order
// they were in. Entries made from data -- recent files, favorite files,
// opened files, Highlighter Sets, Encodings other than Auto, data source
// plugins -- are left out, and so is the Opened files submenu's own entry:
// the submenu is filled once the window has a Theme or a Log File.
const std::vector<ListedAction> actionsBefore892 = {
    // File
    { "New window", ShortcutAction::MainWindowNewWindow },
    { "Open...", ShortcutAction::MainWindowOpenFile },
    { "Open from clipboard", ShortcutAction::MainWindowOpenFromClipboard },
    { "Open Command Output...", ShortcutAction::MainWindowOpenCommandOutput },
    { "Open from URL...", ShortcutAction::MainWindowOpenFromUrl },
    { "Clear List", nullptr },
    { "Open Session...", ShortcutAction::MainWindowOpenSession },
    { "Save Session As...", ShortcutAction::MainWindowSaveSessionAs },
    { "Close", ShortcutAction::MainWindowCloseFile },
    { "Close All", ShortcutAction::MainWindowCloseAll },
    { "Preferences...", ShortcutAction::MainWindowPreference },
    { "Exit", ShortcutAction::MainWindowQuit },
    // Edit
    { "Copy", ShortcutAction::MainWindowCopy },
    { "Copy as Shown", nullptr },
    { "Select All", ShortcutAction::MainWindowSelectAll },
    { "Find...", ShortcutAction::MainWindowOpenQf },
    { "Go to line...", ShortcutAction::LogViewJumpToLine },
    { "Go to timestamp...", ShortcutAction::LogViewJumpToTimestamp },
    { "Set search limits to time range...", ShortcutAction::LogViewSearchLimitsTimeRange },
    { "Set search limits around current line...", ShortcutAction::LogViewSearchLimitsAroundLine },
    { "Copy full path", ShortcutAction::MainWindowCopyPathToClipboard },
    { "Open containing folder", ShortcutAction::MainWindowOpenContainingFolder },
    { "Open in editor", ShortcutAction::MainWindowOpenInEditor },
    { "Clear file...", ShortcutAction::MainWindowClearFile },
    // View
    { "Matches overview", nullptr },
    { "Line numbers in main view", nullptr },
    { "Line numbers in filtered view", nullptr },
    { "Wrap text", ShortcutAction::MainWindowTextWrap },
    { "Show Value Names", ShortcutAction::MainWindowShowValueNames },
    { "Matches Only", nullptr },
    { "Follow File", ShortcutAction::MainWindowFollowFile },
    { "Reload", ShortcutAction::MainWindowReload },
    { "Chart Panel", nullptr },
    { "Show Filter Frequency", nullptr },
    // Tools
    { "Predefined filters...", nullptr },
    { "Value Names...", nullptr },
    { "Import Chipmunk filters...", nullptr },
    { "Regex Lab...", nullptr },
    { "Manage Tab Groups...", nullptr },
    { "Scratchpad", ShortcutAction::MainWindowScratchpad },
    { "Filters panel", nullptr },
    { "Value Names tab", nullptr },
    { "Command Palette...", ShortcutAction::MainWindowCommandPalette },
    // Highlighters
    { "Configure highlighters...", nullptr },
    // Encoding
    { "Auto", nullptr },
    // Favorites
    { "Add to favorites", nullptr },
    { "Remove from favorites...", nullptr },
    // Plugins
    { "Plugin Management...", nullptr },
    // Help
    { "Documentation...", nullptr },
    { "Report issue...", nullptr },
    { "Generate crash dump", nullptr },
    { "About Qt", nullptr },
    { "About", nullptr },
};

/// The path of the one entry of the menu bar with the text.
QString pathOf( const std::vector<MenuEntry>& entries, const QString& text )
{
    const auto found = entriesNamed( entries, text );
    REQUIRE( found.size() == 1 );
    return found.front().path;
}

struct ShownWindow {
    ShownWindow()
        : session( std::make_shared<Session>( testSettingsPolicies(),
                                              std::make_shared<LogFormatCatalog>() ) )
        , window( std::make_unique<MainWindow>(
              WindowSession{ session, QStringLiteral( "MenuBar" ), 0 },
              std::make_shared<logsquirl::plugins::ApplicationPlugins>() ) )
    {
        showUntilExposed( *window );
        QTest::qWait( 50 );
    }

    std::shared_ptr<Session> session;
    std::unique_ptr<MainWindow> window;
};

/// The keys configured in the Shortcuts list, these for its lifetime.
class ConfiguredShortcuts {
public:
    explicit ConfiguredShortcuts( const ShortcutAction::ConfiguredShortcuts& shortcuts )
        : stored_( Configuration::get().shortcuts() )
    {
        Configuration::get().setShortcuts( shortcuts );
    }
    ~ConfiguredShortcuts()
    {
        Configuration::get().setShortcuts( stored_ );
    }
    ConfiguredShortcuts( const ConfiguredShortcuts& ) = delete;
    ConfiguredShortcuts& operator=( const ConfiguredShortcuts& ) = delete;

private:
    ShortcutAction::ConfiguredShortcuts stored_;
};

/// The one action of the menu bar with the text.
QAction* menuBarAction( MainWindow& window, const QString& text )
{
    const auto found = entriesNamed( menuBarEntries( window ), text );
    REQUIRE( found.size() == 1 );
    return found.front().action;
}

// A regex Log Format: a timestamp, a level, a component and a body.
const char* const MenuBarFormatJson = R"({
    "menubar_test_log": {
        "title": "Menu Bar Test",
        "regex": {
            "basic": {
                "pattern": "^MENUBAR (?<timestamp>\\d{6}) (?<level>[A-Z]+) \\[(?<component>[a-z]+)\\] (?<body>.*)$"
            }
        },
        "timestamp-field": "timestamp",
        "level-field": "level",
        "body-field": "body",
        "value": {
            "component": { "kind": "string", "identifier": true }
        },
        "sample": [{ "line": "MENUBAR 000001 INFO [main] hello" }]
    }
})";

void writeFile( const QString& path, const QByteArray& bytes )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( file.write( bytes ) == bytes.size() );
}

/// Log Lines of the test's Log Format.
QByteArray formattedLines( int count )
{
    QByteArray bytes;
    for ( int number = 0; number < count; ++number ) {
        bytes += QStringLiteral( "MENUBAR %1 INFO [main] line %2\n" )
                     .arg( number, 6, 10, QChar( '0' ) )
                     .arg( number )
                     .toUtf8();
    }
    return bytes;
}

/// A shown, active window whose Log Files are recognized against the test's
/// Log Format alone, and a folder of Log Files.
struct FormatWindow {
    FormatWindow()
    {
        REQUIRE( formatDir.isValid() );
        REQUIRE( fileDir.isValid() );
        writeFile( QDir( formatDir.path() ).filePath( "menubar_test.json" ), MenuBarFormatJson );
        auto catalog = std::make_shared<LogFormatCatalog>();
        catalog->loadFromDirectory( formatDir.path() );
        REQUIRE( catalog->formatCount() == 1 );

        auto policies = testSettingsPolicies();
        policies.recognition.enabled = true;
        window = std::make_unique<MainWindow>(
            WindowSession{ std::make_shared<Session>( policies, catalog ),
                           QStringLiteral( "MenuBarFormats" ), 0 },
            std::make_shared<logsquirl::plugins::ApplicationPlugins>() );
        showUntilExposed( *window );
        window->activateWindow();
        REQUIRE( QTest::qWaitForWindowActive( window.get(), 5000 ) );
        tabs = window->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabs != nullptr );
    }

    ~FormatWindow()
    {
        window.reset();
        QTest::qWait( 50 );
    }

    FormatWindow( const FormatWindow& ) = delete;
    FormatWindow& operator=( const FormatWindow& ) = delete;

    /// Writes bytes into a new file named name in the folder, and returns its path.
    QString logFile( const QString& name, const QByteArray& bytes )
    {
        const auto path = fileDir.filePath( name );
        writeFile( path, bytes );
        return path;
    }

    /// Opens the Log File in a tab of its own, in front, once it has loaded.
    CrawlerWidget& open( const QString& path )
    {
        auto* before = tabs->currentWidget();
        window->loadFileNonInteractive( path );
        CrawlerWidget* crawler = nullptr;
        REQUIRE( waitUiState(
            [ & ] {
                crawler = qobject_cast<CrawlerWidget*>( tabs->currentWidget() );
                return crawler != nullptr && crawler != before
                       && crawler->state().loadStatus == LoadingStatus::Successful;
            },
            10'000 ) );
        // The Log Format is recognized once the tab heard the load finished.
        QTest::qWait( 100 );
        return *crawler;
    }

    QTemporaryDir formatDir;
    QTemporaryDir fileDir;
    std::unique_ptr<MainWindow> window;
    TabbedCrawlerWidget* tabs = nullptr;
};

/// Cancels the next dialog Export as CSV, and keeps the view that asked it.
class CsvDialogCanceller {
public:
    CsvDialogCanceller()
    {
        QObject::connect( &driver_, &QTimer::timeout, [ this ] {
            auto* dialog = qobject_cast<CsvExportDialog*>( QApplication::activeModalWidget() );
            if ( dialog != nullptr && dialog->isVisible() ) {
                driver_.stop();
                askedBy_ = dialog->parentWidget();
                dialog->reject();
            }
        } );
        driver_.start( 10 );
    }

    QWidget* askedBy() const
    {
        return askedBy_;
    }

private:
    QPointer<QWidget> askedBy_;
    QTimer driver_;
};

/// The Filtered View the crawler shows.
FilteredView* shownFilteredView( const CrawlerWidget& crawler )
{
    for ( auto* view : crawler.findChildren<FilteredView*>() ) {
        if ( view->isVisible() ) {
            return view;
        }
    }
    return nullptr;
}

} // namespace

SCENARIO( "No action of the menu bar is lost, and each keeps its shortcut", "[ui][menubar]" )
{
    GIVEN( "a shown window" )
    {
        const ShownWindow shown;
        const auto entries = menuBarEntries( *shown.window );

        THEN( "every action the menus had before #892 is in the menu bar once, with the keys of "
              "its Shortcuts list entry" )
        {
            const auto& shortcutList = ShortcutAction::defaultShortcutList();
            for ( const auto& listed : actionsBefore892 ) {
                const auto text = QString::fromLatin1( listed.text );
                INFO( text.toStdString() );
                const auto found = entriesNamed( entries, text );
                REQUIRE( found.size() == 1 );
                const auto* action = found.front().action;
                if ( listed.shortcutId == nullptr ) {
                    CHECK( action->shortcuts().isEmpty() );
                    continue;
                }
                CHECK( shortcutList.contains( listed.shortcutId ) );
                const auto keys = ShortcutAction::shortcutKeys( listed.shortcutId, {} );
                CHECK( action->shortcuts() == keys );
            }
        }

        THEN( "the Encoding submenu still lists the Encodings" )
        {
            CHECK( !entriesNamed( entries, QStringLiteral( "UTF-8" ) ).empty() );
        }
    }
}

SCENARIO( "The menu bar is ordered by task in seven menus", "[ui][menubar]" )
{
    GIVEN( "a shown window" )
    {
        const ShownWindow shown;
        const auto entries = menuBarEntries( *shown.window );

        THEN( "it has File, Edit, View, Tools, Favorites, Sources and Help" )
        {
            CHECK(
                topLevelTitles( *shown.window )
                == QStringList{ "File", "Edit", "View", "Tools", "Favorites", "Sources", "Help" } );
        }

        THEN( "the file actions are in the File menu" )
        {
            for ( const auto* text :
                  { "Copy full path", "Open containing folder", "Open in editor", "Clear file...",
                    "Open Session...", "Save Session As...", "Close", "Exit" } ) {
                INFO( text );
                CHECK( pathOf( entries, QString::fromLatin1( text ) ) == "File" );
            }
            CHECK( pathOf( entries, QStringLiteral( "Clear List" ) ) == "File/Open Recent" );
        }

        THEN( "copy, select, find, Go to line and the Search Limits are in the Edit menu" )
        {
            for ( const auto* text :
                  { "Copy", "Copy as Shown", "Select All", "Find...", "Go to line...",
                    "Go to timestamp...", "Set search limits to time range...",
                    "Set search limits around current line..." } ) {
                INFO( text );
                CHECK( pathOf( entries, QString::fromLatin1( text ) ) == "Edit" );
            }
        }

        THEN( "the panels, the sidebar and Encoding are in the View menu" )
        {
            for ( const auto* text :
                  { "Sidebar", "Filters panel", "Scratchpad", "Value Names tab", "Chart Panel",
                    "Show Filter Frequency", "Show Value Names", "Wrap text", "Follow File",
                    "Matches overview", "Reload" } ) {
                INFO( text );
                CHECK( pathOf( entries, QString::fromLatin1( text ) ) == "View" );
            }
            CHECK( pathOf( entries, QStringLiteral( "Auto" ) ) == "View/Encoding" );
            CHECK( pathOf( entries, QStringLiteral( "Opened files" ) ) == "View" );
            CHECK( pathOf( entries, QStringLiteral( "Encoding" ) ) == "View" );
        }

        THEN( "Highlighters, the dialogs, the Plugins and Options are in the Tools menu" )
        {
            CHECK( pathOf( entries, QStringLiteral( "Highlighters" ) ) == "Tools" );
            CHECK( pathOf( entries, QStringLiteral( "Configure highlighters..." ) )
                   == "Tools/Highlighters" );
            CHECK( pathOf( entries, QStringLiteral( "Plugins" ) ) == "Tools" );
            for ( const auto* text :
                  { "Predefined filters...", "Value Names...", "Import Chipmunk filters...",
                    "Regex Lab...", "Manage Tab Groups...", "Command Palette...",
                    "Preferences..." } ) {
                INFO( text );
                CHECK( pathOf( entries, QString::fromLatin1( text ) ) == "Tools" );
            }
            CHECK( pathOf( entries, QStringLiteral( "Plugin Management..." ) ) == "Tools/Plugins" );
        }
    }
}

// Merging, the CSV export and the sidebar are where a user looks for them in
// the menu bar, each the action reached elsewhere before (#890).
SCENARIO( "Merge Log Files and Export as CSV are in the File menu, each with its Shortcuts list "
          "entry",
          "[ui][menubar]" )
{
    GIVEN( "a shown window without a Log File, no keys configured" )
    {
        const ConfiguredShortcuts configured( ShortcutAction::ConfiguredShortcuts{} );
        const ShownWindow shown;
        const auto entries = menuBarEntries( *shown.window );

        THEN( "File holds Merge Log Files... and Export as CSV..., both disabled" )
        {
            CHECK( pathOf( entries, QStringLiteral( "Merge Log Files..." ) ) == "File" );
            CHECK( pathOf( entries, QStringLiteral( "Export as CSV..." ) ) == "File" );
            CHECK_FALSE( menuBarAction( *shown.window, QStringLiteral( "Merge Log Files..." ) )
                             ->isEnabled() );
            CHECK_FALSE(
                menuBarAction( *shown.window, QStringLiteral( "Export as CSV..." ) )->isEnabled() );
        }

        THEN( "the three new entries take their keys from their Shortcuts list entries" )
        {
            const auto& shortcutList = ShortcutAction::defaultShortcutList();
            const std::vector<ListedAction> added = {
                { "Merge Log Files...", ShortcutAction::MainWindowMergeLogFiles },
                { "Export as CSV...", ShortcutAction::MainWindowExportAsCsv },
                { "Sidebar", ShortcutAction::MainWindowToggleSidebar },
            };
            for ( const auto& listed : added ) {
                INFO( listed.text );
                REQUIRE( shortcutList.contains( listed.shortcutId ) );
                const auto* action
                    = menuBarAction( *shown.window, QString::fromLatin1( listed.text ) );
                CHECK( action->shortcuts()
                       == ShortcutAction::shortcutKeys( listed.shortcutId, {} ) );
            }
        }
    }

    GIVEN( "keys configured for the three new entries" )
    {
        const ConfiguredShortcuts configured(
            { { ShortcutAction::MainWindowMergeLogFiles, { "Ctrl+Alt+M" } },
              { ShortcutAction::MainWindowExportAsCsv, { "Ctrl+Alt+E" } },
              { ShortcutAction::MainWindowToggleSidebar, { "Ctrl+Alt+B" } } } );

        WHEN( "a window is shown" )
        {
            const ShownWindow shown;

            THEN( "each entry answers to its keys" )
            {
                CHECK( menuBarAction( *shown.window, QStringLiteral( "Merge Log Files..." ) )
                           ->shortcuts()
                       == QList<QKeySequence>{ QKeySequence( "Ctrl+Alt+M" ) } );
                CHECK( menuBarAction( *shown.window, QStringLiteral( "Export as CSV..." ) )
                           ->shortcuts()
                       == QList<QKeySequence>{ QKeySequence( "Ctrl+Alt+E" ) } );
                CHECK( menuBarAction( *shown.window, QStringLiteral( "Sidebar" ) )->shortcuts()
                       == QList<QKeySequence>{ QKeySequence( "Ctrl+Alt+B" ) } );
            }
        }
    }
}

SCENARIO( "File > Export as CSV... exports the tab in front as its context menu does",
          "[ui][menubar][csvexport]" )
{
    GIVEN( "a window that recognizes the test's Log Format" )
    {
        FormatWindow window;
        auto* exportAsCsv = menuBarAction( *window.window, QStringLiteral( "Export as CSV..." ) );

        THEN( "Export as CSV... is disabled without a Log File" )
        {
            REQUIRE_FALSE( exportAsCsv->isEnabled() );
        }

        AND_GIVEN( "a Log File no Log Format applies to, in front" )
        {
            window.open( window.logFile( "plain.log", "just a line\nanother line\n" ) );

            THEN( "Export as CSV... is disabled, as the views' context menus offer none" )
            {
                REQUIRE_FALSE( exportAsCsv->isEnabled() );
            }
        }

        AND_GIVEN( "two Log Files of the Log Format open, the second in front" )
        {
            window.open( window.logFile( "first.log", formattedLines( 60 ) ) );
            auto& front = window.open( window.logFile( "second.log", formattedLines( 60 ) ) );
            REQUIRE( front.state().hasTableView );
            REQUIRE( exportAsCsv->isEnabled() );

            WHEN( "the Text View has the focus and Export as CSV... is chosen" )
            {
                auto* textView = front.findChild<LogMainView*>();
                REQUIRE( textView != nullptr );
                textView->setFocus();
                QCoreApplication::processEvents();

                const CsvDialogCanceller canceller;
                exportAsCsv->trigger();

                THEN( "the Table View of the tab in front asks, as its context menu's entry "
                      "does" )
                {
                    REQUIRE( canceller.askedBy() != nullptr );
                    REQUIRE( qobject_cast<LogTableView*>( canceller.askedBy() ) != nullptr );
                    REQUIRE( front.isAncestorOf( canceller.askedBy() ) );
                }
            }

            WHEN( "the Filtered View has the focus and Export as CSV... is chosen" )
            {
                auto* filteredView = shownFilteredView( front );
                REQUIRE( filteredView != nullptr );
                filteredView->setFocus();
                QCoreApplication::processEvents();

                const CsvDialogCanceller canceller;
                exportAsCsv->trigger();

                THEN( "the Filtered View of the tab in front asks" )
                {
                    REQUIRE( canceller.askedBy() == filteredView );
                }
            }

            WHEN( "every Log File is closed" )
            {
                menuBarAction( *window.window, QStringLiteral( "Close All" ) )->trigger();
                REQUIRE( waitUiState(
                    [ & ] {
                        return qobject_cast<CrawlerWidget*>( window.tabs->currentWidget() )
                               == nullptr;
                    },
                    5000 ) );

                THEN( "Export as CSV... is disabled" )
                {
                    REQUIRE_FALSE( exportAsCsv->isEnabled() );
                }
            }
        }
    }
}

SCENARIO( "View > Sidebar is checked while the sidebar shows, as the toolbar button is",
          "[ui][menubar][sidebar]" )
{
    GIVEN( "a shown window, its sidebar hidden" )
    {
        const ShownWindow shown;
        auto* sidebar = shown.window->findChild<QDockWidget*>( "sidebarDock" );
        REQUIRE( sidebar != nullptr );
        REQUIRE_FALSE( sidebar->isVisible() );

        auto* viewSidebar = menuBarAction( *shown.window, QStringLiteral( "Sidebar" ) );
        auto* toolBar = shown.window->findChild<QToolBar*>();
        REQUIRE( toolBar != nullptr );
        // One action, in the View menu and on the toolbar.
        REQUIRE( toolBar->actions().contains( viewSidebar ) );
        auto* button = qobject_cast<QToolButton*>( toolBar->widgetForAction( viewSidebar ) );
        REQUIRE( button != nullptr );

        THEN( "View > Sidebar and the toolbar button are unchecked" )
        {
            REQUIRE( viewSidebar->isCheckable() );
            REQUIRE_FALSE( viewSidebar->isChecked() );
            REQUIRE_FALSE( button->isChecked() );
        }

        WHEN( "View > Sidebar is chosen" )
        {
            viewSidebar->trigger();
            QCoreApplication::processEvents();

            THEN( "the sidebar shows, and both are checked" )
            {
                REQUIRE( sidebar->isVisible() );
                REQUIRE( viewSidebar->isChecked() );
                REQUIRE( button->isChecked() );
            }

            AND_WHEN( "the toolbar button is clicked" )
            {
                button->click();
                QCoreApplication::processEvents();

                THEN( "the sidebar is hidden, and both are unchecked" )
                {
                    REQUIRE_FALSE( sidebar->isVisible() );
                    REQUIRE_FALSE( viewSidebar->isChecked() );
                    REQUIRE_FALSE( button->isChecked() );
                }
            }

            AND_WHEN( "the sidebar is closed by its own close button" )
            {
                sidebar->close();
                QCoreApplication::processEvents();

                THEN( "both are unchecked" )
                {
                    REQUIRE_FALSE( sidebar->isVisible() );
                    REQUIRE_FALSE( viewSidebar->isChecked() );
                    REQUIRE_FALSE( button->isChecked() );
                }
            }
        }

        WHEN( "the Filters panel is shown from the View menu" )
        {
            menuBarAction( *shown.window, QStringLiteral( "Filters panel" ) )->trigger();
            QCoreApplication::processEvents();

            THEN( "the sidebar shows, and View > Sidebar is checked" )
            {
                REQUIRE( sidebar->isVisible() );
                REQUIRE( viewSidebar->isChecked() );
            }
        }
    }
}
