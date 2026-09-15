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

#pragma once

#include <cstddef>
#include <memory>
#include <optional>

#include <QTableView>

#include "colorlabelsmanager.h"
#include "containers.h"
#include "linetypes.h"
#include "logformatdefinition.h"
#include "regularexpressionpattern.h"
#include "tableviewselection.h"

class AbstractLogData;
class LogFilteredData;
class LogFormatTableModel;
class LogTableHighlightDelegate;
class Overview;
class OverviewWidget;
class QuickFindPattern;

// The Table View: the Presentation of a Log File as one column per field of
// its Log Format. It owns its model, its highlight delegate, its selection
// (whole Rows and characters inside a cell), its context menu and its
// Overview strip, and remembers column widths per Log Format.
//
// Which Log Format applies is decided by whoever holds the Table View, and
// handed to it through setLogFormat().
class LogTableView : public QTableView {
    Q_OBJECT

public:
    explicit LogTableView( QWidget* parent = nullptr );
    ~LogTableView() override;

    // Show the Log Lines of logData with the given Log Format, or nothing
    // for a null format. The rows appear on the next updateData().
    void setLogFormat( const LogFormatDefinition* format, AbstractLogData* logData );

    // Catch up with the Log File's current Log Lines. filteredData supplies
    // Marks and Matches; with follow, the last Row is scrolled into view.
    void updateData( LogFilteredData* filteredData, bool follow );

    // The Table View positions overviewWidget over its right edge itself, and
    // shows a clicked Log Line.
    void setOverview( Overview* overview, OverviewWidget* overviewWidget );

    // Whether the Table View is the Presentation the user currently sees.
    void setActive( bool active );

    void setQuickFindPattern( std::shared_ptr<QuickFindPattern> pattern );
    void setSearchPattern( const RegularExpressionPattern& pattern );
    void setSearchLimits( LineNumber startLine, LineNumber endLine );
    void setColorLabels( const ColorLabelsManager::QuickHighlightersCollection& labels );
    // Pick up a Configuration change affecting the main search colours.
    void refreshMainSearchHighlighter();
    void updateFont( const QFont& font );

    // Repaint after Marks or Matches changed.
    void updateDecorations();
    // Place the Overview strip and its current-view indicator anew.
    void updateOverview();

    // Scroll the Log Line's Row to the middle and select it.
    void showLogLine( LineNumber line );

    TableViewSelection selection() const;
    QString selectedText() const;

public Q_SLOTS:
    void highlightOverviewLine( LineNumber line );
    void removeOverviewHighlight();

Q_SIGNALS:
    // Sent when a new Row is selected: the Log Line of the first selected Row.
    void newSelection( LineNumber line, LinesCount nLines, LineColumn startCol,
                       LineLength nSymbols );
    void markLines( const logsquirl::vector<LineNumber>& lines );
    void highlightersChange();
    void addToSearch( const QString& text );
    void excludeFromSearch( const QString& text );
    void replaceSearch( const QString& text );
    void setColorLabel( size_t label, const QString& text );
    void clearColorLabels();
    void sendToScratchpad( const QString& text );
    void replaceScratchpad( const QString& text );
    void saveDefaultSplitterSizes();
    void saveToFile();
    void saveSelectedToFile();

protected:
    void mousePressEvent( QMouseEvent* event ) override;
    void mouseMoveEvent( QMouseEvent* event ) override;
    void mouseReleaseEvent( QMouseEvent* event ) override;
    void mouseDoubleClickEvent( QMouseEvent* event ) override;
    void keyPressEvent( QKeyEvent* event ) override;
    bool viewportEvent( QEvent* event ) override;

private:
    void rowSelectionChanged();
    void showContextMenu( const QPoint& pos );
    void copySelection();
    void copySelectionWithLineNumbers();
    void markSelection();

    // Column widths
    void saveColumnWidths();
    bool applySavedColumnWidths();
    void autoSizeColumns();
    void stretchLastColumn();

    // Pixel X in a cell to the character position there.
    int charAtX( const QModelIndex& index, int pixelX ) const;
    void selectWordAt( const QModelIndex& index, int charPos );
    // Hand the in-cell selection to the delegate and repaint.
    void showInCellSelection();

    bool handlesMouse() const;
    void repaintIfActive();

    std::optional<LogFormatDefinition> format_;
    AbstractLogData* logData_ = nullptr;
    LogFormatTableModel* model_ = nullptr;
    LogTableHighlightDelegate* delegate_ = nullptr;

    Overview* overview_ = nullptr;
    OverviewWidget* overviewWidget_ = nullptr;

    ColorLabelsManager::QuickHighlightersCollection colorLabels_;

    TableViewSelection selection_;
    bool selectionDragging_ = false;
    int hoverRow_ = -1;

    bool active_ = false;
    bool columnsNeedSizing_ = false;
    bool programmaticColumnResize_ = false;
};
