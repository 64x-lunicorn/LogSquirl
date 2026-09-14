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

#include "abstractlogdata.h"
#include "dispatch_to.h"
#include "linetypes.h"
#include "log.h"
#include "quickfindpattern.h"
#include "selection.h"

#include "quickfind.h"

void SearchingNotifier::reset()
{
    dotToDisplay_ = 0;
    startTime_ = QTime::currentTime();
}

void SearchingNotifier::sendNotification( LineNumber current_line, LinesCount nb_lines,
                                          bool backward )
{
    LOG_DEBUG << "Emitting Searching....";
    const auto progress = static_cast<int>(
        backward ? ( ( nb_lines.get() - current_line.get() ) / nb_lines.get() * 100 )
                 : ( current_line.get() / nb_lines.get() ) * 100 );

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
    LineNumber::UnderlyingType logLine = {};
    lines_->select( position.get(), &logLine );
    return LineNumber( logLine );
}

QString QuickFindLines::expandedLineString( LineNumber logLine ) const
{
    return logFile_->getExpandedLineString( logLine );
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
    interruptRequested_.set();
    operationWatcher_.waitForFinished();
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
    interruptRequested_.set();
    operationWatcher_.waitForFinished();

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
    interruptRequested_.clear();

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
    // We look at the rest of the first line
    const bool startsOnALine = position < nb_lines && lines.logLineAt( position ) == line;
    if ( startsOnALine
         && matcher.isLineMatching( lines.expandedLineString( line ), start_position.column() ) ) {
        std::tie( found_start_col, found_end_col ) = matcher.getLastMatch();
        found = true;
    }
    else {
        searchingNotifier_.reset();
        // And then the rest of the lines
        if ( startsOnALine ) {
            ++position;
        }
        while ( position < nb_lines ) {
            line = lines.logLineAt( position );
            if ( matcher.isLineMatching( lines.expandedLineString( line ) ) ) {
                std::tie( found_start_col, found_end_col ) = matcher.getLastMatch();
                found = true;
                break;
            }
            ++position;

            // See if we need to notify of the ongoing search
            searchingNotifier_.ping( position, nb_lines, false );

            if ( interruptRequested_ ) {
                break;
            }
        }
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
    interruptRequested_.clear();

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
    // We look at the beginning of the first line
    if ( ( start_position.column() > 0_lcol ) && position < nb_lines
         && lines.logLineAt( position ) == line
         && ( matcher.isLineMatchingBackward( lines.expandedLineString( line ),
                                              start_position.column() ) ) ) {
        std::tie( start_col, end_col ) = matcher.getLastMatch();
        found = true;
    }
    else {
        searchingNotifier_.reset();
        // And then the rest of the lines
        while ( position > 0_lnum ) {
            --position;
            line = lines.logLineAt( position );
            if ( matcher.isLineMatchingBackward( lines.expandedLineString( line ) ) ) {
                std::tie( start_col, end_col ) = matcher.getLastMatch();
                found = true;
                break;
            }

            // See if we need to notify of the ongoing search
            searchingNotifier_.ping( position, nb_lines, true );

            if ( interruptRequested_ ) {
                break;
            }
        }
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
