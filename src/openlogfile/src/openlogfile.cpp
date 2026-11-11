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

#include "encodingdecision.h"
#include "filewatchport.h"
#include "formatrecognition.h"
#include "log.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logformatcatalog.h"
#include "logformatfieldreader.h"
#include "regularexpression.h"

#include "textencoding.h"

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

// The Encoding decideEncoding() shows, by its MIB: one chosen is one this
// build knows, one detected or the locale's one it decodes.
const TextEncoding* shownEncoding( int mib )
{
    const auto* encoding = TextEncoding::forMib( mib );
    return encoding != nullptr ? encoding : TextEncoding::forLocale();
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
    , searchSet_( SearchSet<LogFilteredData>::Hooks{
          .newSearch = [ this ]() -> std::shared_ptr<LogFilteredData> {
              return logData_->getNewFilteredData();
          },
          .preparePattern =
              [ this ]( RegularExpressionPattern pattern ) {
                  return withFieldReader( std::move( pattern ) );
              },
          .heldNoLonger = [ this ] { Q_EMIT searchesHeldNoLonger(); },
      } )
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
    return searchSet_.hasFirstLoadFinished();
}

void OpenLogFile::restoreMarks( const logsquirl::vector<LineNumber>& marks )
{
    loadRule_.restoreMarks( marks );
}

void OpenLogFile::addMark( LineNumber line )
{
    searchSet_.current()->addMark( line );
}

void OpenLogFile::toggleMark( LineNumber line )
{
    searchSet_.current()->toggleMark( line );
}

void OpenLogFile::clearMarks()
{
    searchSet_.current()->clearMarks();
}

AbstractLogData::LineType OpenLogFile::lineType( LineNumber line ) const
{
    return searchSet_.current()->lineTypeByLine( line );
}

QList<LineNumber> OpenLogFile::marks() const
{
    if ( loadRule_.isLoadingFromStart() ) {
        const auto& savedMarks = loadRule_.savedMarks();
        return QList<LineNumber>( savedMarks.begin(), savedMarks.end() );
    }
    return searchSet_.current()->getMarks();
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

    const auto decision = loadRule_.reload();
    if ( decision.dropSearch ) {
        // The cached results go once the Log File is indexed again, which
        // tells every Search that its Log Lines changed. It waits for the
        // first load no longer, is held no longer and is no longer followed.
        searchSet_.clearCurrent();
    }
    if ( decision.clearMarks ) {
        clearMarks();
    }

    logData_->reload();
}

void OpenLogFile::stopLoading()
{
    searchSet_.current()->stop();
    logData_->interruptLoading();
}

const std::shared_ptr<LogData>& OpenLogFile::logData() const
{
    return logData_;
}

const std::shared_ptr<LogFilteredData>& OpenLogFile::filteredData() const
{
    return searchSet_.current();
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
    // A Search made from now on starts with it; the Search Set hands it on
    // to every Search made before, the kept ones included.
    logData_->setSearchPolicy( policy );
    searchSet_.setSearchPolicy( policy );
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
    auto search = searchSet_.startAnother();
    followCurrentSearch();
    return search;
}

void OpenLogFile::makeSearchCurrent( std::shared_ptr<LogFilteredData> search )
{
    const auto another = search && search != searchSet_.current();
    searchSet_.makeCurrent( std::move( search ) );
    if ( another ) {
        followCurrentSearch();
    }
}

void OpenLogFile::requestKeptSearch( const std::shared_ptr<LogFilteredData>& search,
                                     const RegularExpressionPattern& pattern )
{
    searchSet_.requestKept( search, pattern, searchLimits_ );
}

void OpenLogFile::holdWaitingSearches()
{
    searchSet_.holdWaiting();
}

void OpenLogFile::releaseHeldSearches()
{
    searchSet_.releaseHeld( searchLimits_ );
}

bool OpenLogFile::isSearchHeld( const std::shared_ptr<LogFilteredData>& search ) const
{
    return searchSet_.isHeld( search );
}

bool OpenLogFile::holdsSearches() const
{
    return searchSet_.holdsAny();
}

SearchSessionState OpenLogFile::requestSearch( const RegularExpressionPattern& pattern )
{
    if ( searchSet_.requestCurrent( pattern, searchLimits_ ) ) {
        // Nothing to search yet: it runs over the Log Lines once they have
        // loaded, rather than over none now.
        SearchSessionState waiting;
        waiting.pattern = pattern;
        waiting.phase = SearchSessionPhase::Running;
        return waiting;
    }

    // The Search Session validates the pattern itself; an invalid one goes to
    // InvalidPattern synchronously, so the state is conclusive right away.
    return searchSet_.current()->searchState();
}

SearchSessionState OpenLogFile::searchState() const
{
    return searchSet_.current()->searchState();
}

LinesCount OpenLogFile::matchCount() const
{
    return searchSet_.current()->getNbMatches();
}

LinesCount OpenLogFile::displayedLineCount() const
{
    return searchSet_.current()->getNbLine();
}

void OpenLogFile::clearSearch()
{
    searchSet_.clearCurrent();
}

void OpenLogFile::stopSearch()
{
    searchSet_.stopCurrent();
}

void OpenLogFile::changeSearchExpression()
{
    searchSet_.changeCurrentExpression();
}

void OpenLogFile::setAutoRefresh( bool autoRefresh )
{
    searchSet_.setAutoRefresh( autoRefresh );
}

const SearchAutoRefresh& OpenLogFile::searchAutoRefresh() const
{
    return searchSet_.currentAutoRefresh();
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

RegularExpressionPattern OpenLogFile::withFieldReader( RegularExpressionPattern pattern )
{
    // Only a logical combination with Field Terms reads the Log Format: any
    // other pattern reads the same under every Log Format, keeps its cached
    // results and continues over a Log File that grows.
    if ( !pattern.isBoolean || !logFormat_ || !hasFieldTerms( pattern.pattern ) ) {
        pattern.fieldReader.reset();
        return pattern;
    }
    if ( !fieldReader_ || &fieldReader_->format() != logFormat_.get() ) {
        fieldReader_ = std::make_shared<const LogFormatFieldReader>( logFormat_ );
    }
    pattern.fieldReader = fieldReader_;
    return pattern;
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
    // The one decideEncoding() shows now, whatever was settled before; never
    // null, as only an Encoding this build knows is chosen (knownEncoding()).
    return shownEncoding( encodingDecision( std::nullopt ).shown );
}

EncodingDecision OpenLogFile::encodingDecision( std::optional<int> settled ) const
{
    const auto detected = encodingMib( logData_->getDetectedEncoding() );
    return decideEncoding( settled, chosenEncoding_, detected,
                           encodingMib( logData_->getForcedEncoding() ), detected );
}

bool OpenLogFile::settleEncoding()
{
    // Settled after every load: a Log File that only grew keeps what was
    // read in it.
    const auto decision = encodingDecision( settledEncoding_ );
    if ( decision.change == EncodingChange::Unchanged ) {
        return false;
    }
    settledEncoding_ = decision.shown;

    const auto* codec = shownEncoding( decision.shown );
    LOG_INFO << "Reading the Log File as " << codec->name().constData();

    // A load in progress in the old Encoding is of no use. The log data,
    // told the new one once that load is stopped, carries out the rest of the
    // decision: it loads the Log File again when the new Encoding splits it
    // into Log Lines differently, otherwise it tells every Search that the
    // Log Lines decode differently.
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
    const auto searched = searchSet_.current()->searchState();
    auto decision
        = loadRule_.loadFinished( status, lineCount, searchSet_.currentAutoRefresh(), searchLimits_,
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
            searchSet_.current()->request( withFieldReader( searched.pattern ),
                                           decision.continueOver->start,
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

    // The Searches requested while the Log File loaded run over the whole of
    // it now, the current one and the kept ones beside it; the ones held when
    // they waited for this load wait on, until they are released. A Log File
    // that did not load has nothing to search: they are dropped. Only the
    // first load finds any waiting.
    searchSet_.loadFinished( status == LoadingStatus::Successful, searchLimits_ );

    if ( decision.recognizeFormat ) {
        const auto previousFormat = logFormat_;
        load.formatRecognized = recognizeFormat();
        // Whether it is taken again as the Log File grows the rule decides
        // from what was found (#794).
        loadRule_.formatRecognitionTaken( logFormat_ != nullptr, lineCount );
        // A logical combination reads its Field Terms through the Log
        // Format (#798): the Search requested above read them through the
        // one before, so it is requested again under the one recognized now,
        // and its cached results are not those of the other Log Format.
        const auto& pattern = searchSet_.currentPattern();
        if ( logFormat_ != previousFormat && pattern.isBoolean
             && hasFieldTerms( pattern.pattern ) ) {
            searchSet_.requestCurrentAgain( searchLimits_ );
        }
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
    // The current Search's results no longer describe a truncated Log File;
    // whether a Search continues or starts again stays with its own
    // auto-refresh, which every Search requested hears of, kept or current.
    // Their cached results go once the Log File is indexed again, which tells
    // every Search that its Log Lines changed.
    if ( decision.report == LoadRule::Change::Truncated ) {
        searchSet_.truncateFile();
    }
    if ( decision.forgetLogFormat ) {
        logFormat_.reset();
        fieldReader_.reset();
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

    searchSet_.restartCurrent( searchLimits_ );
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
    searchConnection_ = connect( searchSet_.current().get(), &LogFilteredData::searchStateChanged,
                                 this, &OpenLogFile::searchUpdated );
}
