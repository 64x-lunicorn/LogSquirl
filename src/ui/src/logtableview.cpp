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
#include <numeric>
#include <utility>

#include <QFileDialog>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>

#include "abstractlogdata.h"
#include "abstractlogview.h"
#include "clipboard.h"
#include "linessaver.h"
#include "logfiltereddata.h"
#include "logformattablemodel.h"
#include "logtablehighlightdelegate.h"
#include "overview.h"
#include "overviewwidget.h"
#include "persistentinfo.h"
#include "presentationmenu.h"
#include "quickfind.h"
#include "quickfindpattern.h"
#include "regularexpression.h"
#include "theme.h"
#include "wordrule.h"

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

    // The font Log Lines are drawn in arrives through updateFont(), which
    // whoever builds this view calls before the first frame and again whenever
    // it changes: this view reads no setting for it.

    // Highlight delegate for match/mark row coloring and text highlighting:
    // it paints from the state this view holds.
    state_.quickFindPattern = std::make_shared<QuickFindPattern>();
    delegate_ = new LogTableHighlightDelegate( state_, rows_, this );
    // The settings that color Log Lines reach the delegate through
    // setDecorationPolicy(), which whoever builds this view calls before the
    // first frame and again whenever they change: this view derives no Policy
    // of its own.
    setItemDelegate( delegate_ );

    // The Color Labels follow the Theme: the delegate reads their colors as
    // it paints, so a Theme applied only repaints (ADR-0006).
    Theme::whenApplied( this, [ this ] { viewport()->update(); } );

    quickFind_ = std::make_unique<QuickFind>(
        [ this ]() { return QuickFindLines::everyLogLine( *logData_ ); },
        [ this ]( LineNumber logLine ) {
            const auto row = rows_->rowOf( logLine );
            return model_ != nullptr && row.has_value() && *row >= 0 && *row < model_->rowCount();
        } );
    // Direct: QuickFind checked that the Log Line is shown in the same call.
    connect( quickFind_.get(), &QuickFind::searchDone, this, &LogTableView::showQuickFindResult,
             Qt::DirectConnection );
    connect( quickFind_.get(), &QuickFind::notify, this, &LogTableView::notifyQuickFind );
    connect( quickFind_.get(), &QuickFind::clearNotification, this,
             &LogTableView::clearQuickFindNotification );

    setContextMenuPolicy( Qt::CustomContextMenu );
    connect( this, &QWidget::customContextMenuRequested, this, &LogTableView::showContextMenu );

    horizontalHeader()->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( horizontalHeader(), &QWidget::customContextMenuRequested, this,
             &LogTableView::showHeaderContextMenu );

    // Move the overview's current-view indicator along when scrolling
    connect( verticalScrollBar(), &QScrollBar::valueChanged, this,
             [ this ]() { updateOverview(); } );

    // The wheel, a drag and the scrollbar's own steps and pages move the
    // slider before its value: where it is moved to says whether the user
    // leaves the bottom.
    connect( verticalScrollBar(), &QAbstractSlider::actionTriggered, this,
             [ this ]() { leaveFollowAwayFromBottom( verticalScrollBar()->sliderPosition() ); } );
    // Following, the last Row stays in sight as the Text View's last line
    // does: a shorter viewport, a larger font or Rows added while hidden move
    // the bottom, not the user.
    connect( verticalScrollBar(), &QAbstractSlider::rangeChanged, this,
             [ this ]( int, int maximum ) {
                 if ( follow_ ) {
                     verticalScrollBar()->setValue( maximum );
                 }
             } );
}

LogTableView::~LogTableView()
{
    quickFind_->stopSearch();
}

void LogTableView::setLogFormat( const LogFormatDefinition* format, AbstractLogData* logData )
{
    // A Find next or previous under way searches the Log Lines about to go
    quickFind_->stopSearch();

    if ( model_ ) {
        // Disconnect header signals before removing the model to prevent
        // duplicate connections when updateData() reconnects.
        disconnect( horizontalHeader(), &QHeaderView::sectionResized, this,
                    &LogTableView::saveColumnWidths );
        setModel( nullptr );
        delete model_;
        model_ = nullptr;
    }

    format_ = format;
    logData_ = logData;
    columnsNeedSizing_ = false;

    state_.selection = TableViewSelection{};

    // The overview no longer shows anything valid
    if ( overviewWidget_ ) {
        overviewWidget_->hide();
    }
}

void LogTableView::updateData( bool follow, const QDate& modificationDate )
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

    const auto lineCount = logData_->getNbLine().get();
    const int lineCountInt
        = static_cast<int>( std::min( lineCount, static_cast<uint64_t>( INT_MAX ) ) );
    model_->setModificationDate( modificationDate );
    model_->setLineCount( lineCountInt );
    // New Log Lines may match where QuickFind last found nothing more.
    quickFind_->resetLimits();
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
    if ( state_.quickFindPattern ) {
        disconnect( state_.quickFindPattern.get(), nullptr, this, nullptr );
    }
    state_.quickFindPattern = std::move( pattern );
    // A new pattern may match beyond where the last one found nothing more.
    connect( state_.quickFindPattern.get(), &QuickFindPattern::patternUpdated, this,
             [ this ]() { quickFind_->resetLimits(); } );
}

void LogTableView::setSearchPattern( const RegularExpressionPattern& pattern )
{
    delegate_->setSearchPattern( pattern );
    repaintIfActive();
}

void LogTableView::setCurrentSearch( const LogFilteredData* search )
{
    state_.currentSearch = search;
    repaintIfActive();
}

void LogTableView::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    state_.searchStart = startLine;
    state_.searchEnd = endLine;
    repaintIfActive();
}

void LogTableView::setColorLabels( const std::vector<QStringList>& labels )
{
    state_.setColorLabelWords( labels );
    repaintIfActive();
}

void LogTableView::setDecorationPolicy( const DecorationPolicy& policy )
{
    delegate_->setDecorationPolicy( policy );
    repaintIfActive();
}

void LogTableView::setPresentationPolicy( const PresentationPolicy& policy )
{
    // Both Presentations share the one Overview: the Table View makes room
    // for it, or takes the room back.
    if ( overview_ != nullptr ) {
        overview_->setVisible( policy.overviewVisible );
    }
    updateOverview();
}

void LogTableView::allowFollowMode( bool )
{
    // The Table View never engages follow itself.
}

void LogTableView::followSet( bool checked )
{
    follow_ = checked;
    // Following starts at the bottom, as in the Text View.
    if ( follow_ && model_ && model_->rowCount() > 0 ) {
        scrollToBottom();
    }
}

void LogTableView::leaveFollowAwayFromBottom( int position )
{
    if ( follow_ && position < verticalScrollBar()->maximum() ) {
        follow_ = false;
        Q_EMIT followModeChanged( false );
    }
}

void LogTableView::updateFont( const QFont& font )
{
    setFont( font );
    // Adjust row height to fit the font
    const QFontMetrics fm( font );
    verticalHeader()->setDefaultSectionSize( fm.height() + 2 );
    horizontalHeader()->setFont( font );
}

void LogTableView::registerShortcuts()
{
    // The Table View has no shortcuts of its own: its keys are the table's.
}

void LogTableView::updateDecorations()
{
    if ( active_ ) {
        viewport()->update();
        updateOverview();
    }
}

void LogTableView::rereadLogLines()
{
    if ( model_ ) {
        model_->rereadRows();
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

    const bool shouldShow = active_ && overview_ && overview_->isVisible();
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
    auto selection = state_.selection;
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
                  && state_.selection.extendInCell( index.row(), index.column(), charPos );
            if ( !extended ) {
                state_.selection.startInCell( index.row(), index.column(), charPos );
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
        if ( newHoverRow != state_.hoverRow ) {
            const int oldHoverRow = std::exchange( state_.hoverRow, newHoverRow );
            updateRow( oldHoverRow );
            updateRow( state_.hoverRow );
        }

        // Drag to extend the in-cell selection, within the same cell only
        if ( selectionDragging_ && index.isValid()
             && state_.selection.extendInCell( index.row(), index.column(),
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
        if ( !state_.selection.hasInCellSelection() ) {
            state_.selection.clearInCell();
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
            selectWordAt( index, characterAtX( index, event->pos().x() ) );
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
    else if ( event->type() == QEvent::Leave && handlesMouse() && state_.hoverRow >= 0 ) {
        const int oldHoverRow = std::exchange( state_.hoverRow, -1 );
        updateRow( oldHoverRow );
    }

    return QTableView::viewportEvent( event );
}

// QTableView tells the accessibility clients of every selection and every
// current Row. On macOS, Qt answers that by building an accessibility element
// for every Row anew, seconds for a Log File of millions of Log Lines, and
// every view keeps the Table View on its Log Line, so every click in any view
// paid for it. Both go to QAbstractItemView, which repaints, and skip the
// events.
void LogTableView::selectionChanged( const QItemSelection& selected,
                                     const QItemSelection& deselected )
{
    // Skipping QTableView is the point (see above).
    QAbstractItemView::selectionChanged( selected, // NOLINT(bugprone-parent-virtual-call)
                                         deselected );
}

void LogTableView::currentChanged( const QModelIndex& current, const QModelIndex& previous )
{
    // Skipping QTableView is the point (see above).
    QAbstractItemView::currentChanged( current, // NOLINT(bugprone-parent-virtual-call)
                                       previous );
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
                leaveFollowAwayFromBottom( verticalScrollBar()->value() );
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

    // Page Up, the arrow keys and Home may move the Rows; a key that moves
    // nothing, Shift alone among them, leaves follow as it is.
    const int before = verticalScrollBar()->value();
    QTableView::keyPressEvent( event );
    if ( verticalScrollBar()->value() != before ) {
        leaveFollowAwayFromBottom( verticalScrollBar()->value() );
    }
}

void LogTableView::paintEvent( QPaintEvent* event )
{
    const auto paintPass = delegate_->paintPass();
    QTableView::paintEvent( event );
}

void LogTableView::updateRow( int row )
{
    // A Row scrolled out of sight lies outside the viewport, and nothing of
    // it is repainted.
    if ( row >= 0 ) {
        viewport()->update(
            QRect( 0, rowViewportPosition( row ), viewport()->width(), rowHeight( row ) ) );
    }
}

int LogTableView::charAtX( const QModelIndex& index, int pixelX ) const
{
    return LogTableHighlightDelegate::charIndexAtX( index.data( Qt::DisplayRole ).toString(),
                                                    QFontMetrics( font() ),
                                                    visualRect( index ).left(), pixelX );
}

int LogTableView::characterAtX( const QModelIndex& index, int pixelX ) const
{
    return LogTableHighlightDelegate::characterAtX( index.data( Qt::DisplayRole ).toString(),
                                                    QFontMetrics( font() ),
                                                    visualRect( index ).left(), pixelX );
}

// Select the word holding the character at charPos in a cell, as the Text
// View does; a character right of the text counts as the last one.
void LogTableView::selectWordAt( const QModelIndex& index, int charPos )
{
    const auto cellText = index.data( Qt::DisplayRole ).toString();
    if ( cellText.isEmpty() ) {
        return;
    }

    const auto word
        = wordAt( cellText, std::min( charPos, static_cast<int>( cellText.size() ) - 1 ) );
    if ( !word ) {
        return;
    }

    state_.selection.selectInCell( index.row(), index.column(), word->first, word->second );
    showInCellSelection();
}

void LogTableView::showContextMenu( const QPoint& pos )
{
    // Chosen entries may destroy the Table View, and the menu with it.
    QPointer<QMenu> menu = createContextMenu( pos ).release();
    menu->exec( viewport()->mapToGlobal( pos ) );
    delete menu;
}

void LogTableView::showHeaderContextMenu( const QPoint& pos )
{
    if ( !model_ ) {
        return;
    }
    const auto* header = horizontalHeader();
    const auto column = header->logicalIndexAt( pos );
    const auto fieldName = model_->headerData( column, Qt::Horizontal ).toString();
    // The elapsed time is no field of the Log Format: there is nothing to count.
    if ( fieldName.isEmpty() || model_->isElapsedColumn( column ) ) {
        return;
    }

    QPointer<QMenu> menu = new QMenu( this );
    const auto* countAction = menu->addAction( tr( "Count values" ) );
    const auto* chosen = menu->exec( header->mapToGlobal( pos ) );
    const bool count = chosen == countAction;
    delete menu;
    if ( count ) {
        Q_EMIT countValuesRequested( fieldName );
    }
}

std::unique_ptr<QMenu> LogTableView::createContextMenu( const QPoint& pos )
{
    const auto currentSelection = selection();

    PresentationMenu::Report report;
    report.selectedLogLines = currentSelection.selectedLogLines( *rows_ );
    report.textWithinLogLine = currentSelection.hasInCellSelection();
    if ( model_ && ( report.textWithinLogLine || report.selectedLogLines.size() == 1 ) ) {
        report.selectedText = currentSelection.selectedText( *model_ );
    }
    report.logLineUnderCursor = logLineAt( pos );
    report.hasUnmarkedLogLines
        = std::any_of( report.selectedLogLines.begin(), report.selectedLogLines.end(),
                       [ this ]( LineNumber line ) {
                           return !rows_->lineType( state_.currentSearch, line )
                                       .testFlag( AbstractLogData::LineTypeFlags::Mark );
                       } );
    report.colorLabels = state_.colorLabelWords();

    PresentationMenu::Entries entries;
    entries.highlightersChange = [ this ]() { Q_EMIT highlightersChange(); };
    entries.addColorLabel = [ this ]( size_t label ) { Q_EMIT addColorLabel( label ); };
    entries.clearColorLabels = [ this ]() { Q_EMIT clearColorLabels(); };
    entries.mark = [ this ]() { markSelection(); };
    entries.copy = [ this ]() { copySelection(); };
    entries.copyWithLineNumbers = [ this ]() { copySelectionWithLineNumbers(); };
    entries.sendToScratchpad = [ this ]() { Q_EMIT sendSelectionToScratchpad(); };
    entries.replaceScratchpad = [ this ]() { Q_EMIT replaceScratchpadWithSelection(); };
    entries.findNext = [ this ]() { findSelected( true ); };
    entries.findPrevious = [ this ]() { findSelected( false ); };
    entries.replaceSearch = [ this ]() { Q_EMIT replaceSearch( selectedText() ); };
    entries.addToSearch = [ this ]() { Q_EMIT addToSearch( selectedText() ); };
    entries.excludeFromSearch = [ this ]() { Q_EMIT excludeFromSearch( selectedText() ); };
    entries.setSearchStart = [ this ]( LineNumber logLine ) {
        const auto end = state_.searchEnd.value_or(
            LineNumber( logData_ != nullptr ? logData_->getNbLine().get() : 0 ) );
        Q_EMIT changeSearchLimits( logLine, end );
    };
    // The end is the Log Line after the last one searched.
    entries.setSearchEnd = [ this ]( LineNumber logLine ) {
        Q_EMIT changeSearchLimits( state_.searchStart, logLine + 1_lcount );
    };
    entries.clearSearchLimits = [ this ]() { Q_EMIT clearSearchLimits(); };
    entries.saveSplitterPosition = [ this ]() { Q_EMIT saveDefaultSplitterSizes(); };
    entries.saveToFile = [ this ]() { saveToFile(); };
    entries.saveSelectedToFile = [ this ]() { saveSelectedToFile(); };

    return PresentationMenu::create( this, report, entries );
}

void LogTableView::findSelected( bool forward )
{
    const auto inCell = state_.selection.inCell();
    if ( !model_ || !logData_ || !state_.selection.hasInCellSelection() || !inCell ) {
        return;
    }

    // The search starts from the Row holding the selected characters; which
    // Rows are selected is left as it is.
    selectionModel()->setCurrentIndex( model_->index( inCell->row, inCell->column ),
                                       QItemSelectionModel::NoUpdate );

    // What AbstractLogView::findNextSelected() does: the QuickFind bar takes
    // the pattern and the direction and runs the search in the view that has
    // the focus, which this one takes first.
    setFocus( Qt::OtherFocusReason );
    Q_EMIT changeQuickFind( selectedText(),
                            forward ? QuickFindMux::Forward : QuickFindMux::Backward );
    Q_EMIT searchNext();
}

Selection LogTableView::quickFindStart() const
{
    Selection from;
    const auto current = currentIndex();
    if ( !model_ || !current.isValid() ) {
        return from;
    }

    const auto logLine = rows_->logLineAt( current.row() );
    const auto inCell = state_.selection.inCell();
    if ( inCell && state_.selection.hasInCellSelection() && inCell->row == current.row() ) {
        const auto cells = cellsInLogLine( inCell->row );
        const auto* span = cells ? cells->spanOf( inCell->column ) : nullptr;
        if ( cells && span ) {
            const auto start = std::min( inCell->startChar, inCell->endChar );
            const auto end = std::min( std::max( inCell->startChar, inCell->endChar ),
                                       span->end - span->start );
            if ( start < end ) {
                from.selectPortion( logLine,
                                    LineColumn{ cells->displayColumnOf( span->start + start ) },
                                    LineColumn{ cells->displayColumnOf( span->start + end ) - 1 } );
                return from;
            }
        }
    }

    from.selectLine( logLine );
    return from;
}

std::optional<LogTableView::CellsInLogLine> LogTableView::cellsInLogLine( int row ) const
{
    auto spans = model_->columnSpans( row );
    if ( !spans ) {
        return std::nullopt;
    }
    const auto rawLine
        = model_->index( row, 0 ).data( LogFormatTableModel::RawLineRole ).toString();

    // QuickFind reads the Log Line as the Log File expands it: with its tabs
    // expanded, or as it is when the Log File expands none.
    auto displayColumns = rawToDisplayColumns( rawLine );
    const auto expandedLength = logData_->getExpandedLineString( rows_->logLineAt( row ) ).size();
    if ( expandedLength != displayColumns.back() ) {
        std::iota( displayColumns.begin(), displayColumns.end(), 0 );
    }
    return CellsInLogLine{ std::move( *spans ), std::move( displayColumns ) };
}

void LogTableView::searchUsing( QuickFindSearch search )
{
    if ( model_ && logData_ ) {
        ( quickFind_.get()->*search )( quickFindStart(), state_.quickFindPattern->getMatcher() );
    }
}

void LogTableView::searchForward()
{
    searchUsing( &QuickFind::searchForward );
}

void LogTableView::searchBackward()
{
    searchUsing( &QuickFind::searchBackward );
}

void LogTableView::incrementallySearchForward()
{
    searchUsing( &QuickFind::incrementallySearchForward );
}

void LogTableView::incrementallySearchBackward()
{
    searchUsing( &QuickFind::incrementallySearchBackward );
}

void LogTableView::incrementalSearchStop()
{
    // The Row of the match stays selected; without one, the Row the search
    // started from still is.
    quickFind_->incrementalSearchStop();
}

void LogTableView::incrementalSearchAbort()
{
    const auto initial = quickFind_->incrementalSearchAbort();
    if ( const auto line = initial.selectedLine() ) {
        state_.selection.clearInCell();
        showInCellSelection();
        showLogLine( *line );
    }
    // As the Text View does: the QuickFind bar forgets the pattern.
    Q_EMIT changeQuickFind( "", QuickFindMux::Forward );
}

void LogTableView::showQuickFindResult( bool hasMatch, const Portion& logLinePortion )
{
    if ( !hasMatch || !logLinePortion.isValid() || !model_ ) {
        return;
    }

    const auto row = rows_->rowOf( logLinePortion.line() );
    if ( !row ) {
        return;
    }

    state_.selection.clearInCell();
    if ( const auto cells = cellsInLogLine( *row ) ) {
        selectCellHolding( *row, *cells, logLinePortion );
    }
    else {
        // A JSON or logfmt Log Format places its fields nowhere in the Log
        // Line: the first cell the pattern matches holds the match.
        const auto matcher = state_.quickFindPattern->getMatcher();
        for ( int column = 0; column < model_->columnCount(); ++column ) {
            const auto cellText = model_->index( *row, column ).data( Qt::DisplayRole ).toString();
            if ( matcher.isLineMatching( cellText ) ) {
                const auto [ start, end ] = matcher.getLastMatch();
                state_.selection.selectInCell( *row, column, static_cast<int>( start.get() ),
                                               static_cast<int>( end.get() ) + 1 );
                break;
            }
        }
    }
    showInCellSelection();

    showLogLine( logLinePortion.line() );
}

void LogTableView::selectCellHolding( int row, const CellsInLogLine& cells,
                                      const Portion& logLinePortion )
{
    const auto matchStart = logLinePortion.startColumn().get<int>();
    const auto matchEnd = logLinePortion.endColumn().get<int>() + 1;
    for ( int column = 0; column < model_->columnCount(); ++column ) {
        const auto* span = cells.spanOf( column );
        if ( span == nullptr || cells.displayColumnOf( span->start ) > matchStart
             || matchEnd > cells.displayColumnOf( span->end ) ) {
            continue;
        }
        // The characters of the cell the match covers: a match beginning or
        // ending inside a tab's expansion covers the tab.
        auto start = span->start;
        while ( start + 1 < span->end && cells.displayColumnOf( start + 1 ) <= matchStart ) {
            ++start;
        }
        auto end = start + 1;
        while ( end < span->end && cells.displayColumnOf( end ) < matchEnd ) {
            ++end;
        }
        state_.selection.selectInCell( row, column, start - span->start, end - span->start );
        return;
    }
}

// Copy the selected text: the characters selected inside a cell, or else the
// selected Rows with their cells separated by tabs. Copied as the Text View
// copies (sendSelectionToClipboard).
void LogTableView::copySelection()
{
    sendSelectionToClipboard( [ this ] { return selectedText(); } );
}

// Copy the selected Rows with their line numbers prepended.
void LogTableView::copySelectionWithLineNumbers()
{
    if ( !model_ ) {
        return;
    }

    sendSelectionToClipboard( [ this ] {
        const auto lines = selectedLogLines();

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

        return copied.join( '\n' );
    } );
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

    auto& settings = PersistentInfo::getSettings( app_settings{} );
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

    auto& settings = PersistentInfo::getSettings( app_settings{} );
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
    // those lines are already in the OS page cache (Format Recognition reads the
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
