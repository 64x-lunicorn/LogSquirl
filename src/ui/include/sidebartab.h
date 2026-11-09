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

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class SoloSelection;

// A tab of the sidebar that shows groups of checks (#923): the Filters tab
// and the Value Names tab. It lays out and styles the search field, the tree
// of groups with their checks and the buttons Select All, Deselect All and
// Edit...; a tab built on it hands it the groups with their checks
// (setGroups()), reads the checks back (checkedKeys(), uncheckedKeys())
// when checksChanged() says the user changed them, and does Edit... when
// editRequested() asks.
//
// The search hides the entries whose group name, name and pattern do not
// contain it, and leaves their checks alone (#901). Select All and Deselect
// All check what the search shows. A double-click on an entry or a group,
// or "Show only this" of its context menu, checks only it; "Restore previous
// selection" brings back the checks from before (#889).
class SidebarTab : public QWidget {
    Q_OBJECT

public:
    // One check of a sidebar tab: a Predefined Filter or a Naming Rule.
    struct Entry {
        // What the tab knows the check by: unique within the tab, never empty.
        QString key{};
        // Shown as "name  —  pattern", or as the pattern alone when the name is
        // empty or the pattern itself. The pattern is the tool tip.
        QString name{};
        QString pattern{};
        bool checked = false;
    };

    // A group of checks of a sidebar tab: a Filter Group or a Naming Group.
    struct Group {
        // Where the group comes from; a Plugin or Team group says so in its name
        // and tool tip.
        enum class Kind { Own, Plugin, Team };

        QString name{};
        // The Source of a Plugin group: the plugin's name.
        QString pluginName{};
        // A group without entries is a check of its own under this key; with
        // an empty key it is no check (the Filters tab's empty Filter Group).
        QString key{};
        QList<Entry> entries{};
        Kind kind = Kind::Own;
        // The check of a group without entries; that of one with entries is
        // theirs.
        bool checked = false;
    };

    explicit SidebarTab( const QString& searchPlaceholder, QWidget* parent = nullptr );

    // Shows these groups, in this order, with their checks, and keeps the
    // search. A check changed and not yet told with checksChanged() is
    // overtaken by them.
    void setGroups( const QList<Group>& groups );

    // The line shown below the tree while it has no groups; none when empty,
    // as by default.
    void setEmptyHint( const QString& hint );

    // The keys of the checked entries and groups, in tree order, shown by the
    // search or not.
    QStringList checkedKeys() const;
    // The keys of the unchecked entries and groups, in tree order.
    QStringList uncheckedKeys() const;

Q_SIGNALS:
    // The user changed checks: by a click (one signal for all the entries a
    // group's check changes), Select All, Deselect All, Show only this or
    // Restore previous selection. Not for setGroups().
    void checksChanged();

    // The search changed which entries are shown; their checks are as they
    // were.
    void searchChanged();

    // Edit... was clicked.
    void editRequested();

protected:
    void showEvent( QShowEvent* event ) override;

private Q_SLOTS:
    void onItemChanged( QTreeWidgetItem* item, int column );
    void onItemDoubleClicked( QTreeWidgetItem* item, int column );
    void onSearchTextChanged( const QString& text );
    void selectAll();
    void deselectAll();

private:
    void applySearch();
    // Checks or unchecks what the search shows.
    void setShownChecked( Qt::CheckState state );
    QStringList keysWithCheck( bool checked ) const;
    void tellChecksChanged();

    QLineEdit* searchBox_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    // "Show only this" and the way back.
    SoloSelection* solo_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QPushButton* selectAllButton_ = nullptr;
    QPushButton* deselectAllButton_ = nullptr;
    QPushButton* editButton_ = nullptr;

    // Gathers the itemChanged() a group's check gives for every entry into
    // one checksChanged().
    QTimer* checksTimer_ = nullptr;

    // Set while the tree is changed from here: its itemChanged() is not the
    // user's.
    bool updatingTree_ = false;
};
