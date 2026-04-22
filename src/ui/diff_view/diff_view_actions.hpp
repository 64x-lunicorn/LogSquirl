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

// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAction>
#include <QWidget>

/// Creates and owns the QActions used in the diff view context menus.
class DiffViewActions {
  public:
    /// Build actions parented to the given widget.
    explicit DiffViewActions( QWidget* parent );

    QAction* setAnchorAction() const { return setAnchorAction_; }
    QAction* clearAnchorsAction() const { return clearAnchorsAction_; }
    QAction* swapPanesAction() const { return swapPanesAction_; }

  private:
    QAction* setAnchorAction_ = nullptr;
    QAction* clearAnchorsAction_ = nullptr;
    QAction* swapPanesAction_ = nullptr;
};
