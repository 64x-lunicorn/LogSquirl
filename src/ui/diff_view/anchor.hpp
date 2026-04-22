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
#include <vector>

#include <QJsonArray>

/// A single anchor point linking a line in Pane A to a line in Pane B.
struct AnchorPoint {
    int64_t lineA = 0;
    int64_t lineB = 0;

    bool operator==( const AnchorPoint& other ) const noexcept
    {
        return lineA == other.lineA && lineB == other.lineB;
    }
};

/// Ordered collection of anchor points used for scroll synchronization.
///
/// Anchors are kept sorted by lineA.  The mapAtoB / mapBtoA functions
/// perform piecewise-linear interpolation between adjacent anchors so
/// that scrolling one pane smoothly tracks the other.
class AnchorSet {
public:
    AnchorSet() = default;

    /// Add a new anchor pair.  Keeps the set sorted by lineA.
    void add( int64_t lineA, int64_t lineB );

    /// Remove the anchor at the given index.
    void remove( std::size_t index );

    /// Remove all anchors.
    void clear() noexcept;

    /// Number of anchor pairs.
    [[nodiscard]] std::size_t size() const noexcept;

    /// True when no anchors are set.
    [[nodiscard]] bool empty() const noexcept;

    /// Access the anchor at the given index.
    [[nodiscard]] const AnchorPoint& at( std::size_t index ) const;

    /// Map a line number from Pane A to Pane B via linear interpolation.
    /// Returns the input unchanged when fewer than one anchor exists.
    [[nodiscard]] int64_t mapAtoB( int64_t lineA ) const;

    /// Map a line number from Pane B to Pane A via linear interpolation.
    /// Returns the input unchanged when fewer than one anchor exists.
    [[nodiscard]] int64_t mapBtoA( int64_t lineB ) const;

    /// Swap A and B sides of every anchor.
    void swapPanes();

    /// Serialize to a JSON array for workspace persistence.
    [[nodiscard]] QJsonArray toJson() const;

    /// Deserialize from a JSON array.  Replaces existing anchors.
    void fromJson( const QJsonArray& array );

private:
    std::vector<AnchorPoint> anchors_;
};
