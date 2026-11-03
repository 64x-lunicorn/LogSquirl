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

#pragma once

#include <functional>
#include <memory>

#include <QHash>
#include <QObject>
#include <QPoint>
#include <QString>

class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

// "Show only this" of a sidebar tab's tree of groups and their checks, the
// Filters tab's and the Value Names tab's (#889), and the way back.
//
// The tree's checks are those of its units: an item the unit key names, a
// Predefined Filter or a Naming Rule, or a group of its own when it has no
// items under it. showOnly() checks the units of an item, a unit or a whole
// group, and unchecks every other unit across all groups, shown by the
// search or not; it remembers the checks from before, which restore() then
// brings back. A double-click and the context menu's "Show only this" both
// end in showOnly().
//
// The checks are remembered by their unit keys, so that they outlive the
// tree being built again. A unit not there at the time of the last
// showOnly() keeps its check on restore().
//
// The tree's own signals are blocked while showOnly() and restore() change
// the checks; checksChanged() follows, for the panel to take them over.
class SoloSelection : public QObject {
    Q_OBJECT

public:
    // The key of the unit an item is, empty when the item is no unit: a group
    // with items under it.
    using UnitKey = std::function<QString( const QTreeWidgetItem* )>;

    SoloSelection( QTreeWidget* tree, UnitKey unitKey );

    // Checks the units of `item` and unchecks every other one; remembers the
    // checks from before, those of the last showOnly().
    void showOnly( QTreeWidgetItem* item );

    // Whether showOnly() left checks restore() can bring back.
    bool canRestore() const;

    // Brings back the checks from before the last showOnly(), once.
    void restore();

    // The context menu for `item`, which is nullptr off the items: "Show only
    // this" for an item, and "Restore previous selection", enabled while
    // canRestore().
    std::unique_ptr<QMenu> contextMenu( QTreeWidgetItem* item );

    // The tool tip line that names the double-click.
    static QString doubleClickHint();

    // A tool tip of `text`, shown literally, followed by doubleClickHint().
    static QString toolTipWithHint( const QString& text );

Q_SIGNALS:
    // showOnly() or restore() changed the checks of the tree.
    void checksChanged();

private:
    // Calls `visit` with every unit of the tree and its key.
    void forEachUnit( const std::function<void( QTreeWidgetItem*, const QString& )>& visit ) const;
    void showContextMenu( const QPoint& position );

    QTreeWidget* tree_;
    UnitKey unitKey_;
    // Unit key to whether it was checked, before the last showOnly().
    QHash<QString, bool> checksBefore_;
    bool canRestore_ = false;
};
