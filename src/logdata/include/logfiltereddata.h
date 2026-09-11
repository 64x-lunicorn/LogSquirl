/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2017 Nicolas Bonnefon and other contributors
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

#ifndef LOGFILTEREDDATA_H
#define LOGFILTEREDDATA_H

#include <cstdint>
#include <functional>
#include <memory>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QStringList>

#include "abstractlogdata.h"
#include "hsregularexpression.h"
#include "linetypes.h"
#include "logfiltereddataworker.h"
#include "searchsession.h"
#include "synchronization.h"

class LogData;
class QTimer;

// A list of matches found in a LogData, it stores all the matching lines,
// which can be accessed using the AbstractLogData interface, together with
// the original line number where they were found.
// Constructing such objet does not start the search.
// This object should be constructed by a LogData.
class LogFilteredData : public AbstractLogData {
    Q_OBJECT

public:
    // Constructor used by LogData, which hands on the Search Policy it was
    // built with: everything this object and its Search Session know about
    // the settings arrives here.
    LogFilteredData( const LogData* logData, const SearchPolicy& searchPolicy );

    // Destructor: disconnects signals before member destruction to prevent
    // use-after-destroy from a queued signal firing during teardown.
    ~LogFilteredData() override;

    // Requests results for regExp over [startLine, endLine), superseding
    // whatever run is currently in flight -- callers no longer need to
    // interrupt or clear before requesting. When regExp/startLine match
    // the currently held run and endLine only grows, continues that run
    // (used when the file on disk has grown) rather than starting over.
    void request( const RegularExpressionPattern& regExp, LineNumber startLine,
                  LineNumber endLine );
    // Shortcut for request() on the whole file.
    void request( const RegularExpressionPattern& regExp );
    // Go idle: clears the pattern, the results and (optionally) the cache.
    void request( bool dropCache = false );
    // Stops the in-flight run, if any, keeping whatever has been found so
    // far. A no-op if nothing is running.
    void stop();

    // Returns the line number in the original LogData where the element
    // 'index' was found.
    LineNumber getMatchingLineNumber( LineNumber index ) const;
    // Returns the line 'index' in filterd log data that matches
    // given original line number
    LineNumber getLineIndexNumber( LineNumber lineNumber ) const;

    // Returns the number of lines in the source log data
    LinesCount getNbTotalLines() const;
    // Returns the number of matches (independently of the visibility)
    LinesCount getNbMatches() const;
    // Returns the number of marks (independently of the visibility)
    LinesCount getNbMarks() const;

    LineType lineTypeByIndex( LineNumber index ) const;
    LineType lineTypeByLine( LineNumber lineNumber ) const;

    // Marks interface (delegated to a Marks object)

    // Add a mark at the given line
    void addMark( LineNumber line );
    // Get the first mark after the line passed
    OptionalLineNumber getMarkAfter( LineNumber line ) const;
    // Get the first mark before the line passed
    OptionalLineNumber getMarkBefore( LineNumber line ) const;
    // Delete the mark present on the passed line
    void deleteMark( LineNumber line );
    // Toggle presence of the mark on the passed line.
    void toggleMark( LineNumber line );
    // Completely clear the marks list.
    void clearMarks();
    // Get all marked lines
    QList<LineNumber> getMarks() const;

    // Changes what the AbstractLogData returns via its getXLines/getNbLines
    // API.
    enum class VisibilityFlags {
        None = static_cast<LineType::Int>( LineTypeFlags::Plain ), // this is for internal use
        Matches = static_cast<LineType::Int>( LineTypeFlags::Match ),
        Marks = static_cast<LineType::Int>( LineTypeFlags::Mark ),
        Context = static_cast<LineType::Int>( LineTypeFlags::Context ),
    };
    Q_ENUM( VisibilityFlags );
    Q_DECLARE_FLAGS( Visibility, VisibilityFlags )
    void setVisibility( Visibility visibility );
    Visibility visibility() const;

    void iterateOverLines( const std::function<void( LineNumber )>& callback ) const;

    // Replaces the Search Policy, for this object and its Search Session.
    // Called when a setting on the Search axis changed; the Log File this
    // was built from does the calling, so every LogFilteredData is reached,
    // not only the one the active tab happens to be showing.
    void setSearchPolicy( const SearchPolicy& searchPolicy );

    // Rebuilds context (breadcrumb) lines around matches/marks.
    // Call after search completes or contextLinesCount changes.
    void rebuildContextLines();

    // The Search Session's current typed state (pattern, range, match
    // count, progress, phase, whether results came from cache).
    SearchSession::State searchState() const;

Q_SIGNALS:
    // Sent whenever the Search Session's state changes: on progress, on
    // completion (from a real run or from cache), when stopped, when
    // going idle, or when the pattern fails to compile.
    void searchStateChanged( SearchSession::State state );

private Q_SLOTS:
    void handleSessionStateChanged( SearchSession::State state );

private:
    // Implementation of virtual functions
    QString doGetLineString( LineNumber line ) const override;
    QString doGetExpandedLineString( LineNumber line ) const override;
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount number ) const override;
    logsquirl::vector<QString> doGetExpandedLines( LineNumber first,
                                                   LinesCount number ) const override;
    logsquirl::vector<QString>
    doGetLines( LineNumber first, LinesCount number,
                const std::function<QString( LineNumber )>& lineGetter ) const;
    LineNumber doGetLineNumber( LineNumber index ) const override;
    LinesCount doGetNbLine() const override;
    LineLength doGetMaxLength() const override;
    LineLength doGetLineLength( LineNumber line ) const override;

    void doSetDisplayEncoding( const char* encoding ) override;
    QTextCodec* doGetDisplayEncoding() const override;

    void doAttachReader() const override;
    void doDetachReader() const override;

    // Insert new mark into filteredItemsCache_.
    void updateCacheWithMark( uint32_t index, LineNumber line );

    // Returns whether the line number passed is in our list of matching ones.
    bool isLineMatched( LineNumber lineNumber ) const;
    // Returns wheither the passed line has a mark on it.
    bool isLineMarked( LineNumber line ) const;

    // List of the matching line numbers
    SearchResultArray matching_lines_;
    SearchResultArray marks_;
    SearchResultArray marks_and_matches_;
    // Combined result including Context Lines (session_.contextLines()).
    mutable SearchResultArray with_context_;

    const LogData* sourceLogData_;

    LineLength maxLength_;
    LineLength maxLengthMarks_;
    // Number of lines of the LogData that has been searched for:
    LinesCount nbLinesProcessed_;

    Visibility visibility_;

    // Owns the pattern, the run in flight, its results and its progress.
    SearchSession session_;

    // The run id matching_lines_ was last synced from. When a notification
    // carries the same id, only the delta since then needs to be applied;
    // a different id (a new run, a continuation, a cache hit) means
    // matching_lines_ must be replaced wholesale instead.
    SearchId lastSyncedSearchId_{ 0 };

private:
    // Utility functions
    const SearchResultArray& currentResultArray() const;
    LineNumber findLogDataLine( LineNumber lineNum ) const;
    LineNumber findFilteredLine( LineNumber lineNum ) const;

    // update maxLengthMarks_ when a Marks was changed.
    void updateMaxLengthMarks( OptionalLineNumber added_line, OptionalLineNumber removed_line );
};

Q_DECLARE_OPERATORS_FOR_FLAGS( LogFilteredData::Visibility )
Q_DECLARE_METATYPE( LogFilteredData::Visibility )

#endif
