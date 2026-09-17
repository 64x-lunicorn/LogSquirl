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

#include "displayedlines.h"

#include <algorithm>
#include <cstdint>
#include <utility>

DisplayedLines::DisplayedLines( const SearchResultArray& matches,
                                std::function<LinesCount()> nbLogLines, int contextLinesCount )
    : matches_( matches )
    , nbLogLines_( std::move( nbLogLines ) )
    , contextLinesCount_( contextLinesCount )
{
    refreshLines();
}

void DisplayedLines::setShown( LineType shown )
{
    shown_ = shown;
    refreshLines();
}

DisplayedLines::LineType DisplayedLines::shown() const
{
    return shown_;
}

void DisplayedLines::setContextLinesCount( int contextLinesCount )
{
    if ( contextLinesCount == contextLinesCount_ ) {
        return;
    }

    contextLinesCount_ = contextLinesCount;
    rebuildContextLines();
    refreshLines();
}

void DisplayedLines::matchesArrived()
{
    refreshLines();
}

void DisplayedLines::searchCompleted()
{
    rebuildContextLines();
    refreshLines();
}

void DisplayedLines::searchDiscarded()
{
    contextLines_ = SearchResultArray();
    refreshLines();
}

bool DisplayedLines::addMark( LineNumber line )
{
    const bool added = marks_.addChecked( line.get() );
    if ( added ) {
        rebuildContextLines();
        refreshLines();
    }
    return added;
}

bool DisplayedLines::removeMark( LineNumber line )
{
    const bool removed = marks_.removeChecked( line.get() );
    if ( removed ) {
        rebuildContextLines();
        refreshLines();
    }
    return removed;
}

void DisplayedLines::clearMarks()
{
    marks_ = SearchResultArray();
    rebuildContextLines();
    refreshLines();
}

const SearchResultArray& DisplayedLines::marks() const
{
    return marks_;
}

OptionalLineNumber DisplayedLines::markAfter( LineNumber line ) const
{
    // rank counts the Marks up to and including line, so it is also the
    // index of the first Mark after it.
    LineNumber::UnderlyingType nextMark;
    if ( marks_.select( marks_.rank( line.get() ), &nextMark ) ) {
        return LineNumber( nextMark );
    }
    return {};
}

OptionalLineNumber DisplayedLines::markBefore( LineNumber line ) const
{
    // The Marks strictly before line: rank counts line itself when it is marked.
    const LineNumber::UnderlyingType marksBefore
        = marks_.rank( line.get() ) - ( marks_.contains( line.get() ) ? 1 : 0 );

    if ( marksBefore == 0 ) {
        return {};
    }

    LineNumber::UnderlyingType previousMark;
    if ( marks_.select( marksBefore - 1, &previousMark ) ) {
        return LineNumber( previousMark );
    }
    return {};
}

DisplayedLines::LineType DisplayedLines::lineType( LineNumber line ) const
{
    LineType type = LineTypeFlags::Plain;

    if ( marks_.contains( line.get() ) ) {
        type |= LineTypeFlags::Mark;
    }
    if ( matches_.contains( line.get() ) ) {
        type |= LineTypeFlags::Match;
    }
    // A Match found after the Context Lines were built can sit on one of
    // them: it is a Match, not a Context Line.
    if ( type == LineTypeFlags::Plain && contextLines_.contains( line.get() ) ) {
        type |= LineTypeFlags::Context;
    }

    return type;
}

const SearchResultArray& DisplayedLines::lines() const
{
    switch ( source_ ) {
    case Source::Matches:
        return matches_;
    case Source::Marks:
        return marks_;
    case Source::Combined:
        break;
    }
    return combinedLines_;
}

LinesCount DisplayedLines::count() const
{
    return LinesCount( lines().cardinality() );
}

OptionalLineNumber DisplayedLines::logLineAt( LineNumber position ) const
{
    LineNumber::UnderlyingType line;
    if ( lines().select( position.get(), &line ) ) {
        return LineNumber( line );
    }
    return {};
}

LineNumber DisplayedLines::positionOf( LineNumber line ) const
{
    const LineNumber::UnderlyingType rank = lines().rank( line.get() );
    return LineNumber( rank > 0 ? rank - 1 : 0 );
}

DisplayedLinesCursor DisplayedLines::cursorAt( LineNumber position ) const
{
    return DisplayedLinesCursor( lines(), position );
}

void DisplayedLines::rebuildContextLines()
{
    contextLines_ = SearchResultArray();

    if ( contextLinesCount_ <= 0 ) {
        return;
    }

    const auto nbLogLines = nbLogLines_().get();
    if ( nbLogLines == 0 ) {
        return;
    }

    // Expand each Match and Mark +-contextLinesCount_ Log Lines.
    SearchResultArray matchesAndMarks;
    if ( !marks_.isEmpty() ) {
        matchesAndMarks = matches_ | marks_;
    }
    const auto& around = marks_.isEmpty() ? matches_ : matchesAndMarks;

    struct Expansion {
        SearchResultArray* contextLines;
        const SearchResultArray* around;
        uint64_t reach;
        uint64_t lastLine;
    };
    Expansion expansion{ &contextLines_, &around, static_cast<uint64_t>( contextLinesCount_ ),
                         nbLogLines - 1 };

    around.iterate(
        []( uint64_t line, void* context ) -> bool {
            const auto* e = static_cast<Expansion*>( context );
            const auto first = line > e->reach ? line - e->reach : uint64_t{ 0 };
            const auto last = std::min( line + e->reach, e->lastLine );
            for ( auto neighbour = first; neighbour <= last; ++neighbour ) {
                if ( !e->around->contains( neighbour ) ) {
                    e->contextLines->add( neighbour );
                }
            }
            return true;
        },
        static_cast<void*>( &expansion ) );
}

void DisplayedLines::refreshLines()
{
    const bool matchesShown = shown_.testFlag( LineTypeFlags::Match );
    const bool marksShown = shown_.testFlag( LineTypeFlags::Mark ) || !matchesShown;
    const bool contextLinesShown
        = shown_.testFlag( LineTypeFlags::Context ) && !contextLines_.isEmpty();

    if ( !contextLinesShown && !( matchesShown && marksShown ) ) {
        source_ = matchesShown ? Source::Matches : Source::Marks;
    }
    else if ( !contextLinesShown && marks_.isEmpty() ) {
        source_ = Source::Matches;
    }
    else if ( !contextLinesShown && matches_.isEmpty() ) {
        source_ = Source::Marks;
    }
    else {
        source_ = Source::Combined;
        combinedLines_ = contextLinesShown ? contextLines_ : SearchResultArray();
        if ( matchesShown ) {
            combinedLines_ |= matches_;
        }
        if ( marksShown ) {
            combinedLines_ |= marks_;
        }
        return;
    }

    combinedLines_ = SearchResultArray();
}

DisplayedLinesCursor::DisplayedLinesCursor( const SearchResultArray& lines, LineNumber position )
    : lines_( &lines )
    , count_( static_cast<std::int64_t>( lines.cardinality() ) )
    , position_( position.get() < static_cast<LineNumber::UnderlyingType>( count_ )
                     ? static_cast<std::int64_t>( position.get() )
                     : count_ )
    , iterator_( lines.end() )
{
    if ( position_ < count_ ) {
        selectPosition();
    }
}

bool DisplayedLinesCursor::hasLine() const
{
    return position_ >= 0 && position_ < count_;
}

LineNumber DisplayedLinesCursor::position() const
{
    return LineNumber( static_cast<LineNumber::UnderlyingType>( position_ ) );
}

LineNumber DisplayedLinesCursor::logLine() const
{
    return LineNumber( *iterator_ );
}

void DisplayedLinesCursor::next()
{
    if ( position_ >= count_ ) {
        return;
    }

    ++position_;
    if ( position_ == 0 ) {
        iterator_ = lines_->begin();
    }
    else if ( position_ < count_ ) {
        ++iterator_;
    }
}

void DisplayedLinesCursor::previous()
{
    if ( position_ < 0 ) {
        return;
    }

    --position_;
    if ( position_ == count_ - 1 && position_ >= 0 ) {
        // From past the last Log Line: the iterator there cannot step back
        // over empty containers, so the last one is looked up once.
        selectPosition();
    }
    else if ( position_ >= 0 ) {
        --iterator_;
    }
}

logsquirl::vector<LineNumber> DisplayedLinesCursor::takeForward( LinesCount count )
{
    logsquirl::vector<LineNumber> taken;
    taken.reserve( static_cast<std::size_t>(
        std::max( std::int64_t{ 0 },
                  std::min( static_cast<std::int64_t>( count.get() ), count_ - position_ ) ) ) );
    for ( ; hasLine() && taken.size() < count.get(); next() ) {
        taken.push_back( logLine() );
    }
    return taken;
}

logsquirl::vector<LineNumber> DisplayedLinesCursor::takeBackward( LinesCount count )
{
    logsquirl::vector<LineNumber> taken;
    for ( ; hasLine() && taken.size() < count.get(); previous() ) {
        taken.push_back( logLine() );
    }
    std::reverse( taken.begin(), taken.end() );
    return taken;
}

void DisplayedLinesCursor::selectPosition()
{
    LineNumber::UnderlyingType line = {};
    lines_->select( static_cast<LineNumber::UnderlyingType>( position_ ), &line );
    iterator_.move_equalorlarger( line );
}
