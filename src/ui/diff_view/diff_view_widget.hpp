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

#pragma once

#include <memory>

#include <QByteArray>
#include <QComboBox>
#include <QCompleter>
#include <QMenu>
#include <QPalette>
#include <QSplitter>
#include <QStandardItemModel>
#include <QToolButton>
#include <QWidget>

#include "anchor.hpp"
#include "anchor_controller.hpp"
#include "diff_view_actions.hpp"
#include "loadingstatus.h"

class CrawlerWidget;
class GutterWidget;
class InfoLine;
class LogData;
class LogFilteredData;
class MergedFilteredView;
class MergedLogData;
class QuickFindPattern;
class SavedSearches;

/// Two full CrawlerWidget instances side-by-side with anchor-based scroll sync.
///
/// Each CrawlerWidget's main log view is displayed in the top half.
/// A single shared search bar drives both panes, and the filtered results
/// from both panes are merged into one interleaved view sorted by anchor sync.
class DiffViewWidget : public QWidget {
    Q_OBJECT

  public:
    explicit DiffViewWidget( QWidget* parent = nullptr );
    ~DiffViewWidget() override;

    /// Stop any ongoing indexing or search in both panes.
    void stopLoading();

    /// Load files into the two panes.
    void openFiles( const QString& fileA, const QString& fileB );

    /// Access the anchor set for external manipulation.
    AnchorSet& anchors();
    const AnchorSet& anchors() const;

    /// Access each CrawlerWidget pane.
    CrawlerWidget* leftPane() const;
    CrawlerWidget* rightPane() const;

    /// Returns a display title for the tab.
    QString tabTitle() const;

    /// Serialize the diff view state (file paths, anchors, splitter geometry).
    QByteArray saveState() const;

    /// Restore a previously saved diff view state.
    /// Returns true if the state was restored, false if the data was invalid.
    bool restoreState( const QByteArray& state );

  Q_SIGNALS:
    /// Required by TabbedCrawlerWidget::addCrawler<T> template.
    void dataStatusChanged( DataStatus status );

  public Q_SLOTS:
    /// Enable or disable scroll synchronization.
    void setScrollSyncEnabled( bool enabled );

  private Q_SLOTS:
    /// Called when either pane's main view scrolls.
    void onLeftPaneScrolled();
    void onRightPaneScrolled();

    /// Called when an anchor is added or removed — updates gutter and search bar.
    void onAnchorsChanged();

  private:
    /// Build the UI layout.
    void setup();

    /// Create a CrawlerWidget with its own LogData and wire it up.
    CrawlerWidget* createPane( const QString& filePath );

    /// Connect scroll signals for a pane.
    void connectScrollSignals( CrawlerWidget* pane );

    /// Inject anchor actions into a CrawlerWidget's context menu.
    void injectContextMenuActions( CrawlerWidget* pane, AnchorController::Pane side );

    /// Hide the per-pane search/filtered-view bottom panel.
    void hidePerPaneSearchPanel( CrawlerWidget* pane );

    /// Hide the overview sidebar in a pane to maximise text width.
    void hidePerPaneOverview( CrawlerWidget* pane );

    /// Connect searchProgressed signals to rebuild the merged view.
    void connectSearchSignals();

    /// Apply the configured monospace font to the merged filtered view.
    void applyMergedViewFont();

    /// Update the shared search bar enabled state.
    void updateSearchBarState();

    /// Update info label text from controller state.
    void updateStatusBar();

    /// Load icons for the search bar buttons.
    void loadIcons();

    // --- Layout ---
    QSplitter* mainSplitter_ = nullptr;       // vertical: panes above, results below
    QSplitter* paneSplitter_ = nullptr;       // horizontal: left | gutter | right
    MergedLogData* mergedData_ = nullptr;      // merged search results
    MergedFilteredView* mergedView_ = nullptr; // AbstractLogView over merged data

    // --- Panes ---
    CrawlerWidget* leftPane_ = nullptr;
    CrawlerWidget* rightPane_ = nullptr;
    GutterWidget* gutter_ = nullptr;

    // --- Data (owned by DiffViewWidget, not by Session) ---
    std::shared_ptr<LogData> logDataA_;
    std::shared_ptr<LogFilteredData> filteredDataA_;
    std::shared_ptr<LogData> logDataB_;
    std::shared_ptr<LogFilteredData> filteredDataB_;

    // --- Shared resources for CrawlerWidgets ---
    std::shared_ptr<QuickFindPattern> quickFindPatternA_;
    std::shared_ptr<QuickFindPattern> quickFindPatternB_;
    std::shared_ptr<QuickFindPattern> mergedQuickFindPattern_;

    // --- Anchors ---
    AnchorSet anchors_;
    AnchorController* anchorController_ = nullptr;
    DiffViewActions* diffActions_ = nullptr;

    // --- Tracked selection lines (updated on right-click) ---
    int64_t leftSelectedLine_ = 0;
    int64_t rightSelectedLine_ = 0;

    // --- Shared search bar (1:1 replica of CrawlerWidget layout) ---
    QWidget* searchPanel_ = nullptr;
    QComboBox* visibilityBox_ = nullptr;
    QStandardItemModel* visibilityModel_ = nullptr;
    QToolButton* matchCaseButton_ = nullptr;
    QToolButton* useRegexpButton_ = nullptr;
    QToolButton* inverseButton_ = nullptr;
    QToolButton* booleanButton_ = nullptr;
    QToolButton* searchRefreshButton_ = nullptr;
    QComboBox* searchLineEdit_ = nullptr;
    QCompleter* searchLineCompleter_ = nullptr;
    QMenu* searchLineContextMenu_ = nullptr;
    QToolButton* clearButton_ = nullptr;
    QToolButton* searchButton_ = nullptr;
    QToolButton* keepSearchResultsButton_ = nullptr;
    QToolButton* stopButton_ = nullptr;
    InfoLine* searchInfoLine_ = nullptr;
    QPalette searchInfoLineDefaultPalette_;

    // --- Scroll sync ---
    bool scrollSyncEnabled_ = true;
    bool scrollGuard_ = false;

    // --- File paths ---
    QString filePathA_;
    QString filePathB_;
};
