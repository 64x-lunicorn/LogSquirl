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
#include "menu_wording.h"
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
#include <QTranslator>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <utility>
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
// the submenu is filled once the window has a Theme or a Log File. Each is
// named as it is since #891, in title case.
const std::vector<ListedAction> actionsBefore892 = {
    // File
    { "New Window", ShortcutAction::MainWindowNewWindow },
    { "Open...", ShortcutAction::MainWindowOpenFile },
    { "Open from Clipboard", ShortcutAction::MainWindowOpenFromClipboard },
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
    { "Go to Line...", ShortcutAction::LogViewJumpToLine },
    { "Go to Timestamp...", ShortcutAction::LogViewJumpToTimestamp },
    { "Set Search Limits to Time Range...", ShortcutAction::LogViewSearchLimitsTimeRange },
    { "Set Search Limits Around Current Line...", ShortcutAction::LogViewSearchLimitsAroundLine },
    { "Copy Full Path", ShortcutAction::MainWindowCopyPathToClipboard },
    { "Open Containing Folder", ShortcutAction::MainWindowOpenContainingFolder },
    { "Open in Editor", ShortcutAction::MainWindowOpenInEditor },
    { "Clear File...", ShortcutAction::MainWindowClearFile },
    // View
    { "Matches Overview", nullptr },
    { "Line Numbers in Main View", nullptr },
    { "Line Numbers in Filtered View", nullptr },
    { "Wrap Text", ShortcutAction::MainWindowTextWrap },
    { "Show Value Names", ShortcutAction::MainWindowShowValueNames },
    { "Matches Only", nullptr },
    { "Follow File", ShortcutAction::MainWindowFollowFile },
    { "Reload", ShortcutAction::MainWindowReload },
    { "Chart Panel", nullptr },
    { "Show Filter Frequency", nullptr },
    // Tools
    { "Predefined Filters...", nullptr },
    { "Value Names...", nullptr },
    { "Import Chipmunk Filters...", nullptr },
    { "Regex Lab...", nullptr },
    { "Manage Tab Groups...", nullptr },
    { "Scratchpad", ShortcutAction::MainWindowScratchpad },
    { "Filters Panel", nullptr },
    { "Value Names Tab", nullptr },
    { "Command Palette...", ShortcutAction::MainWindowCommandPalette },
    // Highlighters
    { "Configure Highlighters...", nullptr },
    // Encoding
    { "Auto", nullptr },
    // Favorites
    { "Add to Favorites", nullptr },
    { "Remove from Favorites...", nullptr },
    // Plugins
    { "Plugin Management...", nullptr },
    // Help
    { "Documentation...", nullptr },
    { "Report Issue...", nullptr },
    { "Generate Crash Dump", nullptr },
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

/// The menus of the menu bar and their submenus, each with its path.
void collectMenus( QMenu& menu, const QString& path,
                   std::vector<std::pair<QString, QMenu*>>& menus )
{
    menus.emplace_back( path, &menu );
    for ( auto* action : menu.actions() ) {
        if ( auto* submenu = action->menu() ) {
            collectMenus( *submenu, path + u'/' + plain( submenu->title() ), menus );
        }
    }
}

std::vector<std::pair<QString, QMenu*>> menuBarMenus( MainWindow& window )
{
    std::vector<std::pair<QString, QMenu*>> menus;
    for ( auto* top : window.menuBar()->actions() ) {
        if ( auto* menu = top->menu() ) {
            collectMenus( *menu, plain( menu->title() ), menus );
        }
    }
    return menus;
}

/// Whether macOS moves the action to its application menu, by its role or by
/// the words Qt reads the role from.
bool movesToApplicationMenu( const QAction& action )
{
    switch ( action.menuRole() ) {
    case QAction::AboutRole:
    case QAction::AboutQtRole:
    case QAction::PreferencesRole:
    case QAction::QuitRole:
        return true;
    case QAction::TextHeuristicRole: {
        const auto text = plain( action.text() ).toLower();
        for ( const auto* word : { "about", "config", "options", "setup", "settings", "preferences",
                                   "quit", "exit" } ) {
            if ( text.startsWith( QLatin1String( word ) ) ) {
                return true;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

/// A key of its own for every entry of the Shortcuts list.
ShortcutAction::ConfiguredShortcuts distinctKeys()
{
    static const QStringList modifiers
        = { "Ctrl+Alt+Shift+", "Ctrl+Alt+", "Ctrl+Shift+", "Alt+Shift+", "Ctrl+", "Alt+" };
    constexpr int functionKeys = 35;
    ShortcutAction::ConfiguredShortcuts keys;
    int index = 0;
    for ( const auto& entry : ShortcutAction::defaultShortcutList() ) {
        REQUIRE( index / functionKeys < modifiers.size() );
        keys[ entry.first ] = { modifiers.at( index / functionKeys )
                                + QStringLiteral( "F%1" ).arg( index % functionKeys + 1 ) };
        ++index;
    }
    return keys;
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
                  { "Copy Full Path", "Open Containing Folder", "Open in Editor", "Clear File...",
                    "Open Session...", "Save Session As...", "Close", "Exit" } ) {
                INFO( text );
                CHECK( pathOf( entries, QString::fromLatin1( text ) ) == "File" );
            }
            CHECK( pathOf( entries, QStringLiteral( "Clear List" ) ) == "File/Open Recent" );
        }

        THEN( "copy, select, find, Go to line and the Search Limits are in the Edit menu" )
        {
            for ( const auto* text :
                  { "Copy", "Copy as Shown", "Select All", "Find...", "Go to Line...",
                    "Go to Timestamp...", "Set Search Limits to Time Range...",
                    "Set Search Limits Around Current Line..." } ) {
                INFO( text );
                CHECK( pathOf( entries, QString::fromLatin1( text ) ) == "Edit" );
            }
        }

        THEN( "the panels, the sidebar and Encoding are in the View menu" )
        {
            for ( const auto* text :
                  { "Sidebar", "Filters Panel", "Scratchpad", "Value Names Tab", "Chart Panel",
                    "Show Filter Frequency", "Show Value Names", "Wrap Text", "Follow File",
                    "Matches Overview", "Reload" } ) {
                INFO( text );
                CHECK( pathOf( entries, QString::fromLatin1( text ) ) == "View" );
            }
            CHECK( pathOf( entries, QStringLiteral( "Auto" ) ) == "View/Encoding" );
            CHECK( pathOf( entries, QStringLiteral( "Opened Files" ) ) == "View" );
            CHECK( pathOf( entries, QStringLiteral( "Encoding" ) ) == "View" );
        }

        THEN( "Highlighters, the dialogs, the Plugins and Options are in the Tools menu" )
        {
            CHECK( pathOf( entries, QStringLiteral( "Highlighters" ) ) == "Tools" );
            CHECK( pathOf( entries, QStringLiteral( "Configure Highlighters..." ) )
                   == "Tools/Highlighters" );
            CHECK( pathOf( entries, QStringLiteral( "Plugins" ) ) == "Tools" );
            for ( const auto* text :
                  { "Predefined Filters...", "Value Names...", "Import Chipmunk Filters...",
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
            menuBarAction( *shown.window, QStringLiteral( "Filters Panel" ) )->trigger();
            QCoreApplication::processEvents();

            THEN( "the sidebar shows, and View > Sidebar is checked" )
            {
                REQUIRE( sidebar->isVisible() );
                REQUIRE( viewSidebar->isChecked() );
            }
        }
    }
}

// The menu bar names, cases and spells its entries one way (#891).
SCENARIO( "The entries of the menu bar are named, cased and spelled one way", "[ui][menubar]" )
{
    GIVEN( "a shown window" )
    {
        const ShownWindow shown;
        const auto entries = menuBarEntries( *shown.window );

        THEN( "every top-level menu has a mnemonic, and so have Highlighters and Plugins" )
        {
            for ( auto* top : shown.window->menuBar()->actions() ) {
                if ( auto* menu = top->menu() ) {
                    INFO( menu->title().toStdString() );
                    CHECK( menuwording::hasMnemonic( menu->title() ) );
                }
            }
            for ( const auto* title : { "Highlighters", "Plugins" } ) {
                INFO( title );
                const auto found = entriesNamed( entries, QString::fromLatin1( title ) );
                REQUIRE( found.size() == 1 );
                REQUIRE( found.front().action->menu() != nullptr );
                CHECK( menuwording::hasMnemonic( found.front().action->menu()->title() ) );
            }
        }

        THEN( "no menu shows two separators in a row, nor does it on macOS once the entries "
              "of the application menu are taken out" )
        {
            for ( const auto& [ path, menu ] : menuBarMenus( *shown.window ) ) {
                INFO( path.toStdString() );
                CHECK_FALSE( menuwording::hasSeparatorsInARow( *menu ) );

                QList<const QAction*> moved;
                for ( const auto* action : menu->actions() ) {
                    if ( movesToApplicationMenu( *action ) ) {
                        moved.append( action );
                    }
                }
                CHECK_FALSE( menuwording::hasSeparatorsInARow( *menu, moved ) );
            }
        }

        THEN( "every entry is in title case, and none ends in the one-character ellipsis" )
        {
            for ( const auto& entry : entries ) {
                const auto text = entry.action->text();
                INFO( ( entry.path + u'/' + text ).toStdString() );
                CHECK_FALSE( menuwording::hasEllipsisCharacter( text ) );
                // Entries made from data -- recent, favorite and opened files,
                // Encodings, Highlighter Sets -- are named by it, and a
                // placeholder in parentheses says there is none.
                if ( entry.action->actionGroup() != nullptr || entry.text.startsWith( u'(' ) ) {
                    continue;
                }
                CHECK( menuwording::isTitleCase( text ) );
            }
        }
    }

    GIVEN( "a key of its own for every entry of the Shortcuts list" )
    {
        const auto keys = distinctKeys();
        const ConfiguredShortcuts configured( keys );

        WHEN( "a window is shown" )
        {
            const ShownWindow shown;

            THEN( "every entry of the menu bar with a key is named as its Shortcuts list entry, "
                  "but for its mnemonic and the dots of a dialog" )
            {
                const auto& shortcutList = ShortcutAction::defaultShortcutList();
                int named = 0;
                for ( const auto& entry : menuBarEntries( *shown.window ) ) {
                    const auto shortcuts = entry.action->shortcuts();
                    if ( shortcuts.isEmpty() ) {
                        continue;
                    }
                    INFO( ( entry.path + u'/' + entry.text ).toStdString() );
                    const auto key = shortcuts.front().toString( QKeySequence::PortableText );
                    const auto listed
                        = std::find_if( keys.cbegin(), keys.cend(), [ &key ]( const auto& item ) {
                              return QKeySequence( item.second.front() )
                                         .toString( QKeySequence::PortableText )
                                     == key;
                          } );
                    REQUIRE( listed != keys.cend() );
                    CHECK( shortcutList.at( listed->first ).name
                           == menuwording::actionName( entry.action->text() ) );
                    ++named;
                }
                // Every entry the Shortcuts list gives keys to, not a few.
                CHECK( named >= 30 );
            }
        }
    }
}

namespace {

/// A language in which every text is its source behind a marker: whatever
/// is retitled from its translation source shows the marker.
class MarkingTranslator : public QTranslator {
public:
    static QString marked( const QString& source )
    {
        return QStringLiteral( "[xx] " ) + source;
    }

    QString translate( const char* /*context*/, const char* sourceText,
                       const char* /*disambiguation*/ = nullptr, int /*n*/ = -1 ) const override
    {
        if ( sourceText == nullptr || *sourceText == '\0' ) {
            return {};
        }
        return marked( QString::fromUtf8( sourceText ) );
    }

    bool isEmpty() const override
    {
        return false;
    }
};

/// The title of every menu and submenu of the menu bar, by path.
std::vector<std::pair<QString, QString>> menuTitles( MainWindow& window )
{
    std::vector<std::pair<QString, QString>> titles;
    for ( const auto& [ path, menu ] : menuBarMenus( window ) ) {
        titles.emplace_back( path, menu->title() );
    }
    return titles;
}

} // namespace

// A language change retitles every menu of the menu bar, the Plugins and
// Sources menus and the Open Recent and Encoding submenus too (#913).
SCENARIO( "A language change retitles every menu and submenu of the menu bar", "[ui][menubar]" )
{
    GIVEN( "a shown window" )
    {
        const ShownWindow shown;
        const auto before = menuTitles( *shown.window );
        REQUIRE( before.size() >= 7 );

        WHEN( "the language changes" )
        {
            MarkingTranslator translator;
            REQUIRE( QCoreApplication::installTranslator( &translator ) );
            QCoreApplication::sendPostedEvents( nullptr, QEvent::LanguageChange );
            QCoreApplication::processEvents();

            THEN( "every menu and submenu title is its translation" )
            {
                const auto after = menuTitles( *shown.window );
                REQUIRE( after.size() == before.size() );
                for ( std::size_t index = 0; index < before.size(); ++index ) {
                    // The Encoding families are named by the Encodings they
                    // hold, in no language.
                    if ( before[ index ].first.startsWith( QStringLiteral( "View/Encoding/" ) ) ) {
                        continue;
                    }
                    INFO( before[ index ].first.toStdString() );
                    CHECK( after[ index ].second
                           == MarkingTranslator::marked( before[ index ].second ) );
                }
            }

            QCoreApplication::removeTranslator( &translator );
            QCoreApplication::sendPostedEvents( nullptr, QEvent::LanguageChange );
            QCoreApplication::processEvents();

            THEN( "going back retitles them as they were" )
            {
                CHECK( menuTitles( *shown.window ) == before );
            }
        }
    }
}
