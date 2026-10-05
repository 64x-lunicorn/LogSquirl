/*
 * Copyright (C) 2009, 2010, 2013, 2014, 2015 Nicolas Bonnefon and other contributors
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

#ifndef LOGDATA_H
#define LOGDATA_H

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>

#include "textencoding.h"
#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>
#include <qregularexpression.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "abstractlogdata.h"
#include "encodingdetector.h"
#include "fileholder.h"
#include "loadingstatus.h"
#include "logdataoperation.h"
#include "searchblocksource.h"
#include "settingspolicies.h"

class IndexingData;
class LogData;
class LogFilteredData;

// The log data as the block source a Search reads its Log Lines through.
class LogDataBlockSource final : public SearchBlockSource {
public:
    explicit LogDataBlockSource( const LogData& logData )
        : logData_( logData )
    {
    }

    LinesCount getNbLines() const override;
    RawLines getLinesRaw( LineNumber first, LinesCount number ) const override;
    void attachReader() const override;
    void detachReader() const override;

private:
    const LogData& logData_;
};

// Thrown when trying to attach an already attached LogData
class CantReattachErr : public std::exception {};

// Represents a complete set of data to be displayed (ie. a log file content)
// This class is thread-safe.
class LogData : public AbstractLogData {
    Q_OBJECT

    // Reads raw Log Lines for a Search, through getLinesRaw().
    friend class LogDataBlockSource;

public:
    // Lets a test reach the Index it holds.
    template <class T>
    struct access_by;

    // The four Policies are everything this object knows about the
    // settings: what indexing a Log File needs, what running a Search on
    // it needs (handed on to every LogFilteredData built from it), how the
    // file itself is opened, and how its bytes become the text of its Log
    // Lines. It reads no setting of its own -- the log data library does
    // not link the settings library.
    LogData( const IndexingPolicy& indexingPolicy, const SearchPolicy& searchPolicy,
             const FileAccessPolicy& fileAccessPolicy, const DecodingPolicy& decodingPolicy );
    ~LogData();

    LogData( const LogData& ) = delete;
    LogData& operator=( const LogData&& ) = delete;

    LogData( LogData&& ) = delete;
    LogData& operator=( LogData&& ) = delete;

    // Attaches the LogData to a file on disk
    // It starts the asynchronous indexing and returns (almost) immediately
    // Attaching to a non existant file works and the file is reported
    // to be empty.
    // Reattaching is forbidden and will throw.
    void attachFile( const QString& fileName );
    // Interrupt the loading and report a null file.
    // Does nothing if no loading in progress.
    void interruptLoading();
    // Creates a new filtered data.
    // ownership is passed to the caller
    std::unique_ptr<LogFilteredData> getNewFilteredData() const;
    // Returns the size if the file in bytes
    qint64 getFileSize() const;
    // Returns the last modification date for the file.
    // Null if the file is not on disk.
    QDateTime getLastModifiedDate() const;
    // Throw away all the file data and reload/reindex.
    void reload( const TextEncoding* forcedEncoding = nullptr );

    // Get the auto-detected encoding for the indexed text.
    const TextEncoding* getDetectedEncoding() const;

    // Reads the Log File in the Encoding given from now on. When it splits
    // the Log File into Log Lines differently than the Encoding it was
    // indexed in, the Log File is loaded again; otherwise its Log Lines only
    // decode differently, which every LogFilteredData built from it is told.
    void setDisplayEncoding( const TextEncoding& encoding );

    // Replaces the Decoding Policy: every Log Line read from now on, for a
    // view or for a Search, is decoded under it, and decodingPolicyChanged()
    // tells the views to read what they show again. Search results already
    // found are not searched for again.
    void setDecodingPolicy( const DecodingPolicy& decodingPolicy );

    // Replaces the Indexing Policy: the operations requested from now on
    // use it, one already in flight keeps the one it started with.
    void setIndexingPolicy( const IndexingPolicy& indexingPolicy );

    // Replaces the Search Policy, here and in every LogFilteredData built
    // from this Log File -- including the ones a tab kept from an earlier
    // Search, which nothing else holds a list of.
    void setSearchPolicy( const SearchPolicy& searchPolicy );
    const SearchPolicy& searchPolicy() const;

    // There is deliberately no setFileAccessPolicy(): both of its fields
    // are read when an object is built (the FileHolder, and the codec at
    // attach time) and never again. A change to either reaches an open Log
    // File only by reopening it, so a setter would promise more than it
    // could deliver.

    // The text of a sparse set of Log Lines, one entry per Log Line asked
    // for and in the order asked: for each, what getLineString() returns.
    // Nearby Log Lines are merged into runs and each run is read at once,
    // under one lock, with one Decoding Policy for the whole call; each Log
    // Line is decoded on its own. lines may come in any order and repeat; a Log Line past the
    // last one reads as it does on its own. Safe off the UI thread, like
    // reading through searchBlockSource().
    logsquirl::vector<QString> getLinesSparse( std::span<const LineNumber> lines ) const;
    // getExpandedLinesSparse(), from AbstractLogData, reads Log Lines the
    // same way, with tabs expanded.

    // As getLinesSparse(), as getAnsiColoredLines() reads them: as their text
    // under a Decoding Policy that hides ANSI color sequences, with the
    // colors the sequences ask for, whatever the Decoding Policy is.
    logsquirl::vector<AnsiColoredText>
    getAnsiColoredLinesSparse( std::span<const LineNumber> lines ) const;

    // As getLinesSparse(), as UTF-8: for each Log Line asked for, in the order
    // asked, what getLineString() returns converted to UTF-8, followed by a
    // line feed -- byte for byte. A Log Line that is UTF-8 in a UTF-8 Log
    // File, or ASCII in an ASCII compatible one, is copied as it was read and
    // never decoded; any other is decoded on its own and converted back.
    std::string getUtf8LinesSparse( std::span<const LineNumber> lines ) const;

    // Reads Log Lines one after another, in the order asked, each from at
    // most maxBytes of its bytes, decoded as getLinesSparse() decodes them,
    // and hands each to onLine( text, cut ) -- cut when the Log Line has more
    // bytes than were read -- until onLine returns false. A Log Line past the
    // last one indexed ends the read. Never reads more than maxBytes of a Log
    // Line, nor a Log Line after the one onLine refused: what a plugin reads
    // of a selection is bounded by that (#663).
    void readLinePrefixes( std::span<const LineNumber> lines, qint64 maxBytes,
                           const std::function<bool( QString&& text, bool cut )>& onLine ) const;

    // What a Search on this Log File reads its Log Lines through. Lives as
    // long as this object.
    const SearchBlockSource& searchBlockSource() const;

    // A change on disk was heard of for fileName: this Log File or another
    // watched one. The Log File is checked on disk -- reopened first when it
    // was replaced under its name -- and fileChanged() tells what changed.
    // A change to another file is ignored unless this one was replaced.
    //
    // This object watches nothing itself: whoever follows the Log File
    // hears of changes and calls this (see OpenLogFile). Call it only once a
    // file is attached.
    void fileChangedOnDisk( const QString& fileName );

Q_SIGNALS:
    // Sent during the 'attach' process to signal progress
    // percent being the percentage of completion.
    void loadingProgressed( int percent );
    // Signal the client the file is fully loaded and available. When
    // loading failed, the status is Failed and failure describes what went
    // wrong; it is empty otherwise. Reporting it is up to the client.
    void loadingFinished( LoadingStatus status, const QString& failure = {} );
    // Sent when the file on disk has changed, will be followed
    // by loadingProgressed if needed and then a loadingFinished. When
    // checking the file failed, failure describes what went wrong and the
    // file is taken as truncated.
    void fileChanged( MonitoredFileStatus status, const QString& failure = {} );
    // Sent when the file on disk was checked and had not changed.
    void fileUnchanged();
    // Sent when the Decoding Policy was replaced: every Log Line may read
    // differently now, though the Log File itself did not change.
    void decodingPolicyChanged();

private Q_SLOTS:
    // Called when the worker thread signals the current operation ended
    void indexingFinished( LoadingStatus status, const QString& failure );
    // Called when the worker thread signals the current operation ended
    void checkFileChangesFinished( MonitoredFileStatus status, const QString& failure );

private:
    // Implementation of virtual functions
    QString doGetLineString( LineNumber line ) const override;
    QString doGetExpandedLineString( LineNumber line ) const override;
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount number ) const override;
    logsquirl::vector<QString> doGetExpandedLines( LineNumber first,
                                                   LinesCount number ) const override;
    logsquirl::vector<QString>
    doGetExpandedLinesSparse( std::span<const LineNumber> lines ) const override;
    logsquirl::vector<AnsiColoredText> doGetAnsiColoredLines( LineNumber first,
                                                              LinesCount number ) const override;
    LinesCount doGetNbLine() const override;
    LineLength doGetMaxLength() const override;
    LineLength doGetLineLength( LineNumber line ) const override;
    const TextEncoding* doGetDisplayEncoding() const override;
    void doAttachReader() const override;
    void doDetachReader() const override;

    void reOpenFile() const;

    // The raw Log Lines [first, first + number), as a Search reads them
    // through searchBlockSource() and as the block reads here decode them.
    // Only the offsets are taken under the Index's lock.
    RawLines getLinesRaw( LineNumber first, LinesCount number ) const;
    // Tells every LogFilteredData handed out that the Log Lines from
    // firstChanged on may read differently now.
    void logLinesChanged( LineNumber firstChanged = 0_lnum ) const;

    logsquirl::vector<QString> getLinesFromFile( LineNumber first, LinesCount number,
                                                 QString ( *processLine )( QString&& ) ) const;
    // Reads the Log Lines asked for that are indexed, nearby ones merged into
    // runs, and calls onLine( const ReadLogLine& ) for each, in the order
    // read. Log Lines past the last one are not called for. The Index is
    // looked at under its lock, the Log File is read without it.
    template <typename OnLine>
    void readSparseLines( std::span<const LineNumber> lines, OnLine&& onLine ) const;

private:
    mutable std::unique_ptr<FileHolder> attached_file_;

    // Indexing data, read by us, written by the worker thread
    std::shared_ptr<IndexingData> indexing_data_;

    OperationQueue operationQueue_;

    // Every LogFilteredData handed out by getNewFilteredData(), so that a
    // changed Search Policy reaches all of them. QPointer rather than a
    // shared handle: the caller owns them, and one that has been destroyed
    // simply drops out of this list.
    mutable std::vector<QPointer<LogFilteredData>> filteredData_;

    QString indexingFileName_;
    // mutable std::unique_ptr<QFile> attached_file_;
    // mutable FileId attached_file_id_;

    SearchPolicy searchPolicy_;
    // Both of its fields are read when an object is built and never again:
    // keeping a file closed is fixed when the FileHolder is created, and
    // the default Encoding when the file is attached. A change to either
    // reaches an already-open Log File only by reopening it.
    const FileAccessPolicy fileAccessPolicy_;

    QDateTime lastModifiedDate_;

    // Codec to decode text
    TextCodecHolder codec_;
    // How many Log Lines were indexed before the data added on disk is,
    // taken when a Check finds only growth.
    LinesCount nbLinesBeforeDataAdded_;

    // Read by getLinesRaw() on the Search's threads, so it is only ever
    // touched under the indexing data's lock.
    DecodingPolicy decodingPolicy_;

    LogDataBlockSource searchBlockSource_{ *this };
};

#endif
