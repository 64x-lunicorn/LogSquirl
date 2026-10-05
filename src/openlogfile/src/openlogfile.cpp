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

#include <algorithm>
#include <mutex>
#include <utility>

namespace {

// The Encoding chosen by its MIB: only one this build knows (#552). The
// callers make sure of it; a MIB it does not know is no choice, which is how
// the log data and its index worker take such a default Encoding too.
std::optional<int> knownEncoding( std::optional<int> mib )
{
    const auto known = !mib || TextEncoding::forMib( *mib ) != nullptr;
    Q_ASSERT( known );
    return known ? mib : std::nullopt;
}

} // namespace

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
        chosenEncoding_ = knownEncoding( fileAccessPolicy.defaultEncodingMib );
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
    connect( logData_.get(), &LogData::fileUnchanged, this,
             [ this ] { continueStoppingToWatch( loadRule_.foundUnchanged() ); } );
    connect( logData_.get(), &LogData::decodingPolicyChanged, this,
             &OpenLogFile::decodingPolicyChanged );

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

bool OpenLogFile::hasFirstLoadFinished() const
{
    return loadRule_.hasLoadFinished();
}

void OpenLogFile::restoreMarks( const logsquirl::vector<LineNumber>& marks )
{
    loadRule_.restoreMarks( marks );
}

void OpenLogFile::addMark( LineNumber line )
{
    filteredData_->addMark( line );
}

void OpenLogFile::toggleMark( LineNumber line )
{
    filteredData_->toggleMark( line );
}

void OpenLogFile::clearMarks()
{
    filteredData_->clearMarks();
}

AbstractLogData::LineType OpenLogFile::lineType( LineNumber line ) const
{
    return filteredData_->lineTypeByLine( line );
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
        clearMarks();
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

LinesCount OpenLogFile::lineCount() const
{
    return logData_->getNbLine();
}

qint64 OpenLogFile::fileSize() const
{
    return logData_->getFileSize();
}

QDateTime OpenLogFile::lastModified() const
{
    return logData_->getLastModifiedDate();
}

void OpenLogFile::setIndexingPolicy( const IndexingPolicy& policy )
{
    logData_->setIndexingPolicy( policy );
}

void OpenLogFile::setSearchPolicy( const SearchPolicy& policy )
{
    // The log data hands it on to every Search built from it, the kept ones
    // included, which this object holds no list of.
    logData_->setSearchPolicy( policy );
    Q_EMIT searchPolicyChanged();
}

const SearchPolicy& OpenLogFile::searchPolicy() const
{
    return logData_->searchPolicy();
}

void OpenLogFile::setDecodingPolicy( const DecodingPolicy& policy )
{
    // Log Lines read from now on are decoded under it, and the log data tells
    // the Searches, and through decodingPolicyChanged() the users, to read
    // what they show again. Search results already found stay as they were.
    logData_->setDecodingPolicy( policy );
}

std::shared_ptr<LogFilteredData> OpenLogFile::startAnotherSearch()
{
    keepWaitingSearch();
    loadRule_.waitingSearchDropped();
    filteredData_->stop();
    filteredData_ = logData_->getNewFilteredData();
    followCurrentSearch();
    return filteredData_;
}

void OpenLogFile::makeSearchCurrent( std::shared_ptr<LogFilteredData> search )
{
    if ( search && search != filteredData_ ) {
        keepWaitingSearch();
    }
    loadRule_.waitingSearchDropped();
    filteredData_->stop();
    if ( search && search != filteredData_ ) {
        filteredData_ = std::move( search );
        followCurrentSearch();

        // A kept Search that waits for the first load waits on, current.
        const auto waiting = std::find_if(
            waitingKeptSearches_.begin(), waitingKeptSearches_.end(),
            [ this ]( const WaitingSearch& kept ) { return kept.search.lock() == filteredData_; } );
        if ( waiting != waitingKeptSearches_.end() ) {
            const auto pattern = waiting->pattern;
            waitingKeptSearches_.erase( waiting );
            requestSearch( pattern );
        }
    }
}

void OpenLogFile::requestKeptSearch( const std::shared_ptr<LogFilteredData>& search,
                                     const RegularExpressionPattern& pattern )
{
    if ( !search ) {
        return;
    }
    if ( search == filteredData_ ) {
        requestSearch( pattern );
        return;
    }

    // Requested again, it waits with the pattern requested last.
    std::erase_if( waitingKeptSearches_, [ &search ]( const WaitingSearch& kept ) {
        return kept.search.lock() == search;
    } );
    if ( !loadRule_.hasLoadFinished() ) {
        // Nothing to search yet: it runs over the Log Lines once they have
        // loaded, rather than over none now.
        waitingKeptSearches_.push_back( WaitingSearch{ search, pattern } );
        return;
    }
    search->request( pattern, searchLimits_.start, searchLimits_.end );
}

void OpenLogFile::keepWaitingSearch()
{
    if ( loadRule_.searchWaitsForLoad() ) {
        waitingKeptSearches_.push_back( WaitingSearch{ filteredData_, searchPattern_ } );
    }
}

SearchSessionState OpenLogFile::requestSearch( const RegularExpressionPattern& pattern )
{
    searchPattern_ = pattern;

    if ( loadRule_.searchRequested() ) {
        // Nothing to search yet: it runs over the Log Lines once they have
        // loaded, rather than over none now.
        SearchSessionState waiting;
        waiting.pattern = pattern;
        waiting.phase = SearchSessionPhase::Running;
        return waiting;
    }

    // The Search Session validates the pattern itself; an invalid one goes to
    // InvalidPattern synchronously, so the state is conclusive right away.
    filteredData_->request( pattern, searchLimits_.start, searchLimits_.end );
    auto state = filteredData_->searchState();

    if ( state.phase != SearchSessionPhase::InvalidPattern ) {
        autoRefresh_.startSearch();
    }
    else {
        autoRefresh_.resetState();
    }

    return state;
}

SearchSessionState OpenLogFile::searchState() const
{
    return filteredData_->searchState();
}

LinesCount OpenLogFile::matchCount() const
{
    return filteredData_->getNbMatches();
}

LinesCount OpenLogFile::displayedLineCount() const
{
    return filteredData_->getNbLine();
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
    searchLimits_ = LoadRule::SearchLimits{ startLine, endLine };
    tellSearchLimits();
}

void OpenLogFile::tellSearchLimits()
{
    if ( toldSearchLimits_ == searchLimits_ ) {
        return;
    }
    toldSearchLimits_ = searchLimits_;
    Q_EMIT searchLimitsChanged( searchLimits_.start, searchLimits_.end );
}

LineNumber OpenLogFile::searchStartLine() const
{
    return searchLimits_.start;
}

LineNumber OpenLogFile::searchEndLine() const
{
    return searchLimits_.end;
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
    chosenEncoding_ = knownEncoding( mib );
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
    // Only an Encoding this build knows is chosen (knownEncoding()), so this
    // is never null.
    if ( chosenEncoding_ ) {
        return TextEncoding::forMib( *chosenEncoding_ );
    }
    const auto* detected = logData_->getDetectedEncoding();
    return detected ? detected : TextEncoding::forLocale();
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
    logData_->setDisplayEncoding( *codec );
    return true;
}

void OpenLogFile::handleLoadingFinished( LoadingStatus status, const QString& failure )
{
    // Watched once a load has succeeded, and asked again after every one,
    // as the log data always did: the port ignores a file it already
    // watches.
    if ( status == LoadingStatus::Successful && fileWatch_ && loadRule_.isWatching() ) {
        fileWatch_->addFile( fileName_ );
        if ( !watched_ ) {
            watched_ = true;
            // What was written to it while the first load ran, before it was
            // watched, no watcher reports: it is checked for once, the way a
            // change the port reports is (#629).
            QMetaObject::invokeMethod(
                this, [ this ] { handleChangeOnDisk( fileName_ ); }, Qt::QueuedConnection );
        }
    }

    const auto lineCount = logData_->getNbLine();

    // Settled before any Search runs below, so it matches the Log Lines as
    // they read, and before the users hear of the load. The Encoding detected
    // is known only now.
    const auto encodingSettledAnew = settleEncoding();

    // The Log Lines the Search ran over, as it stands once the Encoding is
    // settled.
    const auto searched = filteredData_->searchState();
    auto decision
        = loadRule_.loadFinished( status, lineCount, autoRefresh_, searchLimits_,
                                  LoadRule::SearchLimits{ searched.startLine, searched.endLine } );

    LoadFinished load;
    load.status = status;
    load.failure = failure;
    load.fromStart = decision.fromStart;
    load.onlyAppended = decision.onlyAppended;

    // The Search Limits as the Load Rule settled them: every view shows them
    // before the Search runs over them.
    searchLimits_ = decision.searchLimits;
    tellSearchLimits();

    // The Search follows the Log Lines loaded: it continues over the ones
    // added within the Search Limits, or starts again over a Log File
    // truncated under it, within the Limits as they are now.
    switch ( decision.searchRefresh ) {
    case LoadRule::SearchRefresh::None:
        break;
    case LoadRule::SearchRefresh::Continue:
        // Same pattern and start, a larger end: the Search Session continues
        // the run rather than starting over, when it has Log Lines to run
        // over.
        if ( decision.continueOver ) {
            filteredData_->request( searched.pattern, decision.continueOver->start,
                                    decision.continueOver->end );
        }
        break;
    case LoadRule::SearchRefresh::Restart:
        restartSearch();
        load.searchRestarted = true;
        break;
    }

    for ( const auto& mark : decision.savedMarksToApply ) {
        addMark( mark );
    }

    if ( decision.runWaitingSearch ) {
        // The Search requested while the Log File loaded runs over the whole
        // of it now.
        requestSearch( searchPattern_ );
    }
    // So do the kept ones, beside it; a Log File that did not load has
    // nothing to search. Only the first load finds any waiting.
    for ( const auto& waiting : std::exchange( waitingKeptSearches_, {} ) ) {
        const auto search = waiting.search.lock();
        if ( search && status == LoadingStatus::Successful ) {
            search->request( waiting.pattern, searchLimits_.start, searchLimits_.end );
        }
    }

    if ( decision.recognizeFormat ) {
        load.formatRecognized = recognizeFormat();
    }

    Q_EMIT loadingFinished( load );

    if ( encodingSettledAnew ) {
        Q_EMIT encodingChanged();
    }

    // Whatever this load brought, the Log File may have grown during it: it
    // is checked again until a check finds nothing new.
    continueStoppingToWatch( decision.watching );
}

void OpenLogFile::stopWatching()
{
    if ( !loadRule_.isWatching() ) {
        return;
    }
    const auto step = loadRule_.stopWatching( !fileName_.isEmpty() );

    if ( fileWatch_ ) {
        disconnect( fileWatch_.get(), nullptr, this, nullptr );
        if ( watched_ ) {
            fileWatch_->removeFile( fileName_ );
            watched_ = false;
        }
    }

    continueStoppingToWatch( step );
}

void OpenLogFile::continueStoppingToWatch( LoadRule::WatchingStep step )
{
    switch ( step ) {
    case LoadRule::WatchingStep::None:
        break;
    case LoadRule::WatchingStep::CheckAgain:
        logData_->fileChangedOnDisk( fileName_ );
        break;
    case LoadRule::WatchingStep::StoppedStillChanging:
        LOG_WARNING << "The Log File " << fileName_ << " still changes, no longer checked";
        Q_EMIT watchingStopped();
        break;
    case LoadRule::WatchingStep::Stopped:
        Q_EMIT watchingStopped();
        break;
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
        clearMarks();
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
