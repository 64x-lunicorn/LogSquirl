/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
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

#ifndef LOGFILTEREDDATAWORKERTHREAD_H
#define LOGFILTEREDDATAWORKERTHREAD_H

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>

#include <QObject>
#include <QString>

#include <qthreadpool.h>

#ifndef Q_MOC_RUN
#include <roaring.hh>
#include <roaring64map.hh>
#endif

#include <type_safe/strong_typedef.hpp>

#include "linetypes.h"
#include "regularexpression.h"
#include "settingspolicies.h"
#include "synchronization.h"

class SearchBlockSource;

// Identifies one Search run. A new run gets a fresh id; whatever owns the
// worker compares an incoming result's id against the id it is currently
// waiting on to tell a result belonging to a superseded run from a live one.
struct SearchId : type_safe::strong_typedef<SearchId, uint64_t>,
                  type_safe::strong_typedef_op::equality_comparison<SearchId> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = uint64_t;

    UnderlyingType get() const
    {
        return type_safe::get( *this );
    }
};
Q_DECLARE_METATYPE( SearchId )

// Class encapsulating a single matching line
// Contains the line number the line was found in and its content.
class MatchingLine {
public:
    MatchingLine( LineNumber line )
        : lineNumber_{ line }
    {
    }

    // Accessors
    LineNumber lineNumber() const
    {
        return lineNumber_;
    }

    bool operator<( const MatchingLine& other ) const
    {
        return lineNumber_ < other.lineNumber_;
    }

private:
    LineNumber lineNumber_;
};

// This is an array of matching lines.
// It shall be implemented for random lookup speed, so
// a fixed "in-place" array (vector) is probably fine.
using SearchResultArray = roaring::Roaring64Map;

// The line at position in lines, counting from 0 in their order; none at or
// past their end.
inline OptionalLineNumber lineAtPosition( const SearchResultArray& lines, LineNumber position )
{
    LineNumber::UnderlyingType line = {};
    if ( !lines.select( position.get(), &line ) ) {
        return {};
    }
    return LineNumber( line );
}

struct SearchResults {
    SearchResultArray newMatches;
    LineLength maxLength;
    LinesCount processedLines;
};

// This class is a mutex protected set of search result data.
// It is thread safe.
//
// The blocks of a parallel Search are combined in whatever order their
// matching finishes. The Log Lines counted as processed are only those up to
// the first one not searched yet, so that a Search continued from there leaves
// no Log Line out; a block combined beyond that gap counts once the gap is
// closed. How many Matches there are is not counted here: the Matches are a
// set, and a Log Line searched again (the last one, when a Search continues)
// is in it once however often it matched.
class SearchData {
public:
    // will clear new matches
    SearchResults takeCurrentResults() const;

    // Starts counting the Log Lines searched from line: those before it count
    // as searched, whatever was combined from it on no longer does. The
    // Matches already found are kept.
    void searchFrom( LineNumber line );

    // Atomically add the Matches of the block of blockLines Log Lines from
    // blockStart, which has been searched.
    void addAll( LineLength length, const SearchResultArray& matches, LineNumber blockStart,
                 LinesCount blockLines );

    // The first Log Line not searched yet: every Log Line before it was.
    LineNumber getLastProcessedLine() const;

    // Atomically clear the data.
    void clear();

private:
    mutable SharedMutex dataMutex_;

    mutable SearchResultArray newMatches_;
    LineLength maxLength_{ 0 };
    LineNumber searchedUntil_{ 0 };
    // The blocks combined beyond searchedUntil_, by their first Log Line, with
    // the Log Line after their last one.
    std::map<LineNumber::UnderlyingType, LineNumber::UnderlyingType> searchedAhead_;
};

class SearchOperation : public QObject {
    Q_OBJECT
public:
    // compiledExpression is shared (not copied) with whoever validated the
    // pattern before starting this run, so the (expensive, Hyperscan-
    // backed) compile happens exactly once per request() rather than once
    // there and once more here.
    // The Search Policy is copied in when the operation is built, so
    // everything this run reads about the settings is fixed for its
    // duration: the options dialog is modal to the window but does not
    // stop this pool, and a setting changed mid-run takes effect on the
    // next run rather than half way through this one.
    SearchOperation( const SearchBlockSource& blockSource, SearchId searchId,
                     const std::atomic<uint64_t>& activeSearchId,
                     std::shared_ptr<const RegularExpression> compiledExpression,
                     LineNumber startLine, LineNumber endLine, SearchPolicy searchPolicy );

    // Run the search operation, reporting how it ended through
    // searchFinished. An exception escaping the run is a failure of the
    // engine: the results are dropped and searchFinished carries its
    // description, rather than a dialog being opened or the exception
    // thrown further.
    void run( SearchData& result );

Q_SIGNALS:
    void searchProgressed( int percent, LineNumber initialLine, SearchId searchId );
    // interrupted is true when this run was superseded by another (or explicitly
    // interrupted) before it reached the end of its range. failure describes
    // what went wrong when the run failed, and is empty otherwise.
    void searchFinished( SearchId searchId, LineNumber initialLine, bool interrupted,
                         const QString& failure );

protected:
    // The run itself, which run() reports the failure of.
    virtual void doRun( SearchData& result ) = 0;

    // Implement the common part of the search, passing
    // the shared results and the line to begin the search from.
    void doSearch( SearchData& result, LineNumber initialLine );

    // True once another run has become the active one, i.e. this run has been
    // superseded (by a newer search) or explicitly interrupted.
    bool isSuperseded() const;

    SearchId searchId_;
    const std::atomic<uint64_t>& activeSearchId_;
    const std::shared_ptr<const RegularExpression> compiledExpression_;
    const SearchBlockSource& blockSource_;
    LineNumber startLine_;
    LineNumber endLine_;
    const SearchPolicy searchPolicy_;
};

// A Search over its whole range, which replaces whatever the Search Data
// holds -- and replaces it even when this run is superseded before it runs, so
// that the run after it, which may continue from the data rather than replace
// it too, never continues on another pattern's Matches.
class FullSearchOperation : public SearchOperation {
    Q_OBJECT
public:
    FullSearchOperation( const SearchBlockSource& blockSource, SearchId searchId,
                         const std::atomic<uint64_t>& activeSearchId,
                         std::shared_ptr<const RegularExpression> compiledExpression,
                         LineNumber startLine, LineNumber endLine, SearchPolicy searchPolicy )
        : SearchOperation( blockSource, searchId, activeSearchId, std::move( compiledExpression ),
                           startLine, endLine, searchPolicy )
    {
    }

protected:
    void doRun( SearchData& result ) override;
};

class UpdateSearchOperation : public SearchOperation {
    Q_OBJECT
public:
    UpdateSearchOperation( const SearchBlockSource& blockSource, SearchId searchId,
                           const std::atomic<uint64_t>& activeSearchId,
                           std::shared_ptr<const RegularExpression> compiledExpression,
                           LineNumber startLine, LineNumber endLine, LineNumber position,
                           SearchPolicy searchPolicy )
        : SearchOperation( blockSource, searchId, activeSearchId, std::move( compiledExpression ),
                           startLine, endLine, searchPolicy )
        , initialPosition_( position )
    {
    }

protected:
    void doRun( SearchData& result ) override;

private:
    LineNumber initialPosition_;
};

class LogFilteredDataWorker : public QObject {
    Q_OBJECT

public:
    // The Search Policy is what this worker knows about the settings; it
    // reads none itself.
    LogFilteredDataWorker( const SearchBlockSource& blockSource, const SearchPolicy& searchPolicy );
    ~LogFilteredDataWorker() noexcept override;

    LogFilteredDataWorker( const LogFilteredDataWorker& ) = delete;
    LogFilteredDataWorker& operator=( const LogFilteredDataWorker&& ) = delete;

    LogFilteredDataWorker( LogFilteredDataWorker&& ) = delete;
    LogFilteredDataWorker& operator=( LogFilteredDataWorker&& ) = delete;

    // Start the search with the passed (already-compiled, e.g. by however
    // validated it) expression. Returns the id of the run started, which
    // becomes the new active run -- superseding whatever was running
    // before, without waiting for it to acknowledge.
    SearchId search( std::shared_ptr<const RegularExpression> compiledExpression,
                     LineNumber startLine, LineNumber endLine );
    // Continue the previous search starting at the passed position
    // in the source file (line number). Returns the id of the run started.
    SearchId updateSearch( std::shared_ptr<const RegularExpression> compiledExpression,
                           LineNumber startLine, LineNumber endLine, LineNumber position );

    // Replaces the Search Policy used by the runs started from now on. A
    // run already in flight keeps the Policy it was started with.
    void setSearchPolicy( const SearchPolicy& searchPolicy );

    // Interrupts the search if one is in progress. Does not wait for it to
    // acknowledge; the run simply stops being the active one, so its next
    // progress check will see itself as superseded.
    void interrupt();

    // get the current indexing data
    SearchResults getSearchResults() const;

Q_SIGNALS:
    // Sent during the indexing process to signal progress
    // percent being the percentage of completion.
    void searchProgressed( int percent, LineNumber initialLine, SearchId searchId );
    // Sent once a run stops, one way or another. interrupted is true when the
    // run was superseded or explicitly interrupted before reaching its end;
    // failure describes what went wrong when the run failed.
    void searchFinished( SearchId searchId, LineNumber initialLine, bool interrupted,
                         const QString& failure );

private:
    void connectSignalsAndRun( SearchOperation* operationRequested );

private:
    const SearchBlockSource& blockSource_;

    // Read and written under operationsMutex_ and copied into every
    // operation as it is started, so a run never reads it from the pool
    // thread while the UI thread is replacing it.
    SearchPolicy searchPolicy_;

    // The id of the run currently considered "active". A run compares its own
    // id against this to tell whether it has been superseded; interrupt() (and
    // starting a new run) simply change what this holds. 0 means no run active.
    std::atomic<uint64_t> activeSearchId_{ 0 };
    std::atomic<uint64_t> nextSearchId_{ 0 };

    QThreadPool operationsPool_;
    Mutex operationsMutex_;

    // Shared indexing data
    SearchData searchData_;
};

#endif
