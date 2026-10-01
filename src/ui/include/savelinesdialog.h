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

#include <memory>

#include <QCoreApplication>

class QFileDialog;
class QWidget;

// The file dialog Save to file and Save selected to file of a text view ask
// with while the view shows Value Names: under the file, the check "With
// Value Names", off at first (#647). The platform's own dialog cannot hold
// it, so this one is Qt's; a view showing none asks with the platform's as
// before.
class SaveLinesDialog {
    Q_DECLARE_TR_FUNCTIONS( SaveLinesDialog )

public:
    // Not shown yet: the caller runs it. Should Qt's dialog ever be laid out
    // other than on a grid, it comes without the check.
    static std::unique_ptr<QFileDialog> create( QWidget* parent );

    // Whether a dialog create() built has "With Value Names", and it is checked.
    static bool withValueNames( const QFileDialog& dialog );

    // The object name of the check, which a test finds it by.
    static constexpr auto WithValueNamesName = "withValueNames";
};
