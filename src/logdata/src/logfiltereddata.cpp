/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2013, 2017 Nicolas Bonnefon and other contributors
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

// This file implements LogFilteredData
// It stores a pointer to the LogData that created it,
// so should always be destroyed before the LogData.

#include "log.h"

#include <QString>

#include <algorithm>
#include <functional>
#include <span>
#include <vector>

#include "logdata.h"
#include "logfiltereddata.h"

#include "readablesize.h"
#include "synchronization.h"

LogFilteredData::~LogFilteredData()
{
    // Disconnect all signals before members (in particular session_) are
    // destroyed, on top of session_'s own teardown safety.
    disconnect();
}

// Usual constructor: just copy the data, the search is started by request()
LogFilteredData::LogFilteredData( const LogData* logData, const SearchPolicy& searchPolicy )
    : AbstractLogData()
    , sourceLogData_( logData )
    , session_( logData->searchBlockSource(), searchPolicy )
    , displayedLines_(
          session_.matches(), [ logData ] { return logData->getNbLine(); },
          searchPolicy.contextLinesCount )
{
    connect( &session_, &SearchSession::stateChanged, this,
             &LogFilteredData::handleSessionStateChanged );
}

void LogFilteredData::request( const RegularExpressionPattern& regExp )
{
    request( regExp, 0_lnum, LineNumber( getNbTotalLines().get() ) );
}

// Request results for regExp over [startLine, endLine) -- the Session
// owns the cache and decides on its own whether this hits it, continues
// its current run, or supersedes it as a fresh one.
void LogFilteredData::request( const RegularExpressionPattern& regExp, LineNumber startLine,
                               LineNumber endLine )
{
    LOG_DEBUG << "Entering request";
    session_.request( regExp, startLine, endLine );
}

void LogFilteredData::request( bool dropCache )
{
    LOG_DEBUG << "Entering request (idle)";
    session_.request();

    if ( dropCache ) {
        session_.dropCache();
    }
}

void LogFilteredData::stop()
{
    session_.stop();
}

SearchSession::State LogFilteredData::searchState() const
{
    return session_.state();
}

LineNumber LogFilteredData::getMatchingLineNumber( LineNumber matchNum ) const
{
    return findLogDataLine( matchNum );
}

LineNumber LogFilteredData::getLineIndexNumber( LineNumber lineNumber ) const
{
    return findFilteredLine( lineNumber );
}

LinesCount LogFilteredData::getNbTotalLines() const
{
    return sourceLogData_->getNbLine();
}

LinesCount LogFilteredData::getNbMatches() const
{
    return LinesCount( session_.matches().cardinality() );
}

LinesCount LogFilteredData::getNbMarks() const
{
    return LinesCount( displayedLines_.marks().cardinality() );
}

SearchResultArray LogFilteredData::copyDisplayedLines() const
{
    return displayedLines_.lines();
}

const LogData& LogFilteredData::sourceLogData() const
{
    return *sourceLogData_;
}

LogFilteredData::LineType LogFilteredData::lineTypeByIndex( LineNumber index ) const
{
    return lineTypeByLine( findLogDataLine( index ) );
}

LogFilteredData::LineType LogFilteredData::lineTypeByLine( LineNumber lineNumber ) const
{
    return displayedLines_.lineType( lineNumber );
}

void LogFilteredData::iterateOverLines( const std::function<void( LineNumber )>& callback ) const
{
    using CallbackFn = std::function<void( LineNumber )>;
    displayedLines_.lines().iterate(
        []( uint64_t line, void* context ) -> bool {
            auto* callbackFn = static_cast<CallbackFn*>( context );
            callbackFn->operator()( LineNumber( line ) );
            return true;
        },
        static_cast<void*>( const_cast<CallbackFn*>( &callback ) ) );
}

DisplayedLines::Count LogFilteredData::countDisplayedLines( LineNumber first, LineNumber end ) const
{
    return displayedLines_.countIn( first, end );
}

uint64_t LogFilteredData::displayedLinesRewrites() const
{
    return displayedLines_.rewrites();
}

void LogFilteredData::setSearchPolicy( const SearchPolicy& searchPolicy )
{
    session_.setSearchPolicy( searchPolicy );
    displayedLines_.setContextLinesCount( searchPolicy.contextLinesCount );
}

// Delegation to the Displayed Lines

void LogFilteredData::toggleMark( LineNumber line )
{
    if ( ( line >= 0_lnum ) && line < sourceLogData_->getNbLine() ) {
        if ( !displayedLines_.removeMark( line ) ) {
            displayedLines_.addMark( line, sourceLogData_->getLineLength( line ) );
        }
    }
    else {
        LOG_ERROR << "LogFilteredData::toggleMark trying to toggle a mark outside of the file.";
    }
}

void LogFilteredData::addMark( LineNumber line )
{
    if ( ( line >= 0_lnum ) && line < sourceLogData_->getNbLine() ) {
        if ( !displayedLines_.marks().contains( line.get() ) ) {
            displayedLines_.addMark( line, sourceLogData_->getLineLength( line ) );
        }
    }
    else {
        LOG_ERROR << "LogFilteredData::addMark trying to create a mark outside of the file.";
    }
}

OptionalLineNumber LogFilteredData::getMarkAfter( LineNumber line ) const
{
    return displayedLines_.markAfter( line );
}

OptionalLineNumber LogFilteredData::getMarkBefore( LineNumber line ) const
{
    return displayedLines_.markBefore( line );
}

void LogFilteredData::deleteMark( LineNumber line )
{
    displayedLines_.removeMark( line );
}

void LogFilteredData::clearMarks()
{
    displayedLines_.clearMarks();
}

void LogFilteredData::logLinesChanged( LineNumber firstChanged )
{
    const auto nbLines = sourceLogData_->getNbLine();
    displayedLines_.logLinesChanged( firstChanged, [ this, nbLines ]( LineNumber line ) {
        return line < nbLines ? sourceLogData_->getLineLength( line ) : 0_length;
    } );
}

QList<LineNumber> LogFilteredData::getMarks() const
{
    QList<LineNumber> markedLines;
    displayedLines_.marks().iterate(
        []( uint64_t line, void* context ) -> bool {
            static_cast<QList<LineNumber>*>( context )->append( LineNumber( line ) );
            return true;
        },
        static_cast<void*>( &markedLines ) );

    return markedLines;
}

void LogFilteredData::setVisibility( Visibility visi )
{
    // Each visibility flag has the value of the line type it shows.
    displayedLines_.setShown( LineType::fromInt( visi.toInt() ) );
}

LogFilteredData::Visibility LogFilteredData::visibility() const
{
    return Visibility::fromInt( displayedLines_.shown().toInt() );
}

//
// Q_SLOTS:
//
void LogFilteredData::handleSessionStateChanged( SearchSession::State state )
{
    using Phase = SearchSession::Phase;

    // The Search Session's Matches have changed by now; the Displayed Lines
    // read them in place and only need to know how far: by the new Matches
    // alone when they grew, entirely when they were replaced.
    const auto* newMatches = session_.newMatches();
    // A Log Line that stopped matching -- the previously last one of a grown
    // Log File, searched again once it was complete -- leaves them before the
    // Matches that arrived with it join them.
    if ( !session_.removedMatches().isEmpty() ) {
        displayedLines_.matchesRemoved( session_.removedMatches() );
    }
    switch ( state.phase ) {
    case Phase::Idle:
    case Phase::InvalidPattern:
    case Phase::Failed:
        // Nothing was run (or the run was abandoned or failed): nothing to keep.
        displayedLines_.searchDiscarded();
        break;
    case Phase::Complete:
        // From a real run or from the cache alike, so Context Lines never
        // belong to whatever ran previously.
        if ( newMatches != nullptr ) {
            displayedLines_.searchCompleted( *newMatches );
        }
        else {
            displayedLines_.searchCompleted();
        }
        LOG_INFO << "Matches size " << readableSize( session_.matches().getSizeInBytes( false ) )
                 << ", marks size "
                 << readableSize( displayedLines_.marks().getSizeInBytes( false ) )
                 << ", displayed lines size "
                 << readableSize( displayedLines_.lines().getSizeInBytes( false ) );
        break;
    case Phase::Running:
    case Phase::Interrupted:
        if ( newMatches != nullptr ) {
            displayedLines_.matchesArrived( *newMatches );
        }
        else {
            displayedLines_.matchesArrived();
        }
        break;
    }

    Q_EMIT searchStateChanged( state );
}

LineNumber LogFilteredData::findLogDataLine( LineNumber index ) const
{
    const auto line = displayedLines_.logLineAt( index );
    if ( line.has_value() ) {
        return *line;
    }

    if ( displayedLines_.count().get() > 0 ) {
        LOG_ERROR << "Index too big in LogFilteredData: " << index << " cache size "
                  << displayedLines_.count();
    }
    return maxValue<LineNumber>();
}

LineNumber LogFilteredData::findFilteredLine( LineNumber lineNum ) const
{
    return displayedLines_.positionOf( lineNum );
}

// Implementation of the virtual function.
QString LogFilteredData::doGetLineString( LineNumber index ) const
{
    const auto line = findLogDataLine( index );
    if ( line == maxValue<LineNumber>() ) {
        return {};
    }
    return sourceLogData_->getLineString( line );
}

// Implementation of the virtual function.
QString LogFilteredData::doGetExpandedLineString( LineNumber index ) const
{
    const auto line = findLogDataLine( index );
    if ( line == maxValue<LineNumber>() ) {
        return {};
    }
    return sourceLogData_->getExpandedLineString( line );
}

// Implementation of the virtual function.
logsquirl::vector<QString> LogFilteredData::doGetLines( LineNumber first_line,
                                                        LinesCount number ) const
{
    return readDisplayedLines( first_line, number, &LogData::getLinesSparse );
}

// Implementation of the virtual function.
logsquirl::vector<QString> LogFilteredData::doGetExpandedLines( LineNumber first_line,
                                                                LinesCount number ) const
{
    return readDisplayedLines( first_line, number, &LogData::getExpandedLinesSparse );
}

logsquirl::vector<QString> LogFilteredData::readDisplayedLines(
    LineNumber first_line, LinesCount number,
    logsquirl::vector<QString> ( LogData::*readSparse )( std::span<const LineNumber> ) const ) const
{
    // The displayed Log Lines are walked from the first position on and read
    // at once; the positions past the last one read as nothing.
    const auto logLines = displayedLines_.cursorAt( first_line ).takeForward( number );
    auto lines = ( sourceLogData_->*readSparse )( logLines );
    lines.resize( number.get() );
    return lines;
}

LineNumber LogFilteredData::doGetLineNumber( LineNumber index ) const
{
    return getMatchingLineNumber( index );
}

// Implementation of the virtual function.
LinesCount LogFilteredData::doGetNbLine() const
{
    return displayedLines_.count();
}

// Implementation of the virtual function.
LineLength LogFilteredData::doGetMaxLength() const
{
    return displayedLines_.maxLength( session_.maxLength() );
}

// Implementation of the virtual function.
LineLength LogFilteredData::doGetLineLength( LineNumber lineNum ) const
{
    LineNumber line = findLogDataLine( lineNum );
    return sourceLogData_->getLineLength( line );
}

void LogFilteredData::doSetDisplayEncoding( const char* encoding )
{
    LOG_DEBUG << "AbstractLogData::setDisplayEncoding: " << encoding;
}

QTextCodec* LogFilteredData::doGetDisplayEncoding() const
{
    return sourceLogData_->getDisplayEncoding();
}

void LogFilteredData::doAttachReader() const
{
    sourceLogData_->attachReader();
}

void LogFilteredData::doDetachReader() const
{
    sourceLogData_->detachReader();
}
