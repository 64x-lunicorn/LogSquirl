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
#include "logpresentation.h"
#include "regularexpressionpattern.h"
#include "rowmapping.h"
#include "settingspolicies.h"
#include "tableviewselection.h"

class AbstractLogData;
class LogFilteredData;
class LogFormatTableModel;
class LogTableHighlightDelegate;
class Overview;
class OverviewWidget;
class Portion;
class QMenu;
class QuickFind;
class QuickFindPattern;

// The Table View: the Presentation of a Log File as one column per field of
// its Log Format. It owns its model, its highlight delegate, its selection
// (whole Rows and characters inside a cell), its context menu and its
// Overview strip, and remembers column widths per Log Format.
//
// Which Log Format applies is decided by whoever holds the Table View, and
// handed to it through setLogFormat().
//
// Which Log Line each Row shows is its RowMapping's to say; everything the
// Table View hands out is a Log Line obtained through it.
class LogTableView : public QTableView, public LogPresentation {
    Q_OBJECT

public:
    // Each Row shows one Log Line.
    explicit LogTableView( QWidget* parent = nullptr );
    // Each Row shows the Log Line rows maps it onto.
    explicit LogTableView( std::shared_ptr<const RowMapping> rows, QWidget* parent = nullptr );
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
    // Place the Overview strip and its current-view indicator anew.
    void updateOverview();

    // LogPresentation
    // The characters selected inside a cell if there are any, otherwise the
    // selected Rows, each as its cells separated by tabs.
    QString selectedText() const override;
    OptionalLineNumber logLineAt( const QPoint& pos ) const override;
    // Scroll the Log Line's Row to the middle and select it.
    void showLogLine( LineNumber line ) override;
    // Selects the Log Line's Row: the Table View selects no characters for
    // a Log Line.
    void showLogLinePortion( LineNumber line, LinesCount nLines, LineColumn startCol,
                             LineLength nSymbols ) override;
    // Repaints, if the Table View is active.
    void updateDecorations() override;
    // Drops the Rows read so far; they are read again as they are shown.
    void rereadLogLines() override;
    void updateFont( const QFont& font ) override;
    void registerShortcuts() override;
    // Hand over the settings that color Log Lines, after a settings change:
    // painting reads no setting of its own.
    void setDecorationPolicy( const DecorationPolicy& policy ) override;
    // Shows the Overview as it says; the Table View has no line numbers.
    void setPresentationPolicy( const PresentationPolicy& policy ) override;
    // Hand over the settings that say how the text the user selected is read
    // as a QuickFind pattern. Call it when the view is built and again after
    // a settings change: the view reads no setting of its own, and nothing is
    // derived from this and kept, so a change takes effect at the next
    // QuickFind.
    void setQuickFindPolicy( const QuickFindPolicy& policy ) override;
    // Ignored: the Table View follows only as the Text View does.
    void allowFollowMode( bool allow ) override;
    void setColorLabels( const ColorLabelsManager::QuickHighlightersCollection& labels ) override;
    void setSearchLimits( LineNumber startLine, LineNumber endLine ) override;
    // Saves the Log Lines of the selected Rows, in Log Line order.
    void saveSelectedTo( const QString& filename ) override;

    TableViewSelection selection() const;
    // The Log Lines of the selected Rows.
    logsquirl::vector<LineNumber> selectedLogLines() const;

public Q_SLOTS:
    void highlightOverviewLine( LineNumber line );
    void removeOverviewHighlight();

Q_SIGNALS:
    // The signals every Presentation emits, named and meant as the Text
    // View's (see LogPresentation). The Table View declares only those it
    // emits: turning following on or off from the view, zooming with the
    // wheel and the exit-view shortcut are the Text View's alone, and their
    // signals are not in the set.

    // Sent when a new Row is selected: the Log Line of the first selected Row.
    void newSelection( LineNumber startLine, LinesCount nLines, LineColumn startCol,
                       LineLength nSymbols );
    void markLines( const logsquirl::vector<LineNumber>& lines );
    void highlightersChange();
    void addToSearch( const QString& selection );
    void excludeFromSearch( const QString& selection );
    void replaceSearch( const QString& selection );
    void changeSearchLimits( LineNumber startLine, LineNumber endLine );
    void clearSearchLimits();
    // Label the selected text; the holder asks selectedText() for it.
    void addColorLabel( size_t label );
    void clearColorLabels();
    // Send the selected text; the holder asks selectedText() for it.
    void sendSelectionToScratchpad();
    void replaceScratchpadWithSelection();
    void saveDefaultSplitterSizes();
    void activity();

protected:
    // The context menu for the current selection, opened at pos in viewport
    // coordinates; built by PresentationMenu, and not yet shown.
    std::unique_ptr<QMenu> createContextMenu( const QPoint& pos );

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
    // Asks for a file, and saves the Log Lines of the selected Rows to it.
    void saveSelectedToFile();
    // Asks for a file, and saves the Log Lines of every Row to it.
    void saveToFile();

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

    // Selects the next (or previous) Log Line after the one whose characters
    // are selected that matches them, among the Log Lines the Rows show. The
    // selected characters become the QuickFind pattern, as in the Text View.
    void findSelected( bool forward );
    // Selects the Row of the Log Line QuickFind found, and the matching
    // characters in its first cell holding them.
    void showQuickFindResult( bool hasMatch, const Portion& logLinePortion );

    bool handlesMouse() const;
    void repaintIfActive();

    std::shared_ptr<const RowMapping> rows_;
    // Not owned: the coordinator holds the Log Format for as long as it is set
    const LogFormatDefinition* format_ = nullptr;
    AbstractLogData* logData_ = nullptr;
    // Supplies the Marks.
    LogFilteredData* filteredData_ = nullptr;
    LogFormatTableModel* model_ = nullptr;
    LogTableHighlightDelegate* delegate_ = nullptr;

    Overview* overview_ = nullptr;
    OverviewWidget* overviewWidget_ = nullptr;

    ColorLabelsManager::QuickHighlightersCollection colorLabels_;

    // The Search Limits last set; without an end, they end with the Log File.
    LineNumber searchStart_;
    OptionalLineNumber searchEnd_;

    TableViewSelection selection_;

    // How the text the user selected is read as a QuickFind pattern, as this
    // view's holder last handed it over.
    QuickFindPolicy quickFindPolicy_;

    std::shared_ptr<QuickFindPattern> quickFindPattern_;
    // Searches for Find next and Find previous, off the UI thread.
    std::unique_ptr<QuickFind> quickFind_;
    bool selectionDragging_ = false;
    int hoverRow_ = -1;

    bool active_ = false;
    bool columnsNeedSizing_ = false;
    bool programmaticColumnResize_ = false;
};
