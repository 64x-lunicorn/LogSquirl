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

#include "textencoding.h"

#include <mutex>
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
    if ( fileAccessPolicy.defaultEncodingMib >= 0 ) {
        chosenEncoding_ = fileAccessPolicy.defaultEncodingMib;
    }

    // The log data registers the types it signals with itself; this is the
    // one type only the Open Log File sends (#394).
    static std::once_flag registered;
    std::call_once( registered, [] {
        qRegisterMetaType<OpenLogFile::LoadFinished>( "OpenLogFile::LoadFinished" );
    } );

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
    loadRule_.restoreMarks( marks );
}

QList<LineNumber> OpenLogFile::marks() const
{
    if ( loadRule_.isLoadingFromStart() ) {
        const auto& savedMarks = loadRule_.savedMarks();
        return QList<LineNumber>( savedMarks.begin(), savedMarks.end() );
    }
    return filteredData_->getMarks();
}

void OpenLogFile::reload()
{
    if ( fileName_.isEmpty() ) {
        // No Log File is attached yet: this is a restored tab still waiting
        // for its turn (#300). There is nothing to load again, and nothing
        // here may be dropped -- the Marks saved with the Session are still
        // to be applied -- so reloading it means loading it, which whoever
        // holds the load queue does (#332). Its first load brings everything
        // a reload would: the Log Format is recognized, the saved Search runs.
        Q_EMIT loadRequested();
        return;
    }

    autoRefresh_.resetState();

    const auto decision = loadRule_.reload();
    if ( decision.dropSearch ) {
        // The cached results go once the Log File is indexed again, which
        // tells every Search that its Log Lines changed.
        filteredData_->request();
    }
    if ( decision.clearMarks ) {
        filteredData_->clearMarks();
    }

    logData_->reload();
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
    loadRule_.waitingSearchDropped();
    filteredData_->stop();
    filteredData_ = logData_->getNewFilteredData();
    followCurrentSearch();
    return filteredData_;
}

void OpenLogFile::makeSearchCurrent( std::shared_ptr<LogFilteredData> search )
{
    loadRule_.waitingSearchDropped();
    filteredData_->stop();
    if ( search && search != filteredData_ ) {
        filteredData_ = std::move( search );
        followCurrentSearch();
    }
}

SearchSession::State OpenLogFile::requestSearch( const RegularExpressionPattern& pattern )
{
    searchPattern_ = pattern;

    if ( loadRule_.searchRequested() ) {
        // Nothing to search yet: it runs over the Log Lines once they have
        // loaded, rather than over none now.
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
    loadRule_.searchCleared();
    filteredData_->request();
    autoRefresh_.resetState();
}

void OpenLogFile::stopSearch()
{
    loadRule_.waitingSearchDropped();
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

void OpenLogFile::setEncoding( std::optional<int> mib )
{
    chosenEncoding_ = mib;
    if ( settleEncoding() ) {
        Q_EMIT encodingChanged();
    }
}

std::optional<int> OpenLogFile::chosenEncoding() const
{
    return chosenEncoding_;
}

const TextEncoding* OpenLogFile::encoding() const
{
    const TextEncoding* codec = chosenEncoding_ ? TextEncoding::forMib( *chosenEncoding_ )
                                                : logData_->getDetectedEncoding();
    return codec ? codec : TextEncoding::forLocale();
}

bool OpenLogFile::settleEncoding()
{
    const auto* codec = encoding();

    // Settled after every load: a Log File that only grew keeps what was
    // read in it.
    if ( settledEncoding_ == codec->mibEnum() ) {
        return false;
    }
    settledEncoding_ = codec->mibEnum();

    LOG_INFO << "Reading the Log File as " << codec->name().constData();

    // The log data loads the Log File again when the new Encoding splits it
    // into Log Lines differently; a load in progress in the old one is of
    // no use. Otherwise the Log Lines only decode differently, which it tells
    // every Search.
    logData_->interruptLoading();
    logData_->setDisplayEncoding( codec->name().constData() );
    return true;
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

    const auto lineCount = logData_->getNbLine();
    const auto nbLines = LineNumber( lineCount.get() );
    auto decision = loadRule_.loadFinished( status, lineCount, autoRefresh_ );

    LoadFinished load;
    load.status = status;
    load.failure = failure;
    load.fromStart = decision.fromStart;
    load.onlyAppended = decision.onlyAppended;

    // Settled before any Search runs below, so it matches the Log Lines as
    // they read, and before the users hear of the load. The Encoding detected
    // is known only now.
    const auto encodingSettledAnew = settleEncoding();

    // The Search follows the Log Lines loaded: it continues over the ones
    // added, or starts again over a Log File truncated under it.
    switch ( decision.searchRefresh ) {
    case LoadRule::SearchRefresh::None:
        break;
    case LoadRule::SearchRefresh::Continue:
        searchEndLine_ = nbLines;
        // Same pattern and start, a larger end: the Search Session continues
        // the run rather than starting over.
        filteredData_->request( filteredData_->searchState().pattern, searchStartLine_,
                                searchEndLine_ );
        break;
    case LoadRule::SearchRefresh::Restart:
        searchEndLine_ = nbLines;
        restartSearch();
        load.searchRestarted = true;
        break;
    }

    // A finished load makes the Search Limits the whole Log File again.
    searchStartLine_ = 0_lnum;
    searchEndLine_ = nbLines;

    for ( const auto& mark : decision.savedMarksToApply ) {
        filteredData_->addMark( mark );
    }

    if ( decision.runWaitingSearch ) {
        // The Search requested while the Log File loaded runs over the whole
        // of it now.
        requestSearch( searchPattern_ );
    }

    if ( decision.recognizeFormat ) {
        load.formatRecognized = recognizeFormat();
    }

    Q_EMIT loadingFinished( load );

    if ( encodingSettledAnew ) {
        Q_EMIT encodingChanged();
    }
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
    const auto decision = loadRule_.changedOnDisk( status );

    if ( decision.clearMarks ) {
        filteredData_->clearMarks();
    }
    if ( decision.dropSearch ) {
        // The Search's results no longer describe the Log File; whether it
        // continues or starts again stays with the auto-refresh. Its cached
        // results go once the Log File is indexed again, which tells every
        // Search that its Log Lines changed.
        filteredData_->request();
        autoRefresh_.truncateFile();
    }
    if ( decision.forgetLogFormat ) {
        logFormat_.reset();
    }

    switch ( decision.report ) {
    case LoadRule::Change::Grew:
        Q_EMIT grew( failure );
        break;
    case LoadRule::Change::Truncated:
        Q_EMIT truncated( failure );
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
