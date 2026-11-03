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

#ifndef CONTEXT_MENU_ENTRIES_H
#define CONTEXT_MENU_ENTRIES_H

// Reading the context menu of the Log Lines in a test: its top level, its
// More submenu, and the entry a test chooses, wherever of the two it is
// (#894).

#include <QAction>
#include <QMenu>
#include <QString>
#include <QStringList>

// The title of the submenu holding the rare entries.
inline const QString MoreSubmenu = QStringLiteral( "More" );

// The entries of a menu, in order, without its separators; a submenu counts
// as one entry, its title.
inline QStringList entriesOf( const QMenu& menu )
{
    QStringList entries;
    for ( const auto* action : menu.actions() ) {
        if ( !action->isSeparator() ) {
            entries << action->text();
        }
    }
    return entries;
}

// The More submenu of a context menu, or nullptr.
inline QMenu* moreMenuOf( const QMenu& menu )
{
    for ( const auto* action : menu.actions() ) {
        if ( action->menu() != nullptr && action->text() == MoreSubmenu ) {
            return action->menu();
        }
    }
    return nullptr;
}

// The entries of the More submenu of a context menu, in order, without its
// separators; none when there is no More.
inline QStringList moreEntriesOf( const QMenu& menu )
{
    const auto* more = moreMenuOf( menu );
    return more != nullptr ? entriesOf( *more ) : QStringList{};
}

// The entry of a context menu with the text, at its top level or under More,
// or nullptr.
inline QAction* contextMenuEntry( const QMenu& menu, const QString& text )
{
    for ( auto* action : menu.actions() ) {
        if ( !action->isSeparator() && action->text() == text ) {
            return action;
        }
    }
    if ( const auto* more = moreMenuOf( menu ) ) {
        for ( auto* action : more->actions() ) {
            if ( !action->isSeparator() && action->text() == text ) {
                return action;
            }
        }
    }
    return nullptr;
}

// Those of the entries the context menu has neither at its top level nor
// under More.
inline QStringList unreachableEntries( const QMenu& menu, const QStringList& entries )
{
    QStringList unreachable;
    for ( const auto& entry : entries ) {
        if ( contextMenuEntry( menu, entry ) == nullptr ) {
            unreachable << entry;
        }
    }
    return unreachable;
}

// The most entries the top level of the context menu of the Log Lines has,
// the More submenu and the view's own entries counted, the plugins' not
// (#894).
constexpr qsizetype MaxTopLevelEntries = 12;

// The entries the context menu of the Log Lines offered before it was sorted
// into a top level and More (#894), opened with one whole Log Line selected;
// each must stay reachable, at the top or under More.
//
// The Text View's.
inline QStringList textViewEntriesBefore894()
{
    return {
        "Highlighters",
        "Color labels",
        "&Mark",
        "&Copy this line",
        "Copy this line with line number",
        "Copy as shown",
        "Send to scratchpad",
        "Replace scratchpad",
        "Find &next",
        "Find &previous",
        "&Replace search",
        "&Add to search",
        "&Exclude from search",
        "Set search start",
        "Set search end",
        "Clear search limits",
        "Set selection start",
        "Set selection end",
        "Save splitter position",
        "Save to file",
        "Save selected to file",
    };
}

// The Filtered View's, with a Log Format recognized: the Text View's and
// Export as CSV.
inline QStringList filteredViewEntriesBefore894()
{
    auto entries = textViewEntriesBefore894();
    entries << "Export as CSV...";
    return entries;
}

// The Table View's, opened on a cell of the column "timestamp": no Copy as
// shown and no selection start or end, but the cell's value searched in its
// column and Export as CSV.
inline QStringList tableViewEntriesBefore894()
{
    auto entries = textViewEntriesBefore894();
    entries.removeAll( "Copy as shown" );
    entries.removeAll( "Set selection start" );
    entries.removeAll( "Set selection end" );
    entries.insert( entries.indexOf( "Set search start" ), "Search this value in timestamp" );
    entries << "Export as CSV...";
    return entries;
}

#endif // CONTEXT_MENU_ENTRIES_H
