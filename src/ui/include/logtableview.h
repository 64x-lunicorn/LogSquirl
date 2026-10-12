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
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QDate>
#include <QStringList>
#include <QTableView>
#include <QTimer>

#include "colorlabelsmanager.h"
#include "columnlayout.h"
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
#include "tablesorter.h"
#include "tablesortkeys.h"
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
class QShortcut;
class QuickFind;
class QuickFindPattern;
class Selection;

// The Table View: the Presentation of a Log File as one column per field of
// its Log Format. It owns its model, its highlight delegate, its selection
// (whole Rows and characters inside a cell), its context menu and its
// Overview strip, and keeps a Column Layout per Log Format.
//
// Which Log Format applies is decided by whoever holds the Table View, and
// handed to it through setLogFormat().
//
// The model keeps the Log Format's columns in the Log Format's order; which
// of them are shown, in which order and how wide is the Column Layout, which
// the header holds (its visual order of the model's logical columns) and the
// user changes from the header: a column header dragged to another place
// moves the column, and the header's context menu hides and shows columns,
// one always staying shown, and resets them. The layout is saved per Log
// Format as it changes, and applied when a Log File of the Log Format opens.
// A Row is unchanged by its layout: its cells are the model's, whichever are
// shown.
//
// Which Log Line each Row shows is its RowMapping's to say; everything the
// Table View hands out is a Log Line obtained through it. A click on a column
// header gives the Rows a Sort Order: the Rows sorted by that column, then
// the other way round, then Log Line order again. The sort reads the column's
// keys off the UI thread (TableSorter), the header showing its progress with
// the indicator, and the Rows switch to the new order at once when it is
// done; a second click on the header or Esc stops it, the Rows staying as
// they were. A Row keeps being its Log Line, so the selection, Marks, Matches
// and QuickFind land on the same Log Lines sorted or not. A followed Log
// File keeps its Sort Order: the Log Lines it appends are keyed alone and
// merged in at their place. The Sort Order lives with the tab and is saved
// nowhere; a Column Layout does not hold it, and hiding the sorted column
// keeps it.
//
// Matches Only (#798) narrows the Rows to the Log Lines that are Matches of
// the current Search: a Mark or a Context Line has a Row only when it is a
// Match. It follows the Search as it runs, as auto-refresh adds Matches and
// as another Search replaces it, and turns itself off when the Search is
// cleared. It composes with the Sort Order: Matches Only says which Rows
// there are (MatchRows), the Sort Order orders them (SortedRows over them).
// A Row stays its Log Line, so the selection, Go to Log Line, QuickFind and
// Copy address the right Log Lines. It is the Table View's alone, per tab,
// and saved nowhere.
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
    // (ADR 0010); without one, the current year. Under a Sort Order, Log
    // Lines appended since are merged in at their place, in the background;
    // fewer Log Lines than sorted (a truncation) sort the Rows anew.
    void updateData( bool follow, const QDate& modificationDate = {} );
    // The Log File was loaded from its start again (a reload, a truncation):
    // the Rows read so far are stale, and a Sort Order is sorted anew once
    // the next updateData() brings the new Log Lines.
    void reloaded();

    // Sort Order
    struct SortOrder {
        // A logical column of the model.
        int column = 0;
        Qt::SortOrder order = Qt::AscendingOrder;

        bool operator==( const SortOrder& ) const = default;
    };
    // The Sort Order the Rows are in; none in Log Line order. A sort under
    // way has not changed it yet.
    std::optional<SortOrder> sortOrder() const
    {
        return sortOrder_;
    }
    // Starts sorting the Rows by column in that direction, as a click on its
    // header does; the Rows switch once the sort is done. None is Log Line
    // order, taken at once.
    void sortBy( std::optional<SortOrder> order );
    // A Sort Order by the name of its column: what survives the Log Format
    // being set anew, as a truncation sets it.
    struct NamedSortOrder {
        QString column;
        Qt::SortOrder order = Qt::AscendingOrder;
    };
    // The Sort Order with its column's name; none in Log Line order.
    std::optional<NamedSortOrder> namedSortOrder() const;
    // Starts sorting by the column of that name; nothing without one.
    void sortBy( const NamedSortOrder& order );
    // What a click on the header of column does: by that column ascending,
    // a second click descending, a third Log Line order; a click on the
    // header of the column being sorted stops the sort.
    void cycleSortOrder( int column );
    // The sort under way, none without one: a sort of every Row, or the
    // merge of appended Log Lines into the Sort Order.
    std::optional<SortOrder> sortInProgress() const
    {
        return sorting_;
    }
    // Whether the sort under way is a merge of appended Log Lines, which a
    // click or Esc does not stop.
    bool isMerging() const
    {
        return merging_;
    }
    // Stops the sort under way; the Rows stay as they are.
    void stopSorting();

    // Matches Only: whether only the Rows of the current Search's Matches
    // are shown.
    bool matchesOnly() const
    {
        return matchesOnly_;
    }
    // Shows only the Matches' Rows, or every Row again; matchesOnlyChanged()
    // says what it is now. Without a current Search there is nothing to
    // narrow to: it stays off, and says so.
    void setMatchesOnly( bool on );

    // Selects the Row of the next Mark below the current Row, or the next
    // above it, in the order the Rows are shown; nothing when there is none.
    void selectNextMark();
    void selectPreviousMark();
    // The same for the Matches of the current Search.
    void selectNextMatch();
    void selectPreviousMatch();

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

    // The Column Layout the Table View shows its Log Format with, as its
    // header holds it; empty without a Log Format.
    ColumnLayout columnLayout() const;
    // Restores the Log Format's order, shows every column and drops the
    // saved widths, so the default widths apply again; the layout saved for
    // the Log Format is dropped.
    void resetColumns();

    // The Log File shown, whose name the CSV export proposes.
    void setLogFilePath( const QString& path );
    // What the dialog Export as CSV starts with: every Row or the selected
    // ones, the Line column (unchecked) and every column of the table in the
    // order the Column Layout shows them, the hidden ones unchecked.
    CsvExportDialog::Setup csvExportSetup() const;
    // Exports the Rows to a CSV file as chosen in that dialog: every Row in
    // its order, or the selected ones in the order shown.
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

    // The selected Rows in the order shown, and the characters selected in
    // a cell.
    TableViewSelection selection() const;
    // The Log Lines of the selected Rows, in the order the Rows are shown.
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

    // The Rows changed their order: a sort finished, appended Log Lines were
    // merged in, or the Rows are back in Log Line order.
    void sortOrderChanged();
    // Matches Only was switched on or off, by whoever asked or by the Search
    // being cleared; also sent, with false, when it was asked for and there
    // is no Search to narrow to.
    void matchesOnlyChanged( bool on );
    // The user asked, from a cell's context menu, to narrow the Search to
    // the Log Lines whose field named column has the cell's value (#798).
    void searchValueInColumn( const QString& column, const QString& value );

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
    // The context menu of the header of a column (a logical column of the
    // model; -1 for none): Matches Only, a separator, Count values for that
    // column, a separator, the check list of the columns in the shown order,
    // a separator, Reset columns. Not yet shown.
    std::unique_ptr<QMenu> createHeaderMenu( int column );

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
    // The entries of the header menu, each kind in a method of its own, so
    // that more can join them.
    void addMatchesOnlyEntry( QMenu& menu );
    void addCountValuesEntry( QMenu& menu, int column );
    void addColumnEntries( QMenu& menu );
    void addResetColumnsEntry( QMenu& menu );
    void copySelection();
    void copySelectionWithLineNumbers();
    void markSelection();
    // Asks for a file, and saves the Log Lines of the selected Rows to it.
    void saveSelectedToFile();
    // Asks for a file, and saves the Log Lines of every Row to it.
    void saveToFile();
    // Opens the dialog Export as CSV, and exports as chosen there.
    void exportAsCsv();
    // The columns the CSV export offers: Line, then every column of the table
    // as the Column Layout shows them.
    std::vector<CsvColumn> csvColumns() const;

    // Column Layout
    // Shows or hides a column, and saves the layout; the last column shown
    // stays shown.
    void setColumnShown( int column, bool shown );
    // Saves the layout the header holds for the Log Format, unless the Table
    // View is applying one.
    void saveColumnLayout();
    // Applies the layout saved for the Log Format; false when none is, or the
    // Log Format's columns changed since.
    bool applySavedColumnLayout();
    void applyColumnLayout( const ColumnLayout& layout );
    // The Log Format's order, every column shown, a width to start from.
    void applyDefaultColumns();
    // While one lives, the layout changes the Table View makes are its own,
    // not the user's: none of them is saved. The header stretches its last
    // section in a posted resize, which its getters run, so what reads the
    // header right after a change holds one too.
    class ApplyingLayout {
    public:
        explicit ApplyingLayout( LogTableView& view )
            : view_( view )
            , was_( std::exchange( view.applyingColumnLayout_, true ) )
        {
        }
        ~ApplyingLayout()
        {
            view_.applyingColumnLayout_ = was_;
        }
        ApplyingLayout( const ApplyingLayout& ) = delete;
        ApplyingLayout& operator=( const ApplyingLayout& ) = delete;

    private:
        LogTableView& view_;
        bool was_;
    };
    QStringList columnNames() const;
    // The column shown last, -1 without one.
    int lastShownColumn() const;
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
    // characters in the cell holding them. A match spanning two cells, in
    // text no cell shows or in a hidden column selects no characters. A JSON
    // or logfmt Log Format places no cell in the Log Line: the first shown
    // cell the pattern matches is taken for it.
    void showQuickFindResult( bool hasMatch, const Portion& logLinePortion );
    // Selects the characters of logLinePortion in the cell of the Row that
    // holds them all, if a cell does.
    void selectCellHolding( int row, const CellsInLogLine& cells, const Portion& logLinePortion );

    bool handlesMouse() const;
    void repaintIfActive();
    // Leaves follow if it is on and the Rows are scrolled to position, away
    // from the bottom: the user moved them there.
    void leaveFollowAwayFromBottom( int position );

    // Sort Order
    // What every sort reads: the Log File, the Log Format and its Rows.
    TableSortRequest sortRequest( const SortOrder& order ) const;
    // Starts the sort of every Log Line by order, superseding one under way.
    void startSort( const SortOrder& order );
    // Starts merging the Log Lines appended since the Rows were sorted.
    void startMerge();
    void onSortFinished( const TableSortResult& result );
    // Shows the sort's progress in the header, with the indicator.
    void showSortProgress();
    // The header's indicator for the Sort Order, or the sort under way.
    void showSortIndicator();
    // The Rows now show the Log Lines rows maps them onto: the model and the
    // delegate are told, the Log Lines selected before stay selected (every
    // Row when every Row was, else the first SelectionKept of them) and the
    // current one is shown.
    void swapRowMapping( std::shared_ptr<const RowMapping> rows );
    // Back to Log Line order at once, the keys and Rows of the Sort Order
    // dropped.
    void dropSortOrder();
    // Selects the Row nearest to the current one in the direction given
    // whose Log Line is of the type: a Mark, or a Match.
    void selectNeighbour( AbstractLogData::LineTypeFlags type, bool below );
    void registerShortcut( const std::string& action, std::function<void()> func );
    static constexpr int SelectionKept = 10'000;

    // Matches Only
    // The Rows before a Sort Order orders them: the Matches' Rows under
    // Matches Only, else every Log Line's.
    std::shared_ptr<const RowMapping> narrowedRows() const
    {
        return matchRows_ ? matchRows_ : baseRows_;
    }
    // Follows the current Search under Matches Only: the Rows are the
    // Matches as they are now, Matches found since appended, others
    // replaced; a Sort Order is kept over them. Off once the Search is
    // cleared.
    void refreshMatchRows();
    // Whether a current Search has a pattern to narrow the Rows to.
    bool hasSearchToNarrowTo() const;

    // Which Log Line each Row shows: baseRows_, as the Table View was built
    // with, in Log Line order, narrowed by Matches Only to matchRows_, or
    // the Sort Order's Rows over either.
    std::shared_ptr<const RowMapping> rows_;
    std::shared_ptr<const RowMapping> baseRows_;
    bool matchesOnly_ = false;
    // The Matches' Rows, while Matches Only is on.
    std::shared_ptr<const MatchRows> matchRows_;
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
    // Whether the Table View is changing the header itself (see
    // ApplyingLayout).
    bool applyingColumnLayout_ = false;

    // Sort Order: the one the Rows are in, and the one being sorted for;
    // whether that sort merges appended Log Lines into the Rows.
    std::optional<SortOrder> sortOrder_;
    std::optional<SortOrder> sorting_;
    bool merging_ = false;
    // The keys of the sorted column, for the Log Lines keyed so far, and the
    // Rows in the Sort Order; none in Log Line order.
    std::shared_ptr<const TableSortKeys> sortKeys_;
    std::shared_ptr<const SortedRows> sortedRows_;
    std::unique_ptr<TableSorter> sorter_;
    QTimer sortProgressTimer_;
    // The Log File was loaded from its start: the Sort Order is sorted anew
    // on the next updateData().
    bool resortOnUpdate_ = false;

    std::map<QString, QShortcut*> shortcuts_;
};
