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
#include "logformatdefinition.h"
#include "logformatmatcher.h"
#include "logformatregistry.h"
#include "logformattablemodel.h"
#include "logtablehighlightdelegate.h"
#include "overviewwidget.h"
#include "quickfindpattern.h"
#include "savedsearches.h"
#include "shortcuts.h"

static constexpr char AnsiColorSequenceRegex[] = "\\x1B\\[([0-9]{1,4}((;|:)[0-9]{1,3})*)?[mK]";

// Palette for error signaling (yellow background)
const QPalette CrawlerWidget::ErrorPalette( Qt::darkYellow );

// Implementation of the view context for the CrawlerWidget
class CrawlerWidgetContext : public ViewContextInterface {
public:
    // Construct from the stored string representation
    explicit CrawlerWidgetContext( const QString& string );
    // Construct from the value passsed
    CrawlerWidgetContext( QList<int> sizes, bool ignoreCase, bool autoRefresh, bool followFile,
                          bool useRegexp, bool inverseRegexp, bool useBooleanCombination,
                          QList<LineNumber> markedLines,
                          QJsonArray chartSeriesJson = {}, bool chartVisible = false )
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
    void loadFromString( const QString& string );
    void loadFromJson( const QString& json );

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
CrawlerWidget::CrawlerWidget( QWidget* parent )
    : QSplitter( parent )
    , iconLoader_{ this }
{
}

// The top line is first one on the main display
LineNumber CrawlerWidget::getTopLine() const
{
    return logMainView_->getTopLine();
}

QString CrawlerWidget::getSelectedText() const
{
    // Table view with active portion selection
    if ( tableViewActive_ && tableCellSelection_.active && tableModel_ ) {
        const auto index = tableModel_->index( tableCellSelection_.row,
                                               tableCellSelection_.column );
        const auto cellText = index.data( Qt::DisplayRole ).toString();
        const auto selected = tableCellSelection_.selectedText( cellText );
        if ( !selected.isEmpty() ) {
            return selected;
        }
    }

    if ( filteredView_->hasFocus() )
        return filteredView_->getSelectedText();
    else
        return logMainView_->getSelectedText();
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

void CrawlerWidget::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::StyleChange ) {
        dispatchToMainThread( [ this ] {
            loadIcons();
            searchInfoLineDefaultPalette_ = this->palette();
        } );
    }

    QWidget::changeEvent( event );
}

//
// Public Q_SLOTS:
//

void CrawlerWidget::stopLoading()
{
    logFilteredData_->interruptSearch();
    logData_->interruptLoading();
}

void CrawlerWidget::reload()
{
    searchState_.resetState();
    constexpr auto DropCache = true;
    logFilteredData_->clearSearch( DropCache );
    logFilteredData_->clearMarks();
    filteredView_->updateData();
    printSearchInfoMessage();

    logData_->reload();

    // A reload is considered as a first load,
    // this is to prevent the "new data" icon to be triggered.
    firstLoadDone_ = false;
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
        filteredView_->trySelectLine( logFilteredData_->getLineIndexNumber( selectedLine ) );
        logMainView_->trySelectLine( selectedLine );
    }
}

//
// Protected functions
//
void CrawlerWidget::doSetData( std::shared_ptr<LogData> logData,
                               std::shared_ptr<LogFilteredData> filteredData )
{
    logData_ = std::move( logData );
    logFilteredData_ = std::move( filteredData );
}

void CrawlerWidget::doSetQuickFindPattern( std::shared_ptr<QuickFindPattern> qfp )
{
    quickFindPattern_ = std::move( qfp );
}

void CrawlerWidget::doSetSavedSearches( SavedSearches* saved_searches )
{
    savedSearches_ = saved_searches;

    // We do setup now, assuming doSetData has been called before
    // us, that's not great really...
    setup();
}

void CrawlerWidget::doSetViewContext( const QString& view_context )
{
    LOG_DEBUG << "CrawlerWidget::doSetViewContext: " << view_context.toLocal8Bit().data();

    const auto context = CrawlerWidgetContext{ view_context };

    setSizes( context.sizes() );
    matchCaseButton_->setChecked( !context.ignoreCase() );
    useRegexpButton_->setChecked( context.useRegexp() );
    inverseButton_->setChecked( context.inverseRegexp() );
    booleanButton_->setChecked( context.useBooleanCombination() );

    searchRefreshButton_->setChecked( context.autoRefresh() );
    // Manually call the handler as it is not called when changing the state programmatically
    searchRefreshChangedHandler( context.autoRefresh() );

    const auto& config = Configuration::get();
    logMainView_->followSet( context.followFile() && config.anyFileWatchEnabled() );

    const auto savedMarks = context.marks();
    std::transform( savedMarks.cbegin(), savedMarks.cend(), std::back_inserter( savedMarkedLines_ ),
                    []( const auto& l ) { return LineNumber( l ); } );

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
        booleanButton_->isChecked(), logFilteredData_->getMarks(),
        chartJson, chartPanel_->isVisible() );

    return static_cast<std::shared_ptr<const ViewContextInterface>>( context );
}

//
// Q_SLOTS:
//

void CrawlerWidget::startNewSearch()
{
    if ( keepSearchResultsButton_->isChecked() ) {
        keepSearchResultsButton_->setChecked( false );

        logFilteredData_->interruptSearch();
        logFilteredData_ = logData_->getNewFilteredData();

        filteredView_ = new FilteredView( logFilteredData_.get(), quickFindPattern_.get() );
        filteredViewsData_[ filteredView_ ] = logFilteredData_;

        connectAllFilteredViewSlots( filteredView_ );

        auto index = tabbedFilteredView_->addTab( filteredView_, "" );
        tabbedFilteredView_->setCurrentIndex( index );

        connect( logFilteredData_.get(), &LogFilteredData::searchProgressed, this,
                 &CrawlerWidget::updateFilteredView, Qt::QueuedConnection );

        logMainView_->useNewFiltering( logFilteredData_.get() );

        applyConfiguration();
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
    logFilteredData_->interruptSearch();
    searchState_.stopSearch();
    printSearchInfoMessage();
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

// When receiving the 'newDataAvailable' signal from LogFilteredData
void CrawlerWidget::updateFilteredView( LinesCount nbMatches, int progress,
                                        LineNumber initialPosition )
{
    LOG_DEBUG << "updateFilteredView received.";

    searchInfoLine_->show();

    if ( progress == 100 ) {
        // Searching done
        printSearchInfoMessage( nbMatches );
        searchInfoLine_->hideGauge();
        // De-activate the stop button
        stopButton_->setEnabled( false );
        stopButton_->hide();
        searchButton_->show();
        clearButton_->show();
    }
    else {
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
        overview_.updateData( logData_->getNbLine() );

        // New data found icon
        if ( initialPosition > 0_lnum ) {
            changeDataStatus( DataStatus::NEW_FILTERED_DATA );
        }

        // Also update the top window for the coloured bullets.
        update();

        // Repaint the table view so match/mark highlights are updated
        if ( logTableView_ && tableViewActive_ ) {
            logTableView_->viewport()->update();
            updateTableOverview();
        }
    }

    // Try to restore the filtered window selection close to where it was
    // only for full searches to avoid disconnecting follow mode!
    if ( ( progress == 100 ) && ( initialPosition == searchStartLine_ )
         && ( !isFollowEnabled() ) ) {
        const auto currenLineIndex = logFilteredData_->getLineIndexNumber( currentLineNumber_ );
        LOG_DEBUG << "updateFilteredView: restoring selection: "
                  << " absolute line number (0based) " << currentLineNumber_ << " index "
                  << currenLineIndex;
        filteredView_->selectAndDisplayLine( currenLineIndex );
        filteredView_->setSearchLimits( searchStartLine_, searchEndLine_ );
    }
}

void CrawlerWidget::jumpToMatchingLine( LineNumber filteredLineNb, LinesCount nLines,
                                        LineColumn startCol, LineLength nSymbols )
{
    const auto mainViewLine = logFilteredData_->getMatchingLineNumber( filteredLineNb );
    logMainView_->selectPortionAndDisplayLine( mainViewLine, nLines, startCol,
                                               nSymbols ); // FIXME: should be done with a signal.

    // Also scroll the table view to the matching row when it is active
    if ( logTableView_ && tableViewActive_ && tableModel_ ) {
        const auto row = static_cast<int>( mainViewLine.get() );
        const auto idx = tableModel_->index( row, 0 );
        logTableView_->scrollTo( idx, QAbstractItemView::PositionAtCenter );
        logTableView_->selectRow( row );
    }
}

void CrawlerWidget::updateLineNumberHandler( LineNumber line, LinesCount nLines,
                                             LineColumn startCol, LineLength nSymbols )
{
    currentLineNumber_ = line;
    Q_EMIT newSelection( line, nLines, startCol, nSymbols );
}

void CrawlerWidget::markLinesFromMain( const logsquirl::vector<LineNumber>& lines )
{
    logsquirl::vector<LineNumber> alreadyMarkedLines;
    alreadyMarkedLines.reserve( lines.size() );

    bool markAdded = false;
    for ( const auto& line : lines ) {
        if ( line >= logData_->getNbLine() ) {
            continue;
        }

        if ( !logFilteredData_->lineTypeByLine( line ).testFlag(
                 AbstractLogData::LineTypeFlags::Mark ) ) {
            logFilteredData_->addMark( line );
            markAdded = true;
        }
        else {
            alreadyMarkedLines.push_back( line );
        }
    }

    if ( !markAdded ) {
        for ( const auto& line : alreadyMarkedLines ) {
            logFilteredData_->toggleMark( line );
        }
    }

    // Recompute the content of both window.
    filteredView_->updateData();
    logMainView_->updateData();

    // Update the match overview
    overview_.updateData( logData_->getNbLine() );

    // Also update the top window for the coloured bullets.
    update();

    // Repaint the table view so mark highlights are updated
    if ( logTableView_ && tableViewActive_ ) {
        logTableView_->viewport()->update();
        updateTableOverview();
    }
}

void CrawlerWidget::markLinesFromFiltered( const logsquirl::vector<LineNumber>& lines )
{
    logsquirl::vector<LineNumber> linesInMain( lines.size() );
    std::transform( lines.cbegin(), lines.cend(), linesInMain.begin(),
                    [ this ]( const auto& filteredLine ) {
                        if ( filteredLine < logData_->getNbLine() ) {
                            return logFilteredData_->getMatchingLineNumber( filteredLine );
                        }
                        else {
                            return maxValue<LineNumber>();
                        }
                    } );

    markLinesFromMain( linesInMain );
}

void CrawlerWidget::applyConfiguration()
{
    const auto& config = Configuration::get();
    QFont font = config.mainFont();

    LOG_DEBUG << "CrawlerWidget::applyConfiguration";

    registerShortcuts();

    // Whatever font we use, we should NOT use kerning
    font.setKerning( false );
    font.setFixedPitch( true );

    // Necessary on systems doing subpixel positionning (e.g. Ubuntu 12.04)
    if ( config.forceFontAntialiasing() ) {
        font.setStyleStrategy( QFont::PreferAntialias );
    }

    font.setBold( config.useBoldFont() );

    if ( config.hideAnsiColorSequences() ) {
        logData_->setPrefilter( AnsiColorSequenceRegex );
    }
    else {
        logData_->setPrefilter( {} );
    }

    logMainView_->setLineNumbersVisible( config.mainLineNumbersVisible() );

    const auto isFollowModeAllowed = config.anyFileWatchEnabled();
    logMainView_->allowFollowMode( isFollowModeAllowed );
    overview_.setVisible( config.isOverviewVisible() );
    logMainView_->refreshOverview();
    logMainView_->updateFont( font );

    // Apply the same font to the table view
    if ( logTableView_ ) {
        logTableView_->setFont( font );
        // Adjust row height to fit the configured font
        const QFontMetrics fm( font );
        logTableView_->verticalHeader()->setDefaultSectionSize( fm.height() + 2 );
        logTableView_->horizontalHeader()->setFont( font );
    }

    // Refresh the table overview visibility to match the overview setting
    updateTableOverview();

    for ( auto i = 0; i < tabbedFilteredView_->count(); ++i ) {
        auto fv = qobject_cast<FilteredView*>( tabbedFilteredView_->widget( i ) );
        fv->setLineNumbersVisible( config.filteredLineNumbersVisible() );
        fv->allowFollowMode( isFollowModeAllowed );
        fv->updateFont( font );
    }

    // Update the SearchLine (history)
    updateSearchCombo();

    FileWatcher::getFileWatcher().updateConfiguration();

    // Rebuild breadcrumb context lines when setting changes
    logFilteredData_->rebuildContextLines();
    for ( const auto& [fv, fd] : filteredViewsData_ ) {
        fd->rebuildContextLines();
    }

    if ( isFollowEnabled() ) {
        changeDataStatus( DataStatus::OLD_DATA );
    }

    // Repaint the table view so highlighter changes are reflected
    if ( logTableView_ && tableViewActive_ ) {
        logTableView_->viewport()->update();
    }
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

void CrawlerWidget::loadingFinishedHandler( LoadingStatus status )
{
    LOG_INFO << "file loading finished, status " << static_cast<int>( status );

    // We need to refresh the main window because the view lines on the
    // overview have probably changed.
    overview_.updateData( logData_->getNbLine() );

    // FIXME, handle topLine
    // logMainView_->updateData( logData_, topLine );
    logMainView_->updateData();

    // Shall we Forbid starting a search when loading in progress?
    // searchButton_->setEnabled( false );

    // searchButton_->setEnabled( true );

    // See if we need to auto-refresh the search
    if ( searchState_.isAutorefreshAllowed() ) {
        searchEndLine_ = LineNumber( logData_->getNbLine().get() );
        if ( searchState_.isFileTruncated() )
            // We need to restart the search
            replaceCurrentSearch( searchLineEdit_->currentText() );
        else
            logFilteredData_->updateSearch( searchStartLine_, searchEndLine_ );
    }

    // Set the encoding for the views
    updateEncoding();

    clearSearchLimits();

    // Also change the data available icon
    if ( firstLoadDone_ ) {
        changeDataStatus( DataStatus::NEW_DATA );
    }
    else {
        firstLoadDone_ = true;
        for ( const auto& m : savedMarkedLines_ ) {
            logFilteredData_->addMark( m );
        }
        logMainView_->setFocus();
    }

    loadingInProgress_ = false;

    // Try auto-detecting log format after first load
    if ( !detectedFormat_ ) {
        tryAutoDetectFormat();
    }
    else if ( tableViewActive_ ) {
        // File was updated — refresh table model contents
        populateTableModel();
    }

    Q_EMIT loadingFinished( status );
}

void CrawlerWidget::fileChangedHandler( MonitoredFileStatus status )
{
    // Handle the case where the file has been truncated
    if ( status == MonitoredFileStatus::Truncated ) {
        // Clear all marks (TODO offer the option to keep them)
        logFilteredData_->clearMarks();
        if ( !searchInfoLine_->text().isEmpty() ) {
            // Invalidate the search
            constexpr auto DropCache = true;
            logFilteredData_->clearSearch( DropCache );
            filteredView_->updateData();
            searchState_.truncateFile();
            printSearchInfoMessage();
            nbMatches_ = 0_lcount;
        }

        // Reset table view state so the format is re-detected after reload
        resetTableViewState();
    }
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

    searchState_.changeExpression();
    printSearchInfoMessage( logFilteredData_->getNbMatches() );
}

void CrawlerWidget::searchRefreshChangedHandler( bool isRefreshing )
{
    searchState_.setAutorefresh( isRefreshing );
    printSearchInfoMessage( logFilteredData_->getNbMatches() );
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

    if ( logFilteredData_->getNbLine() > 0_lcount ) {
        const auto lineIndex = logFilteredData_->getLineIndexNumber( currentLineNumber_ );
        filteredView_->selectAndDisplayLine( lineIndex );
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

    if ( Configuration::get().autoRunSearchOnPatternChange() ) {
        dispatchToMainThread( [ this ] { startNewSearch(); } );
    }
}

void CrawlerWidget::mouseHoveredOverMatch( LineNumber line )
{
    const auto line_in_mainview = logFilteredData_->getMatchingLineNumber( line );

    overviewWidget_->highlightLine( line_in_mainview );
    if ( tableOverviewWidget_ ) {
        tableOverviewWidget_->highlightLine( line_in_mainview );
    }
}

void CrawlerWidget::activityDetected()
{
    changeDataStatus( DataStatus::OLD_DATA );
}

void CrawlerWidget::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    searchStartLine_ = startLine;
    searchEndLine_ = endLine;

    logMainView_->setSearchLimits( startLine, endLine );
    filteredView_->setSearchLimits( startLine, endLine );
}

void CrawlerWidget::clearSearchLimits()
{
    setSearchLimits( 0_lnum, LineNumber( logData_->getNbLine().get() ) );
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

    assert( logData_ );
    assert( logFilteredData_ );

    // The views
    auto bottomWindow = new QWidget;
    bottomWindow->setContentsMargins( 2, 0, 2, 0 );

    overviewWidget_ = new OverviewWidget();
    logMainView_
        = new LogMainView( logData_.get(), quickFindPattern_.get(), &overview_, overviewWidget_ );
    logMainView_->setContentsMargins( 2, 0, 2, 0 );

    filteredView_ = new FilteredView( logFilteredData_.get(), quickFindPattern_.get() );
    filteredViewsData_[ filteredView_ ] = logFilteredData_;
    filteredView_->setContentsMargins( 2, 0, 2, 0 );

    overviewWidget_->setOverview( &overview_ );
    overviewWidget_->setParent( logMainView_ );

    // Connect the search to the top view
    logMainView_->useNewFiltering( logFilteredData_.get() );

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

    QStandardItem* matchesBreadcrumbsItem
        = new QStandardItem( tr( "Matches + breadcrumbs" ) );
    matchesBreadcrumbsItem->setData( QVariant::fromValue(
        VisibilityFlags::Matches | VisibilityFlags::Context ) );
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

    // Table view toggle button (hidden until format is detected)
    tableViewToggle_ = new QToolButton();
    tableViewToggle_->setToolTip( tr( "Toggle table/text view" ) );
    tableViewToggle_->setAccessibleName( tr( "Toggle table view" ) );
    tableViewToggle_->setCheckable( true );
    tableViewToggle_->setIcon( iconLoader_.load( "icons8-table" ) );
    tableViewToggle_->setToolButtonStyle( Qt::ToolButtonIconOnly );
    tableViewToggle_->setContentsMargins( 2, 2, 2, 2 );
    tableViewToggle_->setVisible( false );
    searchLineLayout->addWidget( tableViewToggle_ );

    connect( tableViewToggle_, &QToolButton::toggled, this,
             &CrawlerWidget::toggleTableView );

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
    logTableView_ = new QTableView;
    logTableView_->setSelectionBehavior( QAbstractItemView::SelectRows );
    logTableView_->setAlternatingRowColors( true );
    logTableView_->setShowGrid( false );
    logTableView_->horizontalHeader()->setStretchLastSection( true );
    logTableView_->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    logTableView_->setHorizontalScrollMode( QAbstractItemView::ScrollPerPixel );
    logTableView_->horizontalScrollBar()->setSingleStep( 10 );
    logTableView_->verticalHeader()->setVisible( false );
    logTableView_->setContentsMargins( 2, 0, 2, 0 );
    logTableView_->viewport()->setMouseTracking( true );
    logTableView_->viewport()->setCursor( Qt::IBeamCursor );
    logTableView_->viewport()->installEventFilter( this );
    logTableView_->installEventFilter( this );

    // Apply the same font the text view uses so appearance is consistent
    // from the very first frame (applyConfiguration runs later).
    {
        const auto& config = Configuration::get();
        QFont tableFont = config.mainFont();
        tableFont.setKerning( false );
        tableFont.setFixedPitch( true );
        if ( config.forceFontAntialiasing() ) {
            tableFont.setStyleStrategy( QFont::PreferAntialias );
        }
        tableFont.setBold( config.useBoldFont() );
        logTableView_->setFont( tableFont );

        const QFontMetrics fm( tableFont );
        logTableView_->verticalHeader()->setDefaultSectionSize( fm.height() + 2 );
    }

    // Install highlight delegate for match/mark row coloring and text highlighting
    tableHighlightDelegate_ = new LogTableHighlightDelegate( logTableView_ );
    tableHighlightDelegate_->setQuickFindPattern( quickFindPattern_ );
    logTableView_->setItemDelegate( tableHighlightDelegate_ );

    // Overview (minimap) widget for the table view — shares the same Overview data
    // as the text view so matches/marks are always in sync.
    tableOverviewWidget_ = new OverviewWidget( logTableView_ );
    tableOverviewWidget_->setOverview( &overview_ );
    connect( tableOverviewWidget_, &OverviewWidget::lineClicked, this,
             &CrawlerWidget::tableOverviewLineClicked );

    // Right-click context menu for the table view
    logTableView_->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( logTableView_, &QWidget::customContextMenuRequested, this,
             &CrawlerWidget::showTableViewContextMenu );

    // Update overview position when the table view scrolls
    connect( logTableView_->verticalScrollBar(), &QScrollBar::valueChanged, this,
             [this]() { updateTableOverview(); } );

    mainViewStack_->addWidget( logMainView_ );   // index 0 = text view
    mainViewStack_->addWidget( logTableView_ );   // index 1 = table view
    mainViewStack_->setCurrentIndex( 0 );

    addWidget( mainViewStack_ );
    addWidget( bottomWindow );

    // Chart panel — third pane in the vertical splitter, hidden by default.
    chartPanel_ = new ChartPanel;
    chartPanel_->hide();
    addWidget( chartPanel_ );

    // Default search checkboxes
    auto& config = Configuration::get();
    searchRefreshButton_->setChecked( config.isSearchAutoRefreshDefault() );
    matchCaseButton_->setChecked( !config.isSearchIgnoreCaseDefault() );
    useRegexpButton_->setChecked( config.mainRegexpType() == SearchRegexpType::ExtendedRegexp );
    booleanButton_->setChecked( config.isSearchLogicalCombiningDefault() );

    // Manually call the handler as it is not called when changing the state programmatically
    searchRefreshChangedHandler( searchRefreshButton_->isChecked() );
    useRegexpChangeHandler( useRegexpButton_->isChecked() );
    matchCaseChangedHandler( matchCaseButton_->isChecked() );
    booleanCombiningChangedHandler( booleanButton_->isChecked() );

    // Default splitter position (usually overridden by the config file)
    setSizes( config.splitterSizes() );

    registerShortcuts();
    loadIcons();

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

    connect( logMainView_, &LogMainView::newSelection,
             [ this ]( auto ) { logMainView_->update(); } );

    connect( logMainView_, &LogMainView::newSelection, this,
             &CrawlerWidget::updateLineNumberHandler );

    connect( logMainView_, &LogMainView::markLines, this, &CrawlerWidget::markLinesFromMain );

    connect( logMainView_, &LogMainView::highlightersChange, this,
             &CrawlerWidget::applyConfiguration );

    connect( logMainView_, QOverload<const QString&>::of( &LogMainView::addToSearch ), this,
             &CrawlerWidget::addToSearch );

    connect( logMainView_, QOverload<const QString&>::of( &LogMainView::excludeFromSearch ), this,
             &CrawlerWidget::excludeFromSearch );

    connect( logMainView_, QOverload<const QString&>::of( &LogMainView::replaceSearch ), this,
             &CrawlerWidget::replaceSearch );

    // Follow option (up and down)
    connect( this, &CrawlerWidget::followSet, logMainView_, &LogMainView::followSet );
    connect( logMainView_, &LogMainView::followModeChanged, this,
             &CrawlerWidget::followModeChanged );

    connect( this, &CrawlerWidget::textWrapSet, logMainView_, &LogMainView::textWrapSet );

    // Detect activity in the views
    connect( logMainView_, &LogMainView::activity, this, &CrawlerWidget::activityDetected );

    connect( logMainView_, &LogMainView::changeSearchLimits, this,
             &CrawlerWidget::setSearchLimits );

    connect( logMainView_, &LogMainView::clearSearchLimits, this,
             &CrawlerWidget::clearSearchLimits );

    connect( tabbedFilteredView_, &QTabWidget::currentChanged, this,
             &CrawlerWidget::changeFilteredView );

    connect( tabbedFilteredView_, &QTabWidget::tabCloseRequested, this,
             &CrawlerWidget::closeFilteredView );

    connect( logMainView_, &LogMainView::saveDefaultSplitterSizes, this,
             &CrawlerWidget::saveSplitterSizes );

    connect( logMainView_, &LogMainView::changeFontSize, this, &CrawlerWidget::changeFontSize );

    connect( logFilteredData_.get(), &LogFilteredData::searchProgressed, this,
             &CrawlerWidget::updateFilteredView, Qt::QueuedConnection );

    // Sent load file update to MainWindow (for status update)
    connect( logData_.get(), &LogData::loadingProgressed, this, &CrawlerWidget::loadingProgressed );
    connect( logData_.get(), &LogData::loadingFinished, this,
             &CrawlerWidget::loadingFinishedHandler );
    connect( logData_.get(), &LogData::fileChanged, this, &CrawlerWidget::fileChangedHandler );

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

    // Switch between views
    connect( logMainView_, &AbstractLogView::clearColorLabels, this,
             &CrawlerWidget::clearColorLabels );

    connect( logMainView_, &AbstractLogView::addColorLabel, this,
             &CrawlerWidget::addColorLabelToSelection );

    connect( logMainView_, &AbstractLogView::sendSelectionToScratchpad, this,
             [ this ]() { Q_EMIT sendToScratchpad( logMainView_->getSelectedText() ); } );

    connect( logMainView_, &AbstractLogView::replaceScratchpadWithSelection, this,
             [ this ]() { Q_EMIT replaceDataInScratchpad( logMainView_->getSelectedText() ); } );

    connectAllFilteredViewSlots( filteredView_ );

    // Wire chart panel — provide log data and connect click-to-navigate.
    chartPanel_->setLogData( logData_ );
    connect( chartPanel_, &ChartPanel::lineSelected, this,
             [ this ]( LineNumber line ) {
                 logMainView_->selectAndDisplayLine( line );
             } );

    // Refresh chart data when the file finishes loading.
    connect( logData_.get(), &LogData::loadingFinished, this,
             [ this ]( auto ) {
                 if ( chartPanel_->isVisible() ) {
                     chartPanel_->extractData();
                 }
             } );

    const auto defaultEncodingMib = config.defaultEncodingMib();
    if ( defaultEncodingMib >= 0 ) {
        encodingMib_ = defaultEncodingMib;
    }
}

void CrawlerWidget::changeFilteredView( int tabIndex )
{
    logFilteredData_->interruptSearch();
    if ( tabIndex >= 0 ) {
        auto* tabFilteredView
            = qobject_cast<FilteredView*>( tabbedFilteredView_->widget( tabIndex ) );

        filteredView_ = tabFilteredView;
        logFilteredData_ = filteredViewsData_.at( tabFilteredView );

        Q_EMIT filteredViewChanged();

        logMainView_->useNewFiltering( logFilteredData_.get() );
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
        QFont newFont{ fontInfo.family(), *currentSize };

        fontConfig.setMainFont( newFont );
        logMainView_->updateFont( newFont );
        filteredView_->updateFont( newFont );

        if ( logTableView_ ) {
            logTableView_->setFont( newFont );
            const QFontMetrics fm( newFont );
            logTableView_->verticalHeader()->setDefaultSectionSize( fm.height() + 2 );
            logTableView_->horizontalHeader()->setFont( newFont );
        }
    }
}

void CrawlerWidget::connectAllFilteredViewSlots( FilteredView* view )
{
    connect( view, &FilteredView::newSelection, view, [ view ]( auto ) { view->update(); } );

    connect( view, &FilteredView::newSelection, this, &CrawlerWidget::jumpToMatchingLine );

    connect( view, &FilteredView::markLines, this, &CrawlerWidget::markLinesFromFiltered );

    connect( view, &FilteredView::highlightersChange, this, &CrawlerWidget::applyConfiguration );

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

    connect( view, &FilteredView::mouseLeftHoveringZone, tableOverviewWidget_,
             &OverviewWidget::removeHighlight );

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

    logMainView_->registerShortcuts();
    filteredView_->registerShortcuts();
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
    // Interrupt the search if it's ongoing
    logFilteredData_->interruptSearch();

    // We have to wait for the last search update (100%)
    // before clearing/restarting to avoid having remaining results.

    // FIXME: this is a bit of a hack, we call processEvents
    // for Qt to empty its event queue, including (hopefully)
    // the search update event sent by logFilteredData_. It saves
    // us the overhead of having proper sync.
    QApplication::processEvents( QEventLoop::ExcludeUserInputEvents );

    nbMatches_ = 0_lcount;

    // Switch to "Marks and matches" view when in "Marks" view
    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    if ( !filteredView_->visibility().testFlag( VisibilityFlags::Matches ) ) {
        visibilityBox_->setCurrentIndex( 0 );
    }

    // Clear and recompute the content of the filtered window.
    logFilteredData_->clearSearch();
    filteredView_->updateData();

    // Update the match overview
    overview_.updateData( logData_->getNbLine() );

    if ( !searchText.isEmpty() ) {

        // Constructs the regexp
        auto regexpPattern = RegularExpressionPattern(
            searchText, matchCaseButton_->isChecked(), inverseButton_->isChecked(),
            booleanButton_->isChecked(), !useRegexpButton_->isChecked() );

        RegularExpression hsExpression{ regexpPattern };
        auto isValidExpression = hsExpression.isValid();

        if ( isValidExpression ) {
            // Activate the stop button
            stopButton_->setEnabled( true );
            stopButton_->show();
            clearButton_->hide();
            searchButton_->hide();
            // Start a new asynchronous search
            logFilteredData_->runSearch( regexpPattern, searchStartLine_, searchEndLine_ );
            // Accept auto-refresh of the search
            searchState_.startSearch();
            searchInfoLine_->hide();
            logMainView_->setSearchPattern( regexpPattern );
            filteredView_->setSearchPattern( regexpPattern );
        }
        else {
            // The regexp is wrong
            logFilteredData_->clearSearch();
            filteredView_->updateData();
            searchState_.resetState();

            // Inform the user
            QString errorString = hsExpression.errorString();
            QString errorMessage = tr( "Error in expression" );
            // const int offset = regexp.patternErrorOffset();
            // if ( offset != -1 ) {
            //     errorMessage += " at position ";
            //     errorMessage += QString::number( offset );
            // }
            errorMessage += ": ";
            errorMessage += errorString;
            searchInfoLine_->setPalette( ErrorPalette );
            searchInfoLine_->setText( errorMessage );
            searchInfoLine_->show();

            logMainView_->setSearchPattern( {} );
            filteredView_->setSearchPattern( {} );
        }
    }
    else {
        searchState_.resetState();
        printSearchInfoMessage();
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

    switch ( searchState_.getState() ) {
    case SearchState::NoSearch:
        // Blank text is fine
        break;
    case SearchState::Static:
    case SearchState::Autorefreshing:
        // Some languages translate the plural the same as the singular, so use the full string
        text = nbMatches.get() > 1 ? tr( "%1 matches found" ).arg( nbMatches.get() )
                                   : tr( "%1 match found" ).arg( nbMatches.get() );
        break;
    case SearchState::FileTruncated:
    case SearchState::TruncatedAutorefreshing:
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
            codec = logData_->getDetectedEncoding();
        }
        else {
            codec = QTextCodec::codecForMib( *encodingMib_ );
        }
        return codec ? codec : QTextCodec::codecForLocale();
    }();

    QString encodingPrefix = encodingMib_ ? tr( "Displayed as %1" ) : tr( "Detected as %1" );
    encodingText_ = encodingPrefix.arg( textCodec->name().constData() );

    logData_->interruptLoading();

    logData_->setDisplayEncoding( textCodec->name().constData() );
    logMainView_->forceRefresh();
    logFilteredData_->setDisplayEncoding( textCodec->name().constData() );
    filteredView_->forceRefresh();
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
    logMainView_->setQuickHighlighters( labels );
    filteredView_->setQuickHighlighters( labels );

    // Sync color labels to the table view highlight delegate
    if ( tableHighlightDelegate_ ) {
        tableHighlightDelegate_->setColorLabelWords( labels );
        if ( logTableView_ && tableViewActive_ ) {
            logTableView_->viewport()->update();
        }
    }
}

//
// SearchState implementation
//
void CrawlerWidget::SearchState::resetState()
{
    state_ = NoSearch;
}

void CrawlerWidget::SearchState::setAutorefresh( bool refresh )
{
    autoRefreshRequested_ = refresh;

    if ( refresh ) {
        if ( state_ == Static )
            state_ = Autorefreshing;
        /*
        else if ( state_ == FileTruncated )
            state_ = TruncatedAutorefreshing;
        */
    }
    else {
        if ( state_ == Autorefreshing )
            state_ = Static;
        else if ( state_ == TruncatedAutorefreshing )
            state_ = FileTruncated;
    }
}

void CrawlerWidget::SearchState::truncateFile()
{
    if ( state_ == Autorefreshing || state_ == TruncatedAutorefreshing ) {
        state_ = TruncatedAutorefreshing;
    }
    else {
        state_ = FileTruncated;
    }
}

void CrawlerWidget::SearchState::changeExpression()
{
    if ( state_ == Autorefreshing )
        state_ = Static;
}

void CrawlerWidget::SearchState::stopSearch()
{
    if ( state_ == Autorefreshing )
        state_ = Static;
}

void CrawlerWidget::SearchState::startSearch()
{
    if ( autoRefreshRequested_ )
        state_ = Autorefreshing;
    else
        state_ = Static;
}

/*
 * CrawlerWidgetContext
 */
CrawlerWidgetContext::CrawlerWidgetContext( const QString& string )
{
    if ( string.startsWith( '{' ) ) {
        loadFromJson( string );
    }
    else {
        loadFromString( string );
    }
}

void CrawlerWidgetContext::loadFromString( const QString& string )
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

    useRegexp_ = Configuration::get().mainRegexpType() == SearchRegexpType::ExtendedRegexp;
}

void CrawlerWidgetContext::loadFromJson( const QString& json )
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
        useRegexp_ = Configuration::get().mainRegexpType() == SearchRegexpType::ExtendedRegexp;
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
        chartSeriesJson_ = QJsonDocument::fromJson(
            properties.value( "CS" ).toString().toUtf8() ).array();
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
    if ( !detectedFormat_ ) {
        return;
    }

    tableViewActive_ = tableViewToggle_->isChecked();

    if ( tableViewActive_ ) {
        mainViewStack_->setCurrentIndex( 1 );
        // Defer model population so the view switch renders immediately
        QTimer::singleShot( 0, this, [this]() {
            populateTableModel();
            updateTableOverview();
        } );
    }
    else {
        mainViewStack_->setCurrentIndex( 0 );
        // Hide the table overview and re-show the text view overview
        if ( tableOverviewWidget_ ) {
            tableOverviewWidget_->hide();
        }
    }
}

// Try auto-detecting a log format from the first lines of the file.
// Called after loading finishes, only if the config setting is enabled.
void CrawlerWidget::tryAutoDetectFormat()
{
    const auto& config = Configuration::get();
    if ( !config.autoDetectLogFormats() ) {
        return;
    }

    if ( !logData_ || logData_->getNbLine().get() == 0 ) {
        return;
    }

    // Sample the first 50 lines for detection
    const auto totalLines = logData_->getNbLine().get();
    const int sampleCount = static_cast<int>( std::min( totalLines, uint64_t{ 50 } ) );
    QStringList sampleLines;
    sampleLines.reserve( sampleCount );
    for ( int i = 0; i < sampleCount; ++i ) {
        sampleLines << logData_->getLineString( LineNumber( static_cast<uint64_t>( i ) ) );
    }

    // Load built-in formats, then user formats (user overrides built-in)
    LogFormatRegistry registry;
    registry.loadBuiltinFormats();
    registry.loadUserFormats();

    LogFormatMatcher matcher( registry );
    const auto* match = matcher.detectFormat( sampleLines );

    if ( match ) {
        LOG_INFO << "Auto-detected log format: " << match->name().toStdString();
        detectedFormat_ = std::make_unique<LogFormatDefinition>( *match );
        tableViewToggle_->setVisible( true );
        tableViewToggle_->setToolTip(
            tr( "Toggle table/text view (%1)" ).arg( detectedFormat_->title() ) );

        // Provide format info to the chart panel for template series.
        chartPanel_->setLogFormat( detectedFormat_.get() );

        // Automatically activate table view if the user opted in
        if ( config.autoShowTableView() && !tableViewToggle_->isChecked() ) {
            tableViewToggle_->setChecked( true );
        }
    }
}

// Reset all table view state so the format can be re-detected from scratch.
void CrawlerWidget::resetTableViewState()
{
    if ( tableModel_ ) {
        // Disconnect header signals before removing the model to prevent
        // duplicate connections when populateTableModel() reconnects.
        disconnect( logTableView_->horizontalHeader(), &QHeaderView::sectionResized, this,
                    &CrawlerWidget::saveTableColumnWidths );
        logTableView_->setModel( nullptr );
        delete tableModel_;
        tableModel_ = nullptr;
    }
    detectedFormat_.reset();
    tableViewActive_ = false;
    tableColumnsNeedSizing_ = false;

    // Clear format info from chart panel.
    chartPanel_->setLogFormat( nullptr );

    if ( tableViewToggle_ ) {
        tableViewToggle_->setChecked( false );
        tableViewToggle_->setVisible( false );
    }
    if ( mainViewStack_ ) {
        mainViewStack_->setCurrentIndex( 0 );
    }

    // Hide the overview widget (no longer valid after reset)
    if ( tableOverviewWidget_ ) {
        tableOverviewWidget_->hide();
    }
}

// Return a sanitized format name safe for use as a QSettings group key.
// Replaces characters that are special in QSettings paths.
QString CrawlerWidget::sanitizedFormatName( const QString& name )
{
    QString safe = name;
    safe.replace( '/', '_' );
    safe.replace( '\\', '_' );
    return safe;
}

// Populate the table model from the current logData_ contents.
void CrawlerWidget::populateTableModel()
{
    if ( !detectedFormat_ || !logData_ ) {
        return;
    }

    // Recreate the model if the logData_ pointer changed (e.g. after reload)
    if ( tableModel_ && tableModel_->logDataPtr() != logData_.get() ) {
        resetTableViewState();
        // Re-detect format for the new data
        tryAutoDetectFormat();
        if ( !detectedFormat_ ) {
            return;
        }
    }

    // Create the model if it does not exist yet
    if ( !tableModel_ ) {
        tableModel_ = new LogFormatTableModel( *detectedFormat_, logData_.get(), this );
        logTableView_->setModel( tableModel_ );

        // Save column widths when the user resizes a column
        connect( logTableView_->horizontalHeader(), &QHeaderView::sectionResized, this,
                 &CrawlerWidget::saveTableColumnWidths );

        // Sync table view selection changes to the filtered view / current line
        connect( logTableView_->selectionModel(), &QItemSelectionModel::selectionChanged,
                 this, [this]() { tableViewSelectionChanged(); } );

        // Flag: column widths need initial sizing after first data arrives
        tableColumnsNeedSizing_ = true;
    }

    // Keep the highlight delegate's filtered data reference up to date
    if ( tableHighlightDelegate_ && logFilteredData_ ) {
        tableHighlightDelegate_->setFilteredData( logFilteredData_.get() );
    }

    const auto lineCount = logData_->getNbLine().get();
    const int lineCountInt
        = static_cast<int>( std::min( lineCount, static_cast<uint64_t>( INT_MAX ) ) );
    tableModel_->setLineCount( lineCountInt );

    // Size columns once, after the model has data for the first time
    if ( tableColumnsNeedSizing_ && lineCountInt > 0 ) {
        tableColumnsNeedSizing_ = false;

        // Apply saved widths immediately (fast, no I/O) so the table is usable
        // right away, then refine from actual data asynchronously.
        if ( !applySavedColumnWidths() ) {
            // No saved widths — use a reasonable default until auto-sizing finishes
            programmaticColumnResize_ = true;
            for ( int col = 0; col < tableModel_->columnCount(); ++col ) {
                logTableView_->setColumnWidth( col, 120 );
            }
            programmaticColumnResize_ = false;
        }

        // Defer the expensive auto-sizing so the UI stays responsive
        QTimer::singleShot( 0, this, &CrawlerWidget::autoSizeTableColumns );
    }

    // Follow mode: auto-scroll the table view to the last row
    if ( isFollowEnabled() && lineCountInt > 0 ) {
        logTableView_->scrollToBottom();
    }

    // Refresh the overview widget
    updateTableOverview();
}

// Save column widths for the active log format to QSettings.
void CrawlerWidget::saveTableColumnWidths()
{
    if ( programmaticColumnResize_ || !detectedFormat_ || !tableModel_ || !logTableView_ ) {
        return;
    }

    QSettings settings;
    settings.beginGroup( "logformat/columns/" + sanitizedFormatName( detectedFormat_->name() ) );

    const auto* header = logTableView_->horizontalHeader();
    const int colCount = tableModel_->columnCount();

    // Save column count and a layout fingerprint so stale widths can be detected
    settings.setValue( "_columnCount", colCount );
    QStringList colNames;
    for ( int i = 0; i < colCount; ++i ) {
        colNames << tableModel_->headerData( i, Qt::Horizontal ).toString();
    }
    settings.setValue( "_columnNames", colNames.join( "|" ) );

    for ( int i = 0; i < colCount; ++i ) {
        settings.setValue( QString::number( i ), header->sectionSize( i ) );
    }

    settings.endGroup();
}

// Restore column widths for the active log format from QSettings.
// Returns true if saved widths were applied, false if none were found.
bool CrawlerWidget::restoreTableColumnWidths()
{
    if ( !detectedFormat_ || !tableModel_ || !logTableView_ ) {
        return false;
    }

    // Try saved widths first, then fall back to auto-sizing
    if ( applySavedColumnWidths() ) {
        return true;
    }
    autoSizeTableColumns();
    return true;
}

// Apply saved column widths from QSettings if available and matching.
bool CrawlerWidget::applySavedColumnWidths()
{
    if ( !detectedFormat_ || !tableModel_ || !logTableView_ ) {
        return false;
    }

    QSettings settings;
    settings.beginGroup( "logformat/columns/" + sanitizedFormatName( detectedFormat_->name() ) );

    const int savedColCount = settings.value( "_columnCount", -1 ).toInt();
    const int currentColCount = tableModel_->columnCount();
    if ( savedColCount != currentColCount ) {
        settings.endGroup();
        return false;
    }

    // Verify layout fingerprint matches current columns
    QStringList currentNames;
    for ( int i = 0; i < currentColCount; ++i ) {
        currentNames << tableModel_->headerData( i, Qt::Horizontal ).toString();
    }
    if ( settings.value( "_columnNames" ).toString() != currentNames.join( "|" ) ) {
        settings.endGroup();
        return false;
    }

    programmaticColumnResize_ = true;
    for ( int i = 0; i < currentColCount; ++i ) {
        const int w = settings.value( QString::number( i ), -1 ).toInt();
        if ( w > 0 ) {
            logTableView_->setColumnWidth( i, w );
        }
    }
    programmaticColumnResize_ = false;
    stretchLastTableColumn();

    settings.endGroup();
    return true;
}

// Stretch the last table column to fill any remaining viewport space.
void CrawlerWidget::stretchLastTableColumn()
{
    if ( !tableModel_ || !logTableView_ ) {
        return;
    }

    const int colCount = tableModel_->columnCount();
    const int lastCol = colCount - 1;
    if ( lastCol < 0 ) {
        return;
    }

    const auto* header = logTableView_->horizontalHeader();
    int usedWidth = 0;
    for ( int i = 0; i < colCount; ++i ) {
        usedWidth += header->sectionSize( i );
    }
    const int viewportWidth = logTableView_->viewport()->width();
    if ( usedWidth < viewportWidth ) {
        const int currentLast = header->sectionSize( lastCol );
        programmaticColumnResize_ = true;
        logTableView_->setColumnWidth( lastCol, currentLast + ( viewportWidth - usedWidth ) );
        programmaticColumnResize_ = false;
    }
}

// Compute column widths by sampling rows from the model data.
// Measures actual text width with the view's font metrics to ensure no clipping.
void CrawlerWidget::autoSizeTableColumns()
{
    if ( !tableModel_ || !logTableView_ ) {
        return;
    }

    const int colCount = tableModel_->columnCount();
    const int rowCount = tableModel_->rowCount();
    if ( colCount <= 0 || rowCount <= 0 ) {
        return;
    }

    // Sample a small number of rows from the beginning of the file to find
    // the maximum text width per column.  We only read from the start because
    // those lines are already in the OS page cache (format detection reads the
    // first 50).  Reading from the middle/end of a multi-GB file causes heavy
    // random I/O that freezes the UI.  The column widths are approximate —
    // the user can resize manually if needed.
    const int sampleRows = std::min( rowCount, 50 );
    const auto fm = logTableView_->fontMetrics();
    constexpr int cellPadding = 16; // 4px padding each side + some margin

    QVector<int> maxWidths( colCount, 0 );

    // Start with header text widths as minimum
    for ( int col = 0; col < colCount; ++col ) {
        const auto headerText = tableModel_->headerData( col, Qt::Horizontal ).toString();
        maxWidths[ col ] = fm.horizontalAdvance( headerText ) + cellPadding;
    }

    // Measure cell content widths from the first N rows
    for ( int row = 0; row < sampleRows; ++row ) {
        for ( int col = 0; col < colCount; ++col ) {
            const auto idx = tableModel_->index( row, col );
            const auto text = idx.data( Qt::DisplayRole ).toString();
            if ( !text.isEmpty() ) {
                const int textWidth = fm.horizontalAdvance( text ) + cellPadding;
                if ( textWidth > maxWidths[ col ] ) {
                    maxWidths[ col ] = textWidth;
                }
            }
        }
    }

    programmaticColumnResize_ = true;
    for ( int col = 0; col < colCount; ++col ) {
        logTableView_->setColumnWidth( col, maxWidths[ col ] );
    }
    programmaticColumnResize_ = false;

    stretchLastTableColumn();
}

bool CrawlerWidget::eventFilter( QObject* obj, QEvent* event )
{
    if ( logTableView_ && obj == logTableView_->viewport() ) {
        if ( event->type() == QEvent::Resize ) {
            stretchLastTableColumn();
            updateTableOverview();
        }

        // --- Mouse events on the table viewport for portion selection & hover ---
        if ( tableViewActive_ && tableModel_ ) {
            switch ( event->type() ) {

            case QEvent::MouseButtonPress: {
                const auto* me = static_cast<QMouseEvent*>( event );
                if ( me->button() == Qt::LeftButton ) {
                    const auto index = logTableView_->indexAt( me->pos() );
                    if ( index.isValid() ) {
                        const int charPos = tableCellCharAtX( index, me->pos().x() );

                        if ( me->modifiers() & Qt::ShiftModifier
                             && tableCellSelection_.active
                             && tableCellSelection_.row == index.row()
                             && tableCellSelection_.column == index.column() ) {
                            // Shift-click extends the existing selection
                            tableCellSelection_.endChar = charPos;
                        }
                        else {
                            // Start a new portion selection
                            tableCellSelection_.active = true;
                            tableCellSelection_.row = index.row();
                            tableCellSelection_.column = index.column();
                            tableCellSelection_.startChar = charPos;
                            tableCellSelection_.endChar = charPos;
                        }
                        tableSelectionDragging_ = true;

                        // Update delegate and repaint
                        tableHighlightDelegate_->setPortionSelection(
                            tableCellSelection_.row, tableCellSelection_.column,
                            tableCellSelection_.startChar, tableCellSelection_.endChar );
                        logTableView_->viewport()->update();
                    }
                }
                // Don't consume — let QTableView handle row selection too
                break;
            }

            case QEvent::MouseMove: {
                const auto* me = static_cast<QMouseEvent*>( event );
                const auto index = logTableView_->indexAt( me->pos() );

                // --- Hover highlight ---
                const int newHoverRow = index.isValid() ? index.row() : -1;
                if ( newHoverRow != tableHoverRow_ ) {
                    tableHoverRow_ = newHoverRow;
                    tableHighlightDelegate_->setHoverRow( tableHoverRow_ );
                    logTableView_->viewport()->update();
                }

                // --- Drag to extend portion selection ---
                if ( tableSelectionDragging_ && tableCellSelection_.active && index.isValid() ) {
                    // Only extend within the same row AND same column
                    if ( index.row() == tableCellSelection_.row
                         && index.column() == tableCellSelection_.column ) {
                        const int charPos = tableCellCharAtX( index, me->pos().x() );
                        tableCellSelection_.endChar = charPos;

                        tableHighlightDelegate_->setPortionSelection(
                            tableCellSelection_.row, tableCellSelection_.column,
                            tableCellSelection_.startChar, tableCellSelection_.endChar );
                        logTableView_->viewport()->update();
                    }
                }
                break;
            }

            case QEvent::MouseButtonRelease: {
                const auto* me = static_cast<QMouseEvent*>( event );
                if ( me->button() == Qt::LeftButton && tableSelectionDragging_ ) {
                    tableSelectionDragging_ = false;
                    // If start == end, it was just a click, not a drag — clear portion
                    if ( tableCellSelection_.startChar == tableCellSelection_.endChar ) {
                        tableCellSelection_.clear();
                        tableHighlightDelegate_->clearPortionSelection();
                        logTableView_->viewport()->update();
                    }
                }
                break;
            }

            case QEvent::MouseButtonDblClick: {
                const auto* me = static_cast<QMouseEvent*>( event );
                if ( me->button() == Qt::LeftButton ) {
                    const auto index = logTableView_->indexAt( me->pos() );
                    if ( index.isValid() ) {
                        const int charPos = tableCellCharAtX( index, me->pos().x() );
                        tableSelectWordAt( index, charPos );
                        return true; // Consume to prevent default editing
                    }
                }
                break;
            }

            case QEvent::Leave: {
                // Clear hover when mouse leaves the viewport
                if ( tableHoverRow_ >= 0 ) {
                    tableHoverRow_ = -1;
                    tableHighlightDelegate_->clearHoverRow();
                    logTableView_->viewport()->update();
                }
                break;
            }

            default:
                break;
            }
        }
    }

    // Handle keyboard shortcuts for the table view
    if ( logTableView_ && tableViewActive_ && obj == logTableView_
         && event->type() == QEvent::KeyPress ) {
        const auto* keyEvent = static_cast<QKeyEvent*>( event );
        if ( keyEvent->matches( QKeySequence::Copy ) ) {
            copyTableSelection();
            return true;
        }
        // 'm' to mark/unmark lines (same as text view)
        if ( keyEvent->key() == Qt::Key_M && keyEvent->modifiers() == Qt::NoModifier ) {
            markTableSelection();
            return true;
        }
        // Home = jump to first row, End = jump to last row
        if ( keyEvent->key() == Qt::Key_Home && keyEvent->modifiers() == Qt::ControlModifier ) {
            if ( tableModel_ && tableModel_->rowCount() > 0 ) {
                logTableView_->scrollToTop();
                logTableView_->selectRow( 0 );
            }
            return true;
        }
        if ( keyEvent->key() == Qt::Key_End && keyEvent->modifiers() == Qt::ControlModifier ) {
            if ( tableModel_ && tableModel_->rowCount() > 0 ) {
                logTableView_->scrollToBottom();
                logTableView_->selectRow( tableModel_->rowCount() - 1 );
            }
            return true;
        }
    }

    return QSplitter::eventFilter( obj, event );
}

// Convert a pixel X position to a character index within a table cell.
int CrawlerWidget::tableCellCharAtX( const QModelIndex& index, int pixelX ) const
{
    const auto cellText = index.data( Qt::DisplayRole ).toString();
    if ( cellText.isEmpty() ) {
        return 0;
    }

    const auto cellRect = logTableView_->visualRect( index );
    // 4px left padding matches the delegate
    const int textLeft = cellRect.left() + 4;
    const int relativeX = pixelX - textLeft;

    if ( relativeX <= 0 ) {
        return 0;
    }

    const QFontMetrics fm( logTableView_->font() );

    const int textLen = static_cast<int>( cellText.size() );

    // Binary search for the character position
    for ( int i = 1; i <= textLen; ++i ) {
        const int charRight = fm.horizontalAdvance( cellText.left( i ) );
        if ( relativeX < charRight ) {
            // Check if click is closer to left or right edge of this character
            const int charLeft = fm.horizontalAdvance( cellText.left( i - 1 ) );
            return ( relativeX - charLeft < charRight - relativeX ) ? i - 1 : i;
        }
    }
    return textLen;
}

// Select the word at the given character position in a table cell.
void CrawlerWidget::tableSelectWordAt( const QModelIndex& index, int charPos )
{
    const auto cellText = index.data( Qt::DisplayRole ).toString();
    if ( cellText.isEmpty() ) {
        return;
    }

    const int textLen = static_cast<int>( cellText.size() );

    // Clamp charPos to valid range
    charPos = std::clamp( charPos, 0, textLen - 1 );

    // Find word boundaries (alphanumeric + underscore)
    int start = charPos;
    int end = charPos;

    while ( start > 0 && ( cellText[ start - 1 ].isLetterOrNumber()
                           || cellText[ start - 1 ] == '_' ) ) {
        --start;
    }
    while ( end < textLen && ( cellText[ end ].isLetterOrNumber()
                               || cellText[ end ] == '_' ) ) {
        ++end;
    }

    if ( start == end ) {
        // No word found at position, select the single character
        end = std::min( start + 1, textLen );
    }

    tableCellSelection_.active = true;
    tableCellSelection_.row = index.row();
    tableCellSelection_.column = index.column();
    tableCellSelection_.startChar = start;
    tableCellSelection_.endChar = end;

    tableHighlightDelegate_->setPortionSelection( index.row(), index.column(), start, end );
    logTableView_->viewport()->update();
}

// ── Table view feature implementations ──────────────────────────────────────

// Position and update the overview (minimap) widget beside the table view.
// Mirrors what AbstractLogView::refreshOverview / updateDisplaySize do.
void CrawlerWidget::updateTableOverview()
{
    if ( !tableOverviewWidget_ || !logTableView_ ) {
        return;
    }

    const bool shouldShow = tableViewActive_ && overview_.isVisible();
    if ( !shouldShow ) {
        tableOverviewWidget_->hide();
        return;
    }

    static constexpr int OverviewWidth = 27;

    // Place the overview widget at the right edge of the table view,
    // spanning the full height below the header.
    const int headerHeight = logTableView_->horizontalHeader()->isVisible()
                                 ? logTableView_->horizontalHeader()->height()
                                 : 0;
    const int tableWidth = logTableView_->width();
    const int tableHeight = logTableView_->height();
    const int overviewHeight = tableHeight - headerHeight;

    if ( overviewHeight <= 0 ) {
        tableOverviewWidget_->hide();
        return;
    }

    tableOverviewWidget_->setGeometry(
        tableWidth - OverviewWidth - 1, headerHeight, OverviewWidth, overviewHeight );
    tableOverviewWidget_->show();
    tableOverviewWidget_->raise();

    // Update the current-view position indicator in the overview
    if ( tableModel_ ) {
        const auto* vbar = logTableView_->verticalScrollBar();
        const int firstVisibleRow = vbar->value();
        const int rowHeight = logTableView_->verticalHeader()->defaultSectionSize();
        const int visibleRows = ( rowHeight > 0 ) ? ( overviewHeight / rowHeight ) : 1;
        const int lastVisibleRow = firstVisibleRow + visibleRows;

        overview_.updateCurrentPosition(
            LineNumber( static_cast<uint64_t>( firstVisibleRow ) ),
            LineNumber( static_cast<uint64_t>( lastVisibleRow ) ) );
    }

    tableOverviewWidget_->update();
}

// Handle a click on the table overview — jump to the corresponding row.
void CrawlerWidget::tableOverviewLineClicked( LineNumber line )
{
    if ( !logTableView_ || !tableModel_ ) {
        return;
    }

    const auto row = static_cast<int>( line.get() );
    if ( row >= 0 && row < tableModel_->rowCount() ) {
        const auto idx = tableModel_->index( row, 0 );
        logTableView_->scrollTo( idx, QAbstractItemView::PositionAtCenter );
        logTableView_->selectRow( row );
    }
}

// Handle selection change in the table view: update the text view and
// filtered view to show the same line (just like the text view does).
void CrawlerWidget::tableViewSelectionChanged()
{
    if ( !logTableView_ || !tableModel_ ) {
        return;
    }

    const auto indexes = logTableView_->selectionModel()->selectedRows();
    if ( indexes.isEmpty() ) {
        return;
    }

    const auto row = indexes.first().row();
    const auto lineNumber = LineNumber( static_cast<uint64_t>( row ) );

    // Update the current line tracking (same as updateLineNumberHandler)
    currentLineNumber_ = lineNumber;
    Q_EMIT newSelection( lineNumber, 1_lcount, 0_lcol, 0_length );

    // Keep the text view in sync so switching back shows the same line
    logMainView_->selectAndDisplayLine( lineNumber );

    // If there is a match in the filtered view, select it there too
    if ( logFilteredData_ && logFilteredData_->getNbLine().get() > 0 ) {
        const auto filteredIndex = logFilteredData_->getLineIndexNumber( lineNumber );
        if ( filteredIndex < logFilteredData_->getNbLine() ) {
            filteredView_->selectAndDisplayLine( filteredIndex );
        }
    }

    // Refresh the overview position indicator
    updateTableOverview();
}

// Show a context menu when the user right-clicks the table view.
void CrawlerWidget::showTableViewContextMenu( const QPoint& pos )
{
    if ( !logTableView_ ) {
        return;
    }

    const auto indexes = logTableView_->selectionModel()->selectedRows();
    const bool hasSelection = !indexes.isEmpty();

    // Find the cell the user right-clicked on
    const auto clickedIdx = logTableView_->indexAt( pos );
    auto cellText = ( clickedIdx.isValid() && hasSelection )
                        ? clickedIdx.data( Qt::DisplayRole ).toString()
                        : QString{};

    // Prefer the portion-selected text when a sub-cell selection is active
    if ( tableCellSelection_.active
         && tableCellSelection_.startChar != tableCellSelection_.endChar ) {
        const auto selIdx = tableModel_->index( tableCellSelection_.row,
                                                tableCellSelection_.column );
        const auto selText
            = tableCellSelection_.selectedText( selIdx.data( Qt::DisplayRole ).toString() );
        if ( !selText.isEmpty() ) {
            cellText = selText;
        }
    }

    QMenu menu( logTableView_ );

    // ── Highlighters submenu ──
    auto* highlightersMenu = new HighlightersMenu( tr( "Highlighters" ), &menu );
    highlightersMenu->createHighlightersMenu();
    highlightersMenu->populateHighlightersMenu();
    highlightersMenu->setApplyChange( [this]() {
        logMainView_->update();
        filteredView_->update();
        if ( logTableView_ && tableViewActive_ ) {
            logTableView_->viewport()->update();
        }
    } );
    menu.addMenu( highlightersMenu );

    // ── Color labels submenu ──
    auto* colorLabelsMenu = menu.addMenu( tr( "Color labels" ) );
    const bool hasText = !cellText.isEmpty();
    colorLabelsMenu->setEnabled( hasText );
    QActionGroup* colorLabelsActionGroup = nullptr;

    if ( hasText ) {
        colorLabelsActionGroup = new QActionGroup( &menu );

        // Determine current label for the cell text
        const auto& quickHighlighters
            = HighlighterSetCollection::get().quickHighlighters();
        const auto& currentLabels = colorLabelsManager_.colorLabels();
        std::optional<size_t> currentLabel;
        for ( size_t i = 0; i < currentLabels.size(); ++i ) {
            if ( currentLabels[ i ].contains( cellText ) ) {
                currentLabel = i;
                break;
            }
        }

        auto* noneAction = colorLabelsMenu->addAction( tr( "None" ) );
        noneAction->setActionGroup( colorLabelsActionGroup );
        noneAction->setCheckable( true );
        noneAction->setChecked( !currentLabel.has_value() );
        if ( currentLabel ) {
            noneAction->setData( static_cast<unsigned>( *currentLabel ) );
        }

        colorLabelsMenu->addSeparator();
        const auto maxLabel = std::min( currentLabels.size(),
                                        static_cast<size_t>( quickHighlighters.size() ) );
        for ( size_t i = 0; i < maxLabel; ++i ) {
            const auto& cfg = quickHighlighters.at( static_cast<int>( i ) );
            auto* action = colorLabelsMenu->addAction( cfg.name );
            action->setActionGroup( colorLabelsActionGroup );
            action->setCheckable( true );
            action->setChecked( currentLabel == i );
            action->setData( static_cast<unsigned>( i ) );

            QPixmap pixmap( 20, 10 );
            auto fillColor = cfg.color.backColor;
            fillColor.setAlphaF( 1.0 );
            pixmap.fill( fillColor );
            action->setIcon( QIcon( pixmap ) );
            action->setIconVisibleInMenu( true );
        }
        colorLabelsMenu->addSeparator();
        auto* clearAllAction = colorLabelsMenu->addAction( tr( "Clear all" ) );
        connect( clearAllAction, &QAction::triggered, this, &CrawlerWidget::clearColorLabels );

        connect( colorLabelsActionGroup, &QActionGroup::triggered, this,
                 [this, cellText]( QAction* action ) {
                     if ( action->data().isValid() ) {
                         updateColorLabels( colorLabelsManager_.setColorLabel(
                             static_cast<size_t>( action->data().toInt() ), cellText ) );
                     }
                 } );
    }

    menu.addSeparator();

    // ── Mark ──
    auto* markAction = menu.addAction( hasSelection ? tr( "Mark / Unmark lines" ) : tr( "Mark" ) );
    markAction->setEnabled( hasSelection );
    connect( markAction, &QAction::triggered, this, &CrawlerWidget::markTableSelection );

    menu.addSeparator();

    // ── Copy ──
    auto* copyAction = menu.addAction( tr( "Copy" ) );
    copyAction->setShortcut( QKeySequence::Copy );
    copyAction->setEnabled( hasSelection );
    connect( copyAction, &QAction::triggered, this, &CrawlerWidget::copyTableSelection );

    auto* copyWithLinesAction = menu.addAction( tr( "Copy with line numbers" ) );
    copyWithLinesAction->setEnabled( hasSelection );
    connect( copyWithLinesAction, &QAction::triggered, this,
             &CrawlerWidget::copyTableSelectionWithLineNumbers );

    // ── Scratchpad ──
    auto* sendToScratchpadAction = menu.addAction( tr( "Send to scratchpad" ) );
    sendToScratchpadAction->setEnabled( hasText );
    connect( sendToScratchpadAction, &QAction::triggered, this,
             [this, cellText]() { Q_EMIT sendToScratchpad( cellText ); } );

    auto* replaceInScratchpadAction = menu.addAction( tr( "Replace scratchpad" ) );
    replaceInScratchpadAction->setEnabled( hasText );
    connect( replaceInScratchpadAction, &QAction::triggered, this,
             [this, cellText]() { Q_EMIT replaceDataInScratchpad( cellText ); } );

    menu.addSeparator();

    // ── Search ──
    if ( hasText ) {
        const auto escapedCell = QRegularExpression::escape( cellText );

        auto* replaceSearchAction
            = menu.addAction( tr( "Replace search with \"%1\"" )
                                  .arg( cellText.left( 30 ) ) );
        connect( replaceSearchAction, &QAction::triggered, this,
                 [this, escapedCell]() { replaceSearch( escapedCell ); } );

        auto* addToSearchAction
            = menu.addAction( tr( "Add \"%1\" to search" )
                                  .arg( cellText.left( 30 ) ) );
        connect( addToSearchAction, &QAction::triggered, this,
                 [this, escapedCell]() { addToSearch( escapedCell ); } );

        auto* excludeSearchAction
            = menu.addAction( tr( "Exclude \"%1\" from search" )
                                  .arg( cellText.left( 30 ) ) );
        connect( excludeSearchAction, &QAction::triggered, this,
                 [this, escapedCell]() { excludeFromSearch( escapedCell ); } );
    }

    menu.addSeparator();

    // ── Splitter position ──
    auto* saveSplitterAction = menu.addAction( tr( "Save splitter position" ) );
    connect( saveSplitterAction, &QAction::triggered, this,
             &CrawlerWidget::saveSplitterSizes );

    // ── Save to file ──
    auto* saveToFileAction = menu.addAction( tr( "Save to file" ) );
    connect( saveToFileAction, &QAction::triggered, this, [this]() {
        QMetaObject::invokeMethod( logMainView_, "saveToFile" );
    } );

    auto* saveSelectedToFileAction = menu.addAction( tr( "Save selected to file" ) );
    saveSelectedToFileAction->setEnabled( hasSelection );
    connect( saveSelectedToFileAction, &QAction::triggered, this, [this]() {
        QMetaObject::invokeMethod( logMainView_, "saveSelectedToFile" );
    } );

    menu.exec( logTableView_->viewport()->mapToGlobal( pos ) );

    highlightersMenu->clearHighlightersMenu();
}

// Copy selected table rows (display text, tab-separated columns) to clipboard.
// If a portion (in-cell text) selection is active, copy only that portion.
void CrawlerWidget::copyTableSelection()
{
    if ( !logTableView_ || !tableModel_ ) {
        return;
    }

    // If there is an active portion selection, copy just that text
    if ( tableCellSelection_.active && tableCellSelection_.startChar != tableCellSelection_.endChar ) {
        const auto index = tableModel_->index( tableCellSelection_.row,
                                               tableCellSelection_.column );
        const auto cellText = index.data( Qt::DisplayRole ).toString();
        const auto selected = tableCellSelection_.selectedText( cellText );
        if ( !selected.isEmpty() ) {
            QApplication::clipboard()->setText( selected );
            return;
        }
    }

    const auto rows = logTableView_->selectionModel()->selectedRows();
    if ( rows.isEmpty() ) {
        return;
    }

    QStringList lines;
    lines.reserve( rows.size() );
    const int colCount = tableModel_->columnCount();

    for ( const auto& rowIdx : rows ) {
        QStringList cells;
        cells.reserve( colCount );
        for ( int c = 0; c < colCount; ++c ) {
            cells << tableModel_->index( rowIdx.row(), c ).data( Qt::DisplayRole ).toString();
        }
        lines << cells.join( '\t' );
    }

    QApplication::clipboard()->setText( lines.join( '\n' ) );
}

// Copy selected table rows with line numbers prepended.
void CrawlerWidget::copyTableSelectionWithLineNumbers()
{
    if ( !logTableView_ || !tableModel_ ) {
        return;
    }

    const auto rows = logTableView_->selectionModel()->selectedRows();
    if ( rows.isEmpty() ) {
        return;
    }

    QStringList lines;
    lines.reserve( rows.size() );
    const int colCount = tableModel_->columnCount();

    for ( const auto& rowIdx : rows ) {
        // 1-based line number
        const auto lineNum = rowIdx.row() + 1;
        QStringList cells;
        cells.reserve( colCount );
        for ( int c = 0; c < colCount; ++c ) {
            cells << tableModel_->index( rowIdx.row(), c ).data( Qt::DisplayRole ).toString();
        }
        lines << QString( "%1\t%2" ).arg( lineNum ).arg( cells.join( '\t' ) );
    }

    QApplication::clipboard()->setText( lines.join( '\n' ) );
}

// Mark or unmark the selected table rows.
void CrawlerWidget::markTableSelection()
{
    if ( !logTableView_ ) {
        return;
    }

    const auto rows = logTableView_->selectionModel()->selectedRows();
    if ( rows.isEmpty() ) {
        return;
    }

    logsquirl::vector<LineNumber> linesToMark;
    linesToMark.reserve( static_cast<size_t>( rows.size() ) );
    for ( const auto& rowIdx : rows ) {
        linesToMark.push_back( LineNumber( static_cast<uint64_t>( rowIdx.row() ) ) );
    }

    markLinesFromMain( linesToMark );

    // Repaint so mark colors update
    logTableView_->viewport()->update();
}
