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

#include "merged_filtered_view.hpp"

#include "anchor.hpp"
#include "logfiltereddata.h"

#include <algorithm>
#include <numeric>

// ===========================================================================
// MergedLogData
// ===========================================================================

MergedLogData::MergedLogData( QObject* parent )
    : AbstractLogData()
    , maxOriginalLine_( 0 )
{
    setParent( parent );
}

void MergedLogData::setSources( LogFilteredData* dataA, LogFilteredData* dataB,
                                const AnchorSet* anchors )
{
    dataA_ = dataA;
    dataB_ = dataB;
    anchors_ = anchors;
}

void MergedLogData::rebuild()
{
    entries_.clear();
    uint64_t maxOrig = 0;

    // Collect matches from file A, mapping each to B-space via anchors.
    if ( dataA_ ) {
        const auto countA = dataA_->getNbMatches().get();
        for ( uint64_t i = 0; i < countA; ++i ) {
            Entry e;
            e.source = MergedSource::FileA;
            e.filteredIndex = LineNumber( i );
            e.originalLine = dataA_->getMatchingLineNumber( LineNumber( i ) );
            e.sortKey = anchors_ ? anchors_->mapAtoB(
                                       static_cast<int64_t>( e.originalLine.get() ) )
                                 : static_cast<int64_t>( e.originalLine.get() );
            entries_.push_back( e );
            if ( e.originalLine.get() > maxOrig ) {
                maxOrig = e.originalLine.get();
            }
        }
    }

    // Collect matches from file B — already in B-space.
    if ( dataB_ ) {
        const auto countB = dataB_->getNbMatches().get();
        for ( uint64_t i = 0; i < countB; ++i ) {
            Entry e;
            e.source = MergedSource::FileB;
            e.filteredIndex = LineNumber( i );
            e.originalLine = dataB_->getMatchingLineNumber( LineNumber( i ) );
            e.sortKey = static_cast<int64_t>( e.originalLine.get() );
            entries_.push_back( e );
            if ( e.originalLine.get() > maxOrig ) {
                maxOrig = e.originalLine.get();
            }
        }
    }

    // Stable sort so entries at the same mapped position keep A-then-B order.
    std::stable_sort( entries_.begin(), entries_.end(),
                      []( const Entry& a, const Entry& b ) {
                          return a.sortKey < b.sortKey;
                      } );

    maxOriginalLine_ = LineNumber( maxOrig );
}

std::size_t MergedLogData::entryCount() const
{
    return entries_.size();
}

MergedSource MergedLogData::sourceAt( LineNumber index ) const
{
    if ( index.get() >= entries_.size() ) {
        return MergedSource::FileA;
    }
    return entries_[ index.get() ].source;
}

LineNumber MergedLogData::originalLineAt( LineNumber index ) const
{
    if ( index.get() >= entries_.size() ) {
        return LineNumber( 0 );
    }
    return entries_[ index.get() ].originalLine;
}

AbstractLogData::LineType MergedLogData::lineTypeAt( LineNumber index ) const
{
    if ( index.get() >= entries_.size() ) {
        return LineType( LineTypeFlags::Plain );
    }
    const auto& entry = entries_[ index.get() ];
    // All entries are search matches.  We additionally set the Mark bit
    // for file-B lines so the view can distinguish them visually.
    auto type = LineType( LineTypeFlags::Match );
    if ( entry.source == MergedSource::FileB ) {
        type |= LineTypeFlags::Mark;
    }
    return type;
}

LineNumber MergedLogData::maxOriginalLine() const
{
    return maxOriginalLine_;
}

// --- AbstractLogData pure-virtual implementations ---

QString MergedLogData::doGetLineString( LineNumber line ) const
{
    if ( line.get() >= entries_.size() ) {
        return {};
    }
    const auto& entry = entries_[ line.get() ];
    auto* src = ( entry.source == MergedSource::FileA ) ? dataA_ : dataB_;
    return src ? src->getLineString( entry.filteredIndex ) : QString{};
}

QString MergedLogData::doGetExpandedLineString( LineNumber line ) const
{
    if ( line.get() >= entries_.size() ) {
        return {};
    }
    const auto& entry = entries_[ line.get() ];
    auto* src = ( entry.source == MergedSource::FileA ) ? dataA_ : dataB_;
    return src ? src->getExpandedLineString( entry.filteredIndex ) : QString{};
}

logsquirl::vector<QString> MergedLogData::doGetLines( LineNumber first_line,
                                                      LinesCount number ) const
{
    logsquirl::vector<QString> result( number.get() );
    for ( uint64_t i = 0; i < number.get(); ++i ) {
        result[ i ] = doGetLineString( LineNumber( first_line.get() + i ) );
    }
    return result;
}

logsquirl::vector<QString> MergedLogData::doGetExpandedLines( LineNumber first_line,
                                                              LinesCount number ) const
{
    logsquirl::vector<QString> result( number.get() );
    for ( uint64_t i = 0; i < number.get(); ++i ) {
        result[ i ] = doGetExpandedLineString( LineNumber( first_line.get() + i ) );
    }
    return result;
}

LineNumber MergedLogData::doGetLineNumber( LineNumber index ) const
{
    // Return the original line number in the source file.
    if ( index.get() < entries_.size() ) {
        return entries_[ index.get() ].originalLine;
    }
    return LineNumber( 0 );
}

LinesCount MergedLogData::doGetNbLine() const
{
    return LinesCount( entries_.size() );
}

LineLength MergedLogData::doGetMaxLength() const
{
    LineLength maxLen( 0 );
    if ( dataA_ ) {
        maxLen = qMax( maxLen, dataA_->getMaxLength() );
    }
    if ( dataB_ ) {
        maxLen = qMax( maxLen, dataB_->getMaxLength() );
    }
    return maxLen;
}

LineLength MergedLogData::doGetLineLength( LineNumber line ) const
{
    if ( line.get() >= entries_.size() ) {
        return LineLength( 0 );
    }
    const auto& entry = entries_[ line.get() ];
    auto* src = ( entry.source == MergedSource::FileA ) ? dataA_ : dataB_;
    return src ? src->getLineLength( entry.filteredIndex ) : LineLength( 0 );
}

void MergedLogData::doSetDisplayEncoding( const char* /*encoding*/ )
{
    // Encoding is managed by the underlying LogData sources.
}

QTextCodec* MergedLogData::doGetDisplayEncoding() const
{
    // Return encoding from source A by default.
    if ( dataA_ ) {
        return dataA_->getDisplayEncoding();
    }
    return nullptr;
}

void MergedLogData::doAttachReader() const
{
    if ( dataA_ ) {
        dataA_->attachReader();
    }
    if ( dataB_ ) {
        dataB_->attachReader();
    }
}

void MergedLogData::doDetachReader() const
{
    if ( dataA_ ) {
        dataA_->detachReader();
    }
    if ( dataB_ ) {
        dataB_->detachReader();
    }
}

// ===========================================================================
// MergedFilteredView
// ===========================================================================

MergedFilteredView::MergedFilteredView( MergedLogData* data,
                                        const QuickFindPattern* const quickFindPattern,
                                        QWidget* parent )
    : AbstractLogView( data, quickFindPattern, parent )
    , mergedData_( data )
{
    // Navigate to the selected line in the source pane on double-click / Enter.
    connect( this, &AbstractLogView::newSelection, this,
             [ this ]( LineNumber startLine, LinesCount /*nLines*/,
                       LineColumn /*startCol*/, LineLength /*nSymbols*/ ) {
                 if ( startLine.get() < mergedData_->entryCount() ) {
                     Q_EMIT lineActivated( mergedData_->sourceAt( startLine ),
                                           mergedData_->originalLineAt( startLine ) );
                 }
             } );
}

AbstractLogData::LineType MergedFilteredView::lineType( LineNumber lineNumber ) const
{
    return mergedData_->lineTypeAt( lineNumber );
}

LineNumber MergedFilteredView::displayLineNumber( LineNumber lineNumber ) const
{
    if ( lineNumber.get() >= mergedData_->entryCount() ) {
        return LineNumber( 0 );
    }
    // Display 1-based original line number from the source file.
    return mergedData_->originalLineAt( lineNumber ) + 1_lcount;
}

LineNumber MergedFilteredView::maxDisplayLineNumber() const
{
    if ( mergedData_->entryCount() == 0 ) {
        return LineNumber( 0 );
    }
    return mergedData_->maxOriginalLine() + 1_lcount;
}
