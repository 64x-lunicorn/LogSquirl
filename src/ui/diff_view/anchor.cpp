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

#include "anchor.hpp"

#include <algorithm>
#include <stdexcept>

#include <QJsonObject>

// --- AnchorSet ---------------------------------------------------------------

void AnchorSet::add( int64_t lineA, int64_t lineB )
{
    AnchorPoint pt{ lineA, lineB };
    // Insert in sorted order (by lineA).
    auto it = std::lower_bound(
        anchors_.begin(), anchors_.end(), pt,
        []( const AnchorPoint& a, const AnchorPoint& b ) { return a.lineA < b.lineA; } );
    anchors_.insert( it, pt );
}

void AnchorSet::remove( std::size_t index )
{
    if ( index >= anchors_.size() ) {
        throw std::out_of_range( "AnchorSet::remove: index out of range" );
    }
    anchors_.erase( anchors_.begin() + static_cast<std::ptrdiff_t>( index ) );
}

void AnchorSet::clear() noexcept
{
    anchors_.clear();
}

std::size_t AnchorSet::size() const noexcept
{
    return anchors_.size();
}

bool AnchorSet::empty() const noexcept
{
    return anchors_.empty();
}

const AnchorPoint& AnchorSet::at( std::size_t index ) const
{
    return anchors_.at( index );
}

// Piecewise-linear interpolation helper.
// Given a sorted vector of anchors, maps a source value through the
// (srcField → dstField) mapping defined by adjacent pairs.
static int64_t interpolate( const std::vector<AnchorPoint>& anchors, int64_t srcValue,
                            int64_t AnchorPoint::*srcField, int64_t AnchorPoint::*dstField )
{
    if ( anchors.empty() ) {
        return srcValue;
    }

    // Before the first anchor — apply the offset from the first anchor.
    if ( srcValue <= anchors.front().*srcField ) {
        const auto delta = anchors.front().*dstField - anchors.front().*srcField;
        return srcValue + delta;
    }

    // After the last anchor — apply the offset from the last anchor.
    if ( srcValue >= anchors.back().*srcField ) {
        const auto delta = anchors.back().*dstField - anchors.back().*srcField;
        return srcValue + delta;
    }

    // Between two anchors — linear interpolation.
    for ( std::size_t i = 0; i + 1 < anchors.size(); ++i ) {
        const auto& lo = anchors[ i ];
        const auto& hi = anchors[ i + 1 ];

        if ( srcValue >= lo.*srcField && srcValue <= hi.*srcField ) {
            const auto srcSpan = hi.*srcField - lo.*srcField;
            if ( srcSpan == 0 ) {
                return lo.*dstField;
            }
            const auto dstSpan = hi.*dstField - lo.*dstField;
            const auto t = srcValue - lo.*srcField;
            return lo.*dstField + ( t * dstSpan ) / srcSpan;
        }
    }

    // Should not be reached.
    return srcValue;
}

int64_t AnchorSet::mapAtoB( int64_t lineA ) const
{
    return interpolate( anchors_, lineA, &AnchorPoint::lineA, &AnchorPoint::lineB );
}

int64_t AnchorSet::mapBtoA( int64_t lineB ) const
{
    // For B→A we need anchors sorted by lineB.  Build a temporary sorted copy.
    auto sorted = anchors_;
    std::sort( sorted.begin(), sorted.end(),
               []( const AnchorPoint& a, const AnchorPoint& b ) { return a.lineB < b.lineB; } );
    return interpolate( sorted, lineB, &AnchorPoint::lineB, &AnchorPoint::lineA );
}

void AnchorSet::swapPanes()
{
    for ( auto& pt : anchors_ ) {
        std::swap( pt.lineA, pt.lineB );
    }
    // Re-sort by the new lineA.
    std::sort( anchors_.begin(), anchors_.end(),
               []( const AnchorPoint& a, const AnchorPoint& b ) { return a.lineA < b.lineA; } );
}

QJsonArray AnchorSet::toJson() const
{
    QJsonArray arr;
    for ( const auto& pt : anchors_ ) {
        QJsonObject obj;
        obj[ "a" ] = static_cast<qint64>( pt.lineA );
        obj[ "b" ] = static_cast<qint64>( pt.lineB );
        arr.append( obj );
    }
    return arr;
}

void AnchorSet::fromJson( const QJsonArray& array )
{
    anchors_.clear();
    anchors_.reserve( static_cast<std::size_t>( array.size() ) );
    for ( const auto& val : array ) {
        const auto obj = val.toObject();
        add( static_cast<int64_t>( obj[ "a" ].toDouble() ),
             static_cast<int64_t>( obj[ "b" ].toDouble() ) );
    }
}
