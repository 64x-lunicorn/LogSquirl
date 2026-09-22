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
#include <span>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QStringList>

#include "abstractlogdata.h"
#include "displayedlines.h"
#include "hsregularexpression.h"
#include "linetypes.h"
#include "logfiltereddataworker.h"
#include "searchsession.h"
#include "synchronization.h"

class LogData;
class QTimer;

// The Filtered View's log data: presents the Displayed Lines (the Matches
// of its Search Session, the Marks and the Context Lines) through the
// AbstractLogData interface, together with the Log Line each one is, and
// relays the Search Session's state changes.
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

    // Marks interface (delegated to the Displayed Lines)

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
    // The Log Lines of the Log File from firstChanged on may read differently
    // now: it was indexed again, cut short or appended to, or is decoded
    // differently. The lengths remembered for the Marks among them are read
    // again; a Mark past the last Log Line is as wide as nothing.
    void logLinesChanged( LineNumber firstChanged = 0_lnum );
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

    // How many of the Log Lines displayed in [first, end) are Matches and how
    // many are not, whatever the visibility (see DisplayedLines::countIn()).
    DisplayedLines::Count countDisplayedLines( LineNumber first, LineNumber end ) const;
    // Changes whenever the displayed lines change other than by Matches added
    // after all of them (see DisplayedLines::rewrites()).
    uint64_t displayedLinesRewrites() const;

    // Replaces the Search Policy, for this object and its Search Session.
    // Called when a setting on the Search axis changed; the Log File this
    // was built from does the calling, so every LogFilteredData is reached,
    // not only the one the active tab happens to be showing.
    void setSearchPolicy( const SearchPolicy& searchPolicy );

    // The Search Session's current typed state (pattern, range, match
    // count, progress, phase, whether results came from cache).
    SearchSession::State searchState() const;

    // A copy of the lines the Filtered View displays now, as Log Line
    // numbers. Take it on the UI thread: the copy can then be read on another
    // thread (QuickFind's worker) while this object keeps changing.
    SearchResultArray copyDisplayedLines() const;
    // The Log File this was built from.
    const LogData& sourceLogData() const;

Q_SIGNALS:
    // Sent whenever the Search Session's state changes: on progress, on
    // completion (from a real run or from cache), when stopped, when
    // going idle, or when the pattern fails to compile. The Displayed Lines
    // have followed the Matches by then.
    void searchStateChanged( SearchSession::State state );

private:
    // Implementation of virtual functions
    QString doGetLineString( LineNumber line ) const override;
    QString doGetExpandedLineString( LineNumber line ) const override;
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount number ) const override;
    logsquirl::vector<QString> doGetExpandedLines( LineNumber first,
                                                   LinesCount number ) const override;
    // The text of the Log Lines displayed at [first, first + number), read
    // from the Log File at once with readSparse; empty past the last one.
    logsquirl::vector<QString> readDisplayedLines(
        LineNumber first, LinesCount number,
        logsquirl::vector<QString> ( LogData::*readSparse )( std::span<const LineNumber> )
            const ) const;
    LineNumber doGetLineNumber( LineNumber index ) const override;
    LinesCount doGetNbLine() const override;
    LineLength doGetMaxLength() const override;
    LineLength doGetLineLength( LineNumber line ) const override;

    void doSetDisplayEncoding( const char* encoding ) override;
    QTextCodec* doGetDisplayEncoding() const override;

    void doAttachReader() const override;
    void doDetachReader() const override;

    const LogData* sourceLogData_;

    // Owns the pattern, the run in flight, its Matches and its progress.
    SearchSession session_;
    // Owns the Marks with their lengths and the Context Lines, and reads
    // session_'s Matches in place: declared after session_, so it never
    // outlives them.
    DisplayedLines displayedLines_;

private:
    // Utility functions
    LineNumber findLogDataLine( LineNumber lineNum ) const;
    LineNumber findFilteredLine( LineNumber lineNum ) const;
};

Q_DECLARE_OPERATORS_FOR_FLAGS( LogFilteredData::Visibility )
Q_DECLARE_METATYPE( LogFilteredData::Visibility )

#endif
