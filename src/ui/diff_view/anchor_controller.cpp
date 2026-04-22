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

#include "anchor_controller.hpp"

AnchorController::AnchorController( AnchorSet* anchors, QObject* parent )
    : QObject( parent )
    , anchors_( anchors )
{
}

void AnchorController::setAnchor( Pane pane, int64_t lineNumber )
{
    if ( !pendingPane_.has_value() ) {
        // First click — store as pending.
        pendingPane_ = pane;
        pendingLine_ = lineNumber;
        Q_EMIT pendingChanged();
        return;
    }

    if ( *pendingPane_ == pane ) {
        // Same pane clicked again — replace pending.
        pendingLine_ = lineNumber;
        Q_EMIT pendingChanged();
        return;
    }

    // Second click is on the opposite pane — create the pair.
    int64_t lineA = 0;
    int64_t lineB = 0;

    if ( *pendingPane_ == Pane::Left ) {
        lineA = *pendingLine_;
        lineB = lineNumber;
    }
    else {
        lineA = lineNumber;
        lineB = *pendingLine_;
    }

    anchors_->add( lineA, lineB );

    // Clear pending state.
    pendingPane_.reset();
    pendingLine_.reset();

    Q_EMIT anchorAdded( lineA, lineB );
    Q_EMIT pendingChanged();
    Q_EMIT anchorsChanged();
}

void AnchorController::cancelPending()
{
    pendingPane_.reset();
    pendingLine_.reset();
    Q_EMIT pendingChanged();
}

bool AnchorController::hasPending() const
{
    return pendingPane_.has_value();
}

std::optional<std::pair<AnchorController::Pane, int64_t>> AnchorController::pending() const
{
    if ( !pendingPane_.has_value() ) {
        return std::nullopt;
    }
    return std::make_pair( *pendingPane_, *pendingLine_ );
}

void AnchorController::removeAnchor( std::size_t index )
{
    anchors_->remove( index );
    Q_EMIT anchorsChanged();
}

void AnchorController::clearAnchors()
{
    anchors_->clear();
    cancelPending();
    Q_EMIT anchorsChanged();
}

void AnchorController::swapPanes()
{
    anchors_->swapPanes();
    cancelPending();
    Q_EMIT anchorsChanged();
}
