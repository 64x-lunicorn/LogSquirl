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
#include <QJsonDocument>
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
#include <QTimer>
#include <qglobal.h>
#include <qobject.h>
#include <string>

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

// Palette for error signaling (yellow background)
const QPalette CrawlerWidget::ErrorPalette( Qt::darkYellow );

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

} // namespace

// Implementation of the view context for the CrawlerWidget
class CrawlerWidgetContext : public ViewContextInterface {
public:
    // Construct from the stored string representation. A context stored
    // before the Search line's regexp type was part of one falls back to what
    // the QuickFind Policy says, which is why one is taken here.
    CrawlerWidgetContext( const QString& string, const QuickFindPolicy& quickFindPolicy );
    // Construct from the value passsed
    CrawlerWidgetContext( QList<int> sizes, bool ignoreCase, bool autoRefresh, bool followFile,
                          bool useRegexp, bool inverseRegexp, bool useBooleanCombination,
                          QList<LineNumber> markedLines, QJsonArray chartSeriesJson = {},
                          bool chartVisible = false )
        : sizes_( sizes )
        , ignoreCase_( ignoreCase )
        , autoRefresh_( autoRefresh )
        , followFile_( followFile )
        , useRegexp_( useRegexp )
        , inverseRegexp_( inverseRegexp )
        , useBooleanCombination_( useBooleanCombination )
        , chartSeriesJson_( chartSeriesJson )
        , chartVisible_( chartVisible )
    {
        std::transform( markedLines.cbegin(), markedLines.cend(), std::back_inserter( marks_ ),
                        []( const auto& m ) { return m.get(); } );
    }

    // Implementation of the ViewContextInterface function
    QString toString() const override;

    // Access the Qt sizes array for the QSplitter
    QList<int> sizes() const
    {
        return sizes_;
    }

    bool ignoreCase() const
    {
        return ignoreCase_;
    }
    bool autoRefresh() const
    {
        return autoRefresh_;
    }
    bool followFile() const
    {
        return followFile_;
    }
    bool useRegexp() const
    {
        return useRegexp_;
    }
    bool inverseRegexp() const
    {
        return inverseRegexp_;
    }
    bool useBooleanCombination() const
    {
        return useBooleanCombination_;
    }

    QList<LineNumber::UnderlyingType> marks() const
    {
        return marks_;
    }

    QJsonArray chartSeriesJson() const
    {
        return chartSeriesJson_;
    }
    bool chartVisible() const
    {
        return chartVisible_;
    }

private:
    // useRegexpByPolicy: what the QuickFind Policy says the Search line reads
    // its pattern as, used when the stored context does not say.
    void loadFromString( const QString& string, bool useRegexpByPolicy );
    void loadFromJson( const QString& json, bool useRegexpByPolicy );

private:
    QList<int> sizes_;

    bool ignoreCase_;
    bool autoRefresh_;
    bool followFile_;
    bool useRegexp_;
    bool inverseRegexp_;
    bool useBooleanCombination_;

    QList<LineNumber::UnderlyingType> marks_;
    QJsonArray chartSeriesJson_;
    bool chartVisible_ = false;
};

// Constructor only does trivial construction. The real work is done once
// the data is attached.
CrawlerWidget::CrawlerWidget( const ViewBuild& build, QWidget* parent )
    : QSplitter( parent )
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
    // The Encoding a Log File is read with by default is settled when it is
    // opened.
    fileAccessPolicy_ = build.policies.fileAccess;

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
    return encodingMib_;
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
    filteredView_->updateData();
    printSearchInfoMessage();
}

void CrawlerWidget::setEncoding( std::optional<int> mib )
{
    encodingMib_ = std::move( mib );
    updateEncoding();

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

    const auto context = CrawlerWidgetContext{ viewContext, viewSet_.quickFindPolicy() };

    setSizes( context.sizes() );
    matchCaseButton_->setChecked( !context.ignoreCase() );
    useRegexpButton_->setChecked( context.useRegexp() );
    inverseButton_->setChecked( context.inverseRegexp() );
    booleanButton_->setChecked( context.useBooleanCombination() );

    searchRefreshButton_->setChecked( context.autoRefresh() );
    // Manually call the handler as it is not called when changing the state programmatically
    searchRefreshChangedHandler( context.autoRefresh() );

    logMainView_->followSet( context.followFile() && watchPolicy_.anyWatchEnabled() );

    // Saving and restoring Marks with the Session is the user interface's;
    // when they are applied is the Open Log File's.
    const auto savedMarks = context.marks();
    logsquirl::vector<LineNumber> savedMarkedLines;
    std::transform( savedMarks.cbegin(), savedMarks.cend(), std::back_inserter( savedMarkedLines ),
                    []( const auto& l ) { return LineNumber( l ); } );
    openLogFile_->restoreMarks( savedMarkedLines );

    // Restore chart series and visibility
    const auto chartJson = context.chartSeriesJson();
    if ( !chartJson.isEmpty() ) {
        QList<ChartSeriesDefinition> defs;
        for ( const auto& val : chartJson ) {
            defs.append( ChartSeriesDefinition::fromJson( val.toObject() ) );
        }
        chartPanel_->setSeriesDefinitions( defs );
    }
    if ( context.chartVisible() ) {
        chartPanel_->show();
    }
}

std::shared_ptr<const ViewContextInterface> CrawlerWidget::doGetViewContext() const
{
    // Serialize current chart series definitions to JSON
    QJsonArray chartJson;
    for ( const auto& def : chartPanel_->seriesDefinitions() ) {
        chartJson.append( def.toJson() );
    }

    auto context = std::make_shared<const CrawlerWidgetContext>(
        sizes(), ( !matchCaseButton_->isChecked() ), searchRefreshButton_->isChecked(),
        logMainView_->isFollowEnabled(), useRegexpButton_->isChecked(), inverseButton_->isChecked(),
        booleanButton_->isChecked(), openLogFile_->filteredData()->getMarks(), chartJson,
        chartPanel_->isVisible() );

    return static_cast<std::shared_ptr<const ViewContextInterface>>( context );
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
                                     "Find \"" + searchLineEdit_->currentText() + "\"" );

    // Record the search line in the recent list
    // (reload the list first in case another glogg changed it)
    const auto& searches = SavedSearches::getSynced();
    savedSearches_->addRecent( searchLineEdit_->currentText() );
    searches.save();

    // Update the SearchLine (history)
    updateSearchCombo();
    // Call the private function to do the search
    replaceCurrentSearch( searchLineEdit_->currentText() );
}

void CrawlerWidget::stopSearch()
{
    openLogFile_->stopSearch();
    printSearchInfoMessage();

    // An interrupted run no longer reports completion (it is not one), so the
    // button/gauge cleanup that normally happens on 100% progress has to happen
    // here instead, immediately, rather than waiting on a signal that won't come.
    searchInfoLine_->hideGauge();
    stopButton_->setEnabled( false );
    stopButton_->hide();
    searchButton_->show();
    clearButton_->show();
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
    const auto currentText = searchLineEdit_->currentText();

    Q_EMIT saveCurrentSearchAsPredefinedFilter( currentText );
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
    // (searchInfoLine_, stopButton_, ...), a stale notification from a
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
    const auto progress = state.progress;
    const bool isComplete = ( state.phase == SearchSession::Phase::Complete );
    const bool isFailed = ( state.phase == SearchSession::Phase::Failed );
    const bool isDone = isComplete || isFailed || state.phase == SearchSession::Phase::Interrupted
                        || state.phase == SearchSession::Phase::InvalidPattern;

    searchInfoLine_->show();

    if ( isDone ) {
        // Searching done, one way or another. Only a real completion and a
        // failure, which the engine reports only here, get their message
        // from here -- Interrupted/InvalidPattern already had
        // theirs set by whoever drove the Session into that phase
        // (stopSearch(), replaceCurrentSearch()'s error path), and
        // re-deriving one from a bare phase here would just guess.
        if ( isComplete ) {
            printSearchInfoMessage( nbMatches );
        }
        else if ( isFailed ) {
            searchInfoLine_->setPalette( ErrorPalette );
            searchInfoLine_->setText( tr( "Search failed" ) );
            offerIssueReport( state.errorString );
        }
        searchInfoLine_->hideGauge();
        // De-activate the stop button
        stopButton_->setEnabled( false );
        stopButton_->hide();
        searchButton_->show();
        clearButton_->show();
    }
    else if ( state.phase == SearchSession::Phase::Running ) {
        // Search in progress
        // We ignore 0% and 100% to avoid a flash when the search is very short
        if ( progress > 0 ) {
            // Some languages translate the plural the same as the singular, so use the full string

            searchInfoLine_->setText(
                tr( "Search in progress (%1 %)..." ).arg( QString::number( progress ) )
                + ( nbMatches.get() > 1 ? tr( " %1 matches found so far." )
                                              .arg( QString::number( nbMatches.get() ) )
                                        : tr( " %1 match found so far." )
                                              .arg( QString::number( nbMatches.get() ) ) ) );

            searchInfoLine_->displayGauge( progress );
        }
    }

    // If more (or less, e.g. come back to 0) matches have been found
    if ( nbMatches != nbMatches_ ) {
        nbMatches_ = nbMatches;

        // Recompute the content of the filtered window.
        filteredView_->updateData();

        // Update the match overview
        overview_.updateData( openLogFile_->logData()->getNbLine() );

        // New data found icon: fires for a continuation (autorefresh
        // extending the range) and equally for a fresh search whose
        // range starts past the beginning of the file (Search Limits),
        // matching what a non-zero initialLine used to signal before the
        // Search Session existed.
        if ( state.isContinuation || state.startLine > 0_lnum ) {
            changeDataStatus( DataStatus::NEW_FILTERED_DATA );
        }

        // Also update the Presentations for the colored bullets.
        update();
        for ( auto* presentation : presentations() ) {
            presentation->updateDecorations();
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

void CrawlerWidget::updateLineNumberHandler( LineNumber line, LinesCount nLines,
                                             LineColumn startCol, LineLength nSymbols )
{
    // A Presentation not shown follows the one shown, and reports nothing
    // of its own.
    const auto* reporter = dynamic_cast<const LogPresentation*>( sender() );
    if ( reporter != nullptr && reporter != presentation_ ) {
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
    if ( reporter != nullptr && reporter == logTableView_ && !syncingSelection_
         && openLogFile_->filteredData() && openLogFile_->filteredData()->getNbLine().get() > 0 ) {
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

    // Recompute the content of the filtered window.
    filteredView_->updateData();

    // Update the match overview
    overview_.updateData( openLogFile_->logData()->getNbLine() );

    // Also update the Presentations for the colored bullets.
    update();
    for ( auto* presentation : presentations() ) {
        presentation->updateDecorations();
    }
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

    // Set the encoding for the views
    updateEncoding();

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
        filteredView_->updateData();
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
    printSearchInfoMessage( openLogFile_->filteredData()->getNbMatches() );
}

void CrawlerWidget::searchRefreshChangedHandler( bool isRefreshing )
{
    openLogFile_->setAutoRefresh( isRefreshing );
    printSearchInfoMessage( openLogFile_->filteredData()->getNbMatches() );
}

void CrawlerWidget::matchCaseChangedHandler( bool shouldMatchCase )
{
    searchLineCompleter_->setCaseSensitivity( shouldMatchCase ? Qt::CaseSensitive
                                                              : Qt::CaseInsensitive );

    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::booleanCombiningChangedHandler( bool )
{
    resetStateOnSearchPatternChanges();
}

void CrawlerWidget::useRegexpChangeHandler( bool )
{
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
    QString searchPattern;
    for ( const auto& filter : filters ) {
        combinePatterns( searchPattern, escapeSearchPattern( filter.pattern, filter.useRegex ) );
    }
    setSearchPattern( searchPattern );
}

QString CrawlerWidget::escapeSearchPattern( const QString& pattern, bool isRegex ) const
{
    auto escapedPattern = ( !isRegex && useRegexpButton_->isChecked() )
                              ? QRegularExpression::escape( pattern )
                              : pattern;

    if ( booleanButton_->isChecked() ) {
        escapedPattern.replace( '"', "\"" ).prepend( '"' ).append( '"' );
    }

    return escapedPattern;
}

QString& CrawlerWidget::combinePatterns( QString& currentPattern, const QString& newPattern ) const
{
    if ( !currentPattern.isEmpty() ) {
        if ( booleanButton_->isChecked() ) {
            currentPattern.append( " or " );
        }
        else if ( useRegexpButton_->isChecked() ) {
            currentPattern.append( '|' );
        }
    }

    currentPattern.append( newPattern );

    return currentPattern;
}

void CrawlerWidget::addToSearch( const QString& searchString )
{
    const auto newPattern = escapeSearchPattern( searchString );
    QString currentPattern = searchLineEdit_->currentText();
    setSearchPattern( combinePatterns( currentPattern, newPattern ) );
}

void CrawlerWidget::excludeFromSearch( const QString& searchString )
{
    QString currentPattern = searchLineEdit_->currentText();

    const auto wasInBooleanCombinationMode = booleanButton_->isChecked();
    if ( !wasInBooleanCombinationMode ) {
        currentPattern.replace( '"', "\"" ).prepend( '"' ).append( '"' );
    }

    booleanButton_->setChecked( true );

    const auto newPattern = escapeSearchPattern( searchString );

    if ( !currentPattern.isEmpty() ) {
        currentPattern.append( " and " );
    }

    currentPattern.append( "not(" ).append( newPattern ).append( ')' );
    setSearchPattern( currentPattern );
}

void CrawlerWidget::replaceSearch( const QString& searchString )
{
    setSearchPattern( escapeSearchPattern( searchString ) );
}

void CrawlerWidget::setSearchPattern( const QString& searchPattern )
{
    searchLineEdit_->setEditText( searchPattern );
    // Set the focus to lineEdit so that the user can press 'Return' immediately
    searchLineEdit_->lineEdit()->setFocus();

    if ( viewSet_.quickFindPolicy().autoRunSearchOnPatternChange ) {
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

    visibilityBox_ = new QComboBox();
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
    searchInfoLine_->setSizePolicy( QSizePolicy::Minimum, QSizePolicy::Minimum );
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
    tableViewToggle_->setIcon( iconLoader_.load( "icons8-table" ) );
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

    // The search button row starts as the QuickFind Policy says. Only here:
    // a Policy arriving later leaves the buttons as the user has set them.
    searchRefreshButton_->setChecked( viewSet_.quickFindPolicy().searchAutoRefreshDefault );
    matchCaseButton_->setChecked( !viewSet_.quickFindPolicy().searchIgnoreCaseDefault );
    useRegexpButton_->setChecked( viewSet_.quickFindPolicy().mainRegexpType
                                  == SearchRegexpType::ExtendedRegexp );
    booleanButton_->setChecked( viewSet_.quickFindPolicy().searchLogicalCombiningDefault );

    // Manually call the handler as it is not called when changing the state programmatically
    searchRefreshChangedHandler( searchRefreshButton_->isChecked() );
    useRegexpChangeHandler( useRegexpButton_->isChecked() );
    matchCaseChangedHandler( matchCaseButton_->isChecked() );
    booleanCombiningChangedHandler( booleanButton_->isChecked() );

    // Default splitter position (usually overridden by the config file)
    setSizes( Configuration::get().splitterSizes() );

    loadIcons();
    Theme::whenApplied( this, [ this ] {
        loadIcons();
        searchInfoLineDefaultPalette_ = palette();
    } );

    // Connect the signals
    connect( searchLineEdit_->lineEdit(), &QLineEdit::returnPressed, searchButton_,
             &QToolButton::click );
    connect( searchLineEdit_->lineEdit(), &QLineEdit::textEdited, this,
             &CrawlerWidget::searchTextChangeHandler );

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

    // Search auto-refresh
    connect( searchRefreshButton_, &QPushButton::toggled, this,
             &CrawlerWidget::searchRefreshChangedHandler );

    connect( matchCaseButton_, &QPushButton::toggled, this,
             &CrawlerWidget::matchCaseChangedHandler );

    connect( useRegexpButton_, &QPushButton::toggled, this,
             &CrawlerWidget::useRegexpChangeHandler );

    connect( booleanButton_, &QPushButton::toggled, this,
             &CrawlerWidget::booleanCombiningChangedHandler );

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

    // Refresh chart data when the file finishes loading.
    connect( openLogFile_.get(), &OpenLogFile::loadingFinished, this, [ this ]( const auto& ) {
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

    // Once every view is in the View Set, which registers theirs too.
    registerShortcuts();

    const auto defaultEncodingMib = fileAccessPolicy_.defaultEncodingMib;
    if ( defaultEncodingMib >= 0 ) {
        encodingMib_ = defaultEncodingMib;
    }
}

template <class Presentation>
void CrawlerWidget::connectPresentation( Presentation* presentation )
{
    connect( presentation, &Presentation::newSelection, presentation,
             [ presentation ]() { presentation->update(); } );

    connect( presentation, &Presentation::newSelection, this,
             &CrawlerWidget::updateLineNumberHandler );

    connect( presentation, &Presentation::markLines, this, &CrawlerWidget::markLinesFromMain );

    // A Highlighter Set ticked in a view's menu reaches every open Log File.
    connect( presentation, &Presentation::highlightersChange, this,
             [ this ]() { reportChange( Changed::HighlighterSets ); } );

    connect( presentation, QOverload<const QString&>::of( &Presentation::addToSearch ), this,
             &CrawlerWidget::addToSearch );

    connect( presentation, QOverload<const QString&>::of( &Presentation::excludeFromSearch ), this,
             &CrawlerWidget::excludeFromSearch );

    connect( presentation, QOverload<const QString&>::of( &Presentation::replaceSearch ), this,
             &CrawlerWidget::replaceSearch );

    // Detect activity in the views
    connect( presentation, &Presentation::activity, this, &CrawlerWidget::activityDetected );

    connect( presentation, &Presentation::changeSearchLimits, this,
             &CrawlerWidget::setSearchLimits );

    connect( presentation, &Presentation::clearSearchLimits, this,
             &CrawlerWidget::clearSearchLimits );

    connect( presentation, &Presentation::saveDefaultSplitterSizes, this,
             &CrawlerWidget::saveSplitterSizes );

    connect( presentation, &Presentation::clearColorLabels, this,
             &CrawlerWidget::clearColorLabels );

    connect( presentation, &Presentation::addColorLabel, this,
             &CrawlerWidget::addColorLabelToSelection );

    connect( presentation, &Presentation::sendSelectionToScratchpad, this,
             [ this ]() { Q_EMIT sendToScratchpad( presentation_->selectedText() ); } );

    connect( presentation, &Presentation::replaceScratchpadWithSelection, this,
             [ this ]() { Q_EMIT replaceDataInScratchpad( presentation_->selectedText() ); } );
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
    const auto searchText = searchLineEdit_->currentText().trimmed();
    if ( searchText.isEmpty() ) {
        return;
    }

    // Split the search text into individual patterns.
    QStringList patterns;
    if ( booleanButton_->isChecked() ) {
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
    else if ( useRegexpButton_->isChecked() ) {
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

    auto fontInfo = QFontInfo( fontConfig.mainFont() );
    const auto availableSizes = FontUtils::availableFontSizes( fontInfo.family() );

    auto currentSize
        = std::find( availableSizes.cbegin(), availableSizes.cend(), fontInfo.pointSize() );
    if ( increase && currentSize != std::prev( availableSizes.cend() ) ) {
        currentSize = std::next( currentSize );
    }
    else if ( !increase && currentSize != availableSizes.begin() ) {
        currentSize = std::prev( currentSize );
    }

    if ( currentSize != availableSizes.cend() ) {
        fontConfig.setMainFont( QFont{ fontInfo.family(), *currentSize } );
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

    // The Filtered View hands out Log Lines, as the main view does.
    connect( view, &FilteredView::markLines, this, &CrawlerWidget::markLinesFromMain );

    connect( view, &FilteredView::highlightersChange, this,
             [ this ]() { reportChange( Changed::HighlighterSets ); } );

    connect( view, QOverload<const QString&>::of( &FilteredView::addToSearch ), this,
             &CrawlerWidget::addToSearch );

    connect( view, QOverload<const QString&>::of( &FilteredView::excludeFromSearch ), this,
             &CrawlerWidget::excludeFromSearch );

    connect( view, QOverload<const QString&>::of( &FilteredView::replaceSearch ), this,
             &CrawlerWidget::replaceSearch );

    connect( view, &FilteredView::mouseHoveredOverLine, this,
             &CrawlerWidget::mouseHoveredOverMatch );

    connect( view, &FilteredView::mouseLeftHoveringZone, overviewWidget_,
             &OverviewWidget::removeHighlight );

    connect( view, &FilteredView::mouseLeftHoveringZone, logTableView_,
             &LogTableView::removeOverviewHighlight );

    connect( this, &CrawlerWidget::followSet, view, &FilteredView::followSet );

    connect( view, &FilteredView::followModeChanged, this, &CrawlerWidget::followModeChanged );

    connect( this, &CrawlerWidget::textWrapSet, view, &FilteredView::textWrapSet );

    connect( view, &FilteredView::activity, this, &CrawlerWidget::activityDetected );

    connect( view, &FilteredView::changeSearchLimits, this, &CrawlerWidget::setSearchLimits );

    connect( view, &FilteredView::saveDefaultSplitterSizes, this,
             &CrawlerWidget::saveSplitterSizes );

    connect( view, &FilteredView::changeFontSize, this, &CrawlerWidget::changeFontSize );

    connect( view, &FilteredView::clearSearchLimits, this, &CrawlerWidget::clearSearchLimits );

    connect( view, &AbstractLogView::addColorLabel, this,
             &CrawlerWidget::addColorLabelToSelection );

    connect( view, &AbstractLogView::sendSelectionToScratchpad, this,
             [ view, this ]() { Q_EMIT sendToScratchpad( view->getSelectedText() ); } );

    connect( view, &AbstractLogView::replaceScratchpadWithSelection, this,
             [ view, this ]() { Q_EMIT replaceDataInScratchpad( view->getSelectedText() ); } );

    connect( view, &FilteredView::exitView, logMainView_,
             QOverload<>::of( &LogMainView::setFocus ) );

    connect( view, &AbstractLogView::clearColorLabels, this, &CrawlerWidget::clearColorLabels );

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
    searchRefreshButton_->setIcon( iconLoader_.load( "icons8-search-refresh" ) );
    useRegexpButton_->setIcon( iconLoader_.load( "regex" ) );
    inverseButton_->setIcon( iconLoader_.load( "icons8-not-equal" ) );
    booleanButton_->setIcon( iconLoader_.load( "icons8-venn-diagram" ) );
    clearButton_->setIcon( iconLoader_.load( "icons8-delete" ) );
    searchButton_->setIcon( iconLoader_.load( "icons8-search" ) );
    keepSearchResultsButton_->setIcon( iconLoader_.load( "icons8-lock" ) );
    matchCaseButton_->setIcon( iconLoader_.load( "icons8-font-size" ) );
    stopButton_->setIcon( iconLoader_.load( "icons8-close-window" ) );
}

// Create a new search using the text passed, replace the currently
// used one and destroy the old one.
void CrawlerWidget::replaceCurrentSearch( const QString& searchText )
{
    LOG_INFO << "replacing current search with " << searchText;

    // The request supersedes whatever search is in flight: any of its
    // results still arriving after this point carry its (now stale) id and
    // are discarded on arrival, so there is nothing to wait for here.

    // Clear and recompute the content of the filtered window.
    openLogFile_->clearSearch();
    prepareForNewSearch();

    if ( !searchText.isEmpty() ) {

        // Constructs the regexp
        auto regexpPattern = RegularExpressionPattern(
            searchText, matchCaseButton_->isChecked(), inverseButton_->isChecked(),
            booleanButton_->isChecked(), !useRegexpButton_->isChecked() );

        // Start a new asynchronous search over the Search Limits -- the
        // Session validates the pattern itself; on failure it goes to
        // InvalidPattern synchronously (without touching the worker), so the
        // state is already conclusive.
        showSearchRequested( openLogFile_->requestSearch( regexpPattern ) );
    }
    else {
        printSearchInfoMessage();
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

    filteredView_->updateData();

    // Update the match overview
    overview_.updateData( openLogFile_->logData()->getNbLine() );
}

void CrawlerWidget::showSearchRequested( const SearchSession::State& state )
{
    if ( state.phase != SearchSession::Phase::InvalidPattern ) {
        // Activate the stop button
        stopButton_->setEnabled( true );
        stopButton_->show();
        clearButton_->hide();
        searchButton_->hide();
        searchInfoLine_->hide();
        logMainView_->setSearchPattern( state.pattern );
        filteredView_->setSearchPattern( state.pattern );
        logTableView_->setSearchPattern( state.pattern );
    }
    else {
        // The regexp is wrong. The request already drove the Session to
        // InvalidPattern, which on its own clears results/Context Lines the
        // same way an idle request would -- no separate clear needed here.
        filteredView_->updateData();

        // Inform the user
        QString errorMessage = tr( "Error in expression" );
        errorMessage += ": ";
        errorMessage += state.errorString;
        searchInfoLine_->setPalette( ErrorPalette );
        searchInfoLine_->setText( errorMessage );
        searchInfoLine_->show();

        logMainView_->setSearchPattern( {} );
        filteredView_->setSearchPattern( {} );
        logTableView_->setSearchPattern( {} );
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

// Print the search info message.
void CrawlerWidget::printSearchInfoMessage( LinesCount nbMatches )
{
    QString text;

    using State = SearchAutoRefresh::State;
    switch ( openLogFile_->searchAutoRefresh().state() ) {
    case State::NoSearch:
        // Blank text is fine
        break;
    case State::Static:
    case State::Autorefreshing:
        // Some languages translate the plural the same as the singular, so use the full string
        text = nbMatches.get() > 1 ? tr( "%1 matches found" ).arg( nbMatches.get() )
                                   : tr( "%1 match found" ).arg( nbMatches.get() );
        break;
    case State::FileTruncated:
    case State::TruncatedAutorefreshing:
        text = tr( "File truncated on disk" );
        break;
    }

    searchInfoLine_->setPalette( searchInfoLineDefaultPalette_ );
    searchInfoLine_->setText( text );
    searchInfoLine_->setVisible( !text.isEmpty() );
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

// Determine the right encoding and set the views.
void CrawlerWidget::updateEncoding()
{
    const QTextCodec* textCodec = [ this ]() {
        QTextCodec* codec = nullptr;
        if ( !encodingMib_ ) {
            codec = openLogFile_->logData()->getDetectedEncoding();
        }
        else {
            codec = QTextCodec::codecForMib( *encodingMib_ );
        }
        return codec ? codec : QTextCodec::codecForLocale();
    }();

    QString encodingPrefix = encodingMib_ ? tr( "Displayed as %1" ) : tr( "Detected as %1" );
    encodingText_ = encodingPrefix.arg( textCodec->name().constData() );

    openLogFile_->logData()->interruptLoading();

    openLogFile_->logData()->setDisplayEncoding( textCodec->name().constData() );
    logMainView_->rereadLogLines();
    openLogFile_->filteredData()->setDisplayEncoding( textCodec->name().constData() );
    filteredView_->rereadLogLines();
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

/*
 * CrawlerWidgetContext
 */
CrawlerWidgetContext::CrawlerWidgetContext( const QString& string,
                                            const QuickFindPolicy& quickFindPolicy )
{
    const auto useRegexpByPolicy
        = quickFindPolicy.mainRegexpType == SearchRegexpType::ExtendedRegexp;

    if ( string.startsWith( '{' ) ) {
        loadFromJson( string, useRegexpByPolicy );
    }
    else {
        loadFromString( string, useRegexpByPolicy );
    }
}

void CrawlerWidgetContext::loadFromString( const QString& string, bool useRegexpByPolicy )
{
    QRegularExpression regex( "S(\\d+):(\\d+)" );
    QRegularExpressionMatch match = regex.match( string );
    if ( match.hasMatch() ) {
        sizes_ = { match.captured( 1 ).toInt(), match.captured( 2 ).toInt() };
        LOG_DEBUG << "sizes_: " << sizes_[ 0 ] << " " << sizes_[ 1 ];
    }
    else {
        LOG_WARNING << "Unrecognised view size: " << string.toLocal8Bit().data();

        // Default values;
        sizes_ = { 400, 100 };
    }

    QRegularExpression case_refresh_regex( "IC(\\d+):AR(\\d+)" );
    match = case_refresh_regex.match( string );
    if ( match.hasMatch() ) {
        ignoreCase_ = ( match.captured( 1 ).toInt() == 1 );
        autoRefresh_ = ( match.captured( 2 ).toInt() == 1 );

        LOG_DEBUG << "ignore_case_: " << ignoreCase_ << " auto_refresh_: " << autoRefresh_;
    }
    else {
        LOG_WARNING << "Unrecognised case/refresh: " << string.toLocal8Bit().data();
        ignoreCase_ = false;
        autoRefresh_ = false;
    }

    QRegularExpression follow_regex( "AR(\\d+):FF(\\d+)" );
    match = follow_regex.match( string );
    if ( match.hasMatch() ) {
        followFile_ = ( match.captured( 2 ).toInt() == 1 );

        LOG_DEBUG << "follow_file_: " << followFile_;
    }
    else {
        LOG_WARNING << "Unrecognised follow file " << string.toLocal8Bit().data();
        followFile_ = false;
    }

    useRegexp_ = useRegexpByPolicy;
}

void CrawlerWidgetContext::loadFromJson( const QString& json, bool useRegexpByPolicy )
{
    const auto properties = QJsonDocument::fromJson( json.toLatin1() ).toVariant().toMap();

    if ( properties.contains( "S" ) ) {
        const auto sizes = properties.value( "S" ).toList();
        for ( const auto& s : sizes ) {
            sizes_.append( s.toInt() );
        }
    }

    ignoreCase_ = properties.value( "IC" ).toBool();
    autoRefresh_ = properties.value( "AR" ).toBool();
    followFile_ = properties.value( "FF" ).toBool();
    if ( properties.contains( "RE" ) ) {
        useRegexp_ = properties.value( "RE" ).toBool();
    }
    else {
        useRegexp_ = useRegexpByPolicy;
    }

    if ( properties.contains( "IR" ) ) {
        inverseRegexp_ = properties.value( "IR" ).toBool();
    }
    else {
        inverseRegexp_ = false;
    }

    if ( properties.contains( "BC" ) ) {
        useBooleanCombination_ = properties.value( "BC" ).toBool();
    }
    else {
        useBooleanCombination_ = false;
    }

    if ( properties.contains( "M" ) ) {
        const auto marks = properties.value( "M" ).toList();
        for ( const auto& m : marks ) {
            marks_.append( m.toUInt() );
        }
    }

    if ( properties.contains( "CS" ) ) {
        chartSeriesJson_
            = QJsonDocument::fromJson( properties.value( "CS" ).toString().toUtf8() ).array();
    }
    chartVisible_ = properties.value( "CV" ).toBool();
}

QString CrawlerWidgetContext::toString() const
{
    const auto toVariantList = []( const auto& list ) -> QVariantList {
        QVariantList variantList;
        for ( const auto& item : list ) {
            variantList.append( static_cast<qulonglong>( item ) );
        }
        return variantList;
    };

    QVariantMap properies;

    properies[ "S" ] = toVariantList( sizes_ );
    properies[ "IC" ] = ignoreCase_;
    properies[ "AR" ] = autoRefresh_;
    properies[ "FF" ] = followFile_;
    properies[ "RE" ] = useRegexp_;
    properies[ "IR" ] = inverseRegexp_;
    properies[ "BC" ] = useBooleanCombination_;
    properies[ "M" ] = toVariantList( marks_ );

    if ( !chartSeriesJson_.isEmpty() ) {
        properies[ "CS" ] = QString::fromUtf8(
            QJsonDocument( chartSeriesJson_ ).toJson( QJsonDocument::Compact ) );
    }
    properies[ "CV" ] = chartVisible_;

    return QJsonDocument::fromVariant( properies ).toJson( QJsonDocument::Compact );
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
