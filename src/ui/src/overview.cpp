/*
 * Copyright (C) 2011, 2012 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements the Overview class.
// It provides support for drawing the match overview sidebar but
// the actual drawing is done in AbstractLogView which uses this class.

#include "linetypes.h"
#include "log.h"

#include "logfiltereddata.h"

#include "overview.h"

#include <algorithm>

Overview::Overview( std::chrono::milliseconds recomputeInterval, Clock clock )
    : recomputeInterval_( recomputeInterval )
    , clock_( std::move( clock ) )
    , matchLines_()
    , markLines_()
{
    logFilteredData_ = nullptr;
    height_ = 0;
    dirty_ = true;
    visible_ = false;
}

void Overview::setFilteredData( const LogFilteredData* logFilteredData )
{
    LOG_INFO << "OverviewWidget::setFilteredData " << (void*)logFilteredData;

    logFilteredData_ = logFilteredData;
    dirty_ = true;
    paced_ = false;
    // Another LogFilteredData may sit where a former one was: count all again.
    computed_.reset();
}

void Overview::updateData( LinesCount totalNbLine, UpdatePace pace )
{
    LOG_INFO << "OverviewWidget::updateData " << totalNbLine;

    linesInFile_ = totalNbLine;
    // A change due now stays due now when a Search tick follows it.
    paced_ = pace == UpdatePace::WhileSearching && ( !dirty_ || paced_ );
    dirty_ = true;
}

std::optional<std::chrono::milliseconds> Overview::updateView( unsigned height )
{
    // We don't touch the cache if the height hasn't changed
    if ( height == height_ && !dirty_ ) {
        return std::nullopt;
    }

    if ( height == height_ && paced_ && lastRecompute_.has_value() ) {
        const auto sinceRecompute = clock_() - *lastRecompute_;
        if ( sinceRecompute < recomputeInterval_ ) {
            return std::chrono::ceil<std::chrono::milliseconds>( recomputeInterval_
                                                                 - sinceRecompute );
        }
    }

    height_ = height;
    recalculatesLines();
    return std::nullopt;
}

const logsquirl::vector<Overview::WeightedLine>* Overview::getMatchLines() const
{
    return &matchLines_;
}

const logsquirl::vector<Overview::WeightedLine>* Overview::getMarkLines() const
{
    return &markLines_;
}

std::pair<int, int> Overview::getViewLines() const
{
    int top = 0;
    int bottom = static_cast<int>( height_ ) - 1;

    if ( linesInFile_.get() > 0 ) {
        top = static_cast<int>( ( topLine_.get() ) * height_ / ( linesInFile_.get() ) );

        bottom = top + static_cast<int>( nbLines_.get() * height_ / ( linesInFile_.get() ) );
    }

    return std::make_pair( top, bottom );
}

LineNumber Overview::fileLineFromY( int position ) const
{
    const auto line = static_cast<LineNumber::UnderlyingType>(
        static_cast<LineNumber::UnderlyingType>( position ) * linesInFile_.get()
        / static_cast<LineNumber::UnderlyingType>( height_ ) );

    return LineNumber{ line };
}

int Overview::yFromFileLine( LineNumber fileLine ) const
{
    int position = 0;

    if ( linesInFile_.get() > 0 )
        position = static_cast<int>( fileLine.get() * height_ / linesInFile_.get() );

    return position;
}

LineNumber Overview::firstLineOfRow( uint64_t row ) const
{
    // Row y draws the Log Lines L with L * height / lines == y, as
    // yFromFileLine() places them: from ceil( y * lines / height ).
    const auto lines = linesInFile_.get();
    return LineNumber( ( row * lines + height_ - 1 ) / height_ );
}

// Update the internal cache
void Overview::recalculatesLines()
{
    LOG_INFO << "OverviewWidget::recalculatesLines";

    dirty_ = false;
    paced_ = false;
    lastRecompute_ = clock_();

    if ( logFilteredData_ == nullptr ) {
        LOG_INFO << "Overview::recalculatesLines: logFilteredData_ == NULL";
        computed_.reset();
        return;
    }

    const Computed computing{ logFilteredData_, linesInFile_, height_,
                              logFilteredData_->displayedLinesRewrites() };

    uint64_t firstRow = 0;
    const bool onlyAppended
        = computed_.has_value() && computed_->filteredData == computing.filteredData
          && computed_->linesInFile == computing.linesInFile
          && computed_->height == computing.height && computed_->rewrites == computing.rewrites;
    if ( onlyAppended ) {
        // Nothing changed up to the last line drawn: its row is counted again
        // with the rows after it.
        const auto lastPosition = []( const logsquirl::vector<WeightedLine>& lines ) {
            return lines.empty() ? 0 : lines.back().position();
        };
        const auto lastRow = std::max( lastPosition( matchLines_ ), lastPosition( markLines_ ) );
        firstRow = static_cast<uint64_t>( lastRow );
        const auto fromLastRow
            = [ lastRow ]( const WeightedLine& line ) { return line.position() >= lastRow; };
        std::erase_if( matchLines_, fromLastRow );
        std::erase_if( markLines_, fromLastRow );
    }
    else {
        matchLines_.clear();
        markLines_.clear();
    }
    computed_ = computing;

    if ( linesInFile_.get() == 0 || height_ == 0 ) {
        return;
    }

    const auto addLine
        = []( logsquirl::vector<WeightedLine>& lines, int position, uint64_t count ) {
              if ( count == 0 ) {
                  return;
              }
              // (allow multiple matches to look 'darker' than a single one.)
              auto& line = lines.emplace_back( position );
              for ( uint64_t more = 1; more < count && more < WeightedLine::WEIGHT_STEPS; ++more ) {
                  line.load();
              }
          };

    auto rowStart = firstLineOfRow( firstRow );
    for ( uint64_t row = firstRow; row < height_; ++row ) {
        const auto rowEnd = firstLineOfRow( row + 1 );
        if ( rowEnd > rowStart ) {
            const auto count = logFilteredData_->countDisplayedLines( rowStart, rowEnd );
            const auto position = static_cast<int>( row );
            addLine( matchLines_, position, count.matches );
            addLine( markLines_, position, count.others );
        }
        rowStart = rowEnd;
    }
}
