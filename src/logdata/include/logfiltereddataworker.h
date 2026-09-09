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

#include <QObject>

#include <qthreadpool.h>

#ifndef Q_MOC_RUN
#include <roaring.hh>
#include <roaring64map.hh>
#include <tbb/task_group.h>
#endif

#include <type_safe/strong_typedef.hpp>

#include "linetypes.h"
#include "regularexpression.h"
#include "synchronization.h"

class LogData;

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

struct SearchResults {
    SearchResultArray newMatches;
    LineLength maxLength;
    LinesCount processedLines;
};

// This class is a mutex protected set of search result data.
// It is thread safe.
class SearchData {
public:
    // will clear new matches
    SearchResults takeCurrentResults() const;

    // Atomically add to all the existing search data.
    void addAll( LineLength length, const SearchResultArray& matches, LinesCount nbMatches,
                 LinesCount nbLinesProcessed );
    // Get the number of matches
    LinesCount getNbMatches() const;
    // Get the last matched line number
    // That is "last" as in biggest, not latest
    // 0 if no matches have been found yet
    LineNumber getLastMatchedLineNumber() const;

    LineNumber getLastProcessedLine() const;

    // Delete the match for the passed line (if it exist)
    void deleteMatch( LineNumber line );

    // Atomically clear the data.
    void clear();

private:
    mutable SharedMutex dataMutex_;

    SearchResultArray matches_;
    mutable SearchResultArray newMatches_;
    LineLength maxLength_{ 0 };
    LinesCount nbLinesProcessed_{ 0 };
    LinesCount nbMatches_{ 0 };
};

class SearchOperation : public QObject {
    Q_OBJECT
public:
    SearchOperation( const LogData& sourceLogData, SearchId searchId,
                     const std::atomic<uint64_t>& activeSearchId,
                     const RegularExpressionPattern& regExp, LineNumber startLine,
                     LineNumber endLine );

    // Run the search operation, returns true if it has been done
    // and false if it has been cancelled (results not copied)
    virtual void run( SearchData& result ) = 0;

Q_SIGNALS:
    void searchProgressed( LinesCount nbMatches, int percent, LineNumber initialLine,
                           SearchId searchId );
    // interrupted is true when this run was superseded by another (or explicitly
    // interrupted) before it reached the end of its range.
    void searchFinished( SearchId searchId, LinesCount nbMatches, LineNumber initialLine,
                         bool interrupted );

protected:
    // Implement the common part of the search, passing
    // the shared results and the line to begin the search from.
    void doSearch( SearchData& result, LineNumber initialLine );

    // True once another run has become the active one, i.e. this run has been
    // superseded (by a newer search) or explicitly interrupted.
    bool isSuperseded() const;

    SearchId searchId_;
    const std::atomic<uint64_t>& activeSearchId_;
    const RegularExpressionPattern regexp_;
    const LogData& sourceLogData_;
    LineNumber startLine_;
    LineNumber endLine_;
};

class FullSearchOperation : public SearchOperation {
    Q_OBJECT
public:
    FullSearchOperation( const LogData& sourceLogData, SearchId searchId,
                         const std::atomic<uint64_t>& activeSearchId,
                         const RegularExpressionPattern& regExp, LineNumber startLine,
                         LineNumber endLine )
        : SearchOperation( sourceLogData, searchId, activeSearchId, regExp, startLine, endLine )
    {
    }

    void run( SearchData& result ) override;
};

class UpdateSearchOperation : public SearchOperation {
    Q_OBJECT
public:
    UpdateSearchOperation( const LogData& sourceLogData, SearchId searchId,
                           const std::atomic<uint64_t>& activeSearchId,
                           const RegularExpressionPattern& regExp, LineNumber startLine,
                           LineNumber endLine, LineNumber position )
        : SearchOperation( sourceLogData, searchId, activeSearchId, regExp, startLine, endLine )
        , initialPosition_( position )
    {
    }

    void run( SearchData& result ) override;

private:
    LineNumber initialPosition_;
};

class LogFilteredDataWorker : public QObject {
    Q_OBJECT

public:
    explicit LogFilteredDataWorker( const LogData& sourceLogData );
    ~LogFilteredDataWorker() noexcept override;

    LogFilteredDataWorker( const LogFilteredDataWorker& ) = delete;
    LogFilteredDataWorker& operator=( const LogFilteredDataWorker&& ) = delete;

    LogFilteredDataWorker( LogFilteredDataWorker&& ) = delete;
    LogFilteredDataWorker& operator=( LogFilteredDataWorker&& ) = delete;

    // Start the search with the passed regexp. Returns the id of the run started,
    // which becomes the new active run -- superseding whatever was running before,
    // without waiting for it to acknowledge.
    SearchId search( const RegularExpressionPattern& regExp, LineNumber startLine,
                     LineNumber endLine );
    // Continue the previous search starting at the passed position
    // in the source file (line number). Returns the id of the run started.
    SearchId updateSearch( const RegularExpressionPattern& regExp, LineNumber startLine,
                           LineNumber endLine, LineNumber position );

    // Interrupts the search if one is in progress. Does not wait for it to
    // acknowledge; the run simply stops being the active one, so its next
    // progress check will see itself as superseded.
    void interrupt();

    // get the current indexing data
    SearchResults getSearchResults() const;

Q_SIGNALS:
    // Sent during the indexing process to signal progress
    // percent being the percentage of completion.
    void searchProgressed( LinesCount nbMatches, int percent, LineNumber initialLine,
                           SearchId searchId );
    // Sent once a run stops, one way or another. interrupted is true when the
    // run was superseded or explicitly interrupted before reaching its end.
    void searchFinished( SearchId searchId, LinesCount nbMatches, LineNumber initialLine,
                         bool interrupted );

private:
    void connectSignalsAndRun( SearchOperation* operationRequested );

private:
    const LogData& sourceLogData_;

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
