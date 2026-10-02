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
#include <vector>

#include <QDate>
#include <QTableView>

#include "colorlabelsmanager.h"
#include "containers.h"
#include "csvexport.h"
#include "csvexportdialog.h"
#include "linetypes.h"
#include "logfileview.h"
#include "logformatdefinition.h"
#include "logformattablemodel.h"
#include "logpresentation.h"
#include "quickfindmux.h"
#include "regularexpressionpattern.h"
#include "rowmapping.h"
#include "settingspolicies.h"
#include "tableviewselection.h"
#include "tableviewstate.h"

class AbstractLogData;
class LogFilteredData;
class LogTableHighlightDelegate;
class Overview;
class OverviewWidget;
class Portion;
class QFNotification;
class QMenu;
class QuickFind;
class QuickFindPattern;
class Selection;

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
//
// Shown, it is what the window's QuickFind bar searches, as the Text View is:
// QuickFind runs from the characters selected in a cell of the current Row, or
// else from its whole Log Line, and its match is shown as that Log Line's Row,
// selected, with the matching characters selected in the cell holding them.
class LogTableView : public QTableView,
                     public LogFileView,
                     public LogPresentation,
                     public SearchableWidgetInterface {
    Q_OBJECT

public:
    // Lets a test read what the Table View was handed.
    template <class T>
    struct access_by;

    // Each Row shows one Log Line.
    explicit LogTableView( QWidget* parent = nullptr );
    // Each Row shows the Log Line rows maps it onto.
    explicit LogTableView( std::shared_ptr<const RowMapping> rows, QWidget* parent = nullptr );
    ~LogTableView() override;

    // Show the Log Lines of logData with the given Log Format, or nothing
    // for a null format. The rows appear on the next updateData().
    void setLogFormat( const LogFormatDefinition* format, AbstractLogData* logData );

    // Catch up with the Log File's current Log Lines; with follow, the last
    // Row is scrolled into view. The Marks and Matches are the current
    // Search's, handed over by setCurrentSearch(). Timestamps without a year
    // take the year of modificationDate, when the Log File was last written
    // (ADR 0010); without one, the current year.
    void updateData( bool follow, const QDate& modificationDate = {} );

    // The Table View positions overviewWidget over its right edge itself, and
    // shows a clicked Log Line.
    void setOverview( Overview* overview, OverviewWidget* overviewWidget );

    // Whether the Table View is the Presentation the user currently sees.
    void setActive( bool active );

    void setQuickFindPattern( std::shared_ptr<QuickFindPattern> pattern );
    // Place the Overview strip and its current-view indicator anew.
    void updateOverview();

    // LogFileView and LogPresentation
    void setSearchPattern( const RegularExpressionPattern& pattern ) override;
    // Its delegate paints the Search's Marks and Matches.
    void setCurrentSearch( const LogFilteredData* search ) override;
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
    // The Table View shows no Value Names (#647): nothing to do.
    void applyValueNamesChange() override {}
    void updateFont( const QFont& font ) override;
    void registerShortcuts() override;
    // Hand over the settings that color Log Lines, after a settings change:
    // painting reads no setting of its own.
    void setDecorationPolicy( const DecorationPolicy& policy ) override;
    // Shows the Overview as it says; the Table View has no line numbers.
    void setPresentationPolicy( const PresentationPolicy& policy ) override;
    // Ignored: the Table View never engages follow itself, and leaves it
    // however follow was engaged.
    void allowFollowMode( bool allow ) override;
    void setColorLabels( const std::vector<QStringList>& labels ) override;
    void setSearchLimits( LineNumber startLine, LineNumber endLine ) override;
    // Saves the Log Lines of the selected Rows, in Log Line order.
    void saveSelectedTo( const QString& filename ) override;

    // The Log File shown, whose name the CSV export proposes.
    void setLogFilePath( const QString& path );
    // What the dialog Export as CSV starts with: every Row or the selected
    // ones, the Line column (unchecked) and every column of the table.
    CsvExportDialog::Setup csvExportSetup() const;
    // Exports the Rows to a CSV file as chosen in that dialog: every Row in
    // its order, or the selected ones in Log Line order.
    void exportCsvTo( const CsvExportDialog::Choices& choices );

    // SearchableWidgetInterface: QuickFind from quickFindStart(), over every
    // Log Line the Rows show, with the window's QuickFind pattern. Aborting an
    // incremental search selects the Row it started from.
    void searchForward() override;
    void searchBackward() override;
    void incrementallySearchForward() override;
    void incrementallySearchBackward() override;
    void incrementalSearchStop() override;
    void incrementalSearchAbort() override;

    TableViewSelection selection() const;
    // The Log Lines of the selected Rows.
    logsquirl::vector<LineNumber> selectedLogLines() const;
    // The first at most limit of them (#663).
    logsquirl::vector<LineNumber> selectedLogLines( LinesCount limit ) const;

public Q_SLOTS:
    // The View Set, the owner of follow, turned it on or off. Turned on, the
    // last Row is scrolled into view; while it is on, updateData() is told to
    // follow, and the Table View asks to leave it when the user scrolls away
    // from the bottom. It never turns follow on itself.
    void followSet( bool checked ) override;
    void highlightOverviewLine( LineNumber line );
    void removeOverviewHighlight();

Q_SIGNALS:
    // The signals every Presentation emits, named and meant as the Text
    // View's (see LogPresentation). The Table View declares only those it
    // emits: turning following on from the view, zooming with the wheel and
    // the exit-view shortcut are the Text View's alone, and their signals are
    // not in the set.

    // Sent, with false, when the user scrolls away from the bottom while
    // follow is on (#543); never with true. It asks the owner of follow, and
    // the Table View follows until it is handed the outcome (#558).
    void followModeChanged( bool follow );

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
    // The user asked to count the values of a Log Format field, from the
    // context menu of its column header.
    void countValuesRequested( const QString& fieldName );

    // What a searchable tells the QuickFind bar, as the Text View does: a new
    // pattern and direction ("Find next / previous" of the context menu), a
    // search to run in the bar's direction, and QuickFind's notifications.
    void changeQuickFind( const QString& newPattern, QuickFindMux::QFDirection newDirection );
    void searchNext();
    void searchPrevious();
    void notifyQuickFind( const QFNotification& message );
    void clearQuickFindNotification();

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
    // Paints the visible cells in one paint pass of the delegate.
    void paintEvent( QPaintEvent* event ) override;
    // Repaint as QTableView does, without telling the accessibility clients.
    void selectionChanged( const QItemSelection& selected,
                           const QItemSelection& deselected ) override;
    void currentChanged( const QModelIndex& current, const QModelIndex& previous ) override;

private:
    void rowSelectionChanged();
    void showContextMenu( const QPoint& pos );
    // The context menu of a column header, opened at pos in header coordinates.
    void showHeaderContextMenu( const QPoint& pos );
    void copySelection();
    void copySelectionWithLineNumbers();
    void markSelection();
    // Asks for a file, and saves the Log Lines of the selected Rows to it.
    void saveSelectedToFile();
    // Asks for a file, and saves the Log Lines of every Row to it.
    void saveToFile();
    // Opens the dialog Export as CSV, and exports as chosen there.
    void exportAsCsv();
    // The columns the CSV export offers: Line, then every column of the table.
    std::vector<CsvColumn> csvColumns() const;

    // Column widths
    void saveColumnWidths();
    bool applySavedColumnWidths();
    void autoSizeColumns();
    void stretchLastColumn();

    // Repaint the whole width of a Row, if it is shown.
    void updateRow( int row );

    // Pixel X in a cell to the character position there.
    int charAtX( const QModelIndex& index, int pixelX ) const;
    // Pixel X in a cell to the character painted there.
    int characterAtX( const QModelIndex& index, int pixelX ) const;
    void selectWordAt( const QModelIndex& index, int charPos );
    // Repaint the in-cell selection.
    void showInCellSelection();

    // Makes the selected characters the QuickFind pattern and asks the
    // QuickFind bar to search in the given direction, as the Text View does:
    // the bar keeps the pattern and the direction for its next and previous.
    void findSelected( bool forward );
    // Where QuickFind starts from, as the Text View starts from its
    // Selection: the characters selected inside a cell of the current Row,
    // where the Log Format places that cell in the Log Line; otherwise the
    // Log Line of the current Row, whole.
    Selection quickFindStart() const;
    // Where the text of each cell of a Row lies in its Log Line: the raw
    // characters of each column (LogFormatTableModel::columnSpans()), and the
    // column QuickFind reads each raw character at, with tabs expanded.
    struct CellsInLogLine {
        std::vector<std::optional<LogFormatTableModel::TextSpan>> spans;
        logsquirl::vector<int> displayColumns;

        // The span of a column, null when it shows no characters of the Log
        // Line.
        const LogFormatTableModel::TextSpan* spanOf( int column ) const
        {
            if ( column < 0 || static_cast<size_t>( column ) >= spans.size() ) {
                return nullptr;
            }
            const auto& span = spans[ static_cast<size_t>( column ) ];
            return span.has_value() ? &span.value() : nullptr;
        }
        int displayColumnOf( int rawColumn ) const
        {
            return displayColumns[ static_cast<size_t>( rawColumn ) ];
        }
    };
    // None for a JSON or logfmt Log Format, which places its fields nowhere
    // in the Log Line.
    std::optional<CellsInLogLine> cellsInLogLine( int row ) const;
    // Runs search from quickFindStart() with the QuickFind pattern.
    using QuickFindSearch = void ( QuickFind::* )( Selection, QuickFindMatcher );
    void searchUsing( QuickFindSearch search );
    // Selects the Row of the Log Line QuickFind found, and the matching
    // characters in the cell holding them. A match spanning two cells, or in
    // text no cell shows, selects no characters. A JSON or logfmt Log Format
    // places no cell in the Log Line: the first cell the pattern matches is
    // taken for it.
    void showQuickFindResult( bool hasMatch, const Portion& logLinePortion );
    // Selects the characters of logLinePortion in the cell of the Row that
    // holds them all, if a cell does.
    void selectCellHolding( int row, const CellsInLogLine& cells, const Portion& logLinePortion );

    bool handlesMouse() const;
    void repaintIfActive();
    // Leaves follow if it is on and the Rows are scrolled to position, away
    // from the bottom: the user moved them there.
    void leaveFollowAwayFromBottom( int position );

    std::shared_ptr<const RowMapping> rows_;
    // Not owned: the coordinator holds the Log Format for as long as it is set
    const LogFormatDefinition* format_ = nullptr;
    AbstractLogData* logData_ = nullptr;
    QString logFilePath_;
    // When the Log File was last written, as handed to updateData().
    QDate modificationDate_;
    // What the Table View shows over the text of its Rows; its delegate
    // paints from it.
    TableViewState state_;
    LogFormatTableModel* model_ = nullptr;
    LogTableHighlightDelegate* delegate_ = nullptr;

    Overview* overview_ = nullptr;
    OverviewWidget* overviewWidget_ = nullptr;

    // Runs the QuickFind searches the window's QuickFind bar asks for, off the
    // UI thread.
    std::unique_ptr<QuickFind> quickFind_;
    bool selectionDragging_ = false;
    // Whether follow is on, as the owner handed it last through followSet().
    bool follow_ = false;

    bool active_ = false;
    bool columnsNeedSizing_ = false;
    bool programmaticColumnResize_ = false;
};
