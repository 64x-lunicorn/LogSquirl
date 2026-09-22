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

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QInputDialog>
#include <QJsonArray>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
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

// The Search line keeps room for this many characters, whatever else is in
// its row (#261).
constexpr int SearchLineMinimumCharacters = 20;

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
    , searchLine_( build.policies.quickFind )
{
    openLogFile_ = build.openLogFile;
    quickFindPattern_ = build.quickFindPattern;
    savedSearches_ = build.savedSearches;
    changeReport_ = build.changeReport;

    // Every view built below starts with these; a view built later, too (#242).
    viewSet_.setDecorationPolicy( build.policies.decoration );
    viewSet_.setPresentationPolicy( build.policies.presentation );
    viewSet_.setQuickFindPolicy( build.policies.quickFind );
    applyWatchPolicy( build.policies.watch );

    setup();

    if ( !build.viewContext.isEmpty() ) {
        restoreViewContext( build.viewContext );
    }
}

// The top line is first one on the main display
LineNumber CrawlerWidget::getTopLine() const
{
    return logMainView_->getTopLine();
}

QString CrawlerWidget::getSelectedText() const
{
    if ( filteredView_->hasFocus() )
        return filteredView_->getSelectedText();
    else
        return presentation_->selectedText();
}

bool CrawlerWidget::isPartialSelection() const
{
    if ( filteredView_->hasFocus() )
        return filteredView_->isPartialSelection();
    else
        return logMainView_->isPartialSelection();
}

void CrawlerWidget::selectAll()
{
    activeView()->selectAll();
}

std::optional<int> CrawlerWidget::encodingMib() const
{
    return openLogFile_->chosenEncoding();
}

bool CrawlerWidget::isFollowEnabled() const
{
    return logMainView_->isFollowEnabled();
}

bool CrawlerWidget::isTextWrapEnabled() const
{
    return logMainView_->isTextWrapEnabled();
}

QString CrawlerWidget::encodingText() const
{
    return encodingText_;
}

// Return a pointer to the view in which we should do the QuickFind
SearchableWidgetInterface* CrawlerWidget::doGetActiveSearchable() const
{
    return activeView();
}

// Return all the searchable widgets (views)
std::vector<QObject*> CrawlerWidget::doGetAllSearchables() const
{
    std::vector<QObject*> searchables = { logMainView_, filteredView_ };

    return searchables;
}

// Update the state of the parent
void CrawlerWidget::doSendAllStateSignals()
{
    Q_EMIT newSelection( currentLineNumber_, 0_lcount, 0_lcol, 0_length );
    if ( !loadingInProgress_ )
        Q_EMIT loadingFinished( LoadingStatus::Successful );
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
    openLogFile_->reload();
    viewSet_.refreshMatchesAndMarks( openLogFile_->logData()->getNbLine() );
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
    searchLineEdit_->setFocus( Qt::ShortcutFocusReason );
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
        filteredView_->trySelectLine( selectedLine );

        const auto nbLines = openLogFile_->logData()->getNbLine();
        if ( nbLines.get() > 0 ) {
            presentation_->showLogLine(
                std::min( selectedLine, LineNumber( nbLines.get() ) - 1_lcount ) );
        }
    }
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
        // belong to the window, which takes this Policy from its session:
        // nothing is handed on from here.
        viewSet_.setQuickFindPolicy( *change.quickFind );
        searchLine_.setQuickFindPolicy( *change.quickFind );
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
}

void CrawlerWidget::applyWatchPolicy( const WatchPolicy& policy )
{
    watchPolicy_ = policy;

    // Takes following away from every view of this Log File, or gives it
    // back, without the Log File being opened again.
    viewSet_.setFollowAllowed( policy.anyWatchEnabled() );
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
    return viewSet_.quickFindPolicy();
}

const WatchPolicy& CrawlerWidget::watchPolicy() const
{
    return watchPolicy_;
}

void CrawlerWidget::restoreViewContext( const QString& viewContext )
{
    LOG_DEBUG << "CrawlerWidget::restoreViewContext: " << viewContext.toLocal8Bit().data();

    const auto context = decodeViewState( viewContext, viewSet_.quickFindPolicy() );

    setSizes( context.sizes );
    searchLine_.setFlags( { .matchCase = !context.ignoreCase,
                            .useRegexp = context.useRegexp,
                            .inverse = context.inverseRegexp,
                            .booleanCombination = context.useBooleanCombination,
                            .autoRefresh = context.autoRefresh } );
    showSearchFlags();
    // Manually call the handler as it is not called when changing the state programmatically
    searchRefreshChangedHandler( context.autoRefresh );

    logMainView_->followSet( context.followFile && watchPolicy_.anyWatchEnabled() );

    // Saving and restoring Marks with the Session is the user interface's;
    // when they are applied is the Open Log File's.
    const auto& savedMarks = context.marks;
    logsquirl::vector<LineNumber> savedMarkedLines;
    std::transform( savedMarks.cbegin(), savedMarks.cend(), std::back_inserter( savedMarkedLines ),
                    []( const auto& l ) { return LineNumber( l ); } );
    openLogFile_->restoreMarks( savedMarkedLines );

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
    const auto flags = searchLine_.flags();
    state.sizes = sizes();
    state.ignoreCase = !flags.matchCase;
    state.autoRefresh = flags.autoRefresh;
    state.followFile = logMainView_->isFollowEnabled();
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

    return std::make_shared<const ViewStateContext>( std::move( state ) );
}

//
// Q_SLOTS:
//

void CrawlerWidget::startNewSearch()
{
    if ( keepSearchResultsButton_->isChecked() ) {
        keepSearchResultsButton_->setChecked( false );

        const auto search = openLogFile_->startAnotherSearch();

        // A new Filtered View starts with everything the others show.
        filteredView_ = new FilteredView( search.get(), quickFindPattern_.get(),
                                          viewSet_.presentationPolicy().useTextWrap );
        viewSet_.addFilteredView( filteredView_ );
        filteredViewsData_[ filteredView_ ] = search;

        connectAllFilteredViewSlots( filteredView_ );

        auto index = tabbedFilteredView_->addTab( filteredView_, "" );
        tabbedFilteredView_->setCurrentIndex( index );

        connect( search.get(), &LogFilteredData::searchStateChanged, this,
                 &CrawlerWidget::updateFilteredView, Qt::QueuedConnection );

        logMainView_->useNewFiltering( openLogFile_->filteredData().get() );

        // The View Set handed the new Filtered View its font; its shortcuts
        // are registered here.
        registerShortcuts();
    }

    tabbedFilteredView_->setTabText( tabbedFilteredView_->currentIndex(),
                                     "Find \"" + searchLine_.pattern() + "\"" );

    // Record the search line in the recent list
    // (reload the list first in case another glogg changed it)
    const auto& searches = SavedSearches::getSynced();
    savedSearches_->addRecent( searchLine_.pattern() );
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
    searchLine_.stopped( openLogFile_->searchAutoRefresh().state(),
                         openLogFile_->filteredData()->getNbMatches() );
    showSearchLine();
}

void CrawlerWidget::clearSearchHistory()
{
    // Clear line
    searchLineEdit_->clear();

    // Sync and clear saved searches
    auto& searches = SavedSearches::getSynced();
    savedSearches_->clear();
    searches.save();

    searchLineCompleter_->setModel( new QStringListModel( {}, searchLineCompleter_ ) );
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
    Q_EMIT saveCurrentSearchAsPredefinedFilter( searchLine_.pattern() );
}

void CrawlerWidget::showSearchContextMenu()
{
    if ( searchLineContextMenu_ )
        searchLineContextMenu_->exec( QCursor::pos( activeScreen( this ) ) );
}

// When receiving the Search Session's searchStateChanged signal
void CrawlerWidget::updateFilteredView( SearchSession::State state )
{
    LOG_DEBUG << "updateFilteredView received.";

    // Every tab's LogFilteredData keeps its own persistent connection to
    // this slot, so a tab switched away from (e.g. via stop() in
    // changeFilteredView()) can still have a notification queued when it
    // arrives here -- after the current Search has already moved on to the
    // newly-active tab. Since everything below mutates shared, single UI
    // (the Search Line, the views), a stale notification from a
    // no-longer-active tab must not be allowed to touch it.
    if ( sender() != openLogFile_->filteredData().get() ) {
        return;
    }

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
    searchLine_.progressed( state, openLogFile_->searchAutoRefresh().state() );
    showSearchLine();
    if ( const auto failure = searchLine_.display().offerIssueReport; !failure.isEmpty() ) {
        offerIssueReport( failure );
    }

    // If more (or less, e.g. come back to 0) matches have been found
    if ( nbMatches != nbMatches_ ) {
        nbMatches_ = nbMatches;

        // Show the new Matches; the overview, while the Search runs, at a
        // bounded rate.
        viewSet_.refreshMatchesAndMarks( openLogFile_->logData()->getNbLine(),
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
        filteredView_->selectAndDisplayLine( currentLineNumber_ );
        // The View Set already handed this view the Search Limits, which are
        // the Open Log File's; only the redraw handing them over did is left.
        filteredView_->updateDecorations();
    }
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
    if ( &reporter == logTableView_ && !syncingSelection_ && openLogFile_->filteredData()
         && openLogFile_->filteredData()->getNbLine().get() > 0 ) {
        syncingSelection_ = true;
        filteredView_->selectAndDisplayLine( line );
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
        if ( line >= openLogFile_->logData()->getNbLine() ) {
            continue;
        }

        if ( !openLogFile_->filteredData()->lineTypeByLine( line ).testFlag(
                 AbstractLogData::LineTypeFlags::Mark ) ) {
            openLogFile_->filteredData()->addMark( line );
            markAdded = true;
        }
        else {
            alreadyMarkedLines.push_back( line );
        }
    }

    if ( !markAdded ) {
        for ( const auto& line : alreadyMarkedLines ) {
            openLogFile_->filteredData()->toggleMark( line );
        }
    }

    viewSet_.refreshMatchesAndMarks( openLogFile_->logData()->getNbLine() );
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

    if ( ( focus_widget == logMainView_ ) || ( focus_widget == filteredView_ ) )
        qfSavedFocus_ = focus_widget;
    else
        qfSavedFocus_ = nullptr;
}

void CrawlerWidget::exitingQuickFind()
{
    // Restore the focus once the QFBar has been hidden
    if ( qfSavedFocus_ )
        qfSavedFocus_->setFocus();
}

void CrawlerWidget::loadingFinishedHandler( const OpenLogFile::LoadFinished& load )
{
    LOG_INFO << "file loading finished, status " << static_cast<int>( load.status );

    if ( load.status == LoadingStatus::Failed ) {
        offerIssueReport( load.failure );
    }

    // We need to refresh the main window because the view lines on the
    // overview have probably changed.
    overview_.updateData( openLogFile_->logData()->getNbLine() );

    // FIXME, handle topLine
    // logMainView_->updateData( logData_, topLine );
    logMainView_->updateData( load.onlyAppended ? LinesChange::Appended : LinesChange::Any );

    // The Open Log File has refreshed the Search already; one it started
    // again over the truncated Log File is shown like any new Search.
    if ( load.searchRestarted ) {
        prepareForNewSearch();
        showSearchRequested( openLogFile_->filteredData()->searchState() );
    }

    // The Open Log File has settled the Encoding.
    updateEncodingText();

    // The Search Limits are the whole Log File again; every view shows it.
    viewSet_.setSearchLimits( openLogFile_->searchStartLine(), openLogFile_->searchEndLine() );

    // Also change the data available icon
    if ( !load.fromStart ) {
        changeDataStatus( DataStatus::NEW_DATA );
    }
    else {
        logMainView_->setFocus();
    }

    loadingInProgress_ = false;

    if ( load.formatRecognized ) {
        showRecognizedFormat();
    }
    else {
        // File was updated — refresh table model contents
        logTableView_->updateData( openLogFile_->filteredData().get(), isFollowEnabled() );
    }

    Q_EMIT loadingFinished( load.status );
}

void CrawlerWidget::truncatedHandler( const QString& failure )
{
    if ( !failure.isEmpty() ) {
        offerIssueReport( failure );
    }

    // The Open Log File has cleared the Marks, dropped an active Search and
    // forgotten the Log Format.
    if ( openLogFile_->searchAutoRefresh().isFileTruncated() ) {
        viewSet_.refreshMatchesAndMarks( openLogFile_->logData()->getNbLine() );
        printSearchInfoMessage();
        nbMatches_ = 0_lcount;
    }

    resetLogFormat();
}

// Returns a pointer to the window in which the search should be done
AbstractLogView* CrawlerWidget::activeView() const
{
    QWidget* activeView;

    // Search in the window that has focus, or the window where 'Find' was
    // called from, or the main window.
    if ( filteredView_->hasFocus() || logMainView_->hasFocus() )
        activeView = QApplication::focusWidget();
    else
        activeView = qfSavedFocus_;

    if ( activeView ) {
        auto* view = qobject_cast<AbstractLogView*>( activeView );
        return view;
    }
    else {
        LOG_WARNING << "No active view, defaulting to logMainView";
        return logMainView_;
    }
}

void CrawlerWidget::searchForward()
{
    LOG_DEBUG << "CrawlerWidget::searchForward";

    activeView()->searchForward();
}

void CrawlerWidget::searchBackward()
{
    LOG_DEBUG << "CrawlerWidget::searchBackward";

    activeView()->searchBackward();
}

void CrawlerWidget::resetStateOnSearchPatternChanges()
{
    // We suspend auto-refresh

    openLogFile_->changeSearchExpression();
    printSearchInfoMessage();
}

void CrawlerWidget::searchRefreshChangedHandler( bool isRefreshing )
{
    auto flags = searchLine_.flags();
    flags.autoRefresh = isRefreshing;
    searchLine_.setFlags( flags );

    openLogFile_->setAutoRefresh( isRefreshing );
    printSearchInfoMessage();
}

void CrawlerWidget::matchCaseChangedHandler( bool shouldMatchCase )
{
    auto flags = searchLine_.flags();
    flags.matchCase = shouldMatchCase;
    searchLine_.setFlags( flags );

    searchLineCompleter_->setCaseSensitivity( shouldMatchCase ? Qt::CaseSensitive
                                                              : Qt::CaseInsensitive );

    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::booleanCombiningChangedHandler( bool shouldCombine )
{
    auto flags = searchLine_.flags();
    flags.booleanCombination = shouldCombine;
    searchLine_.setFlags( flags );

    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::useRegexpChangeHandler( bool shouldUseRegex )
{
    auto flags = searchLine_.flags();
    flags.useRegexp = shouldUseRegex;
    searchLine_.setFlags( flags );

    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::searchTextChangeHandler( QString )
{
    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::changeFilteredViewVisibility( int index )
{
    QStandardItem* item = visibilityModel_->item( index );
    auto visibility = item->data().value<FilteredView::Visibility>();

    filteredView_->setVisibility( visibility );

    if ( openLogFile_->filteredData()->getNbLine() > 0_lcount ) {
        filteredView_->selectAndDisplayLine( currentLineNumber_ );
    }
}

void CrawlerWidget::setSearchPatternFromPredefinedFilters( const QList<PredefinedFilter>& filters )
{
    showEditedPattern( searchLine_.useFilters( filters ) );
}

void CrawlerWidget::addToSearch( const QString& searchString )
{
    showEditedPattern( searchLine_.add( searchString ) );
}

void CrawlerWidget::excludeFromSearch( const QString& searchString )
{
    showEditedPattern( searchLine_.exclude( searchString ) );
}

void CrawlerWidget::replaceSearch( const QString& searchString )
{
    showEditedPattern( searchLine_.replace( searchString ) );
}

void CrawlerWidget::showEditedPattern( bool runNow )
{
    // Excluding a word switches the logical combination on: its button is
    // set, as the user would, before the pattern is shown.
    showSearchFlags();
    searchLineEdit_->setEditText( searchLine_.pattern() );
    // Set the focus to lineEdit so that the user can press 'Return' immediately
    searchLineEdit_->lineEdit()->setFocus();

    if ( runNow ) {
        dispatchToMainThread( [ this ] { startNewSearch(); } );
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
    // The Log Lines the next Search runs over.
    openLogFile_->setSearchLimits( startLine, endLine );

    // The Search Limits belong to the Log File: every view of it subdues the
    // same Log Lines, the Filtered Views of kept Searches included.
    viewSet_.setSearchLimits( startLine, endLine );
}

void CrawlerWidget::clearSearchLimits()
{
    setSearchLimits( 0_lnum, LineNumber( openLogFile_->logData()->getNbLine().get() ) );
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

    filteredView_ = new FilteredView( openLogFile_->filteredData().get(), quickFindPattern_.get(),
                                      viewSet_.presentationPolicy().useTextWrap );
    filteredViewsData_[ filteredView_ ] = openLogFile_->filteredData();
    filteredView_->setContentsMargins( 2, 0, 2, 0 );

    overviewWidget_->setOverview( &overview_ );
    overviewWidget_->setParent( logMainView_ );

    // Connect the search to the top view
    logMainView_->useNewFiltering( openLogFile_->filteredData().get() );

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

    // Construct the Search Info line
    searchInfoLine_ = new InfoLine();
    searchInfoLine_->setFrameStyle( QFrame::StyledPanel );
    searchInfoLine_->setFrameShadow( QFrame::Sunken );
    searchInfoLine_->setLineWidth( 1 );
    // The match count gives way to the Search line when the row runs out of
    // width: it elides rather than keep the width of its whole text (#261).
    searchInfoLine_->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Minimum );
    searchInfoLine_->setElidesText( true );
    auto searchInfoLineSizePolicy = searchInfoLine_->sizePolicy();
    searchInfoLineSizePolicy.setRetainSizeWhenHidden( false );
    searchInfoLine_->setSizePolicy( searchInfoLineSizePolicy );
    searchInfoLineDefaultPalette_ = this->palette();
    searchInfoLine_->setContentsMargins( 2, 2, 2, 2 );

    matchCaseButton_ = new QToolButton();
    matchCaseButton_->setToolTip( tr( "Match case" ) );
    matchCaseButton_->setAccessibleName( tr( "Match case" ) );
    matchCaseButton_->setCheckable( true );
    matchCaseButton_->setFocusPolicy( Qt::TabFocus );
    matchCaseButton_->setContentsMargins( 2, 2, 2, 2 );

    useRegexpButton_ = new QToolButton();
    useRegexpButton_->setToolTip( tr( "Use regex" ) );
    useRegexpButton_->setAccessibleName( tr( "Use regex" ) );
    useRegexpButton_->setCheckable( true );
    useRegexpButton_->setFocusPolicy( Qt::TabFocus );
    useRegexpButton_->setContentsMargins( 2, 2, 2, 2 );

    inverseButton_ = new QToolButton();
    inverseButton_->setToolTip( tr( "Inverse match" ) );
    inverseButton_->setAccessibleName( tr( "Inverse match" ) );
    inverseButton_->setCheckable( true );
    inverseButton_->setFocusPolicy( Qt::TabFocus );
    inverseButton_->setContentsMargins( 2, 2, 2, 2 );

    booleanButton_ = new QToolButton();
    booleanButton_->setToolTip( tr( "Enable regular expression logical combining" ) );
    booleanButton_->setAccessibleName( tr( "Boolean combining" ) );
    booleanButton_->setCheckable( true );
    booleanButton_->setFocusPolicy( Qt::TabFocus );
    booleanButton_->setContentsMargins( 2, 2, 2, 2 );

    searchRefreshButton_ = new QToolButton();
    searchRefreshButton_->setToolTip( tr( "Auto-refresh" ) );
    searchRefreshButton_->setAccessibleName( tr( "Auto-refresh" ) );
    searchRefreshButton_->setCheckable( true );
    searchRefreshButton_->setFocusPolicy( Qt::TabFocus );
    searchRefreshButton_->setContentsMargins( 2, 2, 2, 2 );

    // Construct the Search line
    searchLineCompleter_ = new QCompleter( savedSearches_->recentSearches(), this );
    searchLineEdit_ = new QComboBox;
    searchLineEdit_->setEditable( true );
    searchLineEdit_->setCompleter( searchLineCompleter_ );
    searchLineEdit_->addItems( savedSearches_->recentSearches() );
    searchLineEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
    searchLineEdit_->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    // Whatever else is in the row, the Search line keeps room for about 20
    // characters of the font it shows (#261).
    searchLineEdit_->setMinimumContentsLength( SearchLineMinimumCharacters );
    searchLineEdit_->lineEdit()->setMaxLength( std::numeric_limits<int>::max() / 1024 );
    searchLineEdit_->setContentsMargins( 2, 2, 2, 2 );
    searchLineEdit_->setAccessibleName( tr( "Search pattern" ) );

    // Keyboard tab order for the search bar and filter buttons
    setTabOrder( searchLineEdit_, matchCaseButton_ );
    setTabOrder( matchCaseButton_, useRegexpButton_ );
    setTabOrder( useRegexpButton_, inverseButton_ );
    setTabOrder( inverseButton_, booleanButton_ );
    setTabOrder( booleanButton_, searchRefreshButton_ );

    QAction* clearSearchHistoryAction = new QAction( tr( "Clear search history" ), this );
    QAction* editSearchHistoryAction = new QAction( tr( "Edit search history" ), this );
    QAction* saveAsPredefinedFilterAction = new QAction( tr( "Save as Filter" ), this );

    searchLineContextMenu_ = searchLineEdit_->lineEdit()->createStandardContextMenu();
    searchLineContextMenu_->addSeparator();
    searchLineContextMenu_->addAction( saveAsPredefinedFilterAction );
    searchLineContextMenu_->addSeparator();
    searchLineContextMenu_->addAction( editSearchHistoryAction );
    searchLineContextMenu_->addAction( clearSearchHistoryAction );
    searchLineEdit_->setContextMenuPolicy( Qt::CustomContextMenu );

    setFocusProxy( searchLineEdit_ );

    clearButton_ = new QToolButton();
    clearButton_->setText( tr( "Clear search text" ) );
    clearButton_->setAutoRaise( true );
    clearButton_->setContentsMargins( 2, 2, 2, 2 );

    searchButton_ = new QToolButton();
    searchButton_->setText( tr( "Search" ) );
    searchButton_->setAutoRaise( true );
    searchButton_->setContentsMargins( 2, 2, 2, 2 );

    keepSearchResultsButton_ = new QToolButton();
    keepSearchResultsButton_->setText( tr( "Keep Results" ) );
    keepSearchResultsButton_->setToolTip(
        tr( "Keep these results and show subsequent results in a new window" ) );
    keepSearchResultsButton_->setCheckable( true );
    keepSearchResultsButton_->setContentsMargins( 2, 2, 2, 2 );

    stopButton_ = new QToolButton();
    stopButton_->setAutoRaise( true );
    stopButton_->setEnabled( false );
    stopButton_->setVisible( false );
    stopButton_->setContentsMargins( 2, 2, 2, 2 );

    auto* searchLineLayout = new QHBoxLayout;
    searchLineLayout->setContentsMargins( 2, 2, 2, 2 );

    searchLineLayout->addWidget( visibilityBox_ );
    searchLineLayout->addWidget( matchCaseButton_ );
    searchLineLayout->addWidget( useRegexpButton_ );
    searchLineLayout->addWidget( inverseButton_ );
    searchLineLayout->addWidget( booleanButton_ );
    searchLineLayout->addWidget( searchRefreshButton_ );
    searchLineLayout->addWidget( searchLineEdit_ );
    searchLineLayout->addWidget( clearButton_ );
    searchLineLayout->addWidget( searchButton_ );
    searchLineLayout->addWidget( keepSearchResultsButton_ );
    searchLineLayout->addWidget( stopButton_ );
    searchLineLayout->addWidget( searchInfoLine_ );

    // Table view toggle button (hidden until a Log Format is recognized)
    tableViewToggle_ = new QToolButton();
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
    tabbedFilteredView_->addTab( filteredView_, "" );
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

    // The search button row starts as the QuickFind Policy says, which the
    // Search Line was built with. Only here: a Policy arriving later leaves
    // the buttons as the user has set them.
    showSearchFlags();

    // Manually call the handler as it is not called when changing the state programmatically
    const auto startingFlags = searchLine_.flags();
    searchRefreshChangedHandler( startingFlags.autoRefresh );
    useRegexpChangeHandler( startingFlags.useRegexp );
    matchCaseChangedHandler( startingFlags.matchCase );
    booleanCombiningChangedHandler( startingFlags.booleanCombination );

    // Default splitter position (usually overridden by the config file)
    setSizes( Configuration::get().splitterSizes() );

    loadIcons();
    Theme::whenApplied( this, [ this ] {
        loadIcons();
        searchInfoLineDefaultPalette_ = palette();
        if ( searchLine_.display().isError ) {
            searchInfoLine_->setPalette( searchInfoErrorPalette() );
        }
    } );

    // Connect the signals
    connect( searchLineEdit_->lineEdit(), &QLineEdit::returnPressed, searchButton_,
             &QToolButton::click );
    connect( searchLineEdit_->lineEdit(), &QLineEdit::textEdited, this,
             &CrawlerWidget::searchTextChangeHandler );
    // Whatever changes the text -- typing, the Clear button, the history --
    // changes the Search Line's pattern.
    connect( searchLineEdit_, &QComboBox::editTextChanged, this,
             [ this ]( const QString& text ) { searchLine_.setPattern( text ); } );
    // The line starts with the latest Search of the history in it.
    searchLine_.setPattern( searchLineEdit_->currentText() );

    connect( searchLineEdit_, &QWidget::customContextMenuRequested, this,
             &CrawlerWidget::showSearchContextMenu );
    connect( saveAsPredefinedFilterAction, &QAction::triggered, this,
             &CrawlerWidget::saveAsPredefinedFilter );
    connect( clearSearchHistoryAction, &QAction::triggered, this,
             &CrawlerWidget::clearSearchHistory );
    connect( editSearchHistoryAction, &QAction::triggered, this,
             &CrawlerWidget::editSearchHistory );
    connect( searchButton_, &QToolButton::clicked, this, &CrawlerWidget::startNewSearch );
    connect( stopButton_, &QToolButton::clicked, this, &CrawlerWidget::stopSearch );
    connect( clearButton_, &QToolButton::clicked, searchLineEdit_, &QComboBox::clearEditText );

    connect( visibilityBox_, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
             &CrawlerWidget::changeFilteredViewVisibility );

    // What the user does in either Presentation
    connectPresentation( logMainView_ );
    connectPresentation( logTableView_ );

    // What only the Text View lets the user do: leave following by moving
    // away from the bottom, or start it at the bottom, and zoom with the
    // wheel. The Table View has neither, and so no such signals.
    connect( logMainView_, &LogMainView::followModeChanged, this,
             &CrawlerWidget::followModeChanged );
    connect( logMainView_, &LogMainView::changeFontSize, this, &CrawlerWidget::changeFontSize );

    // Follow option (down): the Text View follows
    connect( this, &CrawlerWidget::followSet, logMainView_, &LogMainView::followSet );

    connect( this, &CrawlerWidget::textWrapSet, logMainView_, &LogMainView::textWrapSet );

    connect( tabbedFilteredView_, &QTabWidget::currentChanged, this,
             &CrawlerWidget::changeFilteredView );

    connect( tabbedFilteredView_, &QTabWidget::tabCloseRequested, this,
             &CrawlerWidget::closeFilteredView );

    // Every Search keeps its own connection, so a notification is told
    // apart by its sender: see updateFilteredView().
    connect( openLogFile_->filteredData().get(), &LogFilteredData::searchStateChanged, this,
             &CrawlerWidget::updateFilteredView, Qt::QueuedConnection );

    // Sent load file update to MainWindow (for status update)
    connect( openLogFile_.get(), &OpenLogFile::loadingProgressed, this,
             &CrawlerWidget::loadingProgressed );
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
    connect( openLogFile_->logData().get(), &LogData::decodingPolicyChanged, this,
             &CrawlerWidget::applyDecodingPolicyChange );
    connect( openLogFile_.get(), &OpenLogFile::encodingChanged, this,
             &CrawlerWidget::applyEncodingChange );

    // Search auto-refresh
    connect( searchRefreshButton_, &QPushButton::toggled, this,
             &CrawlerWidget::searchRefreshChangedHandler );

    connect( matchCaseButton_, &QPushButton::toggled, this,
             &CrawlerWidget::matchCaseChangedHandler );

    connect( useRegexpButton_, &QPushButton::toggled, this,
             &CrawlerWidget::useRegexpChangeHandler );

    connect( booleanButton_, &QPushButton::toggled, this,
             &CrawlerWidget::booleanCombiningChangedHandler );

    // Inverting the match changes no more than the Search Line's flag.
    connect( inverseButton_, &QPushButton::toggled, this, [ this ]( bool inverse ) {
        auto flags = searchLine_.flags();
        flags.inverse = inverse;
        searchLine_.setFlags( flags );
    } );

    // Advise the parent the checkboxes have been changed
    // (for maintaining default config)
    connect( searchRefreshButton_, &QPushButton::toggled, this,
             &CrawlerWidget::searchRefreshChanged );
    connect( matchCaseButton_, &QPushButton::toggled, this, &CrawlerWidget::matchCaseChanged );

    connectAllFilteredViewSlots( filteredView_ );

    // Wire chart panel — provide log data and connect click-to-navigate.
    chartPanel_->setLogData( openLogFile_->logData() );
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
    viewSet_.addFilteredView( filteredView_ );
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
    if ( tabIndex < 0 ) {
        openLogFile_->filteredData()->stop();
    }
    else {
        auto* tabFilteredView
            = qobject_cast<FilteredView*>( tabbedFilteredView_->widget( tabIndex ) );

        filteredView_ = tabFilteredView;
        viewSet_.makeFilteredViewCurrent( filteredView_ );
        openLogFile_->makeSearchCurrent( filteredViewsData_.at( tabFilteredView ) );

        Q_EMIT filteredViewChanged();

        logMainView_->useNewFiltering( openLogFile_->filteredData().get() );
        changeFilteredViewVisibility( visibilityBox_->currentIndex() );
    }
}

void CrawlerWidget::closeFilteredView( int tabIndex )
{
    auto* tabFilteredView = tabbedFilteredView_->widget( tabIndex );
    connect( tabFilteredView, &QObject::destroyed, this, &CrawlerWidget::filteredViewDestroyed );
    tabFilteredView->deleteLater();
}

void CrawlerWidget::filteredViewDestroyed( QObject* view )
{
    filteredViewsData_.erase( qobject_cast<FilteredView*>( view ) );
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
    const auto searchText = searchLine_.pattern().trimmed();
    if ( searchText.isEmpty() ) {
        return;
    }

    // Split the search text into individual patterns.
    const auto flags = searchLine_.flags();
    QStringList patterns;
    if ( flags.booleanCombination ) {
        // Boolean mode uses "or" as separator between quoted terms.
        // Split on " or " and strip quotes.
        const auto parts = searchText.split( " or ", Qt::SkipEmptyParts );
        for ( auto part : parts ) {
            part = part.trimmed();
            if ( part.startsWith( '"' ) && part.endsWith( '"' ) ) {
                part = part.mid( 1, part.size() - 2 );
            }
            if ( !part.isEmpty() ) {
                patterns.append( part );
            }
        }
    }
    else if ( flags.useRegexp ) {
        // Regex mode: split on top-level '|' (basic heuristic).
        patterns = searchText.split( '|', Qt::SkipEmptyParts );
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

    chartPanel_->addFilterFrequencySeries( patterns );
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

    connect( this, &CrawlerWidget::followSet, view, &FilteredView::followSet );

    connect( view, &FilteredView::followModeChanged, this, &CrawlerWidget::followModeChanged );

    connect( this, &CrawlerWidget::textWrapSet, view, &FilteredView::textWrapSet );

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

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableCaseMatching, [ this ]() { matchCaseButton_->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableRegex, [ this ]() { useRegexpButton_->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableInverseMatching, [ this ]() { inverseButton_->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableRegexCombining, [ this ]() { booleanButton_->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerEnableAutoRefresh, [ this ]() { searchRefreshButton_->toggle(); } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerKeepResults, [ this ]() { keepSearchResultsButton_->toggle(); } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerChangeVisibilityBackward, [ this ]() {
                                          int nextIndex = visibilityBox_->currentIndex() - 1;
                                          if ( nextIndex < 0 ) {
                                              nextIndex = visibilityBox_->count() - 1;
                                          }
                                          visibilityBox_->setCurrentIndex( nextIndex );
                                      } );

    ShortcutAction::registerShortcut(
        configuredShortcuts, shortcuts_, this, Qt::WidgetWithChildrenShortcut,
        ShortcutAction::CrawlerChangeVisibilityToMarksAndMatches, [ this ]() {
            if ( visibilityBox_->count() > 0 ) {
                visibilityBox_->setCurrentIndex( 0 );
            }
        } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerChangeVisibilityToMarks, [ this ]() {
                                          if ( visibilityBox_->count() > 1 ) {
                                              visibilityBox_->setCurrentIndex( 1 );
                                          }
                                      } );

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this,
                                      Qt::WidgetWithChildrenShortcut,
                                      ShortcutAction::CrawlerChangeVisibilityToMatches, [ this ]() {
                                          if ( visibilityBox_->count() > 2 ) {
                                              visibilityBox_->setCurrentIndex( 2 );
                                          }
                                      } );

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
    searchRefreshButton_->setIcon( iconLoader_.loadCheckable( "icons8-search-refresh" ) );
    useRegexpButton_->setIcon( iconLoader_.loadCheckable( "regex" ) );
    inverseButton_->setIcon( iconLoader_.loadCheckable( "icons8-not-equal" ) );
    booleanButton_->setIcon( iconLoader_.loadCheckable( "icons8-venn-diagram" ) );
    clearButton_->setIcon( iconLoader_.load( "icons8-delete" ) );
    searchButton_->setIcon( iconLoader_.load( "icons8-search" ) );
    keepSearchResultsButton_->setIcon( iconLoader_.loadCheckable( "icons8-lock" ) );
    matchCaseButton_->setIcon( iconLoader_.loadCheckable( "icons8-font-size" ) );
    stopButton_->setIcon( iconLoader_.load( "icons8-close-window" ) );
    tableViewToggle_->setIcon( iconLoader_.loadCheckable( "icons8-table" ) );
}

// Create a new search from the Search Line, replace the currently
// used one and destroy the old one.
void CrawlerWidget::replaceCurrentSearch()
{
    const auto searchText = searchLine_.pattern();
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
        showSearchRequested( openLogFile_->requestSearch( searchLine_.request() ) );
    }
    else {
        searchLine_.cleared();
        showSearchLine();
    }
}

void CrawlerWidget::prepareForNewSearch()
{
    nbMatches_ = 0_lcount;

    // Switch to "Marks and matches" view when in "Marks" view
    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    if ( !filteredView_->visibility().testFlag( VisibilityFlags::Matches ) ) {
        visibilityBox_->setCurrentIndex( 0 );
    }

    viewSet_.refreshMatchesAndMarks( openLogFile_->logData()->getNbLine() );
}

void CrawlerWidget::showSearchRequested( const SearchSession::State& state )
{
    // The Search Line shows the Stop button, or the error in the expression.
    searchLine_.requested( state );
    showSearchLine();

    if ( state.phase != SearchSession::Phase::InvalidPattern ) {
        viewSet_.setSearchPattern( state.pattern );
    }
    else {
        // The regexp is wrong. The request already drove the Session to
        // InvalidPattern, which on its own clears results/Context Lines the
        // same way an idle request would -- no separate clear needed here.
        viewSet_.refreshMatchesAndMarks( openLogFile_->logData()->getNbLine() );
        viewSet_.setSearchPattern( {} );
    }
}

// Updates the content of the drop down list for the saved searches,
// called when the SavedSearch has been changed.
void CrawlerWidget::updateSearchCombo()
{
    const QString text = searchLineEdit_->lineEdit()->text();
    searchLineEdit_->clear();

    auto searchHistory = savedSearches_->recentSearches();

    searchLineEdit_->addItems( searchHistory );
    // In case we had something that wasn't added to the list (blank...):
    searchLineEdit_->lineEdit()->setText( text );

    searchLineCompleter_->setModel( new QStringListModel( searchHistory, searchLineCompleter_ ) );
}

void CrawlerWidget::printSearchInfoMessage()
{
    searchLine_.settled( openLogFile_->searchAutoRefresh().state(),
                         openLogFile_->filteredData()->getNbMatches() );
    showSearchLine();
}

void CrawlerWidget::showSearchLine()
{
    const auto display = searchLine_.display();

    const bool running = display.buttons == SearchLine::Buttons::Stop;
    stopButton_->setEnabled( running );
    stopButton_->setVisible( running );
    searchButton_->setVisible( !running );
    clearButton_->setVisible( !running );

    searchInfoLine_->setText( display.text );
    // The gauge is drawn in a palette of its own; without it, the line takes
    // the default palette or the Theme's error colors.
    if ( display.gauge ) {
        searchInfoLine_->displayGauge( *display.gauge );
    }
    else {
        searchInfoLine_->hideGauge();
        searchInfoLine_->setPalette( display.isError ? searchInfoErrorPalette()
                                                     : searchInfoLineDefaultPalette_ );
    }
    searchInfoLine_->setVisible( display.visible );
}

void CrawlerWidget::showSearchFlags()
{
    // Each button tells the Search Line of its own flag when it toggles, so
    // the flags are read once, before any button is set.
    const auto flags = searchLine_.flags();
    matchCaseButton_->setChecked( flags.matchCase );
    useRegexpButton_->setChecked( flags.useRegexp );
    inverseButton_->setChecked( flags.inverse );
    booleanButton_->setChecked( flags.booleanCombination );
    searchRefreshButton_->setChecked( flags.autoRefresh );
}

QPalette CrawlerWidget::searchInfoErrorPalette() const
{
    const Theme& theme = Theme::active();
    auto palette = searchInfoLineDefaultPalette_;
    palette.setColor( QPalette::Window, theme.color( ColorToken::ErrorBackground ) );
    palette.setColor( QPalette::WindowText, theme.color( ColorToken::ErrorText ) );
    return palette;
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
            logTableView_->updateData( openLogFile_->filteredData().get(), isFollowEnabled() );
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
        logTableView_->updateData( openLogFile_->filteredData().get(), isFollowEnabled() );
        return;
    }

    // The Table View still points at the previous Log Format until it is
    // handed the new one, so the previous one stays alive until then.
    const auto previousFormat = std::exchange( recognizedFormat_, std::move( recognized ) );
    logTableView_->setLogFormat( recognizedFormat_.get(), openLogFile_->logData().get() );
    tableViewToggle_->setVisible( true );
    tableViewToggle_->setToolTip(
        tr( "Toggle table/text view (%1)" ).arg( recognizedFormat_->title() ) );

    // Provide format info to the chart panel for template series.
    chartPanel_->setLogFormat( recognizedFormat_.get() );

    // A reload that recognized a different Log Format while the Table View
    // was shown keeps it shown, with the new columns.
    logTableView_->updateData( openLogFile_->filteredData().get(), isFollowEnabled() );
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

    // Clear format info from chart panel.
    chartPanel_->setLogFormat( nullptr );

    if ( tableViewToggle_ ) {
        tableViewToggle_->setChecked( false );
        tableViewToggle_->setVisible( false );
    }
}
