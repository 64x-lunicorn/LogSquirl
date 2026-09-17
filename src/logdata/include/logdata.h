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

#include <memory>

#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTextCodec>
#include <qregularexpression.h>
#include <qtextcodec.h>
#include <span>
#include <string_view>
#include <vector>

#include "abstractlogdata.h"
#include "fileholder.h"
#include "loadingstatus.h"
#include "logdataoperation.h"
#include "logdataworker.h"
#include "searchblocksource.h"
#include "settingspolicies.h"

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
class CantReattachErr {};

// Represents a complete set of data to be displayed (ie. a log file content)
// This class is thread-safe.
class LogData : public AbstractLogData {
    Q_OBJECT

public:
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
    void reload( QTextCodec* forcedEncoding = nullptr );

    // Get the auto-detected encoding for the indexed text.
    QTextCodec* getDetectedEncoding() const;

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

    // There is deliberately no setFileAccessPolicy(): both of its fields
    // are read when an object is built (the FileHolder, and the codec at
    // attach time) and never again. A change to either reaches an open Log
    // File only by reopening it, so a setter would promise more than it
    // could deliver.

    // A block of raw Log Lines, as a Search reads them.
    using RawLines = ::RawLines;

    RawLines getLinesRaw( LineNumber first, LinesCount number ) const;

    // The text of a sparse set of Log Lines, one entry per Log Line asked
    // for and in the order asked: for each, what getLineString() returns.
    // Nearby Log Lines are merged into runs and each run is read at once,
    // under one lock, with one text decoder and one ANSI color filter for the
    // whole call. lines may come in any order and repeat; a Log Line past the
    // last one reads as it does on its own. Safe off the UI thread, like
    // getLinesRaw().
    logsquirl::vector<QString> getLinesSparse( std::span<const LineNumber> lines ) const;
    // getExpandedLinesSparse(), from AbstractLogData, reads Log Lines the
    // same way, with tabs expanded.

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
    LineNumber doGetLineNumber( LineNumber index ) const override;
    LinesCount doGetNbLine() const override;
    LineLength doGetMaxLength() const override;
    LineLength doGetLineLength( LineNumber line ) const override;
    void doSetDisplayEncoding( const char* encoding ) override;
    QTextCodec* doGetDisplayEncoding() const override;
    void doAttachReader() const override;
    void doDetachReader() const override;

    void reOpenFile() const;

    logsquirl::vector<QString> getLinesFromFile( LineNumber first, LinesCount number,
                                                 QString ( *processLine )( QString&& ) ) const;
    logsquirl::vector<QString>
    getSparseLinesFromFile( std::span<const LineNumber> lines,
                            QString ( *processLine )( QString&& ) ) const;

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

    IndexingPolicy indexingPolicy_;
    SearchPolicy searchPolicy_;
    // Both of its fields are read when an object is built and never again:
    // keeping a file closed is fixed when the FileHolder is created, and
    // the default Encoding when the file is attached. A change to either
    // reaches an already-open Log File only by reopening it.
    const FileAccessPolicy fileAccessPolicy_;

    QDateTime lastModifiedDate_;

    // Codec to decode text
    TextCodecHolder codec_;
    MonitoredFileStatus fileChangedOnDisk_;

    // Read by getLinesRaw() on the Search's threads, so it is only ever
    // touched under the indexing data's lock.
    DecodingPolicy decodingPolicy_;

    LogDataBlockSource searchBlockSource_{ *this };
};

#endif
