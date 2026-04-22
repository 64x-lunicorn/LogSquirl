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

// SPDX-License-Identifier: GPL-3.0-or-later

#include "diff_view_widget.hpp"

#include "anchor_controller.hpp"
#include "configuration.h"
#include "crawlerwidget.h"
#include "diff_view_actions.hpp"
#include "diff_view_strings.hpp"
#include "gutter_widget.hpp"
#include "iconloader.h"
#include "infoline.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logmainview.h"
#include "merged_filtered_view.hpp"
#include "quickfindpattern.h"
#include "savedsearches.h"

#include <QDataStream>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QScrollBar>
#include <QVBoxLayout>

#include <limits>

DiffViewWidget::DiffViewWidget( QWidget* parent )
    : QWidget( parent )
{
    setup();
}

DiffViewWidget::~DiffViewWidget()
{
    stopLoading();
}

void DiffViewWidget::stopLoading()
{
    if ( leftPane_ ) {
        leftPane_->stopLoading();
    }
    if ( rightPane_ ) {
        rightPane_->stopLoading();
    }
}

void DiffViewWidget::setup()
{
    auto* mainLayout = new QVBoxLayout( this );
    mainLayout->setContentsMargins( 0, 0, 0, 0 );
    mainLayout->setSpacing( 0 );

    // --- Main vertical splitter: panes on top, results on bottom ---
    mainSplitter_ = new QSplitter( Qt::Vertical, this );
    mainLayout->addWidget( mainSplitter_, /*stretch=*/1 );

    // --- Pane splitter (CrawlerA | Gutter | CrawlerB) ---
    paneSplitter_ = new QSplitter( Qt::Horizontal );
    gutter_ = new GutterWidget( this );
    mainSplitter_->addWidget( paneSplitter_ );

    // --- Bottom panel: search bar + filtered results ---
    auto* bottomPanel = new QWidget;
    auto* bottomLayout = new QVBoxLayout( bottomPanel );
    bottomLayout->setContentsMargins( 0, 0, 0, 0 );
    bottomLayout->setSpacing( 0 );

    // -- Search bar (1:1 replica of CrawlerWidget's layout) --
    searchPanel_ = new QWidget( bottomPanel );
    auto* searchLayout = new QHBoxLayout( searchPanel_ );
    searchLayout->setContentsMargins( 2, 2, 2, 2 );

    // Visibility combo box
    using VisibilityFlags = LogFilteredData::VisibilityFlags;
    visibilityModel_ = new QStandardItemModel( this );

    auto* marksAndMatchesItem = new QStandardItem( tr( "Marks and matches" ) );
    marksAndMatchesItem->setData(
        QVariant::fromValue( VisibilityFlags::Marks | VisibilityFlags::Matches ) );
    visibilityModel_->appendRow( marksAndMatchesItem );

    auto* marksMatchesBreadcrumbsItem
        = new QStandardItem( tr( "Marks, matches + breadcrumbs" ) );
    marksMatchesBreadcrumbsItem->setData( QVariant::fromValue(
        VisibilityFlags::Marks | VisibilityFlags::Matches | VisibilityFlags::Context ) );
    visibilityModel_->appendRow( marksMatchesBreadcrumbsItem );

    auto* matchesBreadcrumbsItem = new QStandardItem( tr( "Matches + breadcrumbs" ) );
    matchesBreadcrumbsItem->setData( QVariant::fromValue(
        VisibilityFlags::Matches | VisibilityFlags::Context ) );
    visibilityModel_->appendRow( matchesBreadcrumbsItem );

    auto* marksItem = new QStandardItem( tr( "Marks" ) );
    marksItem->setData(
        QVariant::fromValue<FilteredView::Visibility>( VisibilityFlags::Marks ) );
    visibilityModel_->appendRow( marksItem );

    auto* matchesItem = new QStandardItem( tr( "Matches" ) );
    matchesItem->setData(
        QVariant::fromValue<FilteredView::Visibility>( VisibilityFlags::Matches ) );
    visibilityModel_->appendRow( matchesItem );

    auto* visibilityView = new QListView( this );
    visibilityView->setMovement( QListView::Static );

    visibilityBox_ = new QComboBox();
    visibilityBox_->setModel( visibilityModel_ );
    visibilityBox_->setView( visibilityView );
    visibilityBox_->setCurrentIndex( 0 );
    visibilityBox_->setContentsMargins( 2, 2, 2, 2 );

    // Toggle buttons
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

    // Search line with completer and history
    auto& savedSearches = SavedSearches::getSynced();
    searchLineCompleter_ = new QCompleter( savedSearches.recentSearches(), this );
    searchLineEdit_ = new QComboBox;
    searchLineEdit_->setEditable( true );
    searchLineEdit_->setCompleter( searchLineCompleter_ );
    searchLineEdit_->addItems( savedSearches.recentSearches() );
    searchLineEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );
    searchLineEdit_->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    searchLineEdit_->lineEdit()->setMaxLength( std::numeric_limits<int>::max() / 1024 );
    searchLineEdit_->setContentsMargins( 2, 2, 2, 2 );
    searchLineEdit_->setAccessibleName( tr( "Search pattern" ) );

    // Tab order
    setTabOrder( searchLineEdit_, matchCaseButton_ );
    setTabOrder( matchCaseButton_, useRegexpButton_ );
    setTabOrder( useRegexpButton_, inverseButton_ );
    setTabOrder( inverseButton_, booleanButton_ );
    setTabOrder( booleanButton_, searchRefreshButton_ );

    // Context menu on search line
    searchLineContextMenu_ = searchLineEdit_->lineEdit()->createStandardContextMenu();
    searchLineEdit_->setContextMenuPolicy( Qt::CustomContextMenu );

    setFocusProxy( searchLineEdit_ );

    // Action buttons
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

    // Info line with progress gauge
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

    // Layout order (same as CrawlerWidget)
    searchLayout->addWidget( visibilityBox_ );
    searchLayout->addWidget( matchCaseButton_ );
    searchLayout->addWidget( useRegexpButton_ );
    searchLayout->addWidget( inverseButton_ );
    searchLayout->addWidget( booleanButton_ );
    searchLayout->addWidget( searchRefreshButton_ );
    searchLayout->addWidget( searchLineEdit_ );
    searchLayout->addWidget( clearButton_ );
    searchLayout->addWidget( searchButton_ );
    searchLayout->addWidget( keepSearchResultsButton_ );
    searchLayout->addWidget( stopButton_ );
    searchLayout->addWidget( searchInfoLine_ );

    bottomLayout->addWidget( searchPanel_ );

    // -- Merged filtered results from both panes --
    mergedData_ = new MergedLogData( this );
    mergedQuickFindPattern_ = std::make_shared<QuickFindPattern>();
    mergedView_ = new MergedFilteredView( mergedData_, mergedQuickFindPattern_.get(),
                                          bottomPanel );
    applyMergedViewFont();
    bottomLayout->addWidget( mergedView_, /*stretch=*/1 );

    mainSplitter_->addWidget( bottomPanel );

    // Give most space to the panes, smaller portion to results.
    mainSplitter_->setStretchFactor( 0, 3 );
    mainSplitter_->setStretchFactor( 1, 1 );

    // Load icons to match CrawlerWidget's search bar.
    loadIcons();

    // Default search checkboxes from config (same as CrawlerWidget).
    auto& config = Configuration::get();
    searchRefreshButton_->setChecked( config.isSearchAutoRefreshDefault() );
    matchCaseButton_->setChecked( !config.isSearchIgnoreCaseDefault() );
    useRegexpButton_->setChecked( config.mainRegexpType() == SearchRegexpType::ExtendedRegexp );
    booleanButton_->setChecked( config.isSearchLogicalCombiningDefault() );

    // Start with search disabled (no anchors yet).
    updateSearchBarState();

    // --- Anchor controller ---
    anchorController_ = new AnchorController( &anchors_, this );
    connect( anchorController_, &AnchorController::anchorsChanged, this,
             &DiffViewWidget::onAnchorsChanged );
    connect( anchorController_, &AnchorController::pendingChanged, this,
             &DiffViewWidget::updateStatusBar );

    // --- Actions ---
    diffActions_ = new DiffViewActions( this );
    connect( diffActions_->clearAnchorsAction(), &QAction::triggered, anchorController_,
             &AnchorController::clearAnchors );
    connect( diffActions_->swapPanesAction(), &QAction::triggered, anchorController_,
             &AnchorController::swapPanes );

    addAction( diffActions_->clearAnchorsAction() );
    addAction( diffActions_->swapPanesAction() );

    // --- Shared search wiring (same connections as CrawlerWidget) ---
    connect( searchLineEdit_->lineEdit(), &QLineEdit::returnPressed, searchButton_,
             &QToolButton::click );

    connect( searchButton_, &QToolButton::clicked, this, [ this ]() {
        const auto pattern = searchLineEdit_->currentText();
        if ( !leftPane_ || !rightPane_ ) {
            return;
        }

        // Record search in history.
        SavedSearches::getSynced().addRecent( pattern );

        const bool matchCase = matchCaseButton_->isChecked();
        const bool useRegexp = useRegexpButton_->isChecked();
        const bool inverse = inverseButton_->isChecked();
        const bool boolean = booleanButton_->isChecked();

        leftPane_->applyExternalSearch( pattern, matchCase, useRegexp, inverse, boolean );
        rightPane_->applyExternalSearch( pattern, matchCase, useRegexp, inverse, boolean );
    } );

    connect( clearButton_, &QToolButton::clicked, searchLineEdit_, &QComboBox::clearEditText );

    connect( searchLineEdit_, &QWidget::customContextMenuRequested, this,
             [ this ]( const QPoint& pos ) {
                 if ( searchLineContextMenu_ ) {
                     searchLineContextMenu_->exec( searchLineEdit_->mapToGlobal( pos ) );
                 }
             } );
}

void DiffViewWidget::loadIcons()
{
    IconLoader iconLoader( this );
    searchRefreshButton_->setIcon( iconLoader.load( "icons8-search-refresh" ) );
    useRegexpButton_->setIcon( iconLoader.load( "regex" ) );
    inverseButton_->setIcon( iconLoader.load( "icons8-not-equal" ) );
    booleanButton_->setIcon( iconLoader.load( "icons8-venn-diagram" ) );
    clearButton_->setIcon( iconLoader.load( "icons8-delete" ) );
    searchButton_->setIcon( iconLoader.load( "icons8-search" ) );
    keepSearchResultsButton_->setIcon( iconLoader.load( "icons8-lock" ) );
    matchCaseButton_->setIcon( iconLoader.load( "icons8-font-size" ) );
    stopButton_->setIcon( iconLoader.load( "icons8-close-window" ) );
}

void DiffViewWidget::openFiles( const QString& fileA, const QString& fileB )
{
    filePathA_ = fileA;
    filePathB_ = fileB;

    leftPane_ = createPane( fileA );
    rightPane_ = createPane( fileB );

    // Insert into splitter: Left | Gutter | Right.
    paneSplitter_->addWidget( leftPane_ );
    paneSplitter_->addWidget( gutter_ );
    paneSplitter_->addWidget( rightPane_ );

    // Make left and right panes share the space equally.
    paneSplitter_->setStretchFactor( 0, 1 );
    paneSplitter_->setStretchFactor( 1, 0 );
    paneSplitter_->setStretchFactor( 2, 1 );

    gutter_->setAnchors( &anchors_ );
    gutter_->setPanes( leftPane_, rightPane_ );

    connectScrollSignals( leftPane_ );
    connectScrollSignals( rightPane_ );

    injectContextMenuActions( leftPane_, AnchorController::Pane::Left );
    injectContextMenuActions( rightPane_, AnchorController::Pane::Right );

    // Hide per-pane search/filtered-view panels so only the shared bar remains.
    hidePerPaneSearchPanel( leftPane_ );
    hidePerPaneSearchPanel( rightPane_ );

    // Hide the overview sidebar in diff panes to maximise horizontal space.
    hidePerPaneOverview( leftPane_ );
    hidePerPaneOverview( rightPane_ );

    // Wire the merged data model to the filtered data from both panes.
    mergedData_->setSources( filteredDataA_.get(), filteredDataB_.get(), &anchors_ );
    connectSearchSignals();

    Q_EMIT dataStatusChanged( DataStatus::NEW_DATA );
}

CrawlerWidget* DiffViewWidget::createPane( const QString& filePath )
{
    auto logData = std::make_shared<LogData>();
    auto filteredData = std::shared_ptr<LogFilteredData>( logData->getNewFilteredData() );

    auto* crawler = new CrawlerWidget( this );
    auto qfp = std::make_shared<QuickFindPattern>();

    // Follow the same setup sequence as Session::openAlways():
    // setData → setQuickFindPattern → setSavedSearches (which triggers setup()).
    crawler->setData( logData, filteredData );
    crawler->setQuickFindPattern( qfp );
    crawler->setSavedSearches( &SavedSearches::getSynced() );

    // Start loading the file.
    logData->attachFile( filePath );

    // Store references so data stays alive.
    if ( !logDataA_ ) {
        logDataA_ = std::move( logData );
        filteredDataA_ = std::move( filteredData );
        quickFindPatternA_ = std::move( qfp );
    }
    else {
        logDataB_ = std::move( logData );
        filteredDataB_ = std::move( filteredData );
        quickFindPatternB_ = std::move( qfp );
    }

    return crawler;
}

void DiffViewWidget::hidePerPaneSearchPanel( CrawlerWidget* pane )
{
    // CrawlerWidget is a QSplitter with two children:
    //   widget(0) = LogMainView  (keep visible)
    //   widget(1) = bottomWindow (search bar + filtered view — hide it)
    if ( pane->count() > 1 ) {
        pane->widget( 1 )->hide();
    }
}

void DiffViewWidget::hidePerPaneOverview( CrawlerWidget* pane )
{
    // Hide the overview sidebar in each pane's LogMainView so the text area
    // stretches to the full width of the pane.
    auto* mainView = pane->findChild<LogMainView*>();
    if ( mainView ) {
        mainView->setOverviewVisible( false );
        // Lock the overview so that CrawlerWidget::applyConfiguration() cannot
        // re-enable it (it reads config.isOverviewVisible() and calls
        // refreshOverview(), which would undo our hide).
        mainView->setOverviewLocked( true );
    }
}

void DiffViewWidget::connectSearchSignals()
{
    // When either pane's search progresses, rebuild the merged data and refresh
    // the view.
    auto rebuildMerged = [ this ]() {
        mergedData_->rebuild();
        mergedView_->updateData();
        // Extend search limits to cover all merged rows so that the view does
        // not dim them as "outside search range" and applies highlighting.
        mergedView_->setSearchLimits( 0_lnum,
                                      LineNumber( mergedData_->getNbLine().get() ) );
    };

    if ( filteredDataA_ ) {
        connect( filteredDataA_.get(), &LogFilteredData::searchProgressed, this,
                 [ rebuildMerged ]( LinesCount /*nbMatches*/, int /*progress*/,
                                   LineNumber /*initialLine*/ ) { rebuildMerged(); } );
    }
    if ( filteredDataB_ ) {
        connect( filteredDataB_.get(), &LogFilteredData::searchProgressed, this,
                 [ rebuildMerged ]( LinesCount /*nbMatches*/, int /*progress*/,
                                   LineNumber /*initialLine*/ ) { rebuildMerged(); } );
    }

    // Navigate to the source line when the user activates a row.
    connect( mergedView_, &MergedFilteredView::lineActivated, this,
             [ this ]( MergedSource source, LineNumber line ) {
                 auto* pane = ( source == MergedSource::FileA ) ? leftPane_ : rightPane_;
                 auto* mainView = pane->findChild<LogMainView*>();
                 if ( mainView ) {
                     mainView->jumpToLine( line );
                 }
             } );
}

void DiffViewWidget::connectScrollSignals( CrawlerWidget* pane )
{
    // Find the LogMainView inside the CrawlerWidget to monitor scroll changes.
    auto* mainView = pane->findChild<LogMainView*>();
    if ( !mainView ) {
        return;
    }

    if ( pane == leftPane_ ) {
        connect( mainView, &LogMainView::activity, this, &DiffViewWidget::onLeftPaneScrolled );
    }
    else {
        connect( mainView, &LogMainView::activity, this, &DiffViewWidget::onRightPaneScrolled );
    }
}

void DiffViewWidget::injectContextMenuActions( CrawlerWidget* pane,
                                                AnchorController::Pane side )
{
    // Find the LogMainView inside the CrawlerWidget.
    auto* mainView = pane->findChild<LogMainView*>();
    if ( !mainView ) {
        return;
    }

    // Track the selected line when the user clicks in this pane.
    connect( mainView, &LogMainView::newSelection, this,
             [ this, side ]( LineNumber startLine, LinesCount, LineColumn, LineLength ) {
                 const auto line = static_cast<int64_t>( startLine.get() );
                 if ( side == AnchorController::Pane::Left ) {
                     leftSelectedLine_ = line;
                 }
                 else {
                     rightSelectedLine_ = line;
                 }
             } );

    // Find the context menu (popupMenu_) via findChild and append our actions.
    auto* menu = mainView->findChild<QMenu*>();
    if ( !menu ) {
        return;
    }

    menu->addSeparator();

    // "Set Anchor" action — each pane gets its own copy so we know which side was clicked.
    auto* setAnchorAction = new QAction( DiffViewStrings::SetAnchor, menu );
    setAnchorAction->setShortcut( QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_A ) );
    connect( setAnchorAction, &QAction::triggered, this, [ this, side ]() {
        const auto line = ( side == AnchorController::Pane::Left ) ? leftSelectedLine_
                                                                    : rightSelectedLine_;
        anchorController_->setAnchor( side, line );
    } );
    menu->addAction( setAnchorAction );

    // Add clear and swap from shared actions.
    menu->addAction( diffActions_->clearAnchorsAction() );
    menu->addAction( diffActions_->swapPanesAction() );
}

void DiffViewWidget::updateStatusBar()
{
    if ( anchorController_->hasPending() ) {
        const auto [ pane, line ] = *anchorController_->pending();
        const auto paneName
            = ( pane == AnchorController::Pane::Left ) ? QStringLiteral( "A" )
                                                       : QStringLiteral( "B" );
        searchInfoLine_->setText(
            QString( DiffViewStrings::PendingAnchor ).arg( paneName ).arg( line + 1 ) );
    }
    else if ( anchors_.empty() ) {
        searchInfoLine_->setText( DiffViewStrings::NoAnchors );
    }
    else {
        searchInfoLine_->setText(
            QString( DiffViewStrings::AnchorCount ).arg( anchors_.size() ) );
    }
}

void DiffViewWidget::onLeftPaneScrolled()
{
    if ( !scrollSyncEnabled_ || scrollGuard_ || anchors_.empty() || !rightPane_ ) {
        return;
    }

    scrollGuard_ = true;

    const auto topLineA = static_cast<int64_t>( leftPane_->getTopLine().get() );
    const auto mappedLine = anchors_.mapAtoB( topLineA );

    auto* rightView = rightPane_->findChild<LogMainView*>();
    if ( rightView ) {
        rightView->scrollToTopLine( LineNumber( static_cast<uint64_t>( std::max( mappedLine, int64_t{ 0 } ) ) ) );
    }

    gutter_->refresh();
    scrollGuard_ = false;
}

void DiffViewWidget::onRightPaneScrolled()
{
    if ( !scrollSyncEnabled_ || scrollGuard_ || anchors_.empty() || !leftPane_ ) {
        return;
    }

    scrollGuard_ = true;

    const auto topLineB = static_cast<int64_t>( rightPane_->getTopLine().get() );
    const auto mappedLine = anchors_.mapBtoA( topLineB );

    auto* leftView = leftPane_->findChild<LogMainView*>();
    if ( leftView ) {
        leftView->scrollToTopLine( LineNumber( static_cast<uint64_t>( std::max( mappedLine, int64_t{ 0 } ) ) ) );
    }

    gutter_->refresh();
    scrollGuard_ = false;
}

void DiffViewWidget::onAnchorsChanged()
{
    updateSearchBarState();
    updateStatusBar();
    gutter_->refresh();

    // Immediately sync scroll positions based on the new anchors so that both
    // panes align at the anchor point without requiring a manual scroll first.
    if ( scrollSyncEnabled_ && !anchors_.empty() && leftPane_ && rightPane_ ) {
        scrollGuard_ = true;
        const auto topLineA = static_cast<int64_t>( leftPane_->getTopLine().get() );
        const auto mappedLine = anchors_.mapAtoB( topLineA );
        auto* rightView = rightPane_->findChild<LogMainView*>();
        if ( rightView ) {
            rightView->scrollToTopLine( LineNumber(
                static_cast<uint64_t>( std::max( mappedLine, int64_t{ 0 } ) ) ) );
        }
        scrollGuard_ = false;
    }
}

void DiffViewWidget::updateSearchBarState()
{
    const bool enabled = !anchors_.empty();

    visibilityBox_->setEnabled( enabled );
    searchLineEdit_->setEnabled( enabled );
    matchCaseButton_->setEnabled( enabled );
    useRegexpButton_->setEnabled( enabled );
    inverseButton_->setEnabled( enabled );
    booleanButton_->setEnabled( enabled );
    searchRefreshButton_->setEnabled( enabled );
    searchButton_->setEnabled( enabled );
    clearButton_->setEnabled( enabled );
    keepSearchResultsButton_->setEnabled( enabled );

    if ( !enabled ) {
        searchLineEdit_->setToolTip( DiffViewStrings::SearchDisabledTooltip );
    }
    else {
        searchLineEdit_->setToolTip( QString{} );
    }
}

void DiffViewWidget::setScrollSyncEnabled( bool enabled )
{
    scrollSyncEnabled_ = enabled;
}

AnchorSet& DiffViewWidget::anchors()
{
    return anchors_;
}

const AnchorSet& DiffViewWidget::anchors() const
{
    return anchors_;
}

CrawlerWidget* DiffViewWidget::leftPane() const
{
    return leftPane_;
}

CrawlerWidget* DiffViewWidget::rightPane() const
{
    return rightPane_;
}

QString DiffViewWidget::tabTitle() const
{
    const auto nameA = QFileInfo( filePathA_ ).fileName();
    const auto nameB = QFileInfo( filePathB_ ).fileName();
    return QString( DiffViewStrings::TabTitle ).arg( nameA, nameB );
}

// --- Persistence ---

// Stream format:  magic(quint32) | version(quint32) | filePathA | filePathB
//                 | anchorsJson(QByteArray) | splitterState(QByteArray)
static constexpr quint32 DiffViewMagic = 0x4C534456; // "LSDV"
static constexpr quint32 DiffViewVersion = 1;

QByteArray DiffViewWidget::saveState() const
{
    QByteArray data;
    QDataStream stream( &data, QIODevice::WriteOnly );
    stream.setVersion( QDataStream::Qt_6_0 );

    stream << DiffViewMagic;
    stream << DiffViewVersion;
    stream << filePathA_;
    stream << filePathB_;

    const auto anchorsDoc = QJsonDocument( anchors_.toJson() );
    stream << anchorsDoc.toJson( QJsonDocument::Compact );

    if ( paneSplitter_ ) {
        stream << paneSplitter_->saveState();
    }
    else {
        stream << QByteArray{};
    }

    if ( mainSplitter_ ) {
        stream << mainSplitter_->saveState();
    }
    else {
        stream << QByteArray{};
    }

    return data;
}

bool DiffViewWidget::restoreState( const QByteArray& state )
{
    QDataStream stream( state );
    stream.setVersion( QDataStream::Qt_6_0 );

    quint32 magic = 0;
    quint32 version = 0;
    stream >> magic >> version;

    if ( magic != DiffViewMagic || version != DiffViewVersion ) {
        return false;
    }

    QString fileA;
    QString fileB;
    QByteArray anchorsJson;
    QByteArray splitterState;
    QByteArray mainSplitterState;

    stream >> fileA >> fileB >> anchorsJson >> splitterState;

    // mainSplitterState is optional (added in version 1 after initial release).
    if ( !stream.atEnd() ) {
        stream >> mainSplitterState;
    }

    if ( stream.status() != QDataStream::Ok ) {
        return false;
    }

    // Open the files into the panes.
    openFiles( fileA, fileB );

    // Restore anchors.
    const auto doc = QJsonDocument::fromJson( anchorsJson );
    if ( doc.isArray() ) {
        anchors_.fromJson( doc.array() );
        onAnchorsChanged();
    }

    // Restore splitter geometries.
    if ( !splitterState.isEmpty() && paneSplitter_ ) {
        paneSplitter_->restoreState( splitterState );
    }
    if ( !mainSplitterState.isEmpty() && mainSplitter_ ) {
        mainSplitter_->restoreState( mainSplitterState );
    }

    return true;
}

void DiffViewWidget::applyMergedViewFont()
{
    if ( !mergedView_ ) {
        return;
    }

    const auto& config = Configuration::get();
    QFont font = config.mainFont();
    font.setKerning( false );
    font.setFixedPitch( true );

    if ( config.forceFontAntialiasing() ) {
        font.setStyleStrategy( QFont::PreferAntialias );
    }

    font.setBold( config.useBoldFont() );

    mergedView_->updateFont( font );
}
