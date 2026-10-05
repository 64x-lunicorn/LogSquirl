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

#include <QString>
#include <QStringList>
#include <QStyle>

#include "fileassociations.h"

class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

// The file types the user chooses from as a tree, grouped as the list groups
// them, one checkable row per type with its id under Qt::UserRole: what the
// File Associations page (#720) and the first-start dialog (#723) show.
namespace FileTypeChoices {

// Fills the tree with the types, every row unchecked.
void fill( QTreeWidget& tree );

// The row of the type with the id; nullptr if none.
QTreeWidgetItem* row( const QTreeWidget& tree, const QString& id );

// The ids of the checked rows, in the order of the list.
QStringList checkedIds( const QTreeWidget& tree );

// How a state is shown: its icon, its word and what that means.
struct StateLook {
    QStyle::StandardPixmap icon;
    QString text;
    QString toolTip;
};

// The look of the state, translated.
StateLook lookOf( FileAssociationState state );

// Shows the state in the column of the row, with the style of the widget.
void showState( QTreeWidgetItem& row, int column, FileAssociationState state,
                const QWidget& widget );

} // namespace FileTypeChoices
