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

#include <vector>

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QListWidget;
class QPushButton;

// A Log File the Merge dialog offers: the path its tab holds it under, the
// name the tab shows and the tab's tooltip.
struct MergeCandidate {
    QString path;
    QString name;
    QString toolTip;
};

// The dialog of Merge… in a tab's context menu (#571): the user checks which
// of the open Log Files take part, puts them in the order they are
// concatenated in, and chooses whether duplicate lines are dropped. It
// remembers nothing: every time it opens with the candidates in the order
// given, all checked, duplicates kept. It knows nothing of the tabs, so it can
// be tested on its own.
class MergeDialog : public QDialog {
    Q_OBJECT

public:
    explicit MergeDialog( const std::vector<MergeCandidate>& candidates,
                          QWidget* parent = nullptr );

    // The paths of the checked Log Files, in the order the list shows them.
    QStringList checkedPaths() const;

    // Whether a line identical to one already written is left out.
    bool dropsDuplicates() const;

private:
    void moveCurrentRow( int by );
    void updateButtons();

    QListWidget* list_ = nullptr;
    QCheckBox* dropDuplicates_ = nullptr;
    QPushButton* mergeButton_ = nullptr;
    QPushButton* moveUpButton_ = nullptr;
    QPushButton* moveDownButton_ = nullptr;
};
