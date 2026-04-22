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

#include <cstdint>
#include <optional>

#include <QObject>

#include "anchor.hpp"

/// Two-click state machine for creating anchor pairs.
///
/// 1. User triggers "Set Anchor" in Pane A → line is stored as pending.
/// 2. User triggers "Set Anchor" in Pane B → pair is created.
/// If the same pane is clicked twice, the pending value is replaced.
class AnchorController : public QObject {
    Q_OBJECT

  public:
    /// Identifies which pane the action came from.
    enum class Pane { Left, Right };

    explicit AnchorController( AnchorSet* anchors, QObject* parent = nullptr );

    /// Handle a "Set Anchor" action from the given pane at the given line.
    void setAnchor( Pane pane, int64_t lineNumber );

    /// Clear the pending state without creating a pair.
    void cancelPending();

    /// True when the first click has been recorded and we're waiting for the second.
    [[nodiscard]] bool hasPending() const;

    /// Returns the pending pane and line, if any.
    [[nodiscard]] std::optional<std::pair<Pane, int64_t>> pending() const;

  Q_SIGNALS:
    /// Emitted when a new anchor pair has been created.
    void anchorAdded( int64_t lineA, int64_t lineB );

    /// Emitted when the pending state changes (for status bar updates).
    void pendingChanged();

    /// Emitted after any mutation to the anchor set (add, remove, clear, swap).
    void anchorsChanged();

  public Q_SLOTS:
    /// Remove the anchor at the given index.
    void removeAnchor( std::size_t index );

    /// Clear all anchors.
    void clearAnchors();

    /// Swap left/right sides of all anchors.
    void swapPanes();

  private:
    AnchorSet* anchors_;

    // Pending state for the two-click workflow.
    std::optional<Pane> pendingPane_;
    std::optional<int64_t> pendingLine_;
};
