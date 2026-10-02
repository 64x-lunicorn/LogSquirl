/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014, 2015 Nicolas Bonnefon and other contributors
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

// This file implements the CrawlerWidget class.
// It is responsible for creating and managing the two views and all
// the UI elements.  It implements the connection between the UI elements.
// It also interacts with the sets of data (full and filtered).

#include "abstractlogview.h"
#include "active_screen.h"
#include "linetypes.h"
#include "log.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <utility>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTimer>
#include <qglobal.h>
#include <qobject.h>
#include <string>
#include <type_traits>

#include "regularexpression.h"
#include "valuecount.h"

#include "crawlerwidget.h"

#include "configuration.h"
#include "dispatch_to.h"
#include "fontutils.h"
#include "highlightersmenu.h"
#include "infoline.h"
#include "issuereporter.h"
#include "logformatcatalog.h"
#include "logformatdefinition.h"
#include "logtableview.h"
#include "overviewwidget.h"
#include "quickfindpattern.h"
#include "regexlabsource.h"
#include "regexlabwindow.h"
#include "savedsearches.h"
#include "shortcuts.h"
#include "theme.h"
#include "viewstatecodec.h"

namespace {

// The desktop application's answer to a failure the engine reports for a
// Log File or a Search: offer the user to report it as an issue. Opened once
// the handler that received the failure has returned, not inside it.
void offerIssueReport( const QString& failure )
{
    dispatchToMainThread( [ failure ]() {
        IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, failure );
    } );
}

// A combo box that gives way when its row runs out of width: it keeps the
// width of its longest item while there is room, and shrinks down to a few
// characters of its font before the Search line has to (#261).
class YieldingComboBox : public QComboBox {
public:
    QSize minimumSizeHint() const override
    {
        constexpr int MinimumCharacters = 5;

        QStyleOptionComboBox option;
        initStyleOption( &option );
        const auto contents
            = QSize( fontMetrics().horizontalAdvance( QLatin1Char( 'X' ) ) * MinimumCharacters,
                     fontMetrics().height() );
        const auto minimum
            = style()->sizeFromContents( QStyle::CT_ComboBox, &option, contents, this );
        return { std::min( minimum.width(), QComboBox::minimumSizeHint().width() ),
                 QComboBox::minimumSizeHint().height() };
    }
};

} // namespace

// Constructor only does trivial construction. The real work is done once
// the data is attached.
CrawlerWidget::CrawlerWidget( const ViewBuild& build, QWidget* parent )
    : QSplitter( parent )
    , keptSearches_( build.openLogFile, viewSet_,
                     [ this ]( LogFilteredData* search ) { return buildFilteredView( search ); } )
{
    openLogFile_ = build.openLogFile;
    quickFindPattern_ = build.quickFindPattern;
    savedSearches_ = build.savedSearches;
    changeReport_ = build.changeReport;

    // Every view built below starts with these; a view built later, too (#242).
    viewSet_.setDecorationPolicy( build.policies.decoration );
    viewSet_.setPresentationPolicy( build.policies.presentation );
    quickFindPolicy_ = build.policies.quickFind;
    applyWatchPolicy( build.policies.watch );

    setup();

    if ( !build.viewContext.isEmpty() ) {
        restoreViewContext( build.viewContext );
    }
}

CrawlerWidget::~CrawlerWidget()
{
    // A Filtered View reads its Search until it is gone, so the tabs go
    // before the Kept Searches let the Searches go, and ask nothing of them
    // as they go.
    if ( tabbedFilteredView_ != nullptr ) {
        disconnect( tabbedFilteredView_, nullptr, this, nullptr );
        delete tabbedFilteredView_;
    }
}

QString CrawlerWidget::getSelectedText() const
{
    if ( currentFilteredView()->hasFocus() )
        return currentFilteredView()->getSelectedText();
    else
        return presentation_->selectedText();
}

bool CrawlerWidget::isPartialSelection() const
{
    if ( filteredViewIsActive() )
        return currentFilteredView()->isPartialSelection();
    else if ( shownPresentation() == logTableView_ )
        return logTableView_->selection().hasInCellSelection();
    else
        return logMainView_->isPartialSelection();
}

SearchLine::Flags CrawlerWidget::searchFlags() const
{
    return searchLine_->flags();
}

logsquirl::vector<LineNumber> CrawlerWidget::selectedLogLines( LinesCount count ) const
{
    // Only the first count are looked at, however many are selected (#663).
    if ( filteredViewWasLastFocused() ) {
        return currentFilteredView()->selectedLogLines( count );
    }
    if ( shownPresentation() == logTableView_ ) {
        return logTableView_->selectedLogLines( count );
    }
    return logMainView_->selectedLogLines( count );
}

logsquirl::vector<LineNumber> CrawlerWidget::logLinesAroundCurrentLine( LinesCount count ) const
{
    // The Presentation not shown follows the one shown, so the text view
    // stands where the Table View does.
    const AbstractLogView* view = filteredViewWasLastFocused()
                                      ? static_cast<const AbstractLogView*>( currentFilteredView() )
                                      : logMainView_;
    return view->logLinesAroundViewPosition( count );
}

std::function<logsquirl::vector<QString>( const logsquirl::vector<LineNumber>& )>
CrawlerWidget::logLineTextReader() const
{
    return [ logData = std::shared_ptr<const LogData>( openLogFile_->logData() ) ](
               const logsquirl::vector<LineNumber>& lines ) {
        // The Log File stays open while its lines are read.
        struct AttachedReader {
            explicit AttachedReader( const LogData& data )
                : data_( data )
            {
                data_.attachReader();
            }
            ~AttachedReader()
            {
                data_.detachReader();
            }
            AttachedReader( const AttachedReader& ) = delete;
            AttachedReader& operator=( const AttachedReader& ) = delete;

            const LogData& data_;
        };
        const AttachedReader attached( *logData );
        return logData->getLinesSparse( lines );
    };
}

bool CrawlerWidget::filteredViewWasLastFocused() const
{
    const auto* focused = window()->focusWidget();
    const auto* filtered = currentFilteredView();
    if ( focused == nullptr ) {
        return qfSavedFocus_ == filtered;
    }
    return focused == filtered || filtered->isAncestorOf( focused );
}

void CrawlerWidget::selectAll()
{
    if ( auto* view = qobject_cast<AbstractLogView*>( activeView() ) )
        view->selectAll();
    else
        logTableView_->selectAll();
}

std::optional<int> CrawlerWidget::encodingMib() const
{
    return openLogFile_->chosenEncoding();
}

bool CrawlerWidget::isFollowEnabled() const
{
    return viewSet_.follows();
}

void CrawlerWidget::followSet( bool follow )
{
    const bool before = viewSet_.follows();
    viewSet_.setFollow( follow );
    if ( viewSet_.follows() != before || viewSet_.follows() != follow ) {
        Q_EMIT followModeChanged( viewSet_.follows() );
    }
}

void CrawlerWidget::endFollowing()
{
    // Followed until the last Log Lines are loaded, so that the views show
    // them.
    connect(
        openLogFile_.get(), &OpenLogFile::watchingStopped, this,
        [ this ] {
            followEnded_ = true;
            applyWatchPolicy( watchPolicy_ );
        },
        Qt::SingleShotConnection );
    openLogFile_->stopWatching();
}

bool CrawlerWidget::isTextWrapEnabled() const
{
    return logMainView_->isTextWrapEnabled();
}

bool CrawlerWidget::isValueNamesShownSet() const
{
    return logMainView_->isValueNamesShownSet();
}

QString CrawlerWidget::getSelectedTextAsShown() const
{
    if ( currentFilteredView()->hasFocus() ) {
        return currentFilteredView()->getSelectedTextAsShown();
    }
    // The Table View shows no Value Names.
    if ( shownPresentation() == logMainView_ ) {
        return logMainView_->getSelectedTextAsShown();
    }
    return presentation_->selectedText();
}

QString CrawlerWidget::encodingText() const
{
    return encodingText_;
}

// Return a pointer to the view in which we should do the QuickFind
SearchableWidgetInterface* CrawlerWidget::doGetActiveSearchable() const
{
    if ( filteredViewIsActive() )
        return currentFilteredView();
    else if ( shownPresentation() == logTableView_ )
        return logTableView_;
    else
        return logMainView_;
}

// Return all the searchable widgets (views): both Presentations, shown or
// not, so that either can hand the QuickFind bar a pattern once it is shown.
std::vector<QObject*> CrawlerWidget::doGetAllSearchables() const
{
    std::vector<QObject*> searchables = { logMainView_, logTableView_, currentFilteredView() };

    return searchables;
}

CrawlerWidget::State CrawlerWidget::state() const
{
    State state;
    state.loadStatus = lastLoadStatus_;
    state.loadFailure = lastLoadFailure_;
    state.loadingProgress = loadingProgress_;
    state.selectedLine = currentLineNumber_;
    state.follows = isFollowEnabled();
    state.textWrap = isTextWrapEnabled();
    state.valueNamesShown = isValueNamesShownSet();
    state.encodingMib = encodingMib();
    state.goToTimestampUnavailable = goToTimestampUnavailableReason();
    state.searchLimitsByTimeUnavailable = searchLimitsByTimeUnavailableReason();
    state.quickFindSearchable = doGetActiveSearchable();
    return state;
}

//
// Public Q_SLOTS:
//

void CrawlerWidget::stopLoading()
{
    openLogFile_->stopLoading();
}

void CrawlerWidget::reload()
{
    // The Open Log File drops the Search and the Marks, and recognizes the
    // Log Format again once it has loaded. A reload is loaded from its start
    // like the first load, so the "new data" icon is not triggered.
    timeNavigation_.reloaded();
    // Loading until the reload has finished, also to a window that shows this
    // tab later (#540).
    lastLoadStatus_.reset();
    loadingProgress_ = 0;
    openLogFile_->reload();
    viewSet_.refreshMatchesAndMarks( openLogFile_->lineCount() );
    printSearchInfoMessage();
}

void CrawlerWidget::setEncoding( std::optional<int> mib )
{
    openLogFile_->setEncoding( mib );
    updateEncodingText();

    update();
}

void CrawlerWidget::focusSearchEdit()
{
    searchLine_->setFocus( Qt::ShortcutFocusReason );
}

void CrawlerWidget::goToLine()
{
    bool isLineSelected = true;
    auto newLine = QInputDialog::getText( this, "Jump to line", "Line number" )
                       .toULongLong( &isLineSelected );

    if ( isLineSelected ) {
        if ( newLine == 0 ) {
            newLine = 1;
        }

        const auto selectedLine
            = LineNumber( static_cast<LineNumber::UnderlyingType>( newLine - 1 ) );
        currentFilteredView()->trySelectLine( selectedLine );

        const auto nbLines = openLogFile_->lineCount();
        if ( nbLines.get() > 0 ) {
            presentation_->showLogLine(
                std::min( selectedLine, LineNumber( nbLines.get() ) - 1_lcount ) );
        }
    }
}

CrawlerWidget::SelectedLogLineTexts CrawlerWidget::selectedLogLineTexts( LinesCount count,
                                                                         qint64 maxBytes ) const
{
    SelectedLogLineTexts texts;
    const auto logLines = selectedLogLines( count );
    if ( logLines.empty() ) {
        return texts;
    }

    const auto& logData = openLogFile_->logData();
    logData->attachReader();
    qint64 utf8Bytes = 0;
    std::size_t read = 0;
    logData->readLinePrefixes( logLines, maxBytes, [ & ]( QString&& text, bool cut ) {
        utf8Bytes += ( texts.lines.isEmpty() ? 0 : 1 ) + text.toUtf8().size();
        texts.lines.append( std::move( text ) );
        ++read;
        texts.lastCut = cut;
        if ( cut || utf8Bytes > maxBytes ) {
            texts.more = read < logLines.size();
            return false;
        }
        return true;
    } );
    logData->detachReader();
    return texts;
}

bool CrawlerWidget::goToLogLine( LineNumber line )
{
    if ( line.get() >= openLogFile_->lineCount().get() ) {
        return false;
    }
    currentFilteredView()->trySelectLine( line );
    presentation_->showLogLine( line );
    return true;
}

QString CrawlerWidget::goToTimestampUnavailableReason() const
{
    return timeNavigation_.goToTimestampUnavailableReason();
}

QString CrawlerWidget::searchLimitsByTimeUnavailableReason() const
{
    return timeNavigation_.searchLimitsByTimeUnavailableReason();
}

TimeNavigation::Source CrawlerWidget::timeNavigationSource() const
{
    if ( !openLogFile_ ) {
        return { nullptr, recognizedFormat_, {} };
    }
    return { openLogFile_->logData(), recognizedFormat_, openLogFile_->lastModified().date() };
}

TimeNavigation::Sink CrawlerWidget::timeNavigationSink()
{
    TimeNavigation::Sink sink;
    sink.showLogLine = [ this ]( LineNumber line ) {
        currentFilteredView()->trySelectLine( line );
        presentation_->showLogLine( line );
    };
    // From here on ordinary Search Limits, lines like any others.
    sink.setSearchLimits
        = [ this ]( LineNumber start, LineNumber end ) { setSearchLimits( start, end ); };
    sink.statusMessage = [ this ]( const QString& text ) { Q_EMIT statusMessage( text ); };
    sink.searchWindowChosen = []( int minutes ) {
        auto& config = Configuration::get();
        config.setSearchWindowMinutes( minutes );
        config.save();
    };
    return sink;
}

void CrawlerWidget::goToTimestamp()
{
    timeNavigation_.goToTimestamp( currentLineNumber_ );
}

void CrawlerWidget::setSearchLimitsToTimeRange()
{
    timeNavigation_.setSearchLimitsToTimeRange( currentLineNumber_ );
}

void CrawlerWidget::setSearchLimitsAroundCurrentLine()
{
    timeNavigation_.setSearchLimitsAroundLine(
        currentLineNumber_, [] { return Configuration::get().searchWindowMinutes(); } );
}

//
// Protected functions
//
void CrawlerWidget::doApplyChange( const ViewChange& change )
{
    // Each reaches every view of this Log File, the Filtered Views of kept
    // Searches included, whether or not its tab is the active one, without
    // the Log File being opened again; a view built later starts with it.
    if ( change.decoration ) {
        viewSet_.setDecorationPolicy( *change.decoration );
    }
    if ( change.presentation ) {
        viewSet_.setPresentationPolicy( *change.presentation );
    }
    if ( change.quickFind ) {
        // The QuickFind bar and the mux that dispatches to this Log File
        // belong to the window, which takes this Policy from its session;
        // the Search line is handed it from here, which says whether an
        // edited pattern runs the Search at once.
        quickFindPolicy_ = *change.quickFind;
        searchLine_->setQuickFindPolicy( *change.quickFind );
    }
    if ( change.watch ) {
        applyWatchPolicy( *change.watch );
    }

    // After the Policies, which what is read again may depend on.
    if ( change.rereadSettingsWithoutPolicy ) {
        rereadSettingsWithoutPolicy();
    }
    else if ( change.font ) {
        viewSet_.setFont( configuredFont() );
    }
    if ( change.highlighterSets ) {
        applyHighlighterSetChange();
    }
    if ( change.valueNames ) {
        viewSet_.applyValueNamesChange();
    }
}

void CrawlerWidget::applyWatchPolicy( const WatchPolicy& policy )
{
    watchPolicy_ = policy;

    // Takes following away from every view of this Log File, or gives it
    // back, without the Log File being opened again.
    const bool followed = viewSet_.follows();
    viewSet_.setFollowAllowed( policy.anyWatchEnabled() && !followEnded_ );
    if ( viewSet_.follows() != followed ) {
        Q_EMIT followModeChanged( viewSet_.follows() );
    }
}

const DecorationPolicy& CrawlerWidget::decorationPolicy() const
{
    return viewSet_.decorationPolicy();
}

const PresentationPolicy& CrawlerWidget::presentationPolicy() const
{
    return viewSet_.presentationPolicy();
}

const QuickFindPolicy& CrawlerWidget::quickFindPolicy() const
{
    return quickFindPolicy_;
}

const WatchPolicy& CrawlerWidget::watchPolicy() const
{
    return watchPolicy_;
}

void CrawlerWidget::restoreViewContext( const QString& viewContext )
{
    LOG_DEBUG << "CrawlerWidget::restoreViewContext: " << viewContext.toLocal8Bit().data();

    const auto context = decodeViewState( viewContext, quickFindPolicy_ );

    setSizes( context.sizes );
    searchLine_->setFlags( { .matchCase = !context.ignoreCase,
                             .useRegexp = context.useRegexp,
                             .inverse = context.inverseRegexp,
                             .booleanCombination = context.useBooleanCombination,
                             .autoRefresh = context.autoRefresh } );

    followSet( context.followFile && watchPolicy_.anyWatchEnabled() );

    // Saving and restoring Marks with the Session is the user interface's;
    // when they are applied is the Open Log File's.
    const auto& savedMarks = context.marks;
    logsquirl::vector<LineNumber> savedMarkedLines;
    std::transform( savedMarks.cbegin(), savedMarks.cend(), std::back_inserter( savedMarkedLines ),
                    []( const auto& l ) { return LineNumber( l ); } );
    openLogFile_->restoreMarks( savedMarkedLines );

    // Where the text view stood; it can stand there once the Log File has
    // loaded. The top needs nothing.
    if ( context.scrollPosition != 0 ) {
        scrollPositionToRestore_ = LineNumber( context.scrollPosition );
    }

    // Restore chart series and visibility
    const auto& chartJson = context.chartSeries;
    if ( !chartJson.isEmpty() ) {
        QList<ChartSeriesDefinition> defs;
        for ( const auto& val : chartJson ) {
            defs.append( ChartSeriesDefinition::fromJson( val.toObject() ) );
        }
        chartPanel_->setSeriesDefinitions( defs );
    }
    if ( context.chartVisible ) {
        chartPanel_->show();
    }
}

std::shared_ptr<const ViewContextInterface> CrawlerWidget::doGetViewContext() const
{
    ViewState state;
    const auto flags = searchLine_->flags();
    state.sizes = sizes();
    state.ignoreCase = !flags.matchCase;
    state.autoRefresh = flags.autoRefresh;
    state.followFile = isFollowEnabled();
    state.useRegexp = flags.useRegexp;
    state.inverseRegexp = flags.inverse;
    state.useBooleanCombination = flags.booleanCombination;
    const auto marks = openLogFile_->marks();
    std::transform( marks.cbegin(), marks.cend(), std::back_inserter( state.marks ),
                    []( const auto& m ) { return m.get(); } );
    for ( const auto& def : chartPanel_->seriesDefinitions() ) {
        state.chartSeries.append( def.toJson() );
    }
    state.chartVisible = chartPanel_->isVisible();
    // A Log File not loaded yet stands where it was restored to.
    state.scrollPosition = scrollPositionToRestore_.value_or( logMainView_->getTopLine() ).get();

    return std::make_shared<const ViewStateContext>( std::move( state ) );
}

//
// Q_SLOTS:
//

void CrawlerWidget::startNewSearch( bool keepResults )
{
    if ( keepResults ) {
        // The current Search is kept, and a new one current in every view: its
        // Filtered View starts with everything the others show.
        auto* view = keptSearches_.startAnother();

        connectAllFilteredViewSlots( view );

        auto index = tabbedFilteredView_->addTab( view, "" );
        tabbedFilteredView_->setCurrentIndex( index );

        // The View Set handed the new Filtered View its font; its shortcuts
        // are registered here.
        registerShortcuts();
    }

    tabbedFilteredView_->setTabText( tabbedFilteredView_->currentIndex(),
                                     "Find \"" + searchLine_->pattern() + "\"" );

    // Record the search line in the recent list
    // (reload the list first in case another glogg changed it)
    const auto& searches = SavedSearches::getSynced();
    savedSearches_->addRecent( searchLine_->pattern() );
    searches.save();

    // Update the SearchLine (history)
    updateSearchCombo();
    // Call the private function to do the search
    replaceCurrentSearch();
}

void CrawlerWidget::stopSearch()
{
    openLogFile_->stopSearch();

    // An interrupted run no longer reports completion (it is not one): the
    // Search Line puts the buttons and the gauge back now, rather than
    // waiting on a signal that won't come.
    searchLine_->stopped( openLogFile_->searchAutoRefresh().state(), openLogFile_->matchCount(),
                          openLogFile_->searchState().undecidedCount );
}

void CrawlerWidget::clearSearchHistory()
{
    // Clear line
    searchLine_->clearHistory();

    // Sync and clear saved searches
    auto& searches = SavedSearches::getSynced();
    savedSearches_->clear();
    searches.save();
}

void CrawlerWidget::editSearchHistory()
{
    // Sync and clear saved searches
    auto& searches = SavedSearches::getSynced();

    auto history = savedSearches_->recentSearches().join( QChar::LineFeed );
    bool ok;
    QString newHistory = QInputDialog::getMultiLineText( this, tr( "logsquirl" ),
                                                         tr( "Search history:" ), history, &ok );

    if ( ok ) {
        savedSearches_->clear();
        auto items = newHistory.split( QChar::LineFeed, Qt::SkipEmptyParts );
        std::for_each( items.rbegin(), items.rend(), [ this ]( const auto& item ) {
            savedSearches_->addRecent( item );
            LOG_INFO << item;
        } );
    }
    searches.save();

    updateSearchCombo();
}

void CrawlerWidget::saveAsPredefinedFilter()
{
    Q_EMIT saveCurrentSearchAsPredefinedFilter( searchLine_->pattern() );
}

void CrawlerWidget::openSearchInRegexLab()
{
    if ( !searchRegexLab_.isNull() ) {
        // A pattern not yet edited in the Lab follows the Search Line; one
        // the user edited there stays.
        if ( searchRegexLab_->pattern() == searchRegexLabOpenedWith_ ) {
            searchRegexLabOpenedWith_ = searchLine_->request();
            searchRegexLab_->setPattern( searchRegexLabOpenedWith_ );
        }
        searchRegexLab_->raise();
        searchRegexLab_->activateWindow();
        return;
    }

    // The pattern exactly as the Search Line requests a Search: a Wildcard
    // or Fixed String reading comes to the Lab as plain text.
    searchRegexLabOpenedWith_ = searchLine_->request();

    // A window of its own, destroyed with this tab: the Search Line it would
    // write back into goes with it. Cancel changes nothing: only applied() is
    // listened for, which the Lab never sends while it is destroyed along
    // with this widget.
    auto* lab = showRegexLab(
        RegexLabOpening{ .engine = openLogFile_->searchPolicy().regexpEngine,
                         .parent = this,
                         .modal = false,
                         .offerApply = true,
                         .sampleSource =
                             [ this ]() {
                                 return regexLabSampleSource(
                                     *this, QFileInfo( openLogFile_->fileName() ).fileName() );
                             } },
        [ this ]( RegexLabWindow& opened ) { opened.setPattern( searchRegexLabOpenedWith_ ); },
        this,
        [ this ]( const RegularExpressionPattern& pattern ) {
            runEditedPattern( searchLine_->apply( pattern ) );
        } );
    searchRegexLab_ = lab;

    // The Lab matches with the engine the Searches of this Log File run on.
    connect( openLogFile_.get(), &OpenLogFile::searchPolicyChanged, lab,
             [ this, lab ]() { lab->setEngine( openLogFile_->searchPolicy().regexpEngine ); } );

    lab->raise();
    lab->activateWindow();
}

void CrawlerWidget::showSearchContextMenu()
{
    if ( countValuesMenu_ ) {
        fillCountValuesMenu();
    }
    if ( searchLineContextMenu_ )
        searchLineContextMenu_->exec( QCursor::pos( activeScreen( this ) ) );
}

// When the Kept Searches tell the current Search's state changed
void CrawlerWidget::updateFilteredView( SearchSession::State state )
{
    LOG_DEBUG << "updateFilteredView received.";

    if ( state.phase == SearchSession::Phase::Idle ) {
        // No search: nothing to report, and nothing here should override
        // whatever the caller that drove the Session idle (e.g.
        // replaceCurrentSearch() on an emptied search box) already decided
        // to show -- nothing to do, in particular no unconditional show()
        // reappearing over an intentionally-hidden or already-updated line.
        return;
    }

    const auto nbMatches = state.matchCount;
    const bool isComplete = ( state.phase == SearchSession::Phase::Complete );
    const bool isDone = isComplete || state.phase == SearchSession::Phase::Failed
                        || state.phase == SearchSession::Phase::Interrupted
                        || state.phase == SearchSession::Phase::InvalidPattern;

    // The Search Line tells the progress, the Matches found or the failure.
    searchLine_->progressed( state, openLogFile_->searchAutoRefresh().state() );
    if ( const auto failure = searchLine_->display().offerIssueReport; !failure.isEmpty() ) {
        offerIssueReport( failure );
    }

    // If more (or less, e.g. come back to 0) matches have been found
    if ( nbMatches != nbMatches_ ) {
        nbMatches_ = nbMatches;

        // Show the new Matches; the overview, while the Search runs, at a
        // bounded rate.
        viewSet_.refreshMatchesAndMarks( openLogFile_->lineCount(),
                                         isDone ? Overview::UpdatePace::Now
                                                : Overview::UpdatePace::WhileSearching );

        // New data found icon: fires for a continuation (autorefresh
        // extending the range) and equally for a fresh search whose
        // range starts past the beginning of the file (Search Limits),
        // matching what a non-zero initialLine used to signal before the
        // Search Session existed.
        if ( state.isContinuation || state.startLine > 0_lnum ) {
            changeDataStatus( DataStatus::NEW_FILTERED_DATA );
        }
    }

    // Try to restore the filtered window selection close to where it was
    // only for full searches to avoid disconnecting follow mode -- and
    // only if the completed run's range still matches the current Search
    // Limits, so a limits change while a search was in flight doesn't
    // apply a stale range to the just-finished (different-range) result.
    if ( isComplete && !state.isContinuation && state.startLine == openLogFile_->searchStartLine()
         && !isFollowEnabled() ) {
        LOG_DEBUG << "updateFilteredView: restoring selection: "
                  << " absolute line number (0based) " << currentLineNumber_;
        currentFilteredView()->selectAndDisplayLine( currentLineNumber_ );
        // The View Set already handed this view the Search Limits, which are
        // the Open Log File's; only the redraw handing them over did is left.
        currentFilteredView()->updateDecorations();
    }

    Q_EMIT searchProgressed( state );
}

void CrawlerWidget::jumpToMatchingLine( LineNumber logLine, LinesCount nLines, LineColumn startCol,
                                        LineLength nSymbols )
{
    if ( syncingSelection_ ) {
        return;
    }

    syncingSelection_ = true;
    presentation_->showLogLinePortion( logLine, nLines, startCol, nSymbols );
    syncingSelection_ = false;
}

void CrawlerWidget::updateLineNumberHandler( const LogPresentation& reporter, LineNumber line,
                                             LinesCount nLines, LineColumn startCol,
                                             LineLength nSymbols )
{
    // A Presentation not shown follows the one shown, and reports nothing
    // of its own.
    if ( &reporter != presentation_ ) {
        return;
    }

    currentLineNumber_ = line;

    // Keep the Presentations not shown on the same Log Line, so switching
    // shows it
    for ( auto* presentation : presentations() ) {
        if ( presentation != presentation_ ) {
            presentation->showLogLine( line );
        }
    }

    // A Row selected in the Table View selects its Log Line, or the Match
    // before it, in the Filtered View too.
    if ( &reporter == logTableView_ && !syncingSelection_
         && openLogFile_->displayedLineCount() > 0_lcount ) {
        syncingSelection_ = true;
        currentFilteredView()->selectAndDisplayLine( line );
        syncingSelection_ = false;
    }

    Q_EMIT newSelection( line, nLines, startCol, nSymbols );
}

void CrawlerWidget::markLinesFromMain( const logsquirl::vector<LineNumber>& lines )
{
    logsquirl::vector<LineNumber> alreadyMarkedLines;
    alreadyMarkedLines.reserve( lines.size() );

    bool markAdded = false;
    for ( const auto& line : lines ) {
        if ( line >= openLogFile_->lineCount() ) {
            continue;
        }

        if ( !openLogFile_->lineType( line ).testFlag( AbstractLogData::LineTypeFlags::Mark ) ) {
            openLogFile_->addMark( line );
            markAdded = true;
        }
        else {
            alreadyMarkedLines.push_back( line );
        }
    }

    if ( !markAdded ) {
        for ( const auto& line : alreadyMarkedLines ) {
            openLogFile_->toggleMark( line );
        }
    }

    viewSet_.refreshMatchesAndMarks( openLogFile_->lineCount() );
}

void CrawlerWidget::broughtToFront()
{
    LOG_DEBUG << "CrawlerWidget::broughtToFront";

    // Another tab may have added to the Search history since.
    updateSearchCombo();

    // The new data a followed Log File had while in the background has now
    // been seen.
    if ( isFollowEnabled() ) {
        changeDataStatus( DataStatus::OLD_DATA );
    }
}

void CrawlerWidget::rereadSettingsWithoutPolicy()
{
    LOG_DEBUG << "CrawlerWidget::rereadSettingsWithoutPolicy";

    // Nothing else: file watching, Context Lines, hiding ANSI color
    // sequences, the colors Log Lines are decorated in, whether line numbers
    // and the overview are shown and whether follow is allowed are driven by
    // Settings Policies, handed down per Axis when a setting actually changes
    // (#95, #107, #190, #192), and a view built since starts with them (#242).
    // The font and the shortcuts have no Policy, so they are read again, and
    // the Search history, which may have been given another length.

    registerShortcuts();

    viewSet_.setFont( configuredFont() );

    updateSearchCombo();
}

void CrawlerWidget::reportChange( Changed change )
{
    if ( changeReport_ ) {
        changeReport_( change );
        return;
    }

    // Not opened through a Session: only this Log File can be told.
    switch ( change ) {
    case Changed::Settings:
        rereadSettingsWithoutPolicy();
        break;
    case Changed::Font:
        viewSet_.setFont( configuredFont() );
        break;
    case Changed::HighlighterSets:
        applyHighlighterSetChange();
        break;
    case Changed::ValueNames:
        viewSet_.applyValueNamesChange();
        break;
    }
}

QFont CrawlerWidget::configuredFont()
{
    const auto& config = Configuration::get();
    QFont font = config.mainFont();

    // Whatever font we use, we should NOT use kerning
    font.setKerning( false );
    font.setFixedPitch( true );

    // Necessary on systems doing subpixel positionning (e.g. Ubuntu 12.04)
    if ( config.forceFontAntialiasing() ) {
        font.setStyleStrategy( QFont::PreferAntialias );
    }

    font.setBold( config.useBoldFont() );

    return font;
}

void CrawlerWidget::applyHighlighterSetChange()
{
    LOG_DEBUG << "CrawlerWidget::applyHighlighterSetChange";

    // Every view of this Log File, the Filtered Views of kept Searches
    // included, picks up the colors of its Color Labels and paints again.
    viewSet_.applyHighlighterSetChange();
}

void CrawlerWidget::applyDecodingPolicyChange()
{
    LOG_DEBUG << "CrawlerWidget::applyDecodingPolicyChange";

    // The Filtered Views of kept Searches included.
    viewSet_.rereadLogLines();
    restartChartExtraction();
}

void CrawlerWidget::enteringQuickFind()
{
    LOG_DEBUG << "CrawlerWidget::enteringQuickFind";

    // Remember who had the focus (only if it is one of our views)
    QWidget* focus_widget = QApplication::focusWidget();

    if ( ( focus_widget == logMainView_ ) || ( focus_widget == logTableView_ )
         || ( focus_widget == currentFilteredView() ) )
        qfSavedFocus_ = focus_widget;
    else
        qfSavedFocus_ = nullptr;
}

void CrawlerWidget::exitingQuickFind()
{
    // Restore the focus once the QFBar has been hidden; a Presentation that
    // had it hands it to the one shown now, should they have been switched.
    if ( !qfSavedFocus_ )
        return;
    if ( qfSavedFocus_ == logMainView_ || qfSavedFocus_ == logTableView_ )
        shownPresentation()->setFocus();
    else
        qfSavedFocus_->setFocus();
}

void CrawlerWidget::loadingFinishedHandler( const OpenLogFile::LoadFinished& load )
{
    LOG_INFO << "file loading finished, status " << static_cast<int>( load.status );

    // We need to refresh the main window because the view lines on the
    // overview have probably changed.
    overview_.updateData( openLogFile_->lineCount() );

    logMainView_->updateData( load.onlyAppended ? LinesChange::Appended : LinesChange::Any );

    // A restored Log File stands where it stood once its first successful
    // load is done, unless it follows the end of the Log File. An interrupted
    // load keeps the position for the next one, and for the next save.
    if ( load.status == LoadingStatus::Successful ) {
        hasLoaded_ = true;
        if ( const auto restored = std::exchange( scrollPositionToRestore_, {} );
             restored.has_value() && !isFollowEnabled() ) {
            logMainView_->showAtTop( *restored );
        }
    }

    // The Open Log File has refreshed the Search already; one it started
    // again over the truncated Log File is shown like any new Search.
    if ( load.searchRestarted ) {
        prepareForNewSearch();
        showSearchRequested( openLogFile_->searchState() );
    }

    // The Open Log File has settled the Encoding.
    updateEncodingText();

    // A time lookup over the old lines has nothing to say about a Log File
    // that was loaded anew; appended lines leave it valid.
    timeNavigation_.loaded( load.onlyAppended );

    // Also change the data available icon
    if ( !load.fromStart ) {
        changeDataStatus( DataStatus::NEW_DATA );
    }
    else {
        logMainView_->setFocus();
    }

    lastLoadStatus_ = load.status;
    lastLoadFailure_ = load.failure;

    if ( load.formatRecognized ) {
        showRecognizedFormat();
    }
    else {
        // File was updated — refresh table model contents
        logTableView_->updateData( isFollowEnabled(), openLogFile_->lastModified().date() );
    }

    Q_EMIT loadingFinished( load.status, load.failure );
}

void CrawlerWidget::truncatedHandler( const QString& failure )
{
    timeNavigation_.truncated();
    if ( !failure.isEmpty() ) {
        offerIssueReport( failure );
    }

    // The Open Log File has cleared the Marks, dropped an active Search and
    // forgotten the Log Format.
    if ( openLogFile_->searchAutoRefresh().isFileTruncated() ) {
        viewSet_.refreshMatchesAndMarks( openLogFile_->lineCount() );
        printSearchInfoMessage();
        nbMatches_ = 0_lcount;
    }

    resetLogFormat();
}

// The view QuickFind searches: the one that has the focus, or the one where
// QuickFind was entered from, the Filtered View or the Presentation shown.
QWidget* CrawlerWidget::activeView() const
{
    if ( filteredViewIsActive() )
        return currentFilteredView();
    else
        return shownPresentation();
}

bool CrawlerWidget::filteredViewIsActive() const
{
    const auto* filtered = currentFilteredView();
    if ( filtered->hasFocus() )
        return true;
    else if ( shownPresentation()->hasFocus() )
        return false;
    else
        return qfSavedFocus_ == filtered;
}

QWidget* CrawlerWidget::shownPresentation() const
{
    if ( presentation_ == logTableView_ )
        return logTableView_;
    else
        return logMainView_;
}

void CrawlerWidget::resetStateOnSearchPatternChanges()
{
    // We suspend auto-refresh

    openLogFile_->changeSearchExpression();
    printSearchInfoMessage();
}

void CrawlerWidget::searchFlagsChangedHandler( const SearchLine::Flags& flags,
                                               const SearchLine::Flags& previous )
{
    // Inverting the match changes no more than the Search Line's flag.
    const bool readingChanged = flags.matchCase != previous.matchCase
                                || flags.useRegexp != previous.useRegexp
                                || flags.booleanCombination != previous.booleanCombination;
    const bool autoRefreshChanged = flags.autoRefresh != previous.autoRefresh;

    if ( readingChanged ) {
        // We suspend auto-refresh
        openLogFile_->changeSearchExpression();
    }
    if ( autoRefreshChanged ) {
        openLogFile_->setAutoRefresh( flags.autoRefresh );
    }
    if ( readingChanged || autoRefreshChanged ) {
        printSearchInfoMessage();
    }
}

void CrawlerWidget::changeFilteredViewVisibility( int index )
{
    QStandardItem* item = visibilityModel_->item( index );
    auto visibility = item->data().value<FilteredView::Visibility>();

    currentFilteredView()->setVisibility( visibility );

    if ( openLogFile_->displayedLineCount() > 0_lcount ) {
        currentFilteredView()->selectAndDisplayLine( currentLineNumber_ );
    }
}

void CrawlerWidget::selectVisibility( FilteredView::Visibility visibility )
{
    // Looked up by what the entry shows, not by its position in the list,
    // so that the shortcuts keep their mode however the list is ordered.
    for ( int row = 0; row < visibilityModel_->rowCount(); ++row ) {
        const auto* item = visibilityModel_->item( row );
        if ( item->data().value<FilteredView::Visibility>() == visibility ) {
            visibilityBox_->setCurrentIndex( row );
            return;
        }
    }
}

void CrawlerWidget::setSearchPatternFromPredefinedFilters( const QList<PredefinedFilter>& filters )
{
    runEditedPattern( searchLine_->useFilters( filters ) );
}

void CrawlerWidget::addToSearch( const QString& searchString )
{
    runEditedPattern( searchLine_->add( searchString ) );
}

void CrawlerWidget::excludeFromSearch( const QString& searchString )
{
    runEditedPattern( searchLine_->exclude( searchString ) );
}

void CrawlerWidget::replaceSearch( const QString& searchString )
{
    runEditedPattern( searchLine_->replace( searchString ) );
}

void CrawlerWidget::runEditedPattern( bool runNow )
{
    // The Search Line has shown the edit; the Search runs once the edit is
    // done, as though the user asked for it there.
    if ( runNow ) {
        QTimer::singleShot( 0, this, [ this ] { searchLine_->requestSearch(); } );
    }
}

void CrawlerWidget::mouseHoveredOverMatch( LineNumber line )
{
    overviewWidget_->highlightLine( line );
    logTableView_->highlightOverviewLine( line );
}

void CrawlerWidget::activityDetected()
{
    changeDataStatus( DataStatus::OLD_DATA );
}

void CrawlerWidget::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    // The Log Lines the next Search runs over; the View Set hears of them
    // from the Open Log File.
    openLogFile_->setSearchLimits( startLine, endLine );
}

void CrawlerWidget::clearSearchLimits()
{
    setSearchLimits( 0_lnum, LineNumber( openLogFile_->lineCount().get() ) );
}

//
// Private functions
//

// Build the widget and connect all the signals, this must be done once
// the data are attached.
void CrawlerWidget::setup()
{
    LOG_INFO << "Setup crawler widget";
    setOrientation( Qt::Vertical );

    assert( openLogFile_ );

    // The views
    auto bottomWindow = new QWidget;
    bottomWindow->setContentsMargins( 2, 0, 2, 0 );

    overviewWidget_ = new OverviewWidget();
    logMainView_
        = new LogMainView( openLogFile_->logData().get(), quickFindPattern_.get(), &overview_,
                           overviewWidget_, viewSet_.presentationPolicy().useTextWrap );
    logMainView_->setContentsMargins( 2, 0, 2, 0 );
    // A tab starts showing Value Names as the Presentation Policy says, and
    // then as the View menu switches them for it (#647); its Filtered Views
    // show them as its Text View does.
    logMainView_->valueNamesShownSet( viewSet_.presentationPolicy().showValueNames );

    // The Log File's first Search, current in every view: the Presentations
    // and the Overview start with it once they are in the View Set.
    auto* firstFilteredView = keptSearches_.showCurrentSearch();

    overviewWidget_->setOverview( &overview_ );
    overviewWidget_->setParent( logMainView_ );

    // Construct the visibility button
    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    visibilityModel_ = new QStandardItemModel( this );

    QStandardItem* marksAndMatchesItem = new QStandardItem( tr( "Marks and matches" ) );
    marksAndMatchesItem->setData(
        QVariant::fromValue( VisibilityFlags::Marks | VisibilityFlags::Matches ) );
    visibilityModel_->appendRow( marksAndMatchesItem );

    QStandardItem* marksMatchesBreadcrumbsItem
        = new QStandardItem( tr( "Marks, matches + breadcrumbs" ) );
    marksMatchesBreadcrumbsItem->setData( QVariant::fromValue(
        VisibilityFlags::Marks | VisibilityFlags::Matches | VisibilityFlags::Context ) );
    visibilityModel_->appendRow( marksMatchesBreadcrumbsItem );

    QStandardItem* matchesBreadcrumbsItem = new QStandardItem( tr( "Matches + breadcrumbs" ) );
    matchesBreadcrumbsItem->setData(
        QVariant::fromValue( VisibilityFlags::Matches | VisibilityFlags::Context ) );
    visibilityModel_->appendRow( matchesBreadcrumbsItem );

    QStandardItem* marksItem = new QStandardItem( tr( "Marks" ) );
    marksItem->setData( QVariant::fromValue<FilteredView::Visibility>( VisibilityFlags::Marks ) );
    visibilityModel_->appendRow( marksItem );

    QStandardItem* matchesItem = new QStandardItem( tr( "Matches" ) );
    matchesItem->setData(
        QVariant::fromValue<FilteredView::Visibility>( VisibilityFlags::Matches ) );
    visibilityModel_->appendRow( matchesItem );

    auto* visibilityView = new QListView( this );
    visibilityView->setMovement( QListView::Static );
    // visibilityView->setMinimumWidth( 170 ); // Only needed with custom style-sheet

    visibilityBox_ = new YieldingComboBox();
    visibilityBox_->setModel( visibilityModel_ );
    visibilityBox_->setView( visibilityView );

    // Select "Marks and matches" by default (same default as the filtered view)
    visibilityBox_->setCurrentIndex( 0 );
    visibilityBox_->setContentsMargins( 2, 2, 2, 2 );

    // TODO: Maybe there is some way to set the popup width to be
    // sized-to-content (as it is when the stylesheet is not overriden) in the
    // stylesheet as opposed to setting a hard min-width on the view above.
    /*visibilityBox_->setStyleSheet( " \
        QComboBox:on {\
            padding: 1px 2px 1px 6px;\
            width: 19px;\
        } \
        QComboBox:!on {\
            padding: 1px 2px 1px 7px;\
            width: 19px;\
            height: 16px;\
            border: 1px solid gray;\
        } \
        QComboBox::drop-down::down-arrow {\
            width: 0px;\
            border-width: 0px;\
        } \
" );*/

    // The Search Line, which starts with the latest Search of the history.
    searchLine_ = new SearchLineWidget( quickFindPolicy_, savedSearches_->recentSearches(), this );
    setFocusProxy( searchLine_ );

    // Its context menu: the pattern edit's own entries, then this widget's.
    QAction* clearSearchHistoryAction = new QAction( tr( "Clear search history" ), this );
    QAction* editSearchHistoryAction = new QAction( tr( "Edit search history" ), this );
    QAction* saveAsPredefinedFilterAction = new QAction( tr( "Save as Filter" ), this );
    QAction* openInRegexLabAction = new QAction( tr( "Open in Regex Lab..." ), this );
    openInRegexLabAction->setStatusTip(
        tr( "Try out the pattern and its options on Log Lines in the Regex Lab" ) );

    searchLineContextMenu_ = searchLine_->createStandardContextMenu();
    searchLineContextMenu_->addSeparator();
    searchLineContextMenu_->addAction( saveAsPredefinedFilterAction );
    searchLineContextMenu_->addAction( openInRegexLabAction );
    countValuesMenu_ = searchLineContextMenu_->addMenu( tr( "Count values of capture group" ) );
    searchLineContextMenu_->addSeparator();
    searchLineContextMenu_->addAction( editSearchHistoryAction );
    searchLineContextMenu_->addAction( clearSearchHistoryAction );

    auto* searchLineLayout = new QHBoxLayout;
    searchLineLayout->setContentsMargins( 2, 2, 2, 2 );

    searchLineLayout->addWidget( visibilityBox_ );
    searchLineLayout->addWidget( searchLine_ );

    // Table view toggle button (hidden until a Log Format is recognized)
    tableViewToggle_ = new QToolButton();
    // Named, so the benchmark mode shows the Table View as a user does (#669).
    tableViewToggle_->setObjectName( "tableViewToggle" );
    tableViewToggle_->setToolTip( tr( "Toggle table/text view" ) );
    tableViewToggle_->setAccessibleName( tr( "Toggle table view" ) );
    tableViewToggle_->setCheckable( true );
    tableViewToggle_->setToolButtonStyle( Qt::ToolButtonIconOnly );
    tableViewToggle_->setContentsMargins( 2, 2, 2, 2 );
    tableViewToggle_->setVisible( false );
    searchLineLayout->addWidget( tableViewToggle_ );

    connect( tableViewToggle_, &QToolButton::toggled, this, &CrawlerWidget::toggleTableView );

    // Construct the bottom window
    tabbedFilteredView_ = new QTabWidget;
    tabbedFilteredView_->setTabsClosable( true );
    tabbedFilteredView_->addTab( firstFilteredView, "" );
    tabbedFilteredView_->setDocumentMode( true );
    tabbedFilteredView_->setTabBarAutoHide( true );

    auto* bottomMainLayout = new QVBoxLayout;
    bottomMainLayout->addLayout( searchLineLayout );
    bottomMainLayout->addWidget( tabbedFilteredView_ );
    bottomMainLayout->setContentsMargins( 2, 2, 2, 2 );
    bottomWindow->setLayout( bottomMainLayout );

    // Wrap main view and table view in a stacked widget for toggling
    mainViewStack_ = new QStackedWidget;
    logTableView_ = new LogTableView;
    logTableView_->setQuickFindPattern( quickFindPattern_ );
    // The table view's overview strip shares the same Overview data as the
    // text view so matches/marks are always in sync.
    logTableView_->setOverview( &overview_, new OverviewWidget() );

    mainViewStack_->addWidget( logMainView_ );
    mainViewStack_->addWidget( logTableView_ );
    showPresentation( false );

    addWidget( mainViewStack_ );
    addWidget( bottomWindow );

    // Chart panel — third pane in the vertical splitter, hidden by default.
    chartPanel_ = new ChartPanel;
    chartPanel_->hide();
    addWidget( chartPanel_ );

    // The Search Line's buttons start as the QuickFind Policy says; the Open
    // Log File auto-refreshes the Search as they do, and hears of every
    // change from the flags the Search Line sends.
    openLogFile_->setAutoRefresh( searchLine_->flags().autoRefresh );
    printSearchInfoMessage();

    // Default splitter position (usually overridden by the config file)
    setSizes( Configuration::get().splitterSizes() );

    loadIcons();
    Theme::whenApplied( this, [ this ] { loadIcons(); } );

    // Connect the signals
    connect( searchLine_, &SearchLineWidget::searchRequested, this,
             &CrawlerWidget::startNewSearch );
    connect( searchLine_, &SearchLineWidget::stopRequested, this, &CrawlerWidget::stopSearch );
    connect( searchLine_, &SearchLineWidget::flagsChanged, this,
             &CrawlerWidget::searchFlagsChangedHandler );
    connect( searchLine_, &SearchLineWidget::patternEdited, this,
             &CrawlerWidget::resetStateOnSearchPatternChanges );
    connect( searchLine_, &SearchLineWidget::contextMenuRequested, this,
             &CrawlerWidget::showSearchContextMenu );
    connect( saveAsPredefinedFilterAction, &QAction::triggered, this,
             &CrawlerWidget::saveAsPredefinedFilter );
    connect( openInRegexLabAction, &QAction::triggered, this,
             &CrawlerWidget::openSearchInRegexLab );
    connect( clearSearchHistoryAction, &QAction::triggered, this,
             &CrawlerWidget::clearSearchHistory );
    connect( editSearchHistoryAction, &QAction::triggered, this,
             &CrawlerWidget::editSearchHistory );

    connect( visibilityBox_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &CrawlerWidget::changeFilteredViewVisibility );

    // What the user does in either Presentation
    connectPresentation( logMainView_ );
    connectPresentation( logTableView_ );
    connect( logTableView_, &LogTableView::countValuesRequested, this,
             &CrawlerWidget::countFieldValues );

    // Leaving following by moving away from the bottom, which both
    // Presentations let the user do (#543), asks the View Set, which owns
    // follow (#558). What only the Text View lets the user do: start
    // following at the bottom, and zoom with the wheel.
    connect( logMainView_, &LogMainView::followModeChanged, this, &CrawlerWidget::followSet );
    connect( logTableView_, &LogTableView::followModeChanged, this, &CrawlerWidget::followSet );
    connect( logMainView_, &LogMainView::changeFontSize, this, &CrawlerWidget::changeFontSize );

    connect( this, &CrawlerWidget::textWrapSet, logMainView_, &LogMainView::textWrapSet );
    connect( this, &CrawlerWidget::valueNamesShownSet, logMainView_,
             &LogMainView::valueNamesShownSet );

    connect( tabbedFilteredView_, &QTabWidget::currentChanged, this,
             &CrawlerWidget::changeFilteredView );

    connect( tabbedFilteredView_, &QTabWidget::tabCloseRequested, this,
             &CrawlerWidget::closeFilteredView );

    // Sent load file update to MainWindow (for status update)
    connect( openLogFile_.get(), &OpenLogFile::loadingProgressed, this, [ this ]( int progress ) {
        lastLoadStatus_.reset();
        loadingProgress_ = progress;
        Q_EMIT loadingProgressed( progress );
    } );
    connect( openLogFile_.get(), &OpenLogFile::loadingFinished, this,
             &CrawlerWidget::loadingFinishedHandler );
    connect( openLogFile_.get(), &OpenLogFile::truncated, this, &CrawlerWidget::truncatedHandler );
    connect( openLogFile_.get(), &OpenLogFile::grew, this, []( const QString& failure ) {
        if ( !failure.isEmpty() ) {
            offerIssueReport( failure );
        }
    } );
    // From the Log File rather than from the Session, so a CrawlerWidget in
    // a tab that is not current is reached too.
    connect( openLogFile_.get(), &OpenLogFile::decodingPolicyChanged, this,
             &CrawlerWidget::applyDecodingPolicyChange );
    // The Search Limits belong to the Log File: every view of it subdues the
    // same Log Lines, the Filtered Views of kept Searches included, as they
    // are set and as a load settles them.
    connect( openLogFile_.get(), &OpenLogFile::searchLimitsChanged, this,
             [ this ]( LineNumber startLine, LineNumber endLine ) {
                 viewSet_.setSearchLimits( startLine, endLine );
             } );
    connect( openLogFile_.get(), &OpenLogFile::encodingChanged, this,
             &CrawlerWidget::applyEncodingChange );

    connectAllFilteredViewSlots( firstFilteredView );

    // Only the current Search's progress reaches the Search Line and the
    // views, once the request that made it is done.
    connect( &keptSearches_, &KeptSearches::currentSearchUpdated, this,
             &CrawlerWidget::updateFilteredView );

    // Wire chart panel — provide log data and connect click-to-navigate.
    chartPanel_->setLogData( openLogFile_->logData() );
    connect( chartPanel_, &ChartPanel::searchRequested, this, &CrawlerWidget::searchForValue );
    connect( chartPanel_, &ChartPanel::lineSelected, this,
             [ this ]( LineNumber line ) { presentation_->showLogLine( line ); } );

    // Refresh chart data when the file finishes loading: only the appended
    // Log Lines, unless the Log File was truncated or loaded from its start.
    connect( openLogFile_.get(), &OpenLogFile::truncated, chartPanel_,
             &ChartPanel::logFileTruncated );
    connect( openLogFile_.get(), &OpenLogFile::loadingFinished, this,
             [ this ]( const OpenLogFile::LoadFinished& load ) {
                 if ( load.fromStart ) {
                     chartPanel_->logFileTruncated();
                 }
                 if ( chartPanel_->isVisible() ) {
                     chartPanel_->extractData();
                 }
             } );

    // The views just built start with everything they show, color and
    // search under, before any of them is painted. No view reads these
    // settings for itself; each Policy arrives again, on its own Axis,
    // whenever a settings change re-derives it (#184). Nor does any view read
    // the font it draws in: it is handed that too.
    viewSet_.setFont( configuredFont() );
    viewSet_.addPresentation( logMainView_ );
    viewSet_.addPresentation( logTableView_ );
    viewSet_.setOverview( &overview_ );

    // Once every view is in the View Set, which registers theirs too.
    registerShortcuts();
}

template <class View>
void CrawlerWidget::connectSharedSignals( View* view )
{
    // The text selected in the view that asked: a Presentation and a
    // Filtered View hand it out under different names.
    const auto selectedText = [ view ]() {
        if constexpr ( std::is_base_of_v<LogPresentation, View> ) {
            return view->selectedText();
        }
        else {
            return view->getSelectedText();
        }
    };

    // Every view hands out Log Lines, the Filtered View as the main view.
    connect( view, &View::markLines, this, &CrawlerWidget::markLinesFromMain );

    // A Highlighter Set ticked in a view's menu reaches every open Log File.
    connect( view, &View::highlightersChange, this,
             [ this ]() { reportChange( Changed::HighlighterSets ); } );

    connect( view, QOverload<const QString&>::of( &View::addToSearch ), this,
             &CrawlerWidget::addToSearch );

    connect( view, QOverload<const QString&>::of( &View::excludeFromSearch ), this,
             &CrawlerWidget::excludeFromSearch );

    connect( view, QOverload<const QString&>::of( &View::replaceSearch ), this,
             &CrawlerWidget::replaceSearch );

    // Detect activity in the views
    connect( view, &View::activity, this, &CrawlerWidget::activityDetected );

    connect( view, &View::changeSearchLimits, this, &CrawlerWidget::setSearchLimits );

    connect( view, &View::clearSearchLimits, this, &CrawlerWidget::clearSearchLimits );

    connect( view, &View::saveDefaultSplitterSizes, this, &CrawlerWidget::saveSplitterSizes );

    connect( view, &View::addColorLabel, this, &CrawlerWidget::addColorLabelToSelection );

    connect( view, &View::clearColorLabels, this, &CrawlerWidget::clearColorLabels );

    connect( view, &View::sendSelectionToScratchpad, this,
             [ this, selectedText ]() { Q_EMIT sendToScratchpad( selectedText() ); } );

    connect( view, &View::replaceScratchpadWithSelection, this,
             [ this, selectedText ]() { Q_EMIT replaceDataInScratchpad( selectedText() ); } );
}

template <class Presentation>
void CrawlerWidget::connectPresentation( Presentation* presentation )
{
    connect( presentation, &Presentation::newSelection, presentation,
             [ presentation ]() { presentation->update(); } );

    // Each Presentation reports as itself, so the one shown is told from
    // the one not shown without asking who sent the signal.
    connect( presentation, &Presentation::newSelection, this,
             [ this, presentation ]( LineNumber line, LinesCount nLines, LineColumn startCol,
                                     LineLength nSymbols ) {
                 updateLineNumberHandler( *presentation, line, nLines, startCol, nSymbols );
             } );

    connectSharedSignals( presentation );
}

void CrawlerWidget::changeFilteredView( int tabIndex )
{
    // No tab is left only while the widget goes: the last one is never closed.
    if ( tabIndex < 0 ) {
        return;
    }

    auto* view = qobject_cast<FilteredView*>( tabbedFilteredView_->widget( tabIndex ) );
    if ( view == nullptr ) {
        return;
    }

    // Nothing to do when a new Search's tab comes to the front: it is current
    // already.
    keptSearches_.makeCurrent( view );

    Q_EMIT filteredViewChanged();

    changeFilteredViewVisibility( visibilityBox_->currentIndex() );
}

void CrawlerWidget::closeFilteredView( int tabIndex )
{
    // A Log File keeps one Search: its last tab is not closed.
    if ( tabbedFilteredView_->count() <= 1 ) {
        return;
    }

    auto* view = qobject_cast<FilteredView*>( tabbedFilteredView_->widget( tabIndex ) );
    if ( view == nullptr ) {
        return;
    }

    // Removing the current tab brings another to the front, whose Search is
    // made current first; then the Search closed goes with its view.
    tabbedFilteredView_->removeTab( tabIndex );
    keptSearches_.drop( view );
}

FilteredView* CrawlerWidget::currentFilteredView() const
{
    return keptSearches_.currentView();
}

FilteredView* CrawlerWidget::buildFilteredView( LogFilteredData* search )
{
    auto* view = new FilteredView( search, quickFindPattern_.get(),
                                   viewSet_.presentationPolicy().useTextWrap );
    view->setContentsMargins( 2, 0, 2, 0 );
    // Asked each time, so every Filtered View, those of kept Searches too,
    // exports with the Log Format recognized now.
    view->setRecognizedFormat( [ this ]() {
        return FilteredView::RecognizedFormat{ recognizedFormat_,
                                               openLogFile_->lastModified().date(),
                                               openLogFile_->fileName() };
    } );
    return view;
}

void CrawlerWidget::saveSplitterSizes() const
{
    LOG_INFO << "Saving default splitter size";
    auto& splitterConfig = Configuration::get();
    splitterConfig.setSplitterSizes( sizes() );
    splitterConfig.save();
}

void CrawlerWidget::toggleChartPanel()
{
    if ( chartPanel_->isVisible() ) {
        chartPanel_->hide();
    }
    else {
        chartPanel_->show();

        // Ensure the chart panel gets a reasonable size.  The splitter may
        // have assigned it 0 height because saved sizes only cover 2 panes.
        auto currentSizes = sizes();
        if ( currentSizes.size() >= 3 && currentSizes[ 2 ] < 120 ) {
            const int chartHeight = 200;
            // Take space proportionally from the first two panes.
            const int total = currentSizes[ 0 ] + currentSizes[ 1 ];
            if ( total > chartHeight + 100 ) {
                const double ratio
                    = static_cast<double>( total - chartHeight ) / static_cast<double>( total );
                currentSizes[ 0 ] = static_cast<int>( currentSizes[ 0 ] * ratio );
                currentSizes[ 1 ] = static_cast<int>( currentSizes[ 1 ] * ratio );
                currentSizes[ 2 ] = chartHeight;
                setSizes( currentSizes );
            }
        }

        // Refresh data when the chart panel becomes visible.
        chartPanel_->extractData();
    }
}

void CrawlerWidget::showFilterFrequency()
{
    const auto searchText = searchLine_->pattern().trimmed();
    if ( searchText.isEmpty() ) {
        return;
    }

    // Split the search text into individual patterns: the chart takes one
    // regexp per series.
    const auto flags = searchLine_->flags();
    QStringList patterns;
    if ( flags.booleanCombination ) {
        // Every sub-pattern of a logical Search, as the Search reads it, gets
        // a series of its own -- a negated one too: how often the excluded
        // word occurs is as telling as how often the others do (#410).
        for ( const auto& subPattern : logicalSubPatterns( searchText ) ) {
            patterns.append( flags.useRegexp ? subPattern
                                             : QRegularExpression::escape( subPattern ) );
        }
        patterns.removeDuplicates();
    }
    else if ( flags.useRegexp ) {
        // Each top-level alternative gets a series of its own; a | inside a
        // group, a character class or escaped is part of its alternative, so
        // every series stays a valid regexp (#411).
        patterns = regexpAlternatives( searchText );
    }
    else {
        patterns.append( QRegularExpression::escape( searchText ) );
    }

    if ( patterns.isEmpty() ) {
        return;
    }

    // Show the chart panel if hidden.
    if ( !chartPanel_->isVisible() ) {
        toggleChartPanel();
    }

    // The chart counts with the Search's Match case (#411).
    chartPanel_->addFilterFrequencySeries( patterns, flags.matchCase );
}

// The Search as a regexp with capture groups: none for a Search that is not a
// plain regexp, and none for one that excludes the Log Lines it matches.
namespace {
QRegularExpression searchRegexp( const SearchLine::Flags& flags, const QString& pattern )
{
    if ( !flags.useRegexp || flags.booleanCombination || flags.inverse || pattern.isEmpty() ) {
        return {};
    }
    return QRegularExpression( pattern, flags.matchCase
                                            ? QRegularExpression::NoPatternOption
                                            : QRegularExpression::CaseInsensitiveOption );
}
} // namespace

void CrawlerWidget::fillCountValuesMenu()
{
    countValuesMenu_->clear();

    const auto regexp = searchRegexp( searchLine_->flags(), searchLine_->pattern() );
    const auto groups = captureGroupCount( regexp );
    const auto names = regexp.namedCaptureGroups();
    for ( int group = 1; group <= groups; ++group ) {
        const auto name = group < names.size() ? names[ group ] : QString();
        const auto text = name.isEmpty() ? tr( "Group %1" ).arg( group )
                                         : tr( "Group %1 (%2)" ).arg( group ).arg( name );
        countValuesMenu_->addAction( text, this,
                                     [ this, group ] { countSearchGroupValues( group ); } );
    }
    // Without a capture group there is nothing to count.
    countValuesMenu_->setEnabled( groups > 0 );
}

void CrawlerWidget::countFieldValues( const QString& fieldName )
{
    if ( !chartPanel_->isVisible() ) {
        toggleChartPanel();
    }
    chartPanel_->countFieldValues( fieldName );
}

void CrawlerWidget::countSearchGroupValues( int group )
{
    const auto flags = searchLine_->flags();
    const auto pattern = searchLine_->pattern();
    const auto regexp = searchRegexp( flags, pattern );
    if ( group < 1 || group > captureGroupCount( regexp ) ) {
        return;
    }
    if ( !chartPanel_->isVisible() ) {
        toggleChartPanel();
    }
    chartPanel_->countCaptureGroupValues( regexp, group,
                                          tr( "group %1 of \"%2\"" ).arg( group ).arg( pattern ) );
}

void CrawlerWidget::searchForValue( const QString& value )
{
    // A literal Search for the value: not a logical combination, not
    // inverted. Match case stays as the user has it.
    auto flags = searchLine_->flags();
    flags.booleanCombination = false;
    flags.inverse = false;
    searchLine_->setFlags( flags );
    searchLine_->replace( value );
    runEditedPattern( true );
}

void CrawlerWidget::changeFontSize( bool increase )
{
    auto& fontConfig = Configuration::get();

    const auto font = fontConfig.mainFont();
    const auto fontInfo = QFontInfo( font );
    const auto availableSizes = FontUtils::availableFontSizes( fontInfo.family() );

    // The zoom steps from the configured size, which need not be one of the
    // offered sizes; what Qt resolved it to only stands in when it has none.
    // The resolved size can be anything -- -1 where no font could be resolved
    // at all, as on Windows' offscreen platform without fonts (#220).
    const auto currentSize = font.pointSize() > 0 ? font.pointSize() : fontInfo.pointSize();
    if ( currentSize <= 0 ) {
        return;
    }

    const auto zoomedSize = FontUtils::zoomedFontSize( availableSizes, currentSize, increase );
    if ( zoomedSize != currentSize ) {
        fontConfig.setMainFont( QFont{ fontInfo.family(), zoomedSize } );
        // The zoomed font is assembled like any other, bold and antialiasing
        // included, and reaches every view of every open Log File. Nothing
        // but the font was written, so nothing else is applied again.
        reportChange( Changed::Font );
    }
}

void CrawlerWidget::connectAllFilteredViewSlots( FilteredView* view )
{
    connect( view, &FilteredView::newSelection, view, [ view ]( auto ) { view->update(); } );

    connect( view, &FilteredView::newSelection, this, &CrawlerWidget::jumpToMatchingLine );

    connectSharedSignals( view );

    connect( view, &FilteredView::mouseHoveredOverLine, this,
             &CrawlerWidget::mouseHoveredOverMatch );

    connect( view, &FilteredView::mouseLeftHoveringZone, overviewWidget_,
             &OverviewWidget::removeHighlight );

    connect( view, &FilteredView::mouseLeftHoveringZone, logTableView_,
             &LogTableView::removeOverviewHighlight );

    // The View Set hands the Filtered View follow; it asks the View Set too.
    connect( view, &FilteredView::followModeChanged, this, &CrawlerWidget::followSet );

    connect( this, &CrawlerWidget::textWrapSet, view, &FilteredView::textWrapSet );
    // A new Filtered View shows Value Names as the tab does.
    view->valueNamesShownSet( logMainView_->isValueNamesShownSet() );
    connect( this, &CrawlerWidget::valueNamesShownSet, view, &FilteredView::valueNamesShownSet );

    connect( view, &FilteredView::changeFontSize, this, &CrawlerWidget::changeFontSize );

    connect( view, &FilteredView::exitView, logMainView_,
             QOverload<>::of( &LogMainView::setFocus ) );

    // The exit-view shortcut is the Text View's; the Table View has none.
    connect( logMainView_, &LogMainView::exitView, view,
             QOverload<>::of( &FilteredView::setFocus ) );
}

void CrawlerWidget::registerShortcuts()
{
    LOG_INFO << "registering shortcuts for crawler widget";

    for ( auto& shortcut : shortcuts_ ) {
        shortcut.second->deleteLater();
    }

    shortcuts_.clear();

    const auto& config = Configuration::get();
    const auto& configuredShortcuts = config.shortcuts();

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerChangeVisibilityForward, [ this ]() {
            visibilityBox_->setCurrentIndex( ( visibilityBox_->currentIndex() + 1 )
                                             % visibilityBox_->count() );
        } );

    // The Search Line's buttons answer to theirs wherever the focus is in
    // this widget, as the others do.
    searchLine_->registerShortcuts( configuredShortcuts, this );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerChangeVisibilityBackward, [ this ]() {
                                          int nextIndex = visibilityBox_->currentIndex() - 1;
                                          if ( nextIndex < 0 ) {
                                              nextIndex = visibilityBox_->count() - 1;
                                          }
                                          visibilityBox_->setCurrentIndex( nextIndex );
                                      } );

    using VisibilityFlags = LogFilteredData::VisibilityFlags;

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerChangeVisibilityToMarksAndMatches,
        [ this ]() { selectVisibility( VisibilityFlags::Marks | VisibilityFlags::Matches ); } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerChangeVisibilityToMarks,
                                      [ this ]() { selectVisibility( VisibilityFlags::Marks ); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerChangeVisibilityToMatches,
        [ this ]() { selectVisibility( VisibilityFlags::Matches ); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerIncreseTopViewSize, [ this ]() { changeTopViewSize( 1 ); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerDecreaseTopViewSize, [ this ]() { changeTopViewSize( -1 ); } );

    const auto exitSearchKeySequence = QKeySequence( QKeySequence::Cancel );
    ShortcutAction::registerShortcut( exitSearchKeySequence.toString(), shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut, [ this ]() {
                                          const auto activeView = this->activeView();
                                          if ( activeView ) {
                                              activeView->setFocus();
                                          }
                                      } );

    std::array<std::string, 9> colorLables = {
        ShortcutAction::LogViewAddColorLabel1, ShortcutAction::LogViewAddColorLabel2,
        ShortcutAction::LogViewAddColorLabel3, ShortcutAction::LogViewAddColorLabel4,
        ShortcutAction::LogViewAddColorLabel5, ShortcutAction::LogViewAddColorLabel6,
        ShortcutAction::LogViewAddColorLabel7, ShortcutAction::LogViewAddColorLabel8,
        ShortcutAction::LogViewAddColorLabel9,
    };

    for ( auto label = 0u; label < colorLables.size(); ++label ) {
        ShortcutAction::registerShortcut(
            configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
            colorLables[ label ], [ this, label ]() { addColorLabelToSelection( label ); } );
    }

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::LogViewAddNextColorLabel, [ this ]() { addNextColorLabelToSelection(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::LogViewClearColorLabels, [ this ]() { clearColorLabels(); } );

    // Every view of this Log File, the Filtered Views of kept Searches included.
    viewSet_.registerShortcuts();
}

void CrawlerWidget::loadIcons()
{
    searchLine_->loadIcons( iconLoader_ );
    tableViewToggle_->setIcon( iconLoader_.loadCheckable( "icons8-table" ) );
}

// Create a new search from the Search Line, replace the currently
// used one and destroy the old one.
void CrawlerWidget::replaceCurrentSearch()
{
    const auto searchText = searchLine_->pattern();
    LOG_INFO << "replacing current search with " << searchText;

    // The request supersedes whatever search is in flight: any of its
    // results still arriving after this point carry its (now stale) id and
    // are discarded on arrival, so there is nothing to wait for here.

    // Clear and recompute the content of the filtered window.
    openLogFile_->clearSearch();
    prepareForNewSearch();

    if ( !searchText.isEmpty() ) {

        // Start a new asynchronous search over the Search Limits -- the
        // Session validates the pattern itself; on failure it goes to
        // InvalidPattern synchronously (without touching the worker), so the
        // state is already conclusive.
        showSearchRequested( openLogFile_->requestSearch( searchLine_->request() ) );
    }
    else {
        searchLine_->cleared();
    }
}

void CrawlerWidget::prepareForNewSearch()
{
    nbMatches_ = 0_lcount;

    // Switch to "Marks and matches" view when in "Marks" view
    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    if ( !currentFilteredView()->visibility().testFlag( VisibilityFlags::Matches ) ) {
        visibilityBox_->setCurrentIndex( 0 );
    }

    viewSet_.refreshMatchesAndMarks( openLogFile_->lineCount() );
}

void CrawlerWidget::showSearchRequested( const SearchSession::State& state )
{
    // The Search Line shows the Stop button, or the error in the expression.
    searchLine_->requested( state );

    if ( state.phase != SearchSession::Phase::InvalidPattern ) {
        viewSet_.setSearchPattern( state.pattern );
    }
    else {
        // The regexp is wrong. The request already drove the Session to
        // InvalidPattern, which on its own clears results/Context Lines the
        // same way an idle request would -- no separate clear needed here.
        viewSet_.refreshMatchesAndMarks( openLogFile_->lineCount() );
        viewSet_.setSearchPattern( {} );
    }
}

// Updates the content of the drop down list for the saved searches,
// called when the SavedSearch has been changed.
void CrawlerWidget::updateSearchCombo()
{
    searchLine_->setHistory( savedSearches_->recentSearches() );
}

void CrawlerWidget::printSearchInfoMessage()
{
    searchLine_->settled( openLogFile_->searchAutoRefresh().state(), openLogFile_->matchCount(),
                          openLogFile_->searchState().undecidedCount );
}

// Change the data status and, if needed, advise upstream.
void CrawlerWidget::changeDataStatus( DataStatus status )
{
    if ( ( status != dataStatus_ )
         && ( !( dataStatus_ == DataStatus::NEW_FILTERED_DATA
                 && status == DataStatus::NEW_DATA ) ) ) {
        dataStatus_ = status;
        Q_EMIT dataStatusChanged( dataStatus_ );
    }
}

void CrawlerWidget::updateEncodingText()
{
    const auto encodingPrefix
        = openLogFile_->chosenEncoding() ? tr( "Displayed as %1" ) : tr( "Detected as %1" );
    encodingText_ = encodingPrefix.arg( openLogFile_->encoding()->name().constData() );
}

void CrawlerWidget::applyEncodingChange()
{
    // The Filtered Views of kept Searches included. Only when the Encoding
    // is another one: a Log File that only grew keeps what its views read and
    // counted, and its chart what it extracted.
    viewSet_.rereadLogLines();
    restartChartExtraction();
}

void CrawlerWidget::restartChartExtraction()
{
    // The reload another Encoding needs is not a load from the start, which
    // restarts the extraction by itself; another Decoding Policy reloads
    // nothing at all.
    chartPanel_->logFileTruncated();
    if ( chartPanel_->isVisible() ) {
        chartPanel_->extractData();
    }
}

// Change the respective size of the two views
void CrawlerWidget::changeTopViewSize( int32_t delta )
{
    int min, max;
    getRange( 1, &min, &max );
    LOG_DEBUG << "CrawlerWidget::changeTopViewSize " << sizes().at( 0 ) << " " << min << " " << max;
    moveSplitter( closestLegalPosition( sizes().at( 0 ) + ( delta * 10 ), 1 ), 1 );
    LOG_DEBUG << "CrawlerWidget::changeTopViewSize " << sizes().at( 0 );
}

void CrawlerWidget::addColorLabelToSelection( size_t label )
{
    updateColorLabels( colorLabelsManager_.setColorLabel( label, getSelectedText() ) );
}

void CrawlerWidget::addNextColorLabelToSelection()
{
    updateColorLabels( colorLabelsManager_.setNextColorLabel( getSelectedText() ) );
}

void CrawlerWidget::clearColorLabels()
{
    updateColorLabels( colorLabelsManager_.clear() );
}

void CrawlerWidget::updateColorLabels(
    const ColorLabelsManager::QuickHighlightersCollection& labels )
{
    // The Color Labels belong to the Log File: every view of it colors them,
    // the Filtered Views of kept Searches included.
    viewSet_.setColorLabels( labels );
}

// Toggle between text view and table view
void CrawlerWidget::toggleTableView()
{
    if ( !recognizedFormat_ ) {
        return;
    }

    const bool showTable = tableViewToggle_->isChecked();
    showPresentation( showTable );

    if ( showTable ) {
        // Defer model population so the view switch renders immediately
        QTimer::singleShot( 0, this, [ this ]() {
            logTableView_->updateData( isFollowEnabled(), openLogFile_->lastModified().date() );
        } );
    }
}

void CrawlerWidget::showPresentation( bool tableView )
{
    if ( tableView ) {
        presentation_ = logTableView_;
        mainViewStack_->setCurrentWidget( logTableView_ );
    }
    else {
        presentation_ = logMainView_;
        mainViewStack_->setCurrentWidget( logMainView_ );
    }
    logTableView_->setActive( tableView );
}

std::array<LogPresentation*, 2> CrawlerWidget::presentations() const
{
    return { logMainView_, logTableView_ };
}

// Show the Log Format the Open Log File recognized. Called from the
// load-finished path only, once per first load, manual reload or truncation.
void CrawlerWidget::showRecognizedFormat()
{
    auto recognized = openLogFile_->logFormat();

    if ( !recognized ) {
        if ( recognizedFormat_ ) {
            resetLogFormat();
        }
        return;
    }

    if ( recognized == recognizedFormat_ ) {
        // Still the very same Log Format: nothing to switch, only the Table
        // View to bring up to date with what was loaded.
        logTableView_->updateData( isFollowEnabled(), openLogFile_->lastModified().date() );
        return;
    }

    // The Table View still points at the previous Log Format until it is
    // handed the new one, so the previous one stays alive until then.
    const auto previousFormat = std::exchange( recognizedFormat_, std::move( recognized ) );
    timeNavigation_.formatChanged();
    logTableView_->setLogFormat( recognizedFormat_.get(), openLogFile_->logData().get() );
    logTableView_->setLogFilePath( openLogFile_->fileName() );
    tableViewToggle_->setVisible( true );
    tableViewToggle_->setToolTip(
        tr( "Toggle table/text view (%1)" ).arg( recognizedFormat_->title() ) );

    // Provide format info to the chart panel for template series.
    chartPanel_->setLogFormat( recognizedFormat_.get() );

    // A reload that recognized a different Log Format while the Table View
    // was shown keeps it shown, with the new columns.
    logTableView_->updateData( isFollowEnabled(), openLogFile_->lastModified().date() );
    if ( viewSet_.presentationPolicy().autoShowTableView && !tableViewToggle_->isChecked() ) {
        // Automatically activate table view if the user opted in
        tableViewToggle_->setChecked( true );
    }
}

// Forget the recognized Log Format, back in the text view.
void CrawlerWidget::resetLogFormat()
{
    logTableView_->setLogFormat( nullptr, nullptr );
    showPresentation( false );
    recognizedFormat_.reset();
    timeNavigation_.formatReset();

    // Clear format info from chart panel.
    chartPanel_->setLogFormat( nullptr );

    if ( tableViewToggle_ ) {
        tableViewToggle_->setChecked( false );
        tableViewToggle_->setVisible( false );
    }
}
