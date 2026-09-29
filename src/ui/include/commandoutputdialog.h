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

#include <vector>

#include "configuration.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

// Asks for a command line to run for its output (#575): the command line, the
// folder it runs in and whether its standard error goes into the tab too. The
// recent commands are offered in the command's combo box, the most recent
// first; choosing one fills in all three. It opens with the most recent one.
class CommandOutputDialog : public QDialog {
    Q_OBJECT

public:
    explicit CommandOutputDialog( std::vector<RecentCommand> recentCommands,
                                  QWidget* parent = nullptr );

    // What the user chose; the command line is trimmed.
    RecentCommand command() const;

    QComboBox* commandBox() const
    {
        return commandBox_;
    }
    QLineEdit* workingFolderEdit() const
    {
        return workingFolderEdit_;
    }
    QCheckBox* standardErrorBox() const
    {
        return standardErrorBox_;
    }
    QPushButton* openButton() const
    {
        return openButton_;
    }

private:
    void showRecentCommand( int index );
    void chooseWorkingFolder();
    void updateOpenButton();

    std::vector<RecentCommand> recentCommands_;
    QComboBox* commandBox_;
    QLineEdit* workingFolderEdit_;
    QCheckBox* standardErrorBox_;
    QPushButton* openButton_ = nullptr;
};
