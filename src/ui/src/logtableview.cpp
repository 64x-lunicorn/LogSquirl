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

#include "logtableview.h"

#include <algorithm>
#include <climits>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>

#include "abstractlogdata.h"
#include "abstractlogview.h"
#include "configuration.h"
#include "highlightersmenu.h"
#include "linessaver.h"
#include "logfiltereddata.h"
#include "logformattablemodel.h"
#include "logtablehighlightdelegate.h"
#include "overview.h"
#include "overviewwidget.h"
#include "regularexpression.h"

namespace {

// A Log Format name safe for use as a QSettings group key.
QString sanitizedFormatName( const QString& name )
{
    QString safe = name;
    safe.replace( '/', '_' );
    safe.replace( '\\', '_' );
    return safe;
}

QString columnWidthsGroup( const LogFormatDefinition& format )
{
    return "logformat/columns/" + sanitizedFormatName( format.name() );
}

} // namespace

LogTableView::LogTableView( QWidget* parent )
    : LogTableView( std::make_shared<OneRowPerLogLine>(), parent )
{
}

LogTableView::LogTableView( std::shared_ptr<const RowMapping> rows, QWidget* parent )
    : QTableView( parent )
    , rows_( std::move( rows ) )
{
    setSelectionBehavior( QAbstractItemView::SelectRows );
    setAlternatingRowColors( true );
    setShowGrid( false );
    horizontalHeader()->setStretchLastSection( true );
    setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
    setHorizontalScrollMode( QAbstractItemView::ScrollPerPixel );
    horizontalScrollBar()->setSingleStep( 10 );
    verticalHeader()->setVisible( false );
    setContentsMargins( 2, 0, 2, 0 );
    viewport()->setMouseTracking( true );
    viewport()->setCursor( Qt::IBeamCursor );

    // Apply the same font the text view uses so appearance is consistent
    // from the very first frame (the configuration is applied later).
    {
        const auto& config = Configuration::get();
        QFont tableFont = config.mainFont();
        tableFont.setKerning( false );
        tableFont.setFixedPitch( true );
        if ( config.forceFontAntialiasing() ) {
            tableFont.setStyleStrategy( QFont::PreferAntialias );
        }
        tableFont.setBold( config.useBoldFont() );
        setFont( tableFont );

        const QFontMetrics fm( tableFont );
        verticalHeader()->setDefaultSectionSize( fm.height() + 2 );
    }

    // Highlight delegate for match/mark row coloring and text highlighting
    delegate_ = new LogTableHighlightDelegate( this );
    delegate_->setRowMapping( rows_ );
    setItemDelegate( delegate_ );

    setContextMenuPolicy( Qt::CustomContextMenu );
    connect( this, &QWidget::customContextMenuRequested, this, &LogTableView::showContextMenu );

    // Move the overview's current-view indicator along when scrolling
    connect( verticalScrollBar(), &QScrollBar::valueChanged, this,
             [ this ]() { updateOverview(); } );
}

LogTableView::~LogTableView() = default;

void LogTableView::setLogFormat( const LogFormatDefinition* format, AbstractLogData* logData )
{
    if ( model_ ) {
        // Disconnect header signals before removing the model to prevent
        // duplicate connections when updateData() reconnects.
        disconnect( horizontalHeader(), &QHeaderView::sectionResized, this,
                    &LogTableView::saveColumnWidths );
        setModel( nullptr );
        delete model_;
        model_ = nullptr;
    }

    format_ = format ? std::make_optional( *format ) : std::nullopt;
    logData_ = logData;
    columnsNeedSizing_ = false;

    selection_ = TableViewSelection{};
    delegate_->clearPortionSelection();

    // The overview no longer shows anything valid
    if ( overviewWidget_ ) {
        overviewWidget_->hide();
    }
}

void LogTableView::updateData( LogFilteredData* filteredData, bool follow )
{
    if ( !format_ || !logData_ ) {
        updateOverview();
        return;
    }

    if ( !model_ ) {
        model_ = new LogFormatTableModel( *format_, logData_, rows_, this );
        setModel( model_ );

        // Save column widths when the user resizes a column
        connect( horizontalHeader(), &QHeaderView::sectionResized, this,
                 &LogTableView::saveColumnWidths );

        connect( selectionModel(), &QItemSelectionModel::selectionChanged, this,
                 [ this ]() { rowSelectionChanged(); } );

        // Column widths need sizing once the first data has arrived
        columnsNeedSizing_ = true;
    }

    if ( filteredData ) {
        delegate_->setFilteredData( filteredData );
    }

    const auto lineCount = logData_->getNbLine().get();
    const int lineCountInt
        = static_cast<int>( std::min( lineCount, static_cast<uint64_t>( INT_MAX ) ) );
    model_->setLineCount( lineCountInt );
    const bool hasRows = model_->rowCount() > 0;

    if ( columnsNeedSizing_ && hasRows ) {
        columnsNeedSizing_ = false;

        // Widths the user saved for this Log Format win; auto-sizing them
        // afterwards would throw the user's widths away.
        if ( !applySavedColumnWidths() ) {
            // No saved widths — use a reasonable default until auto-sizing finishes
            programmaticColumnResize_ = true;
            for ( int col = 0; col < model_->columnCount(); ++col ) {
                setColumnWidth( col, 120 );
            }
            programmaticColumnResize_ = false;

            // Defer the expensive auto-sizing so the UI stays responsive
            QTimer::singleShot( 0, this, &LogTableView::autoSizeColumns );
        }
    }

    if ( follow && hasRows ) {
        scrollToBottom();
    }

    updateOverview();
}

void LogTableView::setOverview( Overview* overview, OverviewWidget* overviewWidget )
{
    overview_ = overview;
    overviewWidget_ = overviewWidget;
    overviewWidget_->setParent( this );
    overviewWidget_->setOverview( overview_ );
    overviewWidget_->hide();

    connect( overviewWidget_, &OverviewWidget::lineClicked, this, &LogTableView::showLogLine );
}

void LogTableView::setActive( bool active )
{
    active_ = active;
    if ( !active_ && overviewWidget_ ) {
        overviewWidget_->hide();
    }
}

void LogTableView::setQuickFindPattern( std::shared_ptr<QuickFindPattern> pattern )
{
    delegate_->setQuickFindPattern( std::move( pattern ) );
}

void LogTableView::setSearchPattern( const RegularExpressionPattern& pattern )
{
    delegate_->setSearchPattern( pattern );
    repaintIfActive();
}

void LogTableView::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    delegate_->setSearchLimits( startLine, endLine );
    repaintIfActive();
}

void LogTableView::setColorLabels( const ColorLabelsManager::QuickHighlightersCollection& labels )
{
    colorLabels_ = labels;
    delegate_->setColorLabelWords( labels );
    repaintIfActive();
}

void LogTableView::refreshMainSearchHighlighter()
{
    delegate_->refreshMainSearchHighlighter();
    repaintIfActive();
}

void LogTableView::updateFont( const QFont& font )
{
    setFont( font );
    // Adjust row height to fit the font
    const QFontMetrics fm( font );
    verticalHeader()->setDefaultSectionSize( fm.height() + 2 );
    horizontalHeader()->setFont( font );
}

void LogTableView::updateDecorations()
{
    if ( active_ ) {
        viewport()->update();
        updateOverview();
    }
}

void LogTableView::repaintIfActive()
{
    if ( active_ ) {
        viewport()->update();
    }
}

// Position the overview (minimap) widget beside the Rows and update its
// current-view indicator. Mirrors what AbstractLogView::refreshOverview /
// updateDisplaySize do.
void LogTableView::updateOverview()
{
    if ( !overviewWidget_ ) {
        return;
    }

    const bool shouldShow = active_ && overview_->isVisible();
    if ( !shouldShow ) {
        overviewWidget_->hide();
        return;
    }

    // Place the overview widget at the right edge of the table view,
    // spanning the full height below the header.
    const int headerHeight = horizontalHeader()->isVisible() ? horizontalHeader()->height() : 0;
    const int overviewHeight = height() - headerHeight;

    if ( overviewHeight <= 0 ) {
        overviewWidget_->hide();
        return;
    }

    overviewWidget_->setGeometry( width() - AbstractLogView::OverviewWidth - 1, headerHeight,
                                  AbstractLogView::OverviewWidth, overviewHeight );
    overviewWidget_->show();
    overviewWidget_->raise();

    if ( model_ ) {
        const int firstVisibleRow = verticalScrollBar()->value();
        const int rowHeight = verticalHeader()->defaultSectionSize();
        const int visibleRows = ( rowHeight > 0 ) ? ( overviewHeight / rowHeight ) : 1;
        const int lastVisibleRow = firstVisibleRow + visibleRows;

        overview_->updateCurrentPosition( rows_->logLineAt( firstVisibleRow ),
                                          rows_->logLineAt( lastVisibleRow ) );
    }

    overviewWidget_->update();
}

void LogTableView::highlightOverviewLine( LineNumber line )
{
    if ( overviewWidget_ ) {
        overviewWidget_->highlightLine( line );
    }
}

void LogTableView::removeOverviewHighlight()
{
    if ( overviewWidget_ ) {
        overviewWidget_->removeHighlight();
    }
}

void LogTableView::showLogLine( LineNumber line )
{
    if ( !model_ ) {
        return;
    }

    const auto row = rows_->rowOf( line );
    if ( row && *row >= 0 && *row < model_->rowCount() ) {
        scrollTo( model_->index( *row, 0 ), QAbstractItemView::PositionAtCenter );
        selectRow( *row );
    }
}

void LogTableView::showLogLinePortion( LineNumber line, LinesCount, LineColumn, LineLength )
{
    showLogLine( line );
}

OptionalLineNumber LogTableView::logLineAt( const QPoint& pos ) const
{
    const auto index = indexAt( pos );
    if ( !model_ || !index.isValid() ) {
        return std::nullopt;
    }
    return rows_->logLineAt( index.row() );
}

TableViewSelection LogTableView::selection() const
{
    auto selection = selection_;
    std::vector<int> rows;
    if ( model_ && selectionModel() ) {
        const auto indexes = selectionModel()->selectedRows();
        rows.reserve( static_cast<size_t>( indexes.size() ) );
        for ( const auto& index : indexes ) {
            rows.push_back( index.row() );
        }
    }
    selection.setRows( std::move( rows ) );
    return selection;
}

QString LogTableView::selectedText() const
{
    return model_ ? selection().selectedText( *model_ ) : QString{};
}

logsquirl::vector<LineNumber> LogTableView::selectedLogLines() const
{
    return selection().selectedLogLines( *rows_ );
}

// A new Row selection: tell the holder the Log Line now selected.
void LogTableView::rowSelectionChanged()
{
    if ( !model_ ) {
        return;
    }

    const auto lines = selectedLogLines();
    if ( lines.empty() ) {
        return;
    }

    Q_EMIT newSelection( lines.front(), 1_lcount, 0_lcol, 0_length );

    updateOverview();
}

bool LogTableView::handlesMouse() const
{
    return active_ && model_;
}

void LogTableView::showInCellSelection()
{
    const auto inCell = selection_.inCell();
    if ( inCell ) {
        delegate_->setPortionSelection( inCell->row, inCell->column, inCell->startChar,
                                        inCell->endChar );
    }
    else {
        delegate_->clearPortionSelection();
    }
    viewport()->update();
}

void LogTableView::mousePressEvent( QMouseEvent* event )
{
    if ( handlesMouse() ) {
        Q_EMIT activity();
    }

    if ( handlesMouse() && event->button() == Qt::LeftButton ) {
        const auto index = indexAt( event->pos() );
        if ( index.isValid() ) {
            const int charPos = charAtX( index, event->pos().x() );

            // Shift-click extends the existing selection within its cell,
            // any other click starts a new one
            const bool extended
                = ( event->modifiers() & Qt::ShiftModifier )
                  && selection_.extendInCell( index.row(), index.column(), charPos );
            if ( !extended ) {
                selection_.startInCell( index.row(), index.column(), charPos );
            }
            selectionDragging_ = true;
            showInCellSelection();
        }
    }

    // QTableView selects the Row too
    QTableView::mousePressEvent( event );
}

void LogTableView::mouseMoveEvent( QMouseEvent* event )
{
    if ( handlesMouse() ) {
        const auto index = indexAt( event->pos() );

        // Hover highlight
        const int newHoverRow = index.isValid() ? index.row() : -1;
        if ( newHoverRow != hoverRow_ ) {
            hoverRow_ = newHoverRow;
            delegate_->setHoverRow( hoverRow_ );
            viewport()->update();
        }

        // Drag to extend the in-cell selection, within the same cell only
        if ( selectionDragging_ && index.isValid()
             && selection_.extendInCell( index.row(), index.column(),
                                         charAtX( index, event->pos().x() ) ) ) {
            showInCellSelection();
        }
    }

    QTableView::mouseMoveEvent( event );
}

void LogTableView::mouseReleaseEvent( QMouseEvent* event )
{
    if ( handlesMouse() && event->button() == Qt::LeftButton && selectionDragging_ ) {
        selectionDragging_ = false;
        // A click without a drag selects no characters
        if ( !selection_.hasInCellSelection() ) {
            selection_.clearInCell();
            showInCellSelection();
        }
    }

    QTableView::mouseReleaseEvent( event );
}

void LogTableView::mouseDoubleClickEvent( QMouseEvent* event )
{
    if ( handlesMouse() && event->button() == Qt::LeftButton ) {
        const auto index = indexAt( event->pos() );
        if ( index.isValid() ) {
            selectWordAt( index, charAtX( index, event->pos().x() ) );
            // Consumed, to prevent default editing
            return;
        }
    }

    QTableView::mouseDoubleClickEvent( event );
}

bool LogTableView::viewportEvent( QEvent* event )
{
    if ( event->type() == QEvent::Resize ) {
        stretchLastColumn();
        updateOverview();
    }
    else if ( event->type() == QEvent::Leave && handlesMouse() && hoverRow_ >= 0 ) {
        hoverRow_ = -1;
        delegate_->clearHoverRow();
        viewport()->update();
    }

    return QTableView::viewportEvent( event );
}

void LogTableView::keyPressEvent( QKeyEvent* event )
{
    if ( active_ ) {
        Q_EMIT activity();

        if ( event->matches( QKeySequence::Copy ) ) {
            copySelection();
            return;
        }
        // 'm' to mark/unmark lines (same as text view)
        if ( event->key() == Qt::Key_M && event->modifiers() == Qt::NoModifier ) {
            markSelection();
            return;
        }
        // Ctrl+Home = jump to first row, Ctrl+End = jump to last row
        if ( event->key() == Qt::Key_Home && event->modifiers() == Qt::ControlModifier ) {
            if ( model_ && model_->rowCount() > 0 ) {
                scrollToTop();
                selectRow( 0 );
            }
            return;
        }
        if ( event->key() == Qt::Key_End && event->modifiers() == Qt::ControlModifier ) {
            if ( model_ && model_->rowCount() > 0 ) {
                scrollToBottom();
                selectRow( model_->rowCount() - 1 );
            }
            return;
        }
    }

    QTableView::keyPressEvent( event );
}

int LogTableView::charAtX( const QModelIndex& index, int pixelX ) const
{
    return LogTableHighlightDelegate::charIndexAtX( index.data( Qt::DisplayRole ).toString(),
                                                    QFontMetrics( font() ),
                                                    visualRect( index ).left(), pixelX );
}

// Select the word at the given character position in a cell.
void LogTableView::selectWordAt( const QModelIndex& index, int charPos )
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

    while ( start > 0
            && ( cellText[ start - 1 ].isLetterOrNumber() || cellText[ start - 1 ] == '_' ) ) {
        --start;
    }
    while ( end < textLen && ( cellText[ end ].isLetterOrNumber() || cellText[ end ] == '_' ) ) {
        ++end;
    }

    if ( start == end ) {
        // No word found at position, select the single character
        end = std::min( start + 1, textLen );
    }

    selection_.selectInCell( index.row(), index.column(), start, end );
    showInCellSelection();
}

void LogTableView::showContextMenu( const QPoint& pos )
{
    const auto currentSelection = selection();
    const bool hasSelection = !currentSelection.selectedLogLines( *rows_ ).empty();

    // The cell the user right-clicked on
    const auto clickedIdx = indexAt( pos );
    auto cellText = ( clickedIdx.isValid() && hasSelection )
                        ? clickedIdx.data( Qt::DisplayRole ).toString()
                        : QString{};

    // Prefer the characters selected inside a cell
    if ( model_ && currentSelection.hasInCellSelection() ) {
        const auto selText = currentSelection.selectedText( *model_ );
        if ( !selText.isEmpty() ) {
            cellText = selText;
        }
    }

    QMenu menu( this );

    // ── Highlighters submenu ──
    auto* highlightersMenu = new HighlightersMenu( tr( "Highlighters" ), &menu );
    highlightersMenu->createHighlightersMenu();
    highlightersMenu->populateHighlightersMenu();
    highlightersMenu->setApplyChange( [ this ]() { Q_EMIT highlightersChange(); } );
    menu.addMenu( highlightersMenu );

    // ── Color labels submenu ──
    auto* colorLabelsMenu = menu.addMenu( tr( "Color labels" ) );
    const bool hasText = !cellText.isEmpty();
    colorLabelsMenu->setEnabled( hasText );
    QActionGroup* colorLabelsActionGroup = nullptr;

    if ( hasText ) {
        colorLabelsActionGroup = new QActionGroup( &menu );

        // Determine current label for the cell text
        const auto& quickHighlighters = HighlighterSetCollection::get().quickHighlighters();
        const auto& currentLabels = colorLabels_;
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
        const auto maxLabel
            = std::min( currentLabels.size(), static_cast<size_t>( quickHighlighters.size() ) );
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
        connect( clearAllAction, &QAction::triggered, this,
                 [ this ]() { Q_EMIT clearColorLabels(); } );

        connect( colorLabelsActionGroup, &QActionGroup::triggered, this,
                 [ this, clickedIdx ]( QAction* action ) {
                     if ( action->data().isValid() ) {
                         selectCellTextUnlessInCell( clickedIdx );
                         Q_EMIT addColorLabel( static_cast<size_t>( action->data().toInt() ) );
                     }
                 } );
    }

    menu.addSeparator();

    // ── Mark ──
    auto* markAction = menu.addAction( hasSelection ? tr( "Mark / Unmark lines" ) : tr( "Mark" ) );
    markAction->setEnabled( hasSelection );
    connect( markAction, &QAction::triggered, this, &LogTableView::markSelection );

    menu.addSeparator();

    // ── Copy ──
    auto* copyAction = menu.addAction( tr( "Copy" ) );
    copyAction->setShortcut( QKeySequence::Copy );
    copyAction->setEnabled( hasSelection );
    connect( copyAction, &QAction::triggered, this, &LogTableView::copySelection );

    auto* copyWithLinesAction = menu.addAction( tr( "Copy with line numbers" ) );
    copyWithLinesAction->setEnabled( hasSelection );
    connect( copyWithLinesAction, &QAction::triggered, this,
             &LogTableView::copySelectionWithLineNumbers );

    // ── Scratchpad ──
    auto* sendToScratchpadAction = menu.addAction( tr( "Send to scratchpad" ) );
    sendToScratchpadAction->setEnabled( hasText );
    connect( sendToScratchpadAction, &QAction::triggered, this, [ this, clickedIdx ]() {
        selectCellTextUnlessInCell( clickedIdx );
        Q_EMIT sendSelectionToScratchpad();
    } );

    auto* replaceInScratchpadAction = menu.addAction( tr( "Replace scratchpad" ) );
    replaceInScratchpadAction->setEnabled( hasText );
    connect( replaceInScratchpadAction, &QAction::triggered, this, [ this, clickedIdx ]() {
        selectCellTextUnlessInCell( clickedIdx );
        Q_EMIT replaceScratchpadWithSelection();
    } );

    menu.addSeparator();

    // ── Search ──
    if ( hasText ) {
        const auto escapedCell = QRegularExpression::escape( cellText );

        auto* replaceSearchAction
            = menu.addAction( tr( "Replace search with \"%1\"" ).arg( cellText.left( 30 ) ) );
        connect( replaceSearchAction, &QAction::triggered, this,
                 [ this, escapedCell ]() { Q_EMIT replaceSearch( escapedCell ); } );

        auto* addToSearchAction
            = menu.addAction( tr( "Add \"%1\" to search" ).arg( cellText.left( 30 ) ) );
        connect( addToSearchAction, &QAction::triggered, this,
                 [ this, escapedCell ]() { Q_EMIT addToSearch( escapedCell ); } );

        auto* excludeSearchAction
            = menu.addAction( tr( "Exclude \"%1\" from search" ).arg( cellText.left( 30 ) ) );
        connect( excludeSearchAction, &QAction::triggered, this,
                 [ this, escapedCell ]() { Q_EMIT excludeFromSearch( escapedCell ); } );
    }

    menu.addSeparator();

    // ── Splitter position ──
    auto* saveSplitterAction = menu.addAction( tr( "Save splitter position" ) );
    connect( saveSplitterAction, &QAction::triggered, this,
             [ this ]() { Q_EMIT saveDefaultSplitterSizes(); } );

    // ── Save to file ──
    auto* saveToFileAction = menu.addAction( tr( "Save to file" ) );
    connect( saveToFileAction, &QAction::triggered, this, &LogTableView::saveToFile );

    auto* saveSelectedToFileAction = menu.addAction( tr( "Save selected to file" ) );
    saveSelectedToFileAction->setEnabled( hasSelection );
    connect( saveSelectedToFileAction, &QAction::triggered, this,
             &LogTableView::saveSelectedToFile );

    menu.exec( viewport()->mapToGlobal( pos ) );

    highlightersMenu->clearHighlightersMenu();
}

// Copy the selected text: the characters selected inside a cell, or else the
// selected Rows with their cells separated by tabs.
void LogTableView::copySelection()
{
    const auto text = selectedText();
    if ( !text.isEmpty() ) {
        QApplication::clipboard()->setText( text );
    }
}

// Copy the selected Rows with their line numbers prepended.
void LogTableView::copySelectionWithLineNumbers()
{
    if ( !model_ ) {
        return;
    }

    const auto lines = selectedLogLines();
    if ( lines.empty() ) {
        return;
    }

    QStringList copied;
    copied.reserve( static_cast<qsizetype>( lines.size() ) );
    const int colCount = model_->columnCount();

    for ( const auto& line : lines ) {
        const auto row = rows_->rowOf( line ).value_or( -1 );
        QStringList cells;
        cells.reserve( colCount );
        for ( int c = 0; c < colCount; ++c ) {
            cells << model_->index( row, c ).data( Qt::DisplayRole ).toString();
        }
        // 1-based line number
        copied << QString( "%1\t%2" ).arg( line.get() + 1 ).arg( cells.join( '\t' ) );
    }

    QApplication::clipboard()->setText( copied.join( '\n' ) );
}

void LogTableView::selectCellTextUnlessInCell( const QModelIndex& index )
{
    if ( !index.isValid() || selection_.hasInCellSelection() ) {
        return;
    }

    const auto cellText = index.data( Qt::DisplayRole ).toString();
    selection_.selectInCell( index.row(), index.column(), 0, static_cast<int>( cellText.size() ) );
    showInCellSelection();
}

void LogTableView::saveToFile()
{
    if ( !model_ || !logData_ ) {
        return;
    }

    const auto filename = QFileDialog::getSaveFileName( this, "Save content" );
    if ( filename.isEmpty() ) {
        return;
    }

    // The save writes positions [0, number of Rows): position n is the
    // Log Line of Row n.
    const auto end = LineNumber( static_cast<uint64_t>( model_->rowCount() ) );
    auto readLines = [ logFile = logData_, rows = rows_ ]( LineNumber first, LinesCount count ) {
        logsquirl::vector<QString> text;
        text.reserve( static_cast<size_t>( count.get() ) );
        for ( auto position = first.get(); position < first.get() + count.get(); ++position ) {
            text.push_back(
                logFile->getLineString( rows->logLineAt( static_cast<int>( position ) ) ) );
        }
        return text;
    };

    saveLinesWithProgress( this, filename, std::move( readLines ), 0_lnum, end,
                           logData_->getDisplayEncoding() );
}

void LogTableView::saveSelectedToFile()
{
    if ( selectedLogLines().empty() ) {
        return;
    }

    const auto filename = QFileDialog::getSaveFileName( this, "Save content" );
    if ( !filename.isEmpty() ) {
        saveSelectedTo( filename );
    }
}

void LogTableView::saveSelectedTo( const QString& filename )
{
    auto lines = selectedLogLines();
    if ( !logData_ || lines.empty() ) {
        return;
    }

    std::sort( lines.begin(), lines.end() );
    lines.erase( std::unique( lines.begin(), lines.end() ), lines.end() );

    // The save writes positions [0, number of selected Log Lines): position n
    // is the n-th selected Log Line.
    const auto end = LineNumber( static_cast<uint64_t>( lines.size() ) );
    auto readLines = [ logFile = logData_, lines ]( LineNumber first, LinesCount count ) {
        logsquirl::vector<QString> text;
        text.reserve( static_cast<size_t>( count.get() ) );
        for ( auto position = first.get(); position < first.get() + count.get(); ++position ) {
            text.push_back( logFile->getLineString( lines[ static_cast<size_t>( position ) ] ) );
        }
        return text;
    };

    saveLinesWithProgress( this, filename, std::move( readLines ), 0_lnum, end,
                           logData_->getDisplayEncoding() );
}

// Mark or unmark the Log Lines of the selected Rows.
void LogTableView::markSelection()
{
    const auto lines = selectedLogLines();
    if ( lines.empty() ) {
        return;
    }

    Q_EMIT markLines( lines );

    // Repaint so mark colors update
    viewport()->update();
}

// Save column widths for the Log Format to QSettings.
void LogTableView::saveColumnWidths()
{
    if ( programmaticColumnResize_ || !format_ || !model_ ) {
        return;
    }

    QSettings settings;
    settings.beginGroup( columnWidthsGroup( *format_ ) );

    const auto* header = horizontalHeader();
    const int colCount = model_->columnCount();

    // Save column count and a layout fingerprint so stale widths can be detected
    settings.setValue( "_columnCount", colCount );
    QStringList colNames;
    for ( int i = 0; i < colCount; ++i ) {
        colNames << model_->headerData( i, Qt::Horizontal ).toString();
    }
    settings.setValue( "_columnNames", colNames.join( "|" ) );

    for ( int i = 0; i < colCount; ++i ) {
        settings.setValue( QString::number( i ), header->sectionSize( i ) );
    }

    settings.endGroup();
}

// Apply column widths saved for the Log Format, if they match its columns.
bool LogTableView::applySavedColumnWidths()
{
    if ( !format_ || !model_ ) {
        return false;
    }

    QSettings settings;
    settings.beginGroup( columnWidthsGroup( *format_ ) );

    const int savedColCount = settings.value( "_columnCount", -1 ).toInt();
    const int currentColCount = model_->columnCount();
    if ( savedColCount != currentColCount ) {
        settings.endGroup();
        return false;
    }

    // Verify layout fingerprint matches current columns
    QStringList currentNames;
    for ( int i = 0; i < currentColCount; ++i ) {
        currentNames << model_->headerData( i, Qt::Horizontal ).toString();
    }
    if ( settings.value( "_columnNames" ).toString() != currentNames.join( "|" ) ) {
        settings.endGroup();
        return false;
    }

    programmaticColumnResize_ = true;
    for ( int i = 0; i < currentColCount; ++i ) {
        const int w = settings.value( QString::number( i ), -1 ).toInt();
        if ( w > 0 ) {
            setColumnWidth( i, w );
        }
    }
    programmaticColumnResize_ = false;
    stretchLastColumn();

    settings.endGroup();
    return true;
}

// Stretch the last column to fill any remaining viewport space.
void LogTableView::stretchLastColumn()
{
    if ( !model_ ) {
        return;
    }

    const int colCount = model_->columnCount();
    const int lastCol = colCount - 1;
    if ( lastCol < 0 ) {
        return;
    }

    const auto* header = horizontalHeader();
    int usedWidth = 0;
    for ( int i = 0; i < colCount; ++i ) {
        usedWidth += header->sectionSize( i );
    }
    const int viewportWidth = viewport()->width();
    if ( usedWidth < viewportWidth ) {
        const int currentLast = header->sectionSize( lastCol );
        programmaticColumnResize_ = true;
        setColumnWidth( lastCol, currentLast + ( viewportWidth - usedWidth ) );
        programmaticColumnResize_ = false;
    }
}

// Compute column widths by sampling rows from the model data.
// Measures actual text width with the view's font metrics to ensure no clipping.
void LogTableView::autoSizeColumns()
{
    if ( !model_ ) {
        return;
    }

    const int colCount = model_->columnCount();
    const int rowCount = model_->rowCount();
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
    const auto fm = fontMetrics();
    // The delegate's padding on both sides, plus a little margin so the
    // widest cell does not sit flush against the column edge.
    constexpr int cellMargin = 8;
    constexpr int cellPadding = 2 * LogTableHighlightDelegate::HorizontalTextPadding + cellMargin;

    QVector<int> maxWidths( colCount, 0 );

    // Start with header text widths as minimum
    for ( int col = 0; col < colCount; ++col ) {
        const auto headerText = model_->headerData( col, Qt::Horizontal ).toString();
        maxWidths[ col ] = fm.horizontalAdvance( headerText ) + cellPadding;
    }

    // Measure cell content widths from the first N rows
    for ( int row = 0; row < sampleRows; ++row ) {
        for ( int col = 0; col < colCount; ++col ) {
            const auto text = model_->index( row, col ).data( Qt::DisplayRole ).toString();
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
        setColumnWidth( col, maxWidths[ col ] );
    }
    programmaticColumnResize_ = false;

    stretchLastColumn();
}
