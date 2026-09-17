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
    // Whatever the Context Lines were built around may be gone.
    contextLinesUpToDate_ = false;
    matchesWithoutContextLines_ = SearchResultArray();
    refreshLines();
}

void DisplayedLines::matchesArrived( const SearchResultArray& newMatches )
{
    if ( contextLinesUpToDate_ ) {
        matchesWithoutContextLines_ |= newMatches;
    }
    refreshLinesAt( newMatches );
}

void DisplayedLines::searchCompleted()
{
    rebuildContextLines();
    refreshLines();
}

void DisplayedLines::searchCompleted( const SearchResultArray& newMatches )
{
    if ( contextLinesUpToDate_ ) {
        matchesWithoutContextLines_ |= newMatches;
    }

    auto changed = updateContextLines();
    if ( !changed ) {
        refreshLines();
        return;
    }
    *changed |= newMatches;
    refreshLinesAt( *changed );
}

void DisplayedLines::searchDiscarded()
{
    // The Marks lose their Context Lines too, until they are next rebuilt.
    contextLines_ = SearchResultArray();
    contextLinesUpToDate_ = false;
    matchesWithoutContextLines_ = SearchResultArray();
    refreshLines();
}

bool DisplayedLines::addMark( LineNumber line )
{
    const bool added = marks_.addChecked( line.get() );
    if ( added ) {
        markToggled( line.get(), true );
    }
    return added;
}

bool DisplayedLines::removeMark( LineNumber line )
{
    const bool removed = marks_.removeChecked( line.get() );
    if ( removed ) {
        markToggled( line.get(), false );
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
    matchesWithoutContextLines_ = SearchResultArray();
    contextLinesUpToDate_ = true;
    contextLinesEnd_ = nbLogLines_().get();

    if ( contextLinesCount_ <= 0 || contextLinesEnd_ == 0 ) {
        return;
    }

    if ( marks_.isEmpty() ) {
        contextLines_ = contextLinesAround( matches_, contextLinesEnd_ );
    }
    else {
        contextLines_ = contextLinesAround( matches_ | marks_, contextLinesEnd_ );
    }
}

std::optional<SearchResultArray> DisplayedLines::updateContextLines()
{
    const auto nbLogLines = nbLogLines_().get();
    if ( !contextLinesUpToDate_ || nbLogLines < contextLinesEnd_ ) {
        rebuildContextLines();
        return std::nullopt;
    }

    // The Log Lines whose Context Lines are missing: the Matches that arrived
    // since, and -- when the Log File grew -- the Matches and Marks whose
    // Context Lines its former end cut off.
    auto around = std::exchange( matchesWithoutContextLines_, SearchResultArray() );
    const auto reach = static_cast<uint64_t>( std::max( contextLinesCount_, 0 ) );
    if ( reach > 0 && nbLogLines > contextLinesEnd_ ) {
        SearchResultArray nearFormerEnd;
        nearFormerEnd.addRange( contextLinesEnd_ > reach ? contextLinesEnd_ - reach : 0,
                                nbLogLines + reach );
        around |= nearFormerEnd & matches_;
        around |= nearFormerEnd & marks_;
    }
    contextLinesEnd_ = nbLogLines;

    if ( reach == 0 ) {
        return around;
    }

    auto added = contextLinesAround( around, nbLogLines );
    // A Log Line that became a Match is no longer a Context Line.
    contextLines_ -= around;
    contextLines_ |= added;

    added |= around;
    return added;
}

SearchResultArray DisplayedLines::contextLinesAround( const SearchResultArray& lines,
                                                      uint64_t nbLogLines ) const
{
    SearchResultArray contextLines;
    if ( contextLinesCount_ <= 0 || nbLogLines == 0 ) {
        return contextLines;
    }

    // Expand each Log Line +-contextLinesCount_ Log Lines, merging the
    // neighbourhoods that touch into one range: [runFirst, runEnd).
    struct Expansion {
        SearchResultArray* contextLines;
        uint64_t reach;
        uint64_t nbLogLines;
        uint64_t runFirst = 0;
        uint64_t runEnd = 0;
        bool inRun = false;
    };
    Expansion expansion{ &contextLines, static_cast<uint64_t>( contextLinesCount_ ), nbLogLines };

    lines.iterate(
        []( uint64_t line, void* context ) -> bool {
            auto* e = static_cast<Expansion*>( context );
            const auto first = line > e->reach ? line - e->reach : uint64_t{ 0 };
            if ( first >= e->nbLogLines ) {
                // So are all the Log Lines after it.
                return false;
            }
            const auto end = std::min( line + e->reach + 1, e->nbLogLines );
            if ( e->inRun && first <= e->runEnd ) {
                e->runEnd = std::max( e->runEnd, end );
            }
            else {
                if ( e->inRun ) {
                    e->contextLines->addRange( e->runFirst, e->runEnd );
                }
                e->runFirst = first;
                e->runEnd = end;
                e->inRun = true;
            }
            return true;
        },
        static_cast<void*>( &expansion ) );

    if ( expansion.inRun ) {
        contextLines.addRange( expansion.runFirst, expansion.runEnd );
    }

    contextLines -= matches_;
    if ( !marks_.isEmpty() ) {
        contextLines -= marks_;
    }
    return contextLines;
}

void DisplayedLines::markToggled( uint64_t line, bool added )
{
    // As a rebuild would, the Context Lines come up to date around every
    // Match first.
    auto changed = updateContextLines();
    if ( !changed ) {
        refreshLines();
        return;
    }
    changed->add( line );

    const auto reach = static_cast<uint64_t>( std::max( contextLinesCount_, 0 ) );
    if ( reach > 0 ) {
        if ( added ) {
            SearchResultArray mark;
            mark.add( line );
            const auto addedContextLines = contextLinesAround( mark, contextLinesEnd_ );
            contextLines_.remove( line );
            contextLines_ |= addedContextLines;
            *changed |= addedContextLines;
        }
        else if ( !matches_.contains( line ) ) {
            // The Log Lines the Mark reached are Context Lines now only if
            // another Match or Mark reaches them, from up to twice as far.
            const auto first = line > reach ? line - reach : uint64_t{ 0 };
            const auto end = std::min( line + reach + 1, contextLinesEnd_ );

            SearchResultArray reached;
            reached.addRange( first, end );

            SearchResultArray neighbourhood;
            neighbourhood.addRange( line > 2 * reach ? line - 2 * reach : uint64_t{ 0 },
                                    line + 2 * reach + 1 );
            auto neighbours = neighbourhood & matches_;
            neighbours |= neighbourhood & marks_;

            contextLines_.removeRange( first, end );
            contextLines_ |= contextLinesAround( neighbours, contextLinesEnd_ ) & reached;
            *changed |= reached;
        }
        // A removed Mark that is a Match keeps its Context Lines.
    }

    refreshLinesAt( *changed );
}

DisplayedLines::Source DisplayedLines::pickSource() const
{
    const bool matchesShown = shown_.testFlag( LineTypeFlags::Match );
    const bool marksShown = shown_.testFlag( LineTypeFlags::Mark ) || !matchesShown;
    const bool contextLinesShown
        = shown_.testFlag( LineTypeFlags::Context ) && !contextLines_.isEmpty();

    if ( !contextLinesShown && !( matchesShown && marksShown ) ) {
        return matchesShown ? Source::Matches : Source::Marks;
    }
    if ( !contextLinesShown && marks_.isEmpty() ) {
        return Source::Matches;
    }
    if ( !contextLinesShown && matches_.isEmpty() ) {
        return Source::Marks;
    }
    return Source::Combined;
}

void DisplayedLines::refreshLines()
{
    source_ = pickSource();
    if ( source_ != Source::Combined ) {
        combinedLines_ = SearchResultArray();
        return;
    }

    const bool matchesShown = shown_.testFlag( LineTypeFlags::Match );
    const bool marksShown = shown_.testFlag( LineTypeFlags::Mark ) || !matchesShown;

    combinedLines_
        = shown_.testFlag( LineTypeFlags::Context ) ? contextLines_ : SearchResultArray();
    if ( matchesShown ) {
        combinedLines_ |= matches_;
    }
    if ( marksShown ) {
        combinedLines_ |= marks_;
    }
}

void DisplayedLines::refreshLinesAt( const SearchResultArray& changed )
{
    if ( source_ != Source::Combined || pickSource() != Source::Combined ) {
        refreshLines();
        return;
    }

    const bool matchesShown = shown_.testFlag( LineTypeFlags::Match );
    const bool marksShown = shown_.testFlag( LineTypeFlags::Mark ) || !matchesShown;

    combinedLines_ -= changed;
    if ( shown_.testFlag( LineTypeFlags::Context ) ) {
        combinedLines_ |= changed & contextLines_;
    }
    if ( matchesShown ) {
        combinedLines_ |= changed & matches_;
    }
    if ( marksShown ) {
        combinedLines_ |= changed & marks_;
    }
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
