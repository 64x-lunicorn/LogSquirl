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

#include <QDialog>
#include <QString>
#include <QStringList>

#include "predefinedfilter.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

// What "Add as Predefined Filter..." of the context menu of the Text View and
// the Filtered View opens (#897): a compact dialog turning the text selected
// in a Log Line into a Predefined Filter. It proposes a name made from the
// selection, one no filter of the Filter Group has, and the selection as the
// pattern, as plain text: the filter finds it whether the Search reads regular
// expressions or not. Regular expression, unchecked by default, turns the
// pattern into the selection escaped as a regex, to edit into one. The user
// picks the Filter Group, one of the user's own, the Default Filter Group by
// default, and whether the Search runs with the filter now.
//
// OK adds the Predefined Filter at the end of the group and saves
// PredefinedFiltersCollection, as the Predefined Filters dialog does. Running
// the Search and refreshing the Filters panel are the caller's.
class AddPredefinedFilterDialog : public QDialog {
    Q_OBJECT

public:
    // The longest name proposed; a longer selection is cut short.
    static constexpr qsizetype MaxProposedNameLength = 40;

    // The name proposed for a Predefined Filter of the selected text: the
    // text on one line, its white space collapsed, cut short when long.
    static QString proposedName( const QString& selectedText );

    explicit AddPredefinedFilterDialog( const QString& selectedText, QWidget* parent = nullptr );

    // The Predefined Filter OK adds, as edited.
    PredefinedFilter filter() const;

    // The id of the Filter Group chosen.
    QString chosenGroupId() const;

    // Whether the Search is to run with the filter once it is added.
    bool applyNow() const;

    // Why OK is disabled: the pattern is empty or no valid regex, or the
    // Filter Group chosen has a filter of the name. Empty when it is enabled
    // or the name is empty.
    QString warning() const;

public Q_SLOTS:
    void accept() override;

private:
    void updateState();
    // Regular expression was checked or unchecked: the pattern is turned into
    // a regex, or back into the plain text it was turned from.
    void regexToggled( bool regex );

    // The names of the filters of the Filter Group chosen.
    QStringList chosenGroupNames() const;

    // The plain text the pattern was before Regular expression turned it into
    // a regex.
    QString plainPattern_;

    QLineEdit* name_ = nullptr;
    QLineEdit* pattern_ = nullptr;
    QCheckBox* regex_ = nullptr;
    QComboBox* group_ = nullptr;
    QCheckBox* applyNow_ = nullptr;
    QLabel* warning_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
