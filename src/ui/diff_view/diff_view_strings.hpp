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

namespace DiffViewStrings {

// Status bar
constexpr auto NoAnchors = "No anchors set";
constexpr auto AnchorCount = "%1 anchor(s)";
constexpr auto PendingAnchor = "Pending anchor: Pane %1, Line %2 — right-click in the other pane";

// Context menu
constexpr auto SetAnchor = "Set Anchor";
constexpr auto RemoveAnchor = "Remove Anchor";
constexpr auto ClearAnchors = "Clear All Anchors";
constexpr auto SwapPanes = "Swap Panes";

// Shared search
constexpr auto SearchPlaceholder = "Shared search pattern…";
constexpr auto SearchDisabledTooltip = "Add at least one anchor to enable shared search";

// Tab title
constexpr auto TabTitle = "Diff: %1 vs %2";

} // namespace DiffViewStrings
