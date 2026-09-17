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

#include "openlogfile.h"

#include "filewatchport.h"
#include "formatrecognition.h"
#include "log.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"

#include <utility>

OpenLogFile::OpenLogFile( const IndexingPolicy& indexingPolicy, const SearchPolicy& searchPolicy,
                          const FileAccessPolicy& fileAccessPolicy,
                          const DecodingPolicy& decodingPolicy,
                          const RecognitionPolicy& recognitionPolicy,
                          std::shared_ptr<const LogFormatCatalog> logFormatCatalog,
                          std::shared_ptr<FileWatchPort> fileWatch, QObject* parent )
    : QObject( parent )
    , fileWatch_( std::move( fileWatch ) )
    , logData_( std::make_shared<LogData>( indexingPolicy, searchPolicy, fileAccessPolicy,
                                           decodingPolicy ) )
    , filteredData_( logData_->getNewFilteredData() )
    , recognitionPolicy_( recognitionPolicy )
    , logFormatCatalog_( std::move( logFormatCatalog ) )
{
    qRegisterMetaType<OpenLogFile::LoadFinished>( "OpenLogFile::LoadFinished" );

    connect( logData_.get(), &LogData::loadingProgressed, this, &OpenLogFile::loadingProgressed );
    connect( logData_.get(), &LogData::loadingFinished, this, &OpenLogFile::handleLoadingFinished );
    connect( logData_.get(), &LogData::fileChanged, this, &OpenLogFile::handleFileChanged );

    if ( fileWatch_ ) {
        // Queued, as the log data always heard of changes: a change is
        // checked from the event loop, never from inside whatever made the
        // port report it. Bound to this object, so a change still on its way
        // when it is destroyed is dropped with it.
        connect( fileWatch_.get(), &FileWatchPort::fileChanged, this,
                 &OpenLogFile::handleChangeOnDisk, Qt::QueuedConnection );
    }

    followCurrentSearch();
}

OpenLogFile::~OpenLogFile()
{
    disconnect( searchConnection_ );

    if ( fileWatch_ ) {
        // Nothing more is heard of before the log data goes, and the Log File
        // is no longer watched on its behalf.
        disconnect( fileWatch_.get(), nullptr, this, nullptr );
        if ( watched_ ) {
            fileWatch_->removeFile( fileName_ );
        }
    }
}

void OpenLogFile::open( const QString& fileName )
{
    logData_->attachFile( fileName );
    fileName_ = fileName;
}

void OpenLogFile::restoreMarks( const logsquirl::vector<LineNumber>& marks )
{
    savedMarks_.insert( savedMarks_.end(), marks.begin(), marks.end() );
}

void OpenLogFile::reload()
{
    autoRefresh_.resetState();
    searchRequested_ = false;
    searchWaitsForLoad_ = false;

    constexpr auto DropCache = true;
    filteredData_->request( DropCache );
    filteredData_->clearMarks();

    // A reload recognizes the Log Format again, so an edited user Log Format
    // is picked up by reloading.
    formatRecognitionPending_ = true;

    logData_->reload();

    // A reload is loaded from its start, like the first load.
    firstLoadDone_ = false;
}

void OpenLogFile::stopLoading()
{
    filteredData_->stop();
    logData_->interruptLoading();
}

const std::shared_ptr<LogData>& OpenLogFile::logData() const
{
    return logData_;
}

const std::shared_ptr<LogFilteredData>& OpenLogFile::filteredData() const
{
    return filteredData_;
}

std::shared_ptr<LogFilteredData> OpenLogFile::startAnotherSearch()
{
    searchWaitsForLoad_ = false;
    filteredData_->stop();
    filteredData_ = logData_->getNewFilteredData();
    followCurrentSearch();
    return filteredData_;
}

void OpenLogFile::makeSearchCurrent( std::shared_ptr<LogFilteredData> search )
{
    searchWaitsForLoad_ = false;
    filteredData_->stop();
    if ( search && search != filteredData_ ) {
        filteredData_ = std::move( search );
        followCurrentSearch();
    }
}

SearchSession::State OpenLogFile::requestSearch( const RegularExpressionPattern& pattern )
{
    searchRequested_ = true;
    searchPattern_ = pattern;

    if ( !loadFinishedOnce_ ) {
        // Nothing to search yet: it runs over the Log Lines once they have
        // loaded, rather than over none now.
        searchWaitsForLoad_ = true;
        SearchSession::State waiting;
        waiting.pattern = pattern;
        waiting.phase = SearchSession::Phase::Running;
        return waiting;
    }

    // The Search Session validates the pattern itself; an invalid one goes to
    // InvalidPattern synchronously, so the state is conclusive right away.
    filteredData_->request( pattern, searchStartLine_, searchEndLine_ );
    auto state = filteredData_->searchState();

    if ( state.phase != SearchSession::Phase::InvalidPattern ) {
        autoRefresh_.startSearch();
    }
    else {
        autoRefresh_.resetState();
    }

    return state;
}

void OpenLogFile::clearSearch()
{
    searchRequested_ = false;
    searchWaitsForLoad_ = false;
    filteredData_->request();
    autoRefresh_.resetState();
}

void OpenLogFile::stopSearch()
{
    searchWaitsForLoad_ = false;
    filteredData_->stop();
    autoRefresh_.stopSearch();
}

void OpenLogFile::changeSearchExpression()
{
    autoRefresh_.changeExpression();
}

void OpenLogFile::setAutoRefresh( bool autoRefresh )
{
    autoRefresh_.setAutoRefresh( autoRefresh );
}

const SearchAutoRefresh& OpenLogFile::searchAutoRefresh() const
{
    return autoRefresh_;
}

void OpenLogFile::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    searchStartLine_ = startLine;
    searchEndLine_ = endLine;
}

LineNumber OpenLogFile::searchStartLine() const
{
    return searchStartLine_;
}

LineNumber OpenLogFile::searchEndLine() const
{
    return searchEndLine_;
}

void OpenLogFile::setRecognitionPolicy( const RecognitionPolicy& policy )
{
    recognitionPolicy_ = policy;
}

const std::shared_ptr<const LogFormatCatalog>& OpenLogFile::logFormatCatalog() const
{
    return logFormatCatalog_;
}

const std::shared_ptr<const LogFormatDefinition>& OpenLogFile::logFormat() const
{
    return logFormat_;
}

int OpenLogFile::formatRecognitionCount() const
{
    return formatRecognitionCount_;
}

void OpenLogFile::handleLoadingFinished( LoadingStatus status, const QString& failure )
{
    // Watched once a load has succeeded, and asked again after every one,
    // as the log data always did: the port ignores a file it already
    // watches.
    if ( status == LoadingStatus::Successful && fileWatch_ ) {
        fileWatch_->addFile( fileName_ );
        watched_ = true;
    }

    LoadFinished load;
    load.status = status;
    load.failure = failure;
    load.fromStart = !firstLoadDone_;

    const auto nbLines = LineNumber( logData_->getNbLine().get() );

    // The Search follows the Log Lines loaded: it continues over the ones
    // added, or starts again over a Log File truncated under it.
    if ( autoRefresh_.isAutoRefreshAllowed() ) {
        searchEndLine_ = nbLines;
        if ( autoRefresh_.isFileTruncated() ) {
            restartSearch();
            load.searchRestarted = true;
        }
        else {
            // Same pattern and start, a larger end: the Search Session
            // continues the run rather than starting over.
            filteredData_->request( filteredData_->searchState().pattern, searchStartLine_,
                                    searchEndLine_ );
        }
    }

    // A finished load makes the Search Limits the whole Log File again.
    searchStartLine_ = 0_lnum;
    searchEndLine_ = nbLines;

    if ( !firstLoadDone_ ) {
        firstLoadDone_ = true;
        for ( const auto& mark : savedMarks_ ) {
            filteredData_->addMark( mark );
        }
        // Applied once: a reload clears the Marks, and they stay cleared.
        savedMarks_.clear();
    }

    loadFinishedOnce_ = true;
    if ( std::exchange( searchWaitsForLoad_, false ) ) {
        // The Search requested while the Log File loaded runs over the whole
        // of it now; a Log File that did not load has nothing to search.
        if ( status == LoadingStatus::Successful ) {
            requestSearch( searchPattern_ );
        }
        else {
            searchRequested_ = false;
        }
    }

    // A Log File with no Log Lines yet has nothing to recognize from, so it
    // waits for a load that brings some.
    if ( formatRecognitionPending_ && nbLines.get() > 0 ) {
        formatRecognitionPending_ = false;
        load.formatRecognized = recognizeFormat();
    }

    Q_EMIT loadingFinished( load );
}

void OpenLogFile::handleChangeOnDisk( const QString& fileName )
{
    // Every change the port reports reaches every Open Log File built with
    // it: the log data ignores a change to another file, unless its own Log
    // File was replaced under its name. Nothing is checked before it is opened.
    if ( !fileName_.isEmpty() ) {
        logData_->fileChangedOnDisk( fileName );
    }
}

void OpenLogFile::handleFileChanged( MonitoredFileStatus status, const QString& failure )
{
    switch ( status ) {
    case MonitoredFileStatus::Truncated:
        // Marks do not survive a truncation.
        filteredData_->clearMarks();
        if ( searchRequested_ ) {
            // The Search's results and its cache no longer describe the Log
            // File.
            constexpr auto DropCache = true;
            filteredData_->request( DropCache );
            autoRefresh_.truncateFile();
        }

        // Forgotten, so it is recognized again once the truncated Log File
        // has loaded.
        logFormat_.reset();
        formatRecognitionPending_ = true;

        Q_EMIT truncated( failure );
        break;
    case MonitoredFileStatus::DataAdded:
    case MonitoredFileStatus::Unchanged:
        Q_EMIT grew( failure );
        break;
    }
}

void OpenLogFile::restartSearch()
{
    LOG_INFO << "restarting the Search over the truncated Log File";

    filteredData_->request();
    requestSearch( searchPattern_ );
}

bool OpenLogFile::recognizeFormat()
{
    if ( !logFormatCatalog_ ) {
        return false;
    }

    ++formatRecognitionCount_;
    logFormat_ = FormatRecognition::recognize( *logData_, recognitionPolicy_, *logFormatCatalog_ );
    if ( logFormat_ ) {
        LOG_INFO << "Recognized log format: " << logFormat_->name().toStdString();
    }
    return true;
}

void OpenLogFile::followCurrentSearch()
{
    disconnect( searchConnection_ );
    searchConnection_ = connect( filteredData_.get(), &LogFilteredData::searchStateChanged, this,
                                 &OpenLogFile::searchUpdated );
}
