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
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "session.h"
#include "shortcuts.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QTest>

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
