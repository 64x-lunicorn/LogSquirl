/*
 * Copyright (C) 2026 LogSquirl Contributors
 *
 * This file is part of LogSquirl.
 *
 * LogSquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LogSquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LogSquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OPENLOGFILE_H
#define OPENLOGFILE_H

#include <memory>

#include <QMetaObject>
#include <QMetaType>
#include <QObject>
#include <QString>

#include "linetypes.h"
#include "loadingstatus.h"
#include "logformatdefinition.h"
#include "regularexpressionpattern.h"
#include "searchautorefresh.h"
#include "searchsession.h"
#include "settingspolicies.h"

class FileWatchPort;
class LogData;
class LogFilteredData;
class LogFormatCatalog;

// A Log File from the moment it is opened until it is closed, together with
// what follows it as it changes on disk: its log data, its Searches and their
// auto-refresh, its Marks and its Log Format.
//
// It decides what growing, truncation and reloading mean. A Search continues
// over the Log Lines that were added, and starts again when the Log File was
// truncated; a reload by hand drops it. Marks do not survive a truncation or a
// reload; the Marks saved with the Session are applied once, after the first
// load. Format Recognition is taken after the first load, and again after a
// reload or a truncation, never on growth.
//
// It tells its users what happened -- the Log File loaded, grew or was
// truncated, the Search updated -- and they only show it. It knows no widget:
// the desktop application and the command line tool follow a Log File the
// same way because both use it.
//
// It hears of changes on disk through the File Watch Port it is built with:
// its Log File is watched from the first load that succeeds until this object
// is destroyed, and the log data checks the file whenever a change is heard
// of. It looks no watcher up by itself (#249).
class OpenLogFile : public QObject {
    Q_OBJECT

public:
    // What a finished load of the Log File brought, once this object has
    // followed it: the Search refreshed, the Search range covering the whole
    // Log File again, saved Marks applied and Format Recognition taken where
    // due.
    struct LoadFinished {
        LoadingStatus status = LoadingStatus::Successful;
        // What went wrong, when status is Failed; empty otherwise.
        QString failure;
        // Loaded from its start -- the Log File was opened or reloaded by
        // hand -- rather than loaded again after it changed on disk.
        bool fromStart = false;
        // The Search started again over the Log File, which had been
        // truncated under it.
        bool searchRestarted = false;
        // Format Recognition was taken; logFormat() tells what it recognized.
        bool formatRecognized = false;
    };

    // The Policies are everything it knows about the settings: those of the
    // log data, and the Recognition Policy with the application's Log Format
    // Catalog that Format Recognition runs on. A null Catalog recognizes
    // nothing.
    //
    // fileWatch is how it hears of changes on disk; it is held until this
    // object is destroyed, and used on the thread it lives in, which has to be
    // this object's. Without one the Log File is not followed on disk.
    OpenLogFile( const IndexingPolicy& indexingPolicy, const SearchPolicy& searchPolicy,
                 const FileAccessPolicy& fileAccessPolicy, const DecodingPolicy& decodingPolicy,
                 const RecognitionPolicy& recognitionPolicy,
                 std::shared_ptr<const LogFormatCatalog> logFormatCatalog,
                 std::shared_ptr<FileWatchPort> fileWatch, QObject* parent = nullptr );
    ~OpenLogFile() override;

    OpenLogFile( const OpenLogFile& ) = delete;
    OpenLogFile& operator=( const OpenLogFile& ) = delete;

    // Starts loading the Log File. It can be opened once.
    void open( const QString& fileName );

    // Marks saved with the Session for this Log File, applied once, when the
    // first load has finished. Hand them over before that.
    void restoreMarks( const logsquirl::vector<LineNumber>& marks );

    // Loads the Log File again from its start: the Search is dropped with its
    // cached results, the Marks are cleared and the Log Format is recognized
    // again once it has loaded.
    void reload();

    // Stops the Search in flight and the load in progress, if any.
    void stopLoading();

    // The log data, for as long as this object lives.
    const std::shared_ptr<LogData>& logData() const;

    // The current Search: the one requestSearch() runs, auto-refresh follows
    // and Marks go to.
    const std::shared_ptr<LogFilteredData>& filteredData() const;

    // Keeps the current Search with its results, and makes a new one, with no
    // pattern yet, current. Returns it.
    std::shared_ptr<LogFilteredData> startAnotherSearch();

    // Makes a Search kept earlier current again, stopping the current one.
    void makeSearchCurrent( std::shared_ptr<LogFilteredData> search );

    // Requests the current Search for pattern over the Search range. It
    // supersedes the Search before it; an invalid pattern leaves no Search
    // active. Returns the Search's state right after the request.
    //
    // Requested before the Log File has first loaded, the Search waits for
    // that load and then runs over the whole Log File, so a user need not
    // wait for loading to request it; the pattern is validated then, and an
    // invalid one is told through searchUpdated(). A load that does not
    // succeed drops it. Until then the returned state is Running.
    SearchSession::State requestSearch( const RegularExpressionPattern& pattern );
    // No Search is active any longer: the current Search goes idle.
    void clearSearch();
    // Stops the current Search, keeping what it found; auto-refresh is
    // suspended.
    void stopSearch();
    // The Search pattern was changed and not yet requested: auto-refresh is
    // suspended.
    void changeSearchExpression();
    // Whether the user asked for the Search to follow the Log File.
    void setAutoRefresh( bool autoRefresh );
    const SearchAutoRefresh& searchAutoRefresh() const;

    // The Log Lines a Search runs over, from startLine up to, not including,
    // endLine. A finished load makes it the whole Log File again.
    void setSearchRange( LineNumber startLine, LineNumber endLine );
    LineNumber searchStartLine() const;
    LineNumber searchEndLine() const;

    // Takes effect at the next Format Recognition.
    void setRecognitionPolicy( const RecognitionPolicy& policy );
    const std::shared_ptr<const LogFormatCatalog>& logFormatCatalog() const;
    // The Log Format recognized for the Log File, if any: one of the
    // Catalog's own, kept even when the Catalog is rebuilt, and forgotten
    // when the Log File is truncated.
    const std::shared_ptr<const LogFormatDefinition>& logFormat() const;
    // How many times Format Recognition was taken.
    int formatRecognitionCount() const;

Q_SIGNALS:
    // Loading has progressed, in percent.
    void loadingProgressed( int percent );
    // A load finished, whether successful or not, and was followed.
    void loadingFinished( const OpenLogFile::LoadFinished& load );
    // Log Lines were added to the Log File on disk; they are being loaded.
    // A failure checking the file is described, empty otherwise.
    void grew( const QString& failure );
    // The Log File was truncated on disk, or checking it failed and it is
    // taken as truncated: the Marks were cleared, an active Search dropped
    // and the Log Format forgotten. It is being loaded again.
    void truncated( const QString& failure );
    // The current Search's state changed: progress, completion, a failure.
    void searchUpdated( SearchSession::State state );

private:
    void handleLoadingFinished( LoadingStatus status, const QString& failure );
    void handleChangeOnDisk( const QString& fileName );
    void handleFileChanged( MonitoredFileStatus status, const QString& failure );
    // Starts the Search again with the pattern last requested, over the
    // Search range.
    void restartSearch();
    // Returns whether Format Recognition was taken.
    bool recognizeFormat();
    void followCurrentSearch();

    // Held for as long as the Log File may be watched: the destructor stops
    // watching it through this port before anything else goes.
    std::shared_ptr<FileWatchPort> fileWatch_;
    QString fileName_;
    // Whether the Log File was handed to the port to watch.
    bool watched_ = false;

    // Declared before the Searches built from it, so it outlives them.
    std::shared_ptr<LogData> logData_;
    std::shared_ptr<LogFilteredData> filteredData_;
    QMetaObject::Connection searchConnection_;

    SearchAutoRefresh autoRefresh_;
    // Whether a Search was requested since the last clearSearch() or reload,
    // valid or not.
    bool searchRequested_ = false;
    // A Search was requested before the first load finished, and runs once
    // it has.
    bool searchWaitsForLoad_ = false;
    // Whether a load of the Log File has finished, whatever its outcome.
    bool loadFinishedOnce_ = false;
    // The pattern last requested, which a restarted Search runs with.
    RegularExpressionPattern searchPattern_;
    LineNumber searchStartLine_;
    LineNumber searchEndLine_;

    bool firstLoadDone_ = false;
    logsquirl::vector<LineNumber> savedMarks_;

    RecognitionPolicy recognitionPolicy_;
    std::shared_ptr<const LogFormatCatalog> logFormatCatalog_;
    // Whether the next load to finish is to recognize the Log Format: the
    // first load, and the one after a reload or a truncation.
    bool formatRecognitionPending_ = true;
    int formatRecognitionCount_ = 0;
    std::shared_ptr<const LogFormatDefinition> logFormat_;
};

Q_DECLARE_METATYPE( OpenLogFile::LoadFinished )

#endif
