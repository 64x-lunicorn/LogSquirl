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
#include <QTimer>

#include <functional>
#include <numeric>
#include <tuple>
#include <vector>

#include "logdata.h"
#include "logfiltereddata.h"

#include "configuration.h"
#include "readablesize.h"
#include "synchronization.h"

LogFilteredData::~LogFilteredData()
{
    // Disconnect all signals before members (in particular session_) are
    // destroyed, on top of session_'s own teardown safety.
    disconnect();
}

// Usual constructor: just copy the data, the search is started by request()
LogFilteredData::LogFilteredData( const LogData* logData )
    : AbstractLogData()
    , matching_lines_( SearchResultArray() )
    , visibility_()
    , session_( *logData )
{
    // Starts with an empty result list
    maxLength_ = 0_length;
    maxLengthMarks_ = 0_length;
    nbLinesProcessed_ = 0_lcount;

    sourceLogData_ = logData;

    visibility_ = VisibilityFlags::Marks | VisibilityFlags::Matches;

    connect( &session_, &SearchSession::stateChanged, this,
             &LogFilteredData::handleSessionStateChanged );
}

void LogFilteredData::request( const RegularExpressionPattern& regExp )
{
    request( regExp, 0_lnum, LineNumber( getNbTotalLines().get() ) );
}

// Request results for regExp over [startLine, endLine), consulting the
// cache first -- the Session decides on its own whether this continues
// its current run or supersedes it as a fresh one.
void LogFilteredData::request( const RegularExpressionPattern& regExp, LineNumber startLine,
                               LineNumber endLine )
{
    LOG_DEBUG << "Entering request";

    const auto& config = Configuration::get();

    currentSearchKey_ = makeCacheKey( regExp, startLine, endLine );
    LOG_INFO << "Search cache key: " << regExp.pattern << "_" << startLine.get() << "_"
             << endLine.get();

    if ( config.useSearchResultsCache() ) {
        const auto cachedResults = searchResultsCache_.find( currentSearchKey_ );
        if ( cachedResults != std::end( searchResultsCache_ ) ) {
            LOG_INFO << "Got result from cache";
            session_.completeFromCache( regExp, startLine, endLine,
                                        cachedResults->second.matching_lines,
                                        cachedResults->second.maxLength );
            return;
        }
    }

    session_.request( regExp, startLine, endLine );
}

void LogFilteredData::request( bool dropCache )
{
    LOG_DEBUG << "Entering request (idle)";

    currentSearchKey_ = {};
    session_.request();

    if ( dropCache ) {
        searchResultsCache_.clear();
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

// Scan the list for the 'lineNumber' passed
bool LogFilteredData::isLineMatched( LineNumber lineNumber ) const
{
    return matching_lines_.contains( lineNumber.get() );
}

LinesCount LogFilteredData::getNbTotalLines() const
{
    return sourceLogData_->getNbLine();
}

LinesCount LogFilteredData::getNbMatches() const
{
    return LinesCount( matching_lines_.cardinality() );
}

LinesCount LogFilteredData::getNbMarks() const
{
    return LinesCount( marks_.cardinality() );
}

LogFilteredData::LineType LogFilteredData::lineTypeByIndex( LineNumber index ) const
{
    return lineTypeByLine( findLogDataLine( index ) );
}

LogFilteredData::LineType LogFilteredData::lineTypeByLine( LineNumber lineNumber ) const
{
    LineType line_type = LineTypeFlags::Plain;

    if ( isLineMarked( lineNumber ) )
        line_type |= LineTypeFlags::Mark;

    if ( isLineMatched( lineNumber ) )
        line_type |= LineTypeFlags::Match;

    // Mark as context only if line is not already a match or mark
    if ( line_type == LineTypeFlags::Plain && context_lines_.contains( lineNumber.get() ) )
        line_type |= LineTypeFlags::Context;

    return line_type;
}

void LogFilteredData::iterateOverLines( const std::function<void( LineNumber )>& callback ) const
{
    using CallbackFn = std::function<void( LineNumber )>;
    const auto& currentResults = currentResultArray();
    currentResults.iterate(
        []( uint64_t line, void* context ) -> bool {
            auto* callbackFn = static_cast<CallbackFn*>( context );
            callbackFn->operator()( LineNumber( line ) );
            return true;
        },
        static_cast<void*>( const_cast<CallbackFn*>( &callback ) ) );
}

void LogFilteredData::rebuildContextLines()
{
    const auto& config = Configuration::get();
    const int contextCount = config.contextLinesCount();

    context_lines_ = SearchResultArray();

    if ( contextCount <= 0 ) {
        return;
    }

    const auto totalLines = sourceLogData_->getNbLine().get();
    if ( totalLines == 0 ) {
        return;
    }

    // Expand each match/mark ±contextCount lines
    const auto& base = marks_and_matches_;

    struct ExpandParams {
        SearchResultArray* result;
        int n;
        uint64_t maxLine;
        const SearchResultArray* baseSet;
    };

    ExpandParams params{ &context_lines_, contextCount, totalLines, &base };

    base.iterate(
        []( uint64_t line, void* ctx ) -> bool {
            auto* p = static_cast<ExpandParams*>( ctx );
            const auto start = ( line > static_cast<uint64_t>( p->n ) )
                                   ? ( line - static_cast<uint64_t>( p->n ) )
                                   : 0ULL;
            const auto end = std::min( line + static_cast<uint64_t>( p->n ), p->maxLine - 1 );
            for ( auto i = start; i <= end; ++i ) {
                if ( !p->baseSet->contains( static_cast<uint64_t>( i ) ) ) {
                    p->result->add( static_cast<uint64_t>( i ) );
                }
            }
            return true;
        },
        static_cast<void*>( &params ) );
}

// Delegation to our Marks object

void LogFilteredData::toggleMark( LineNumber line )
{
    if ( ( line >= 0_lnum ) && line < sourceLogData_->getNbLine() ) {
        if ( !marks_.addChecked( line.get() ) ) {
            marks_.remove( line.get() );
            updateMaxLengthMarks( {}, line );
        }
        else {
            updateMaxLengthMarks( line, {} );
        }
    }
    else {
        LOG_ERROR << "LogFilteredData::toggleMark trying to toggle a mark outside of the file.";
    }
}

void LogFilteredData::addMark( LineNumber line )
{
    if ( ( line >= 0_lnum ) && line < sourceLogData_->getNbLine() ) {
        marks_.add( line.get() );
        updateMaxLengthMarks( line, {} );
    }
    else {
        LOG_ERROR << "LogFilteredData::addMark trying to create a mark outside of the file.";
    }
}

bool LogFilteredData::isLineMarked( LineNumber line ) const
{
    return marks_.contains( line.get() );
}

OptionalLineNumber LogFilteredData::getMarkAfter( LineNumber line ) const
{
    OptionalLineNumber marked_line;
    const LineNumber::UnderlyingType rank = marks_.rank( line.get() );
    LineNumber::UnderlyingType nextMark;
    if ( marks_.select( rank, &nextMark ) ) {
        marked_line = LineNumber( nextMark );
    }

    return marked_line;
}

OptionalLineNumber LogFilteredData::getMarkBefore( LineNumber line ) const
{
    OptionalLineNumber marked_line;

    const LineNumber::UnderlyingType rank = marks_.rank( line.get() );

    if ( rank < 2 ) {
        return marked_line;
    }

    LineNumber::UnderlyingType nextMark;
    if ( marks_.select( rank - 2, &nextMark ) ) {
        marked_line = LineNumber( nextMark );
    }

    return marked_line;
}

void LogFilteredData::deleteMark( LineNumber line )
{
    marks_.remove( line.get() );
    updateMaxLengthMarks( {}, line );
}

void LogFilteredData::updateMaxLengthMarks( OptionalLineNumber added_line,
                                            OptionalLineNumber removed_line )
{
    marks_and_matches_ = matching_lines_ | marks_;

    if ( added_line.has_value() ) {
        maxLengthMarks_ = qMax( maxLengthMarks_, sourceLogData_->getLineLength( *added_line ) );
    }

    // Now update the max length if needed
    if ( removed_line.has_value()
         && sourceLogData_->getLineLength( *removed_line ) >= maxLengthMarks_ ) {
        LOG_DEBUG << "deleteMark recalculating longest mark";
        maxLengthMarks_ = 0_length;
        marks_.iterate(
            []( uint64_t line, void* context ) -> bool {
                auto* self = static_cast<LogFilteredData*>( context );
                self->maxLengthMarks_
                    = qMax( self->maxLengthMarks_,
                            self->sourceLogData_->getLineLength( LineNumber( line ) ) );
                return true;
            },
            static_cast<void*>( this ) );
    }

    rebuildContextLines();
}

void LogFilteredData::clearMarks()
{
    marks_ = {};
    maxLengthMarks_ = 0_length;
    rebuildContextLines();
}

QList<LineNumber> LogFilteredData::getMarks() const
{
    QList<LineNumber> markedLines;
    marks_.iterate(
        []( uint64_t line, void* context ) -> bool {
            static_cast<QList<LineNumber>*>( context )->append( LineNumber( line ) );
            return true;
        },
        static_cast<void*>( &markedLines ) );

    return markedLines;
}

void LogFilteredData::setVisibility( Visibility visi )
{
    visibility_ = visi;
}

LogFilteredData::Visibility LogFilteredData::visibility() const
{
    return visibility_;
}

void LogFilteredData::updateSearchResultsCache()
{
    const auto& config = Configuration::get();
    if ( !config.useSearchResultsCache() ) {
        return;
    }

    if ( currentSearchKey_ == SearchCacheKey{} ) {
        return;
    }

    const uint64_t maxCacheLines = config.searchResultsCacheLines();

    if ( matching_lines_.cardinality() > maxCacheLines ) {
        LOG_DEBUG << "LogFilteredData: too many matches to place in cache";
    }
    else {
        LOG_INFO << "LogFilteredData: caching results for key "
                 << std::get<0>( currentSearchKey_ ).pattern << "_"
                 << std::get<1>( currentSearchKey_ ) << "_" << std::get<2>( currentSearchKey_ );

        searchResultsCache_[ currentSearchKey_ ] = { matching_lines_, maxLength_ };
        auto cacheSize = std::accumulate( searchResultsCache_.cbegin(), searchResultsCache_.cend(),
                                          uint64_t{ 0 }, []( const auto& acc, const auto& next ) {
                                              return acc + next.second.matching_lines.cardinality();
                                          } );

        LOG_INFO << "LogFilteredData: cache size " << cacheSize;

        auto cachedResult = std::begin( searchResultsCache_ );
        while ( cachedResult != std::end( searchResultsCache_ ) && cacheSize > maxCacheLines ) {

            if ( cachedResult->first == currentSearchKey_ ) {
                ++cachedResult;
                continue;
            }

            cacheSize -= cachedResult->second.matching_lines.cardinality();
            cachedResult = searchResultsCache_.erase( cachedResult );
        }
    }
}

//
// Q_SLOTS:
//
void LogFilteredData::handleSessionStateChanged( SearchSession::State state )
{
    using Phase = SearchSession::Phase;

    if ( state.phase == Phase::Idle || state.phase == Phase::InvalidPattern ) {
        // Nothing was run (or the run was abandoned): nothing to keep.
        matching_lines_ = SearchResultArray();
    }
    else if ( state.fromCache || session_.currentSearchId() != lastSyncedSearchId_ ) {
        // A cache hit, or the first notification of a run (fresh or a
        // continuation) we have not synced from yet: session_.matches()
        // is already exactly right, so take it wholesale rather than
        // union it in -- this only runs once per run, not per tick.
        matching_lines_ = session_.matches();
    }
    else {
        // Another tick of a run already synced from: apply just what's
        // new since the last tick, instead of copying/re-unioning the
        // whole (potentially large) accumulated match set every ~100ms.
        matching_lines_ |= session_.takeNewMatches();
    }
    lastSyncedSearchId_ = session_.currentSearchId();

    marks_and_matches_ = matching_lines_ | marks_;
    maxLength_ = session_.maxLength();
    nbLinesProcessed_ = session_.processedLines();

    if ( state.phase == Phase::Complete && !state.fromCache ) {
        // A cache hit already reaches back to a previously-completed run's
        // stored results; it does not itself need (re-)caching or a
        // Context Lines rebuild -- that already happened when the result
        // it is reusing was first produced.
        if ( nbLinesProcessed_.get() == getExpectedSearchEnd( currentSearchKey_ ).get() ) {
            updateSearchResultsCache();
        }

        rebuildContextLines();

        LOG_INFO << "Matches size " << readableSize( matching_lines_.getSizeInBytes( false ) )
                 << ", marks size " << readableSize( marks_.getSizeInBytes( false ) )
                 << ", union size " << readableSize( marks_and_matches_.getSizeInBytes( false ) );
    }

    Q_EMIT searchStateChanged( state );
}

LineNumber LogFilteredData::findLogDataLine( LineNumber index ) const
{
    const auto& currentResults = currentResultArray();

    LineNumber::UnderlyingType line = {};
    if ( currentResults.select( index.get(), &line ) ) {
        return LineNumber( line );
    }
    else {
        if ( !currentResults.isEmpty() ) {
            LOG_ERROR << "Index too big in LogFilteredData: " << index << " cache size "
                      << currentResults.cardinality();
        }
        return maxValue<LineNumber>();
    }
}

const SearchResultArray& LogFilteredData::currentResultArray() const
{
    const SearchResultArray* base = nullptr;
    if ( visibility_.testFlag( VisibilityFlags::Marks )
         && visibility_.testFlag( VisibilityFlags::Matches ) ) {
        base = &marks_and_matches_;
    }
    else if ( visibility_.testFlag( VisibilityFlags::Matches ) ) {
        base = &matching_lines_;
    }
    else {
        base = &marks_;
    }

    if ( context_lines_.isEmpty() || !visibility_.testFlag( VisibilityFlags::Context ) ) {
        return *base;
    }

    // Rebuild the combined array with context lines included
    with_context_ = *base | context_lines_;
    return with_context_;
}

LineNumber LogFilteredData::findFilteredLine( LineNumber lineNum ) const
{
    LineNumber::UnderlyingType index = currentResultArray().rank( lineNum.get() );

    if ( index > 0 ) {
        index--;
    }
    return LineNumber( index );
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
logsquirl::vector<QString> LogFilteredData::doGetLines( LineNumber first_line, LinesCount number ) const
{
    return doGetLines( first_line, number,
                       [ this ]( const auto& line ) { return doGetLineString( line ); } );
}

// Implementation of the virtual function.
logsquirl::vector<QString> LogFilteredData::doGetExpandedLines( LineNumber first_line,
                                                          LinesCount number ) const
{
    return doGetLines( first_line, number,
                       [ this ]( const auto& line ) { return doGetExpandedLineString( line ); } );
}

logsquirl::vector<QString>
LogFilteredData::doGetLines( LineNumber first_line, LinesCount number,
                             const std::function<QString( LineNumber )>& lineGetter ) const
{
    logsquirl::vector<LineNumber::UnderlyingType> lineNumbers( number.get() );
    std::iota( lineNumbers.begin(), lineNumbers.end(), first_line.get() );

    logsquirl::vector<QString> lines( number.get() );
    std::transform(
        lineNumbers.cbegin(), lineNumbers.cend(), lines.begin(),
        [ &lineGetter ]( const auto& line ) { return lineGetter( LineNumber( line ) ); } );

    return lines;
}

LineNumber LogFilteredData::doGetLineNumber(LineNumber index) const
{
    return getMatchingLineNumber(index);
}

// Implementation of the virtual function.
LinesCount LogFilteredData::doGetNbLine() const
{
    const LinesCount::UnderlyingType nbLines = currentResultArray().cardinality();
    return LinesCount( nbLines );
}

// Implementation of the virtual function.
LineLength LogFilteredData::doGetMaxLength() const
{
    return qMax( maxLength_, maxLengthMarks_ );
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
