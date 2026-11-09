/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <functional>

#include <QApplication>
#include <QShortcut>
#include <QWidget>

#include "logsquirl_version.h"
#include "shortcuts.h"

QStringList getKeyBindings( QKeySequence::StandardKey standardKey )
{
    auto bindings = QKeySequence::keyBindings( standardKey );
    QStringList stringBindings;
    std::transform( bindings.cbegin(), bindings.cend(), std::back_inserter( stringBindings ),
                    []( const auto& keySequence ) { return keySequence.toString(); } );

    return stringBindings;
}

QStringList ShortcutAction::defaultShortcutKeys( const std::string& action )
{
    const auto& shortcuts = defaultShortcutList();
    const auto actionShortcuts = shortcuts.find( action );
    return actionShortcuts != shortcuts.end() ? actionShortcuts->second.keySequence : QStringList{};
}

void ShortcutAction::registerShortcut( const ConfiguredShortcuts& configuredShortcuts,
                                       std::map<QString, QShortcut*>& shortcutsStorage,
                                       QWidget* shortcutsParent, Qt::ShortcutContext context,
                                       const std::string& action,
                                       const std::function<void()>& func )
{
    const auto keysConfiguration = configuredShortcuts.find( action );
    const auto keys = keysConfiguration != configuredShortcuts.end()
                          ? keysConfiguration->second
                          : ShortcutAction::defaultShortcutKeys( action );

    for ( const auto& key : keys ) {
        if ( key.isEmpty() ) {
            continue;
        }

        auto shortcut = shortcutsStorage.extract( key );
        if ( shortcut ) {
            shortcut.mapped()->deleteLater();
        }

        registerShortcut( key, shortcutsStorage, shortcutsParent, context, func );
    }
}

void ShortcutAction::registerShortcut( const QString& key,
                                       std::map<QString, QShortcut*>& shortcutsStorage,
                                       QWidget* shortcutsParent, Qt::ShortcutContext context,
                                       const std::function<void()>& func )
{
    auto newShortcut = new QShortcut( QKeySequence( key ), shortcutsParent );
    newShortcut->setContext( context );
    newShortcut->connect( newShortcut, &QShortcut::activated, shortcutsParent,
                          [ func ] { func(); } );
    shortcutsStorage.emplace( key, newShortcut );
}

QList<QKeySequence> ShortcutAction::shortcutKeys( const std::string& action,
                                                  const ConfiguredShortcuts& configuredShortcuts )
{
    const auto keysConfiguration = configuredShortcuts.find( action );
    const auto keys = keysConfiguration != configuredShortcuts.end()
                          ? keysConfiguration->second
                          : ShortcutAction::defaultShortcutKeys( action );

    // An empty key is an empty cell of the Shortcuts table, not a shortcut:
    // stored empty keys leave the action with no shortcut, as registerShortcut does.
    QList<QKeySequence> shortcuts;
    for ( const auto& hotkeys : keys ) {
        if ( !hotkeys.isEmpty() ) {
            shortcuts.append( QKeySequence( hotkeys ) );
        }
    }

    return shortcuts;
}

const ShortcutAction::ShortcutList& ShortcutAction::defaultShortcutList()
{
    static ShortcutList defaultShortcutKeys = {
        {
            MainWindowNewWindow,
            {
                QApplication::tr( "New Window" ),
                QStringList{},
            },
        },
        {
            MainWindowOpenFile,
            {
                QApplication::tr( "Open" ),
                getKeyBindings( QKeySequence::Open ),
            },
        },
        {
            MainWindowCloseFile,
            {
                QApplication::tr( "Close" ),
                getKeyBindings( QKeySequence::Close ),
            },
        },
        {
            MainWindowCloseAll,
            {
                QApplication::tr( "Close All" ),
                QStringList{},
            },
        },
        {
            MainWindowSelectAll,
            {
                QApplication::tr( "Select All" ),
                QStringList{ "Ctrl+A" },
            },
        },
        {
            MainWindowCopy,
            {
                QApplication::tr( "Copy" ),
                getKeyBindings( QKeySequence::Copy ),
            },
        },
        {
            MainWindowQuit,
            {
                QApplication::tr( "Exit" ),
                QStringList{ "Ctrl+Q" },
            },
        },
        {
            MainWindowFullScreen,
            {
                QApplication::tr( "Full Screen" ),
                QStringList{},
            },
        },
        {
            MainWindowMax,
            {
                QApplication::tr( "Maximize Window" ),
                QStringList{},
            },
        },
        {
            MainWindowMin,
            {
                QApplication::tr( "Minimize Window" ),
                QStringList{},
            },
        },
        {
            MainWindowPreference,
            {
                QApplication::tr( "Preferences" ),
                QStringList{},
            },
        },
        {
            MainWindowOpenQf,
            {
                QApplication::tr( "Find" ),
                getKeyBindings( QKeySequence::Find ),
            },
        },
        {
            MainWindowOpenQfForward,
            {
                QApplication::tr( "Quick find forward" ),
                QStringList{ QKeySequence( Qt::Key_Apostrophe ).toString() },
            },
        },
        {
            MainWindowOpenQfBackward,
            {
                QApplication::tr( "Quick find backward" ),
                QStringList{ QKeySequence( Qt::Key_QuoteDbl ).toString() },
            },
        },
        {
            MainWindowFocusSearchInput,
            {
                QApplication::tr( "Set focus to search input" ),
                QStringList{ { "Ctrl+S", "Ctrl+Shift+F" } },
            },
        },
        {
            MainWindowClearFile,
            {
                QApplication::tr( "Clear File" ),
                QStringList{ getKeyBindings( QKeySequence::Cut ) },
            },
        },
        {
            MainWindowOpenContainingFolder,
            {
                QApplication::tr( "Open Containing Folder" ),
                QStringList{},
            },
        },
        {
            MainWindowOpenInEditor,
            {
                QApplication::tr( "Open in Editor" ),
                QStringList{},
            },
        },
        {
            MainWindowCopyPathToClipboard,
            {
                QApplication::tr( "Copy Full Path" ),
                QStringList{},
            },
        },
        {
            MainWindowOpenFromClipboard,
            {
                QApplication::tr( "Open from Clipboard" ),
                getKeyBindings( QKeySequence::Paste ),
            },
        },
        {
            MainWindowOpenFromUrl,
            {
                QApplication::tr( "Open from URL" ),
                QStringList{},
            },
        },
        {
            MainWindowOpenSession,
            {
                QApplication::tr( "Open Session" ),
                QStringList{},
            },
        },
        {
            MainWindowSaveSessionAs,
            {
                QApplication::tr( "Save Session As" ),
                QStringList{},
            },
        },
        {
            MainWindowOpenCommandOutput,
            {
                QApplication::tr( "Open Command Output" ),
                QStringList{},
            },
        },
        {
            MainWindowCommandPalette,
            {
                QApplication::tr( "Command Palette" ),
                QStringList{ QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_P ).toString() },
            },
        },
        {
            MainWindowFollowFile,
            {
                QApplication::tr( "Follow File" ),
                { { QKeySequence( Qt::Key_F ).toString(),
                    QKeySequence( Qt::Key_F10 ).toString() } },
            },
        },
        {
            MainWindowTextWrap,
            {
                QApplication::tr( "Wrap Text" ),
                QStringList{ QKeySequence( Qt::Key_W ).toString() },
            },
        },
        {
            MainWindowShowValueNames,
            {
                QApplication::tr( "Show Value Names" ),
                QStringList{ QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_N ).toString() },
            },
        },
        {
            MainWindowReload,
            {
                QApplication::tr( "Reload" ),
                QStringList{ getKeyBindings( QKeySequence::Refresh ) },
            },
        },
        {
            MainWindowStop,
            {
                QApplication::tr( "Stop file loading" ),
                QStringList{ getKeyBindings( QKeySequence::Cancel ) },
            },
        },
        {
            MainWindowScratchpad,
            {
                QApplication::tr( "Scratchpad" ),
                QStringList{},
            },
        },
        {
            MainWindowMergeLogFiles,
            {
                QApplication::tr( "Merge Log Files" ),
                QStringList{},
            },
        },
        {
            MainWindowExportAsCsv,
            {
                QApplication::tr( "Export as CSV" ),
                QStringList{},
            },
        },
        {
            MainWindowToggleSidebar,
            {
                QApplication::tr( "Sidebar" ),
                QStringList{},
            },
        },
        {
            MainWindowSelectOpenFile,
            {
                QApplication::tr( "Switch to Opened File" ),
                QStringList{ "Ctrl+Shift+O" },
            },
        },
        {
            MainWindowProjectPage,
            {
                QApplication::tr( "LogSquirl on GitHub" ),
                QStringList{},
            },
        },
        {
            MainWindowWhatsNew,
            {
                QApplication::tr( "What's New in %1" ).arg( logsquirlVersion() ),
                QStringList{},
            },
        },
        {
            CrawlerChangeVisibilityForward,
            {
                QApplication::tr( "Change filtered lines visibility forward" ),
                QStringList{ QKeySequence( Qt::Key_V ).toString() },
            },
        },
        {
            CrawlerChangeVisibilityBackward,
            {
                QApplication::tr( "Change filtered lines visibility backward" ),
                QStringList{ "Shift+V" },
            },
        },
        {
            CrawlerChangeVisibilityToMarksAndMatches,
            {
                QApplication::tr( "Change filtered lines visibility to marks and matches" ),
                QStringList{ QKeySequence( Qt::Key_1 ).toString() },
            },
        },
        {
            CrawlerChangeVisibilityToMarks,
            {
                QApplication::tr( "Change filtered lines visibility to marks" ),
                QStringList{ QKeySequence( Qt::Key_2 ).toString() },
            },
        },
        {
            CrawlerChangeVisibilityToMatches,
            {
                QApplication::tr( "Change filtered lines visibility to matches" ),
                QStringList{ QKeySequence( Qt::Key_3 ).toString() },
            },
        },
        {
            CrawlerIncreseTopViewSize,
            {
                QApplication::tr( "Increase main view" ),
                QStringList{ QKeySequence( Qt::Key_Plus ).toString() },
            },
        },
        {
            CrawlerDecreaseTopViewSize,
            {
                QApplication::tr( "Decrease main view" ),
                QStringList{ QKeySequence( Qt::Key_Minus ).toString() },
            },
        },
        {
            CrawlerEnableCaseMatching,
            {
                QApplication::tr( "Enable case matching" ),
                QStringList{ QKeySequence( Qt::Key_4 ).toString() },
            },
        },
        {
            CrawlerEnableRegex,
            {
                QApplication::tr( "Enable regex" ),
                QStringList{ QKeySequence( Qt::Key_5 ).toString() },
            },
        },
        {
            CrawlerEnableInverseMatching,
            {
                QApplication::tr( "Enable inverse matching" ),
                QStringList{ QKeySequence( Qt::Key_6 ).toString() },
            },
        },
        {
            CrawlerEnableRegexCombining,
            {
                QApplication::tr( "Enable regex combining" ),
                QStringList{ QKeySequence( Qt::Key_7 ).toString() },
            },
        },
        {
            CrawlerEnableAutoRefresh,
            {
                QApplication::tr( "Enable auto refresh" ),
                QStringList{ QKeySequence( Qt::Key_8 ).toString() },
            },
        },
        {
            CrawlerKeepResults,
            {
                QApplication::tr( "Keep search results" ),
                QStringList{ QKeySequence( Qt::Key_9 ).toString() },
            },
        },
        // remove by commit 0b75b9d6
        // { QfFindNext, { QApplication::tr( "QuickFind: Find next" ), QStringList{ getKeyBindings(
        // QKeySequence::FindNext ) } } }, { QfFindPrev, { QApplication::tr( "QuickFind: Find
        // previous"
        // ), QStringList{ getKeyBindings( QKeySequence::FindPrevious ) } } },
        {
            LogViewMark,
            {
                QApplication::tr( "Add line mark" ),
                QStringList{ QKeySequence( Qt::Key_M ).toString() },
            },
        },
        {
            LogViewNextMark,
            {
                QApplication::tr( "Jump to next mark" ),
                QStringList{ QKeySequence( Qt::Key_BracketRight ).toString() },
            },
        },
        {
            LogViewPrevMark,
            {
                QApplication::tr( "Jump to previous mark" ),
                QStringList{ QKeySequence( Qt::Key_BracketLeft ).toString() },
            },
        },
        {
            LogViewNextMatch,
            {
                QApplication::tr( "Jump to next match" ),
                QStringList{ QKeySequence( Qt::Key_BraceRight ).toString() },
            },
        },
        {
            LogViewPrevMatch,
            {
                QApplication::tr( "Jump to previous match" ),
                QStringList{ QKeySequence( Qt::Key_BraceLeft ).toString() },
            },
        },
        {
            LogViewSelectionUp,
            {
                QApplication::tr( "Move selection up" ),
                { { QKeySequence( Qt::Key_Up ).toString(), QKeySequence( Qt::Key_K ).toString() } },
            },
        },
        {
            LogViewSelectionDown,
            {
                QApplication::tr( "Move selection down" ),
                { { QKeySequence( Qt::Key_Down ).toString(),
                    QKeySequence( Qt::Key_J ).toString() } },
            },
        },
        {
            LogViewScrollUp,
            {
                QApplication::tr( "Scroll up" ),
                QStringList{ "Ctrl+Up" },
            },
        },
        {
            LogViewScrollDown,
            {
                QApplication::tr( "Scroll down" ),
                QStringList{ "Ctrl+Down" },
            },
        },
        {
            LogViewScrollLeft,
            {
                QApplication::tr( "Scroll left" ),
                { { QKeySequence( Qt::Key_Left ).toString(),
                    QKeySequence( Qt::Key_H ).toString() } },
            },
        },
        {
            LogViewScrollRight,
            {
                QApplication::tr( "Scroll right" ),
                { { QKeySequence( Qt::Key_Right ).toString(),
                    QKeySequence( Qt::Key_L ).toString() } },
            },
        },
        {
            LogViewJumpToStartOfLine,
            {
                QApplication::tr( "Jump to the beginning of the current line" ),
                { QKeySequence( Qt::Key_Home ).toString(),
                  QKeySequence( Qt::Key_AsciiCircum ).toString() },
            },
        },
        {
            LogViewJumpToEndOfLine,
            {
                QApplication::tr( "Jump to the end start of the current line" ),
                QStringList{ QKeySequence( Qt::Key_Dollar ).toString() },
            },
        },
        {
            LogViewJumpToRightOfScreen,
            {
                QApplication::tr( "Jump to the right of the text" ),
                QStringList{ QKeySequence( Qt::Key_End ).toString() },
            },
        },
        {
            LogViewJumpToBottom,
            {
                QApplication::tr( "Jump to the bottom of the text" ),
                QStringList{ { "Ctrl+End", "Shift+G" } },
            },
        },
        {
            LogViewJumpToTop,
            {
                QApplication::tr( "Jump to the top of the text" ),
                QStringList{ "Ctrl+Home" },
            },
        },
        {
            LogViewJumpToLine,
            {
                QApplication::tr( "Go to Line" ),
                QStringList{ "Ctrl+L" },
            },
        },
        {
            LogViewJumpToLineNumber,
            {
                QApplication::tr( "Jump to line number" ),
                QStringList{},
            },
        },
        {
            LogViewJumpToTimestamp,
            {
                QApplication::tr( "Go to Timestamp" ),
                QStringList{ "Ctrl+Shift+L" },
            },
        },
        {
            LogViewSearchLimitsTimeRange,
            {
                QApplication::tr( "Set Search Limits to Time Range" ),
                QStringList{ "Ctrl+Alt+T" },
            },
        },
        {
            LogViewSearchLimitsAroundLine,
            {
                QApplication::tr( "Set Search Limits Around Current Line" ),
                QStringList{ "Ctrl+Alt+W" },
            },
        },
        {
            LogViewQfForward,
            {
                QApplication::tr( "Main view: find next" ),
                getKeyBindings( QKeySequence::FindNext )
                    << QKeySequence( Qt::Key_N ).toString() << "Ctrl+G",
            },
        },
        {
            LogViewQfBackward,
            { QApplication::tr( "Main view: find previous" ),
              getKeyBindings( QKeySequence::FindPrevious ) << "Shift+N"
                                                           << "Ctrl+Shift+G" },
        },
        {
            LogViewQfSelectedForward,
            { QApplication::tr( "Set selection to QuickFind and find next" ),
              { QKeySequence( Qt::Key_Asterisk ).toString(),
                QKeySequence( Qt::Key_Period ).toString() } },
        },
        {
            LogViewQfSelectedBackward,
            {
                QApplication::tr( "Set selection to QuickFind and find previous" ),
                QStringList{ QKeySequence( Qt::Key_Slash ).toString(),
                             QKeySequence( Qt::Key_Comma ).toString() },
            },
        },
        {
            LogViewExitView,
            {
                QApplication::tr( "Release focus from view" ),
                QStringList{ QKeySequence( Qt::Key_Space ).toString() },
            },
        },
        {
            LogViewAddColorLabel1,
            {
                QApplication::tr( "Highlight text with color 1" ),
                QStringList{ "Ctrl+Shift+1" },
            },
        },
        {
            LogViewAddColorLabel2,
            {
                QApplication::tr( "Highlight text with color 2" ),
                QStringList{ "Ctrl+Shift+2" },
            },
        },
        {
            LogViewAddColorLabel3,
            {
                QApplication::tr( "Highlight text with color 3" ),
                QStringList{ "Ctrl+Shift+3" },
            },
        },
        {
            LogViewAddColorLabel4,
            {
                QApplication::tr( "Highlight text with color 4" ),
                QStringList{ "Ctrl+Shift+4" },
            },
        },
        {
            LogViewAddColorLabel5,
            {
                QApplication::tr( "Highlight text with color 5" ),
                QStringList{ "Ctrl+Shift+5" },
            },
        },
        {
            LogViewAddColorLabel6,
            {
                QApplication::tr( "Highlight text with color 6" ),
                QStringList{ "Ctrl+Shift+6" },
            },
        },
        {
            LogViewAddColorLabel7,
            {
                QApplication::tr( "Highlight text with color 7" ),
                QStringList{ "Ctrl+Shift+7" },
            },
        },
        {
            LogViewAddColorLabel8,
            {
                QApplication::tr( "Highlight text with color 8" ),
                QStringList{ "Ctrl+Shift+8" },
            },
        },
        {
            LogViewAddColorLabel9,
            {
                QApplication::tr( "Highlight text with color 9" ),
                QStringList{ "Ctrl+Shift+9" },
            },
        },
        {
            LogViewAddNextColorLabel,
            {
                QApplication::tr( "Highlight text with next color" ),
                QStringList{ "Ctrl+D" },
            },
        },
        {
            LogViewClearColorLabels,
            {
                QApplication::tr( "Clear all color labels" ),
                QStringList{ "Ctrl+Shift+0" },
            },
        },
        {
            LogViewSendSelectionToScratchpad,
            {
                QApplication::tr( "Send selection to scratchpad" ),
                QStringList{ "Ctrl+Z" },
            },
        },
        {
            LogViewReplaceScratchpadWithSelection,
            {
                QApplication::tr( "Replace scratchpad with selection" ),
                QStringList{ "Ctrl+Shift+Z" },
            },
        },
        {
            LogViewAddToSearch,
            {
                QApplication::tr( "Add selection to search pattern" ),
                QStringList{ "Shift+A" },
            },
        },
        {
            LogViewExcludeFromSearch,
            {
                QApplication::tr( "Exclude selection from search pattern " ),
                QStringList{ "Shift+E" },
            },
        },
        {
            LogViewReplaceSearch,
            {
                QApplication::tr( "Replace search pattern with selection" ),
                QStringList{ "Shift+R" },
            },
        },
        {
            LogViewSelectLinesUp,
            {
                QApplication::tr( "Select lines up" ),
                QStringList{ "Shift+Up" },
            },
        },
        {
            LogViewSelectLinesDown,
            {
                QApplication::tr( "Select lines down" ),
                QStringList{ "Shift+Down" },
            },
        },
    };
    return defaultShortcutKeys;
}
