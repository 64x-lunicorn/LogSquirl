/*
 * Copyright (C) 2010, 2013 Nicolas Bonnefon and other contributors
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

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements QuickFind.
// This class implements the Quick Find mechanism over a copy of the lines a
// view displays, the QFP and the selection passed.
// Search is started just after the selection and the selection is updated
// if a match is found.

#include <QtConcurrent>

#include <algorithm>
#include <cstddef>

#include "abstractlogdata.h"
#include "dispatch_to.h"
#include "linetypes.h"
#include "log.h"
#include "quickfindpattern.h"
#include "selection.h"

#include "quickfind.h"

namespace {
// How many Log Lines QuickFind reads at a time: few enough that their text
// takes little memory even when Log Lines are long, many enough that reading
// them costs little more than their bytes.
constexpr LinesCount QuickFindBlockLines{ 1000 };
} // namespace

void SearchingNotifier::reset()
{
    dotToDisplay_ = 0;
    startTime_ = QTime::currentTime();
}

void SearchingNotifier::sendNotification( LineNumber current_line, LinesCount nb_lines,
                                          bool backward )
{
    LOG_DEBUG << "Emitting Searching....";
    // The share of the Log Lines already searched: those before the current
    // one forwards, those from it on backwards.
    const auto total = std::max( nb_lines.get(), LinesCount::UnderlyingType{ 1 } );
    const auto current = std::min( current_line.get(), total );
    const auto searched = backward ? total - current : current;
    const auto progress = static_cast<int>( searched * 100 / total );

    Q_EMIT notify( QFNotificationProgress( progress ) );
    startTime_ = QTime::currentTime().addMSecs( -800 );
}

QuickFindLines QuickFindLines::everyLogLine( const AbstractLogData& logFile )
{
    return QuickFindLines( logFile, logFile.getNbLine(), nullptr );
}

QuickFindLines QuickFindLines::someLogLines( const AbstractLogData& logFile,
                                             SearchResultArray lines )
{
    const LinesCount count( lines.cardinality() );
    return QuickFindLines( logFile, count,
                           std::make_shared<const SearchResultArray>( std::move( lines ) ) );
}

QuickFindLines::QuickFindLines( const AbstractLogData& logFile, LinesCount count,
                                std::shared_ptr<const SearchResultArray> lines )
    : logFile_( &logFile )
    , count_( count )
    , lines_( std::move( lines ) )
{
}

LinesCount QuickFindLines::count() const
{
    return count_;
}

LineNumber QuickFindLines::positionOf( LineNumber logLine ) const
{
    if ( !lines_ ) {
        return std::min( logLine, LineNumber( count_.get() ) );
    }
    // rank() counts the lines up to and including its argument.
    return logLine == 0_lnum ? 0_lnum : LineNumber( lines_->rank( logLine.get() - 1 ) );
}

LineNumber QuickFindLines::logLineAt( LineNumber position ) const
{
    if ( !lines_ ) {
        return position;
    }
    return lineAtPosition( *lines_, position ).value_or( 0_lnum );
}

QuickFindLines::Cursor QuickFindLines::cursorAt( LineNumber position ) const
{
    return lines_ ? Cursor( *lines_, position ) : Cursor( count_, position );
}

logsquirl::vector<QString>
QuickFindLines::expandedLinesText( std::span<const LineNumber> logLines ) const
{
    return logFile_->getExpandedLinesSparse( logLines );
}

QuickFindLines::AttachedReader QuickFindLines::attachReader() const
{
    return AttachedReader( *logFile_ );
}

QuickFindLines::AttachedReader::AttachedReader( const AbstractLogData& logFile )
    : logFile_( logFile )
{
    logFile_.attachReader();
}

QuickFindLines::AttachedReader::~AttachedReader()
{
    logFile_.detachReader();
}

QuickFindLines::Cursor::Cursor( LinesCount count, LineNumber position )
    : count_( static_cast<std::int64_t>( count.get() ) )
    , position_( static_cast<std::int64_t>( std::min( position.get(), count.get() ) ) )
{
}

QuickFindLines::Cursor::Cursor( const SearchResultArray& lines, LineNumber position )
    : lines_( std::in_place, lines, position )
{
}

logsquirl::vector<LineNumber> QuickFindLines::Cursor::takeForward( LinesCount count )
{
    if ( lines_ ) {
        return lines_->takeForward( count );
    }

    const auto end = std::min( count_, position_ + static_cast<std::int64_t>( count.get() ) );
    logsquirl::vector<LineNumber> logLines;
    logLines.reserve( static_cast<std::size_t>( std::max( end - position_, std::int64_t{ 0 } ) ) );
    for ( ; position_ < end; ++position_ ) {
        logLines.emplace_back( static_cast<LineNumber::UnderlyingType>( position_ ) );
    }
    return logLines;
}

logsquirl::vector<LineNumber> QuickFindLines::Cursor::takeBackward( LinesCount count )
{
    if ( lines_ ) {
        return lines_->takeBackward( count );
    }

    if ( position_ < 0 || position_ >= count_ ) {
        return {};
    }
    const auto first
        = std::max( std::int64_t{ 0 }, position_ + 1 - static_cast<std::int64_t>( count.get() ) );
    logsquirl::vector<LineNumber> logLines;
    logLines.reserve( static_cast<std::size_t>( position_ + 1 - first ) );
    for ( auto logLine = first; logLine <= position_; ++logLine ) {
        logLines.emplace_back( static_cast<LineNumber::UnderlyingType>( logLine ) );
    }
    position_ = first - 1;
    return logLines;
}

void QuickFindLines::Cursor::previous()
{
    if ( lines_ ) {
        lines_->previous();
    }
    else if ( position_ >= 0 ) {
        --position_;
    }
}

void QuickFind::LastMatchPosition::set( LineNumber line, LineColumn column )
{
    if ( ( !line_.has_value() ) || ( ( line <= *line_ ) && ( column < column_ ) ) ) {
        line_ = line;
        column_ = column;
    }
}

void QuickFind::LastMatchPosition::set( const FilePosition& position )
{
    set( position.line(), position.column() );
}

bool QuickFind::LastMatchPosition::isLater( OptionalLineNumber line, LineColumn column ) const
{
    if ( !line_.has_value() || !line.has_value() )
        return false;
    else if ( ( *line == *line_ ) && ( column >= column_ ) )
        return true;
    else if ( *line > *line_ )
        return true;
    else
        return false;
}

bool QuickFind::LastMatchPosition::isLater( const FilePosition& position ) const
{
    return isLater( position.line(), position.column() );
}

bool QuickFind::LastMatchPosition::isSooner( OptionalLineNumber line, LineColumn column ) const
{
    if ( !line_.has_value() || !line.has_value() )
        return false;
    else if ( ( *line == *line_ ) && ( column <= column_ ) )
        return true;
    else if ( *line < *line_ )
        return true;
    else
        return false;
}

bool QuickFind::LastMatchPosition::isSooner( const FilePosition& position ) const
{
    return isSooner( position.line(), position.column() );
}

QuickFind::QuickFind( std::function<QuickFindLines()> copyDisplayedLines,
                      std::function<bool( LineNumber )> isDisplayed )
    : copyDisplayedLines_( std::move( copyDisplayedLines ) )
    , isDisplayed_( std::move( isDisplayed ) )
    , searchingNotifier_()
    , incrementalSearchStatus_()
{
    connect( &searchingNotifier_, &SearchingNotifier::notify, this, &QuickFind::sendNotification,
             Qt::DirectConnection );

    connect( &operationWatcher_, &QFutureWatcher<SearchResult>::finished, this,
             &QuickFind::onSearchFutureReady );
}

Selection QuickFind::incrementalSearchStop()
{
    if ( incrementalSearchStatus_.isOngoing() ) {
        Selection s = incrementalSearchStatus_.initialSelection();
        incrementalSearchStatus_ = IncrementalSearchStatus();
        interruptRequested_.set();

        return s;
    }
    else {
        return Selection{};
    }
}

Selection QuickFind::incrementalSearchAbort()
{
    if ( incrementalSearchStatus_.isOngoing() ) {
        Selection s = incrementalSearchStatus_.initialSelection();
        incrementalSearchStatus_ = IncrementalSearchStatus();
        interruptRequested_.set();
        return s;
    }
    else {
        return Selection{};
    }
}

void QuickFind::stopSearch()
{
    LOG_INFO << "Stop search for quickfind " << this;
    interruptSearch();
}

void QuickFind::interruptSearch()
{
    interruptRequested_.set();
    operationWatcher_.waitForFinished();
    // What the search's worker touched -- the state it reported the result
    // into under the future's lock, the Log File it read -- is let go of once
    // this returns: the future dropped by the next search or by the
    // destructor, the view and its Log File closed. The wait above orders the
    // two inside QtCore, which ThreadSanitizer cannot see. Reading the result
    // takes that lock in Qt's inline code, which TSan does see, so what
    // follows is ordered for it too (#482, #527).
    if ( operationFuture_.isResultReadyAt( 0 ) ) {
        [[maybe_unused]] const auto& previous = operationFuture_.resultAt( 0 );
    }
}

void QuickFind::onSearchFutureReady()
{
    const auto result = operationFuture_.result();

    // The limits are only recorded here, on the UI thread, and only if no
    // resetLimits() came since the search started: new lines may match.
    if ( result.reachedLimit && runningSearch_.limitsGeneration == limitsGeneration_ ) {
        if ( runningSearch_.direction == Forward ) {
            lastMatch_.set( runningSearch_.selection.getPreviousPosition() );
        }
        else {
            firstMatch_.set( runningSearch_.selection.getNextPosition() );
        }
    }

    const auto& selection = result.match;

    if ( selection.isValid() && !isDisplayed_( selection.line() ) ) {
        // The view stopped displaying the matched Log Line while the search
        // ran on its copy (a Mark removed, a Search cleared, Context Lines
        // hidden). Go on past it in the same direction, over what the view
        // displays now; an interrupted search is dropped.
        if ( !interruptRequested_ ) {
            LOG_DEBUG << "QuickFind match " << selection.line() << " no longer displayed";
            const auto from
                = runningSearch_.direction == Forward
                      ? FilePosition{ selection.line(), selection.endColumn() + 1_length }
                      : FilePosition{ selection.line(), 0_lcol };
            startSearch( runningSearch_.direction, from, runningSearch_.selection,
                         runningSearch_.matcher );
        }
        return;
    }

    if ( selection.isValid() ) {
        Q_EMIT searchDone( true, selection );
    }
    else if ( incrementalSearchStatus_.direction() != None ) {
        Q_EMIT searchDone( false,
                           Portion{ incrementalSearchStatus_.position().line(), 0_lcol, 0_lcol } );
    }
    else {
        Q_EMIT searchDone( false, selection );
    }
}

void QuickFind::incrementallySearchForward( Selection selection, QuickFindMatcher matcher )
{
    LOG_DEBUG << "QuickFind::incrementallySearchForward";

    // Position where we start the search from
    FilePosition start_position = selection.getNextPosition();

    if ( incrementalSearchStatus_.direction() == Forward ) {
        // An incremental search is active, we restart the search
        // from the initial point
        LOG_DEBUG << "Restart search from initial point";
        start_position = incrementalSearchStatus_.position();
    }
    else {
        // It's a new search so we search from the selection
        incrementalSearchStatus_ = IncrementalSearchStatus( Forward, start_position, selection );
    }

    startSearch( Forward, start_position, selection, matcher );
}

void QuickFind::incrementallySearchBackward( Selection selection, QuickFindMatcher matcher )
{
    LOG_DEBUG << "QuickFind::incrementallySearchBackward";

    // Position where we start the search from
    FilePosition start_position = selection.getPreviousPosition();

    if ( incrementalSearchStatus_.direction() == Backward ) {
        // An incremental search is active, we restart the search
        // from the initial point
        LOG_DEBUG << "Restart search from initial point";
        start_position = incrementalSearchStatus_.position();
    }
    else {
        // It's a new search so we search from the selection
        incrementalSearchStatus_ = IncrementalSearchStatus( Backward, start_position, selection );
    }

    startSearch( Backward, start_position, selection, matcher );
}

void QuickFind::searchForward( Selection selection, QuickFindMatcher matcher )
{
    incrementalSearchStatus_ = IncrementalSearchStatus();
    startSearch( Forward, selection.getNextPosition(), selection, matcher );
}

void QuickFind::searchBackward( Selection selection, QuickFindMatcher matcher )
{
    incrementalSearchStatus_ = IncrementalSearchStatus();
    startSearch( Backward, selection.getPreviousPosition(), selection, matcher );
}

void QuickFind::startSearch( QFDirection direction, const FilePosition& start_position,
                             const Selection& selection, const QuickFindMatcher& matcher )
{
    // The previous search's future is dropped below.
    interruptSearch();
    // Cleared here, before the worker starts, so a stopSearch() that comes
    // before the worker has begun still interrupts it.
    interruptRequested_.clear();

    runningSearch_ = RunningSearch{ direction, selection, matcher, limitsGeneration_ };

    // Optimisation: if we are already past the last (or before the first)
    // match, the search does no search at all.
    const bool pastLimit = direction == Forward ? lastMatch_.isLater( start_position )
                                                : firstMatch_.isSooner( start_position );

    // The copy is taken here, on the UI thread, and the worker reads nothing
    // else of the view's data.
    operationFuture_ = QtConcurrent::run( [ this, direction, lines = copyDisplayedLines_(),
                                            start_position, matcher, pastLimit ]() {
        return direction == Forward ? doSearchForward( lines, start_position, matcher, pastLimit )
                                    : doSearchBackward( lines, start_position, matcher, pastLimit );
    } );

    operationWatcher_.setFuture( operationFuture_ );
}

// Internal implementation of forward search, run on the worker thread.
// Parameters are the Log Lines to search and the position the search shall
// start at.
QuickFind::SearchResult QuickFind::doSearchForward( const QuickFindLines& lines,
                                                    const FilePosition& start_position,
                                                    const QuickFindMatcher& matcher,
                                                    bool afterLastMatch )
{
    bool found = false;
    LineColumn found_start_col{};
    LineColumn found_end_col{};

    if ( !matcher.isActive() )
        return {};

    if ( afterLastMatch ) {
        // Send a notification
        sendNotification( QFNotificationReachedEndOfFile() );

        return {};
    }

    const auto nb_lines = lines.count();
    auto line = start_position.line();
    auto position = lines.positionOf( line );
    LOG_DEBUG << "Start searching at line " << line;
    // On the Log Line the search starts on, only the rest of it after the
    // start column is searched.
    const bool startsOnALine = position < nb_lines && lines.logLineAt( position ) == line;

    // The Log File stays open from one block to the next.
    const auto reader = lines.attachReader();
    searchingNotifier_.reset();
    auto cursor = lines.cursorAt( position );
    bool firstBlock = true;
    while ( !found && !interruptRequested_ ) {
        const auto block = cursor.takeForward( QuickFindBlockLines );
        if ( block.empty() ) {
            break;
        }
        const auto text = lines.expandedLinesText( block );
        for ( std::size_t index = 0; index < block.size() && !interruptRequested_; ++index ) {
            const auto column
                = firstBlock && index == 0 && startsOnALine ? start_position.column() : 0_lcol;
            if ( matcher.isLineMatching( text[ index ], column ) ) {
                std::tie( found_start_col, found_end_col ) = matcher.getLastMatch();
                line = block[ index ];
                found = true;
                break;
            }
        }
        firstBlock = false;
        position += LinesCount( static_cast<LinesCount::UnderlyingType>( block.size() ) );

        // See if we need to notify of the ongoing search
        searchingNotifier_.ping( position, nb_lines, false );
    }

    if ( found ) {
        // Clear any notification
        Q_EMIT clearNotification();

        return { Portion{ line, found_start_col, found_end_col }, false };
    }
    else {
        if ( !interruptRequested_ ) {
            // Send a notification
            sendNotification( QFNotificationReachedEndOfFile{} );

            return { Portion{}, true };
        }
        else {
            // Send a notification
            sendNotification( QFNotificationInterrupted{} );
        }

        return {};
    }
}

// Internal implementation of backward search, run on the worker thread.
// Parameters are the Log Lines to search and the position the search shall
// start at.
QuickFind::SearchResult QuickFind::doSearchBackward( const QuickFindLines& lines,
                                                     const FilePosition& start_position,
                                                     const QuickFindMatcher& matcher,
                                                     bool beforeFirstMatch )
{
    bool found = false;
    LineColumn start_col{};
    LineColumn end_col{};

    if ( !matcher.isActive() )
        return {};

    if ( beforeFirstMatch ) {
        // Send a notification
        sendNotification( QFNotificationReachedBegininningOfFile() );

        return {};
    }

    const auto nb_lines = lines.count();
    auto line = start_position.line();
    // The lines before the start are those at positions below this one.
    auto position = lines.positionOf( line );
    LOG_DEBUG << "Start searching at line " << line;
    // On the Log Line the search starts on, only the beginning of it before
    // the start column is searched, and only if there is one.
    const bool searchesStartLine = start_position.column() > 0_lcol && position < nb_lines
                                   && lines.logLineAt( position ) == line;

    // The Log File stays open from one block to the next.
    const auto reader = lines.attachReader();
    searchingNotifier_.reset();
    auto cursor = lines.cursorAt( position );
    if ( searchesStartLine ) {
        ++position;
    }
    else {
        cursor.previous();
    }
    bool firstBlock = true;
    while ( !found && !interruptRequested_ ) {
        const auto block = cursor.takeBackward( QuickFindBlockLines );
        if ( block.empty() ) {
            break;
        }
        const auto text = lines.expandedLinesText( block );
        for ( auto index = block.size(); index > 0 && !interruptRequested_; --index ) {
            const bool onStartLine = firstBlock && index == block.size() && searchesStartLine;
            const auto column = onStartLine ? start_position.column() : LineColumn{ -1 };
            if ( matcher.isLineMatchingBackward( text[ index - 1 ], column ) ) {
                std::tie( start_col, end_col ) = matcher.getLastMatch();
                line = block[ index - 1 ];
                found = true;
                break;
            }
        }
        firstBlock = false;
        position = position - LinesCount( static_cast<LinesCount::UnderlyingType>( block.size() ) );

        // See if we need to notify of the ongoing search
        searchingNotifier_.ping( position, nb_lines, true );
    }

    if ( found ) {
        // Clear any notification
        Q_EMIT clearNotification();

        return { Portion{ line, start_col, end_col }, false };
    }
    else {
        if ( !interruptRequested_ ) {
            // Send a notification
            sendNotification( QFNotificationReachedBegininningOfFile() );

            return { Portion{}, true };
        }
        else {
            // Send a notification
            sendNotification( QFNotificationInterrupted{} );
        }

        return {};
    }
}

void QuickFind::resetLimits()
{
    lastMatch_.reset();
    firstMatch_.reset();
    ++limitsGeneration_;
}

void QuickFind::sendNotification( QFNotification notification )
{
    dispatchToMainThread( [ this, notification ]() { notify( notification ); } );
}
