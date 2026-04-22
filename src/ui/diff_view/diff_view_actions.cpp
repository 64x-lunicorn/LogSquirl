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

#include "diff_view_actions.hpp"

#include "diff_view_strings.hpp"

#include <QKeySequence>

DiffViewActions::DiffViewActions( QWidget* parent )
{
    setAnchorAction_ = new QAction( DiffViewStrings::SetAnchor, parent );
    setAnchorAction_->setShortcut( QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_A ) );

    clearAnchorsAction_ = new QAction( DiffViewStrings::ClearAnchors, parent );
    clearAnchorsAction_->setShortcut( QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_0 ) );

    swapPanesAction_ = new QAction( DiffViewStrings::SwapPanes, parent );
    swapPanesAction_->setShortcut( QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_S ) );
}
