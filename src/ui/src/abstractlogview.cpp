/*
 * Copyright (C) 2009, 2010, 2011, 2012, 2013, 2015 Nicolas Bonnefon
 * and other contributors
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

// This file implements the AbstractLogView base class.
// All of the drawing and event management common to the two views is
// implemented in this class. What differs between them is the LineMapping
// each is built with: which Log Line each position shows.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <qchar.h>
#include <qcolor.h>
#include <qscreen.h>
#include <utility>
#include <vector>

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGestureEvent>
#include <QInputDialog>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QProgressDialog>
#include <QRect>
#include <QScrollBar>
#include <QShortcut>
#include <QStringView>
#include <QtCore>

#include "abstractlogview.h"
#include "containers.h"
#include "fontutils.h"
#include "highlightedmatch.h"
#include "linetypes.h"
#include "presentationmenu.h"

#include "active_screen.h"
#include "clipboard.h"
#include "configuration.h"
#include "highlighterset.h"
#include "highlightersmenu.h"
#include "linedecorator.h"
#include "log.h"
#include "overview.h"
#include "quickfind.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "shortcuts.h"
#include "wrappedstring.h"

#ifdef Q_OS_WIN
#pragma warning( disable : 4244 )
#endif

namespace {

// The margin geometry has one definition, in ViewportLayout. Hit testing, the
// scrollbars and painting all read it from there.
constexpr int SeparatorWidth = ViewportLayout::SeparatorWidth;
constexpr int BulletAreaWidth = ViewportLayout::BulletAreaWidth;
constexpr int ContentMarginWidth = ViewportLayout::ContentMarginWidth;
constexpr int LineNumberPadding = ViewportLayout::LineNumberPadding;

int textWidth( const QFontMetrics& fm, const QString& text )
{
    return fm.horizontalAdvance( text );
}

int textWidth( const QFontMetrics& fm, const QStringView& text )
{
    if ( text.isEmpty() ) {
        return 0;
    }
    return textWidth( fm, QString::fromRawData( text.data(), logsquirl::isize( text ) ) );
}

std::unique_ptr<QPainter> pixmapPainter( QPaintDevice* paintDevice, const QFont& font )
{
    auto painter = std::make_unique<QPainter>( paintDevice );
    // LOG_DEBUG << "font: " << viewport()->font().family().toStdString();
    // LOG_DEBUG << "font painter: " << painter->font().family().toStdString();

    painter->setFont( font );
    painter->setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing );

    return painter;
}

QFontMetrics pixmapFontMetrics( const QFont& font )
{
    QPixmap pm{ 1, 1 };
    auto devicePainter = pixmapPainter( &pm, font );
    return devicePainter->fontMetrics();
}

class LineChunk {
public:
    LineChunk( LineColumn firstCol, LineColumn endCol, QColor foreColor, QColor backColor )
        : start_{ firstCol }
        , end_{ endCol }
        , foreColor_{ foreColor }
        , backColor_{ backColor }
    {
    }

    LineColumn start() const
    {
        return start_;
    }
    LineColumn end() const
    {
        return end_;
    }

    LineLength size() const
    {
        auto length = end_.get() - start_.get() + 1;
        return length >= 0 ? LineLength{ length } : 0_length;
    }

    QColor foreColor() const
    {
        return foreColor_;
    }

    QColor backColor() const
    {
        return backColor_;
    }

private:
    LineColumn start_ = {};
    LineColumn end_ = {};

    QColor foreColor_;
    QColor backColor_;
};

// Utility class for syntax colouring.
// It stores the chunks of line to draw
// each chunk having a different colour
class LineDrawer {
public:
    explicit LineDrawer( const QColor& backColor )
        : backColor_( backColor )
    {
    }

    // Add a chunk of line using the given colours.
    // Both first_col and last_col are included
    // An empty chunk will be ignored.
    // the first column will be set to 0 if negative
    // The column are relative to the screen
    void addChunk( LineColumn firstCol, LineColumn lastCol, QColor fore, QColor back )
    {
        // LOG_INFO << "addChunk " << firstCol.get() << " " << lastCol.get();
        const auto length = lastCol.get() - firstCol.get() + 1;

        if ( length > 0 ) {
            chunks_.emplace_back( firstCol, lastCol, fore, back );
        }

        // LOG_INFO << "added Chunk of  " << length;
    }

    LineColumn endColumn() const
    {
        return chunks_.empty() ? 0_lcol : chunks_.back().end();
    }

    bool empty() const
    {
        return chunks_.empty();
    }

    // Draw the current line of text using the given painter,
    // in the passed block (in pixels)
    // The line must be cut to fit on the screen.
    // Only its Visual Lines firstVisualLine up to firstVisualLine +
    // visualLineCount are drawn, the first of them at initialYPos.
    // leftExtraBackgroundPx is the an extra margin to start drawing
    // the coloured // background, going all the way to the element
    // left of the line looks better.
    void draw( QPainter* painter, int initialXPos, int initialYPos, int lineWidth,
               const WrappedString& wrappedLines, size_t firstVisualLine, size_t visualLineCount,
               int leftExtraBackgroundPx )
    {
        QFontMetrics fm = painter->fontMetrics();
        const int fontHeight = fm.height();
        const int fontAscent = fm.ascent();
        const size_t endVisualLine = firstVisualLine + visualLineCount;

        int xPos = initialXPos;
        int yPos = initialYPos;
        size_t visualLine = 0;
        // LOG_INFO << "drawing chunks " << chunks_.size();
        for ( const auto& chunk : chunks_ ) {
            // Draw each chunk
            // LOG_INFO << "draw chunk: " << chunk.start().get() << " " << chunk.size().get()
            //         << " empty w: " << wrappedLines.isEmpty();
            const auto& wrappedChunks = wrappedLines.mid( chunk.start(), chunk.size() );
            bool isFirstLine = true;
            for ( const auto& chunkText : wrappedChunks ) {
                if ( !isFirstLine ) {
                    xPos = initialXPos;
                    ++visualLine;
                    if ( visualLine > firstVisualLine ) {
                        yPos += fontHeight;
                    }
                }
                isFirstLine = false;

                if ( visualLine >= endVisualLine ) {
                    // The rest is below the Viewport.
                    return;
                }

                if ( chunkText.isEmpty() || visualLine < firstVisualLine ) {
                    continue;
                }

                auto chunkWidth = textWidth( fm, chunkText );
                if ( xPos == initialXPos ) {
                    // First chunk, we extend the left background a bit,
                    // it looks prettier.
                    painter->fillRect( xPos - leftExtraBackgroundPx, yPos,
                                       chunkWidth + leftExtraBackgroundPx, fontHeight,
                                       chunk.backColor() );
                }
                else {
                    // other chunks...
                    painter->fillRect( xPos, yPos, chunkWidth, fontHeight, chunk.backColor() );
                }

                painter->setPen( chunk.foreColor() );
                painter->drawText(
                    xPos, yPos + fontAscent,
                    QString::fromRawData( chunkText.data(), logsquirl::isize( chunkText ) ) );

                xPos += chunkWidth;
            }
        }

        // Draw the empty block at the end of the line
        int blankWidth = lineWidth - xPos;

        if ( blankWidth > 0 && visualLine >= firstVisualLine )
            painter->fillRect( xPos, yPos, blankWidth, fontHeight, backColor_ );
    }

private:
    logsquirl::vector<LineChunk> chunks_;
    QColor backColor_;
};

} // namespace

void DigitsBuffer::reset()
{
    LOG_DEBUG << "DigitsBuffer::reset()";

    timer_.stop();
    digits_.clear();
}

void DigitsBuffer::add( char character )
{
    LOG_DEBUG << "DigitsBuffer::add()";

    digits_.append( QChar( character ) );
    timer_.start( DigitsTimeout, this );
}

LineNumber::UnderlyingType DigitsBuffer::content()
{
    const auto result = digits_.toULongLong();
    reset();

    return result;
}

bool DigitsBuffer::isEmpty() const
{
    return digits_.isEmpty();
}

void DigitsBuffer::timerEvent( QTimerEvent* event )
{
    if ( event->timerId() == timer_.timerId() ) {
        reset();
    }
    else {
        QObject::timerEvent( event );
    }
}

AbstractLogView::AbstractLogView( const AbstractLogData* newLogData,
                                  const QuickFindPattern* const quickFindPattern,
                                  bool initialTextWrap, QWidget* parent )
    : AbstractLogView( newLogData, std::make_unique<EveryLogLine>( newLogData ), quickFindPattern,
                       initialTextWrap, parent )
{
}

AbstractLogView::AbstractLogView( const AbstractLogData* newLogData,
                                  std::unique_ptr<const LineMapping> lines,
                                  const QuickFindPattern* const quickFindPattern,
                                  bool initialTextWrap, QWidget* parent )
    : QAbstractScrollArea( parent )
    , logData_( newLogData )
    , lines_( std::move( lines ) )
    , scrolling_( scrolledLines_, initialTextWrap )
    , searchEnd_( lines_->logLineCount().get() )
    , quickFindPattern_( quickFindPattern )
    , quickFind_(
          new QuickFind( [ this ]() { return lines_->quickFindLines(); },
                         [ this ]( LineNumber logLine ) { return lines_->shows( logLine ); } ) )
    , pixmapFontMetrics_( pixmapFontMetrics( parent ? parent->font() : QFont() ) )
{
    setViewport( nullptr );

    // The Decoration Setup is the one place that builds what the Line
    // Decorator needs. The settings that color Log Lines reach it through
    // setDecorationPolicy(), which whoever builds this view calls before it
    // is first painted and again whenever they change: this view derives no
    // Policy of its own. The QuickFind pattern outlives this view and is not
    // owned by the setup.
    decorationSetup_.setQuickFindPattern( quickFindPattern_ );

    // Initialise char dimensions from the pixmap-based font metrics so that
    // updateScrollBars() computes sensible values even before the first
    // resizeEvent() (which calls updateDisplaySize()).
    charHeight_ = std::max( pixmapFontMetrics_.height(), 1 );
    charWidth_ = std::max( textWidth( pixmapFontMetrics_, QString( "m" ) ), 1 );

    // Hovering
    setMouseTracking( true );

    connect( quickFindPattern_, SIGNAL( patternUpdated() ), this, SLOT( handlePatternUpdated() ) );
    connect( quickFind_, SIGNAL( notify( const QFNotification& ) ), this,
             SIGNAL( notifyQuickFind( const QFNotification& ) ) );
    connect( quickFind_, SIGNAL( clearNotification() ), this,
             SIGNAL( clearQuickFindNotification() ) );

    // Direct: QuickFind checked that the result's Log Line is displayed in the
    // same call, so the displayed lines cannot change before it is converted
    // to a position to scroll to.
    connect( quickFind_, &QuickFind::searchDone, this, &AbstractLogView::setQuickFindResult,
             Qt::DirectConnection );

    connect( &scrolling_.elasticHook(), &ElasticHook::lengthChanged, this,
             qOverload<>( &AbstractLogView::repaint ) );
    connect( &scrolling_.elasticHook(), &ElasticHook::hooked, this,
             &AbstractLogView::followModeChanged );

    // A step or a page up by the scrollbar leaves follow. Moving it to the
    // maximum it is already at changes no value, so scrollContentsBy() never
    // hears of it; scrolling still lands at the bottom Scroll Position.
    connect( verticalScrollBar(), &QAbstractSlider::actionTriggered, this, [ this ]( int action ) {
        const auto* scrollBar = verticalScrollBar();
        applyScroll( scrolling_.scrollBarActionTriggered(
            action == QAbstractSlider::SliderPageStepSub
                || action == QAbstractSlider::SliderSingleStepSub,
            scrollBar->sliderPosition(), scrollBar->value() ) );
    } );
    connect( verticalScrollBar(), &QAbstractSlider::sliderReleased, this, [ this ]() {
        const auto* scrollBar = verticalScrollBar();
        applyScroll(
            scrolling_.scrollBarReleased( scrollBar->sliderPosition(), scrollBar->value() ) );
    } );
}

LinesCount AbstractLogView::ScrolledLines::lineCount() const
{
    return view_.logData_->getNbLine();
}

QString AbstractLogView::ScrolledLines::lineText( LineNumber position ) const
{
    return view_.logData_->getLineString( position );
}

logsquirl::vector<QString> AbstractLogView::ScrolledLines::lineTexts( LineNumber first,
                                                                      LinesCount count ) const
{
    // Read together, as the Viewport reads them.
    return view_.logData_->getLines( first, count );
}

ScrollingViewport AbstractLogView::ScrolledLines::viewport() const
{
    const auto* area = view_.viewport();
    return ScrollingViewport{ .charWidthPx = view_.charWidth_,
                              .charHeightPx = view_.charHeight_,
                              .widthPx = area->width(),
                              .heightPx = area->height(),
                              .lineNumbersVisible = view_.lineNumbersVisible_,
                              .largestDisplayLineNumber = view_.lines_->logLineCount().get() };
}

AbstractLogView::~AbstractLogView()
{
    // Explicit cleanup: stop any in-flight QuickFind search before destruction. We must
    // delete the worker exactly once — even if stopSearch() throws — and never twice.
    if ( quickFind_ != nullptr ) {
        try {
            quickFind_->stopSearch();
        } catch ( const std::exception& e ) {
            LOG_ERROR << "Failed to stop search: " << e.what();
        } catch ( ... ) {
            LOG_ERROR << "Failed to stop search: unknown exception";
        }
        delete quickFind_;
        quickFind_ = nullptr;
    }
}

//
// Received events
//

void AbstractLogView::changeEvent( QEvent* changeEvent )
{
    QAbstractScrollArea::changeEvent( changeEvent );

    // Stop the timer if the widget becomes inactive
    if ( changeEvent->type() == QEvent::ActivationChange ) {
        if ( !isActiveWindow() )
            autoScrollTimer_.stop();
    }
    viewport()->update();
}

void AbstractLogView::mousePressEvent( QMouseEvent* mouseEvent )
{
    // The Log Line clicked; positions stay within hit testing.
    const auto line = logLineAtY( mouseEvent->pos().y() );

    if ( mouseEvent->button() == Qt::LeftButton ) {
        // Invalidate our cache
        textAreaCache_.invalid_ = true;

        if ( line.has_value() && mouseEvent->modifiers() & Qt::ShiftModifier ) {
            selection_.selectRangeFromPrevious( *line );
            selectionCurrentEndPos_ = logLineFilePosAt( mouseEvent->pos() );
            Q_EMIT newSelection( *line, 1_lcount, 0_lcol, 0_length );
            update();
        }
        else if ( line.has_value() ) {
            if ( mouseEvent->pos().x() < viewportGeometry().bulletZoneWidthPx() ) {
                // Mark a line if it is clicked in the left margin
                // (only if click and release in the same area)
                markingClickInitiated_ = true;
                markingClickLine_ = line;
            }
            else {
                // Select the line, and start a selection
                selection_.selectLine( *line );
                Q_EMIT newSelection( *line, 1_lcount, 0_lcol, 0_length );

                // Remember the click in case we're starting a selection
                selectionStarted_ = true;
                selectionStartPos_ = logLineFilePosAt( mouseEvent->pos() );
                selectionCurrentEndPos_ = selectionStartPos_;
            }
        }
    }
    else if ( mouseEvent->button() == Qt::RightButton ) {
        const auto filePos = logLineFilePosAt( mouseEvent->pos() );

        if ( line.has_value()
             && !selection_.isPortionSelected( *line, filePos.column(), filePos.column() ) ) {
            selection_.selectLine( *line );
            Q_EMIT newSelection( *line, 1_lcount, 0_lcol, 0_length );
            textAreaCache_.invalid_ = true;
        }

        // Display the popup (blocking). Chosen entries may destroy the view,
        // and the menu with it.
        QPointer<QMenu> menu = createContextMenu( mouseEvent->pos() ).release();
        menu->exec( QCursor::pos( activeScreen( this ) ) );
        delete menu;
    }

    Q_EMIT activity();
}

void AbstractLogView::mouseMoveEvent( QMouseEvent* mouseEvent )
{
    // Selection implementation
    if ( selectionStarted_ ) {
        // Invalidate our cache
        textAreaCache_.invalid_ = true;

        const auto thisEndPos = logLineFilePosAt( mouseEvent->pos() );

        if ( thisEndPos != selectionCurrentEndPos_ ) {
            const auto lineNumber = thisEndPos.line();
            // Are we on a different line?
            if ( selectionStartPos_.line() != thisEndPos.line() ) {
                if ( thisEndPos.line() != selectionCurrentEndPos_.line() ) {
                    // This is a 'range' selection
                    selection_.selectRange( selectionStartPos_.line(), lineNumber );

                    Q_EMIT newSelection(
                        lineNumber, selection_.getSelectedLinesCount( *lines_ ),
                        0_lcol, // portion selection always starts from the first column
                        selectedTextLength() );

                    update();
                }
            }
            // So we are on the same line. Are we moving horizontaly?
            else if ( thisEndPos.column() != selectionCurrentEndPos_.column() ) {
                // This is a 'portion' selection
                selection_.selectPortion( lineNumber, selectionStartPos_.column(),
                                          thisEndPos.column() );
                auto selectionStr = getSelectedText();
                Q_EMIT newSelection( lineNumber, 1_lcount, selectionStartPos_.column(),
                                     LineLength( selectionStr.size() ) );
                update();
            }
            // On the same line, and moving vertically then
            else {
                // This is a 'line' selection
                selection_.selectLine( lineNumber );
                Q_EMIT newSelection( lineNumber, 1_lcount, 0_lcol, 0_length );
                update();
            }
            selectionCurrentEndPos_ = thisEndPos;
        }

        // Do we need to scroll while extending the selection?
        QRect visible = viewport()->rect();
        visible.setLeft( viewportGeometry().leftMarginPx() );
        if ( visible.contains( mouseEvent->pos() ) )
            autoScrollTimer_.stop();
        else if ( !autoScrollTimer_.isActive() )
            autoScrollTimer_.start( 100, this );
    }
    else {
        considerMouseHovering( mouseEvent->pos().x(), mouseEvent->pos().y() );
    }
}

void AbstractLogView::mouseReleaseEvent( QMouseEvent* mouseEvent )
{
    if ( markingClickInitiated_ ) {
        markingClickInitiated_ = false;
        const auto line = logLineAtY( mouseEvent->pos().y() );
        if ( line.has_value() && line == markingClickLine_ ) {
            // Invalidate our cache
            textAreaCache_.invalid_ = true;

            Q_EMIT markLines( { *line } );
        }
    }
    else {
        selectionStarted_ = false;
        if ( autoScrollTimer_.isActive() )
            autoScrollTimer_.stop();
        updateGlobalSelection();
    }
}

void AbstractLogView::mouseDoubleClickEvent( QMouseEvent* mouseEvent )
{
    if ( mouseEvent->button() == Qt::LeftButton ) {
        // Invalidate our cache
        textAreaCache_.invalid_ = true;

        const auto pos = logLineFilePosAt( mouseEvent->pos() );
        selectWordAtPosition( pos );
    }

    Q_EMIT activity();
}

void AbstractLogView::timerEvent( QTimerEvent* timerEvent )
{
    if ( timerEvent->timerId() == autoScrollTimer_.timerId() ) {
        QRect visible = viewport()->rect();
        visible.setLeft( viewportGeometry().leftMarginPx() );
        const QPoint globalPos = QCursor::pos( activeScreen( this ) );
        const QPoint pos = viewport()->mapFromGlobal( globalPos );
        QMouseEvent ev( QEvent::MouseMove, pos, globalPos, Qt::LeftButton, Qt::LeftButton,
                        Qt::NoModifier );
        mouseMoveEvent( &ev );
        int deltaX = qMax( pos.x() - visible.left(), visible.right() - pos.x() ) - visible.width();
        int deltaY = qMax( pos.y() - visible.top(), visible.bottom() - pos.y() ) - visible.height();
        int delta = qMax( deltaX, deltaY );

        if ( delta >= 0 ) {
            if ( delta < 7 )
                delta = 7;
            int timeout = 4900 / ( delta * delta );
            autoScrollTimer_.start( timeout, this );

            if ( deltaX > 0 )
                horizontalScrollBar()->triggerAction( pos.x() < visible.center().x()
                                                          ? QAbstractSlider::SliderSingleStepSub
                                                          : QAbstractSlider::SliderSingleStepAdd );

            if ( deltaY > 0 )
                applyScroll(
                    scrolling_.stepVisualLines( pos.y() < visible.center().y() ? -1 : 1 ) );
        }
    }
    QAbstractScrollArea::timerEvent( timerEvent );
}

void AbstractLogView::moveSelectionUp()
{
    const auto delta = qMax( LineNumber::UnderlyingType{ 1 }, digitsBuffer_.content() );
    disableFollow();
    moveSelection( LinesCount( delta ), true );
}

void AbstractLogView::moveSelectionDown()
{
    const auto delta = qMax( LineNumber::UnderlyingType{ 1 }, digitsBuffer_.content() );
    disableFollow();
    moveSelection( LinesCount( delta ), false );
}

void AbstractLogView::registerShortcut( const std::string& action, std::function<void()> func )
{
    const auto& config = Configuration::get();
    const auto& configuredShortcuts = config.shortcuts();

    ShortcutAction::registerShortcut( configuredShortcuts, shortcuts_, this, Qt::WidgetShortcut,
                                      action, func );
}

void AbstractLogView::registerShortcuts()
{
    LOG_INFO << "Reloading shortcuts";

    for ( auto& shortcut : shortcuts_ ) {
        shortcut.second->deleteLater();
    }

    shortcuts_.clear();

    registerShortcut( ShortcutAction::LogViewSelectionUp, [ this ]() { moveSelectionUp(); } );
    registerShortcut( ShortcutAction::LogViewSelectionDown, [ this ]() { moveSelectionDown(); } );

    registerShortcut( ShortcutAction::LogViewScrollUp,
                      [ this ]() { applyScroll( scrolling_.stepPage( false ) ); } );
    registerShortcut( ShortcutAction::LogViewScrollDown,
                      [ this ]() { applyScroll( scrolling_.stepPage( true ) ); } );
    registerShortcut( ShortcutAction::LogViewScrollLeft, [ this ]() {
        horizontalScrollBar()->triggerAction( QScrollBar::SliderPageStepSub );
    } );
    registerShortcut( ShortcutAction::LogViewScrollRight, [ this ]() {
        horizontalScrollBar()->triggerAction( QScrollBar::SliderPageStepAdd );
    } );

    registerShortcut( ShortcutAction::LogViewJumpToTop, [ this ]() {
        const auto first = lines_->logLineAt( 0_lnum );
        if ( first.has_value() ) {
            selectAndDisplayLine( *first );
        }
    } );
    registerShortcut( ShortcutAction::LogViewJumpToBottom, [ this ]() {
        const bool wasAtBottom = scrolling_.position() == scrolling_.bottomScrollPosition();
        if ( !wasAtBottom ) {
            const auto last = lastShownLogLine();
            if ( last.has_value() ) {
                selectAndDisplayLine( *last );
            }
            jumpToBottom();
        }
        else {
            applyScroll( scrolling_.engageFollow() );
        }
    } );

    registerShortcut( ShortcutAction::LogViewJumpToStartOfLine,
                      [ this ]() { jumpToStartOfLine(); } );
    registerShortcut( ShortcutAction::LogViewJumpToEndOfLine, [ this ]() { jumpToEndOfLine(); } );
    registerShortcut( ShortcutAction::LogViewJumpToRightOfScreen,
                      [ this ]() { jumpToRightOfScreen(); } );

    registerShortcut( ShortcutAction::LogViewQfForward, [ this ]() { Q_EMIT searchNext(); } );
    registerShortcut( ShortcutAction::LogViewQfBackward, [ this ]() { Q_EMIT searchPrevious(); } );
    registerShortcut( ShortcutAction::LogViewQfSelectedForward,
                      [ this ]() { findNextSelected(); } );
    registerShortcut( ShortcutAction::LogViewQfSelectedBackward,
                      [ this ]() { findPreviousSelected(); } );

    registerShortcut( ShortcutAction::LogViewMark, [ this ]() { markSelected(); } );

    // Next Mark goes down and previous Mark up, in either view (#233).
    registerShortcut( ShortcutAction::LogViewNextMark, [ this ]() { selectMark( true ); } );
    registerShortcut( ShortcutAction::LogViewPrevMark, [ this ]() { selectMark( false ); } );

    registerShortcut( ShortcutAction::LogViewJumpToLineNumber, [ this ]() {
        // The number counts the lines the view shows, from 1.
        const auto position = LineNumber( qMax( 0ull, digitsBuffer_.content() - 1ull ) );
        const auto logLine = lines_->logLineAt( position );
        if ( logLine.has_value() ) {
            selectAndDisplayLine( *logLine );
        }
        else if ( const auto last = lastShownLogLine(); last.has_value() ) {
            selectAndDisplayLine( *last );
        }
    } );

    registerShortcut( ShortcutAction::LogViewExitView, [ this ]() { Q_EMIT exitView(); } );

    registerShortcut( ShortcutAction::LogViewSendSelectionToScratchpad,
                      [ this ]() { Q_EMIT sendSelectionToScratchpad(); } );

    registerShortcut( ShortcutAction::LogViewReplaceScratchpadWithSelection,
                      [ this ]() { Q_EMIT replaceScratchpadWithSelection(); } );

    registerShortcut( ShortcutAction::LogViewAddToSearch, [ this ]() { addToSearch(); } );
    registerShortcut( ShortcutAction::LogViewExcludeFromSearch,
                      [ this ]() { excludeFromSearch(); } );
    registerShortcut( ShortcutAction::LogViewReplaceSearch, [ this ]() { replaceSearch(); } );

    // The line shown above or below the end of the selection.
    const auto extendSelection = [ this ]( int64_t delta ) {
        const auto end = selectionCurrentEndPos_.line();
        const auto newLine = shownLogLineMovedBy( end, delta );
        if ( !newLine.has_value() || *newLine == end ) {
            // Reached the begin or the end
            return;
        }
        selectAndDisplayRange( FilePosition( *newLine, selectionCurrentEndPos_.column() ) );
    };
    registerShortcut( ShortcutAction::LogViewSelectLinesUp,
                      [ extendSelection ]() { extendSelection( -1 ); } );
    registerShortcut( ShortcutAction::LogViewSelectLinesDown,
                      [ extendSelection ]() { extendSelection( 1 ); } );
}

void AbstractLogView::keyPressEvent( QKeyEvent* keyEvent )
{
    LOG_DEBUG << "keyPressEvent received " << keyEvent->text();

    const auto text = keyEvent->text();

    if ( keyEvent->modifiers() == Qt::NoModifier && text.size() == 1 ) {
        const auto character = text.at( 0 ).toLatin1();
        if ( ( ( character > '0' ) && ( character <= '9' ) )
             || ( !digitsBuffer_.isEmpty() && character == '0' ) ) {
            // Adds the digit to the timed buffer
            digitsBuffer_.add( character );
            keyEvent->accept();
        }
        else if ( digitsBuffer_.isEmpty() && character == '0' ) {
            jumpToStartOfLine();
            keyEvent->accept();
        }
    }
    else {
        keyEvent->ignore();
    }

    if ( keyEvent->isAccepted() ) {
        Q_EMIT activity();
    }
    else {
        // Only pass bare keys to the superclass this is so that
        // shortcuts such as Ctrl+Alt+Arrow are handled by the parent.
        if ( keyEvent->modifiers() == Qt::NoModifier
             || keyEvent->modifiers() == Qt::KeypadModifier ) {
            // The scroll area would step its scrollbar, which counts whole Log
            // Lines; the view steps in Visual Lines instead.
            switch ( keyEvent->key() ) {
            case Qt::Key_Up:
                applyScroll( scrolling_.stepVisualLines( -1 ) );
                break;
            case Qt::Key_Down:
                applyScroll( scrolling_.stepVisualLines( 1 ) );
                break;
            case Qt::Key_PageUp:
                applyScroll( scrolling_.stepPage( false ) );
                break;
            case Qt::Key_PageDown:
                applyScroll( scrolling_.stepPage( true ) );
                break;
            default:
                QAbstractScrollArea::keyPressEvent( keyEvent );
                return;
            }
            keyEvent->accept();
        }
    }
}

void AbstractLogView::wheelEvent( QWheelEvent* wheelEvent )
{
    Q_EMIT activity();

    const auto phase = [ wheelEvent ]() {
        switch ( wheelEvent->phase() ) {
        case Qt::ScrollBegin:
            return WheelPhase::Begin;
        case Qt::ScrollUpdate:
            return WheelPhase::Update;
        case Qt::ScrollEnd:
            return WheelPhase::End;
        case Qt::ScrollMomentum:
            return WheelPhase::Momentum;
        case Qt::NoScrollPhase:
            break;
        }
        return WheelPhase::None;
    }();
    const WheelTurn turn{ .angleDeltaX = wheelEvent->angleDelta().x(),
                          .angleDeltaY = wheelEvent->angleDelta().y(),
                          .pixelDeltaX = wheelEvent->pixelDelta().x(),
                          .pixelDeltaY = wheelEvent->pixelDelta().y(),
                          .fastScrollHeld = wheelEvent->modifiers().testFlag( Qt::AltModifier ),
                          .pageHeld = wheelEvent->modifiers().testFlag( Qt::ShiftModifier ),
                          .phase = phase,
                          .linesPerNotch = QApplication::wheelScrollLines() };

    const auto yDelta = turn.pixels();
    if ( yDelta == 0 ) {
        QAbstractScrollArea::wheelEvent( wheelEvent );
        return;
    }

    if ( wheelEvent->modifiers().testFlag( Qt::ControlModifier ) ) {
        Q_EMIT changeFontSize( yDelta > 0 );
        return;
    }

    const auto answer = scrolling_.turnWheel( turn );
    applyScroll( answer );
    if ( answer.scrollHorizontally ) {
        QAbstractScrollArea::wheelEvent( wheelEvent );
    }
}

void AbstractLogView::resizeEvent( QResizeEvent* )
{
    if ( logData_ == nullptr )
        return;

    LOG_DEBUG << "resizeEvent received";

    updateDisplaySize();
}

bool AbstractLogView::event( QEvent* e )
{
    LOG_DEBUG << "Event! Type: " << e->type();

    // Make sure we ignore the gesture events as
    // they seem to be accepted by default.
    if ( e->type() == QEvent::Gesture ) {
        const auto gestureEvent = static_cast<QGestureEvent*>( e );
        if ( gestureEvent ) {
            const auto gestures = gestureEvent->gestures();
            for ( QGesture* gesture : gestures ) {
                LOG_DEBUG << "Gesture: " << gesture->gestureType();
                gestureEvent->ignore( gesture );
            }

            // Ensure the event is sent up to parents who might care
            return false;
        }
    }

    return QAbstractScrollArea::event( e );
}

void AbstractLogView::scrollContentsBy( int dx, int dy )
{
    LOG_DEBUG << "scrollContentsBy received " << dy << "position " << verticalScrollBar()->value();

    scrolling_.scrollBarMoved( verticalScrollBar()->value(), dx );
    scrollPositionMoved();
}

void AbstractLogView::applyScroll( const ScrollAnswer& answer )
{
    switch ( answer.followChange ) {
    case FollowChange::Leave:
        Q_EMIT followModeChanged( false );
        break;
    case FollowChange::Engage:
        Q_EMIT followModeChanged( true );
        break;
    case FollowChange::None:
        break;
    }

    if ( answer.scrolled ) {
        if ( verticalScrollBar()->value() != answer.scrollBarValue ) {
            // scrollContentsBy() follows, and keeps this Scroll Position.
            verticalScrollBar()->setValue( answer.scrollBarValue );
        }
        else {
            scrollPositionMoved();
        }
    }

    if ( answer.redraw ) {
        updateDecorations();
    }
}

void AbstractLogView::scrollPositionMoved()
{
    // Update the overview if we have one
    if ( overview_ != nullptr ) {
        const auto scrollPosition = scrolling_.position();
        overview_->updateCurrentPosition( scrollPosition.lineNumber,
                                          scrollPosition.lineNumber + getNbVisibleLines() );
    }

    // Are we hovering over a new line?
    const auto mousePos = mapFromGlobal( QCursor::pos( activeScreen( this ) ) );
    considerMouseHovering( mousePos.x(), mousePos.y() );

    // Redraw
    update();
}

void AbstractLogView::paintEvent( QPaintEvent* paintEvent )
{
    const QRect invalidRect = paintEvent->rect();
    if ( ( invalidRect.isEmpty() ) || ( logData_ == nullptr ) )
        return;

    const auto scrollPosition = scrolling_.position();
    const auto firstColumn = scrolling_.firstColumn();
    LOG_DEBUG << "paintEvent received, scrollPosition=" << scrollPosition.lineNumber << ":"
              << scrollPosition.visualLineIndex << " rect: " << invalidRect.topLeft().x() << ", "
              << invalidRect.topLeft().y() << ", " << invalidRect.bottomRight().x() << ", "
              << invalidRect.bottomRight().y();

#ifdef GLOGG_PERF_MEASURE_FPS
    static uint32_t maxline = logData_->getNbLine();
    if ( !perfCounter_.addEvent() && logData_->getNbLine() > maxline ) {
        LOG_WARNING << "Redraw per second: " << perfCounter_.readAndReset()
                    << " lines: " << logData_->getNbLine();
        perfCounter_.addEvent();
        maxline = logData_->getNbLine();
    }
#endif

    auto start = std::chrono::system_clock::now();

    // Can we use our cache?
    if ( textAreaCache_.invalid_ || ( textAreaCache_.first_column_ != firstColumn )
         || ( textAreaCache_.scroll_position_ != scrollPosition ) ) {
        // Only scrolled: what is still in view is moved, not painted again.
        if ( textAreaCache_.invalid_ || textAreaCache_.first_column_ != firstColumn
             || !scrollTextArea( scrollPosition ) ) {
            // Full redraw
            drawTextArea( &textAreaCache_.pixmap_ );
        }

        textAreaCache_.invalid_ = false;
        textAreaCache_.scroll_position_ = scrollPosition;
        textAreaCache_.first_column_ = firstColumn;
        textAreaCache_.content_key_ = viewportContentKey_;

        LOG_DEBUG << "End of writing "
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now() - start )
                         .count();
    }
    else {
        // Use the cache as is: nothing to do!
    }

    // The same geometry hit testing places the text by.
    const auto pullToFollow
        = viewportGeometry().pullToFollowGeometry( scrolling_.pullToFollowState() );

    if ( pullToFollow.barHeightPx && ( pullToFollowCache_.nb_columns_ != getNbVisibleCols() ) ) {
        LOG_DEBUG << "Drawing pull to follow bar";
        pullToFollowCache_.pixmap_
            = drawPullToFollowBar( viewport()->width(), viewport()->devicePixelRatio() );
        pullToFollowCache_.nb_columns_ = getNbVisibleCols();
    }

    QPainter devicePainter( viewport() );
    devicePainter.drawPixmap( 0, pullToFollow.textTopPx, textAreaCache_.pixmap_ );

    // Draw the "pull to follow" zone if needed
    if ( pullToFollow.barHeightPx ) {
        devicePainter.drawPixmap( 0, pullToFollow.barTopPx, pullToFollowCache_.pixmap_ );
    }

    LOG_DEBUG << "End of repaint "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now() - start )
                     .count();
}

void AbstractLogView::setLineMapping( std::unique_ptr<const LineMapping> lines )
{
    lines_ = std::move( lines );
    // Each position may show another Log Line now.
    rereadLogLines();
}

const LineMapping& AbstractLogView::lineMapping() const
{
    return *lines_;
}

void AbstractLogView::setOverview( Overview* overview, OverviewWidget* overviewWidget )
{
    overview_ = overview;
    overviewWidget_ = overviewWidget;

    if ( overviewWidget_ ) {
        connect( overviewWidget_, &OverviewWidget::lineClicked, this,
                 &AbstractLogView::jumpToLine );
    }
    refreshOverview();
}

OptionalLineNumber AbstractLogView::getViewPosition() const
{
    const auto selectedLine = selection_.selectedLine();
    if ( selectedLine.has_value() ) {
        return selectedLine;
    }

    // Middle of the view
    const auto middle
        = scrolling_.position().lineNumber + LinesCount( getNbVisibleLines().get() / 2 );
    const auto line = lines_->logLineAt( middle );
    if ( line.has_value() ) {
        return line;
    }

    // Below the last Log Line shown: the Log Line after it.
    const auto last = lastShownLogLine();
    if ( last.has_value() ) {
        return *last + 1_lcount;
    }
    return std::nullopt;
}

void AbstractLogView::selectMark( bool after )
{
    const auto from = getViewPosition();
    if ( !from.has_value() ) {
        return;
    }

    const auto mark = after ? lines_->shownMarkAfter( *from ) : lines_->shownMarkBefore( *from );
    if ( mark.has_value() ) {
        selectAndDisplayLine( *mark );
    }
}

OptionalLineNumber AbstractLogView::lastShownLogLine() const
{
    const auto count = logData_->getNbLine();
    if ( count.get() == 0 ) {
        return std::nullopt;
    }
    return lines_->logLineAt( LineNumber( count.get() - 1 ) );
}

OptionalLineNumber AbstractLogView::shownLogLineMovedBy( LineNumber logLine, int64_t delta ) const
{
    const auto count = logData_->getNbLine().get();
    if ( count == 0 ) {
        return std::nullopt;
    }

    const auto position = static_cast<int64_t>( lines_->nearestPositionOf( logLine ).get() );
    const auto moved
        = std::clamp<int64_t>( position + delta, 0, static_cast<int64_t>( count ) - 1 );
    return lines_->logLineAt( LineNumber( static_cast<LineNumber::UnderlyingType>( moved ) ) );
}

void AbstractLogView::searchUsingFunction( QuickFindSearchFn searchFunction )
{
    disableFollow();
    ( quickFind_->*searchFunction )( selection_, quickFindPattern_->getMatcher() );
}

void AbstractLogView::setQuickFindResult( bool hasMatch, const Portion& portion )
{
    if ( portion.isValid() ) {
        LOG_DEBUG << "search " << portion.line();
        // The Visual Line holding the start of the found text, at the
        // position its Log Line is shown.
        displayPosition(
            FilePosition{ lines_->nearestPositionOf( portion.line() ), portion.startColumn() } );
        selection_.selectPortion( portion );
        Q_EMIT newSelection( portion.line(), 1_lcount, 0_lcol, 0_length );
    }
    else if ( !hasMatch ) {
        selection_.clear();
    }
}

void AbstractLogView::searchForward()
{
    searchUsingFunction( &QuickFind::searchForward );
}

void AbstractLogView::searchBackward()
{
    searchUsingFunction( &QuickFind::searchBackward );
}

void AbstractLogView::incrementallySearchForward()
{
    searchUsingFunction( &QuickFind::incrementallySearchForward );
}

void AbstractLogView::incrementallySearchBackward()
{
    searchUsingFunction( &QuickFind::incrementallySearchBackward );
}

void AbstractLogView::incrementalSearchAbort()
{
    selection_ = quickFind_->incrementalSearchAbort();
    Q_EMIT changeQuickFind( "", QuickFindMux::Forward );
}

void AbstractLogView::incrementalSearchStop()
{
    auto oldSelection = quickFind_->incrementalSearchStop();
    if ( selection_.isEmpty() ) {
        selection_ = oldSelection;
    }
}

void AbstractLogView::allowFollowMode( bool allow )
{
    scrolling_.allowFollow( allow );
}

void AbstractLogView::setSearchPattern( const RegularExpressionPattern& pattern )
{
    searchPattern_ = pattern;
    decorationSetup_.setSearchPattern( pattern );
    updateDecorations();
}

void AbstractLogView::setQuickHighlighters(
    const std::vector<QuickHighlighters>& quickHighlighters )
{
    quickHighlighters_ = quickHighlighters;
    // The colors are read here, with the words: a repaint builds no
    // Highlighter, so a later change to them arrives by setting the words
    // again.
    decorationSetup_.setColorLabels( quickHighlighters_, colorLabelColors() );
    updateDecorations();
}

void AbstractLogView::setDecorationPolicy( const DecorationPolicy& policy )
{
    decorationSetup_.setPolicy( policy );
    updateDecorations();
}

void AbstractLogView::setPresentationPolicy( const PresentationPolicy& policy )
{
    // Nothing is repainted: what this Policy says reaches the view only when
    // it is scrolled, and scrolling reads it from there each time.
    scrolling_.setPresentationPolicy( policy );
}

void AbstractLogView::followSet( bool checked )
{
    applyScroll( scrolling_.followSet( checked ) );
}

void AbstractLogView::textWrapSet( bool checked )
{
    // The Log Line at the top stays.
    scrolling_.setTextWrap( checked );
    updateScrollBars();
    // No need to drop the Viewport's text here: it is kept only for the text
    // wrapping it was wrapped with, so the new one reads and wraps it again.
    updateDecorations();
}

void AbstractLogView::refreshOverview()
{
    // assert() is stripped from release builds, so a stale call would crash on the
    // ->show()/->hide() lines below. Guard explicitly instead.
    if ( overviewWidget_ == nullptr ) {
        LOG_WARNING << "refreshOverview called before overviewWidget_ was set";
        return;
    }

    // Create space for the Overview if needed
    if ( ( getOverview() != nullptr ) && getOverview()->isVisible() ) {
        setViewportMargins( 0, 0, OverviewWidth, 0 );
        overviewWidget_->show();
    }
    else {
        setViewportMargins( 0, 0, 0, 0 );
        overviewWidget_->hide();
    }
}

void AbstractLogView::setOverviewVisible( bool visible )
{
    if ( overview_ ) {
        overview_->setVisible( visible );
    }
    refreshOverview();
}

// Reset the QuickFind when the pattern is changed.
void AbstractLogView::handlePatternUpdated()
{
    LOG_DEBUG << "AbstractLogView::handlePatternUpdated()";

    quickFind_->resetLimits();
    updateDecorations();
}

// OR the current selection with the current search expression
void AbstractLogView::addToSearch()
{
    if ( selection_.isPortion() ) {
        LOG_DEBUG << "AbstractLogView::addToSearch()";
        Q_EMIT addToSearch( selection_.getSelectedText( *lines_, *logData_ ) );
    }
    else {
        LOG_ERROR << "AbstractLogView::addToSearch called for a wrong type of selection";
    }
}

// Replace the current search expression with the current selection
void AbstractLogView::replaceSearch()
{
    if ( selection_.isPortion() ) {
        LOG_DEBUG << "AbstractLogView::replaceSearch()";
        Q_EMIT replaceSearch( selection_.getSelectedText( *lines_, *logData_ ) );
    }
    else {
        LOG_ERROR << "AbstractLogView::replaceSearch called for a wrong type of selection";
    }
}

void AbstractLogView::excludeFromSearch()
{
    if ( selection_.isPortion() ) {
        LOG_DEBUG << "AbstractLogView::excludeFromSearch()";
        Q_EMIT excludeFromSearch( selection_.getSelectedText( *lines_, *logData_ ) );
    }
    else {
        LOG_ERROR << "AbstractLogView::excludeFromSearch called for a wrong type of selection";
    }
}

// Find next occurrence of the selected text (*)
void AbstractLogView::findNextSelected()
{
    // Use the selected 'word' and search forward
    if ( selection_.isPortion() ) {
        Q_EMIT changeQuickFind( selection_.getSelectedText( *lines_, *logData_ ),
                                QuickFindMux::Forward );
        Q_EMIT searchNext();
    }
}

// Find next previous of the selected text (#)
void AbstractLogView::findPreviousSelected()
{
    if ( selection_.isPortion() ) {
        Q_EMIT changeQuickFind( selection_.getSelectedText( *lines_, *logData_ ),
                                QuickFindMux::Backward );
        Q_EMIT searchNext();
    }
}

// Copy the selection to the clipboard
void AbstractLogView::copy()
{
    sendSelectionToClipboard(
        [ this ] { return selection_.getSelectedText( *lines_, *logData_ ); } );
}

// Copy the selection with line numbers to the clipboard
void AbstractLogView::copyWithLineNumbers()
{
    sendSelectionToClipboard(
        [ this ] { return selection_.getSelectedText( *lines_, *logData_, true ); } );
}

void AbstractLogView::markSelected()
{
    auto lines = selection_.getLines( *lines_ );
    if ( !lines.empty() ) {
        Q_EMIT markLines( lines );
    }
}

void AbstractLogView::saveToFile()
{
    auto start = 0_lnum;
    const auto totalLines = logData_->getNbLine();
    auto end = LineNumber{ totalLines.get() };

    saveLinesToFile( start, end );
}

void AbstractLogView::saveSelectedToFile()
{
    const auto selectedLines = selection_.getLines( *lines_ );
    if ( selectedLines.empty() ) {
        return;
    }

    // A save writes positions: from the first selected line shown through
    // the last.
    const auto start = lines_->nearestPositionOf( selectedLines.front() );
    const auto end = lines_->nearestPositionOf( selectedLines.back() ) + 1_lcount;
    saveLinesToFile( start, end );
}

void AbstractLogView::saveSelectedTo( const QString& filename )
{
    const auto selectedLines = selection_.getLines( *lines_ );
    if ( selectedLines.empty() ) {
        return;
    }

    saveLinesTo( filename, lines_->nearestPositionOf( selectedLines.front() ),
                 lines_->nearestPositionOf( selectedLines.back() ) + 1_lcount );
}

OptionalLineNumber AbstractLogView::logLineAtPoint( const QPoint& pos ) const
{
    return logLineAtY( pos.y() );
}

void AbstractLogView::saveLinesToFile( LineNumber begin, LineNumber end )
{
    const auto filename = QFileDialog::getSaveFileName( this, "Save content" );
    if ( filename.isEmpty() ) {
        return;
    }

    saveLinesTo( filename, begin, end );
}

void AbstractLogView::saveLinesTo( const QString& filename, LineNumber begin, LineNumber end )
{
    // The lines are read through a copy of what the view displays now.
    saveLinesWithProgress( this, filename, linesToSave(), begin, end,
                           logData_->getDisplayEncoding() );
}

DisplayedLinesReader AbstractLogView::linesToSave() const
{
    return lines_->linesToSave();
}

void AbstractLogView::updateSearchLimits()
{
    updateDecorations();

    Q_EMIT changeSearchLimits( searchStart_, searchEnd_ );
}

void AbstractLogView::setSearchStart( LineNumber logLine )
{
    searchStart_ = logLine;
    updateSearchLimits();
}

void AbstractLogView::setSearchEnd( LineNumber logLine )
{
    // The end is the Log Line after the last one searched.
    searchEnd_ = logLine + 1_lcount;
    updateSearchLimits();
}

void AbstractLogView::setSelectionStart()
{
    selectionStart_ = selection_.selectedLine();
}

void AbstractLogView::setSelectionEnd()
{
    const auto selectionEnd = selection_.selectedLine();

    if ( selectionStart_ && selectionEnd ) {
        selection_.selectRange( *selectionStart_, *selectionEnd );
        selectionStart_ = {};

        updateDecorations();
    }
}

//
// Public functions
//

void AbstractLogView::updateData( LinesChange change )
{
    LOG_DEBUG << "AbstractLogView::updateData";

    const auto lastLineNumber = LineNumber( logData_->getNbLine().get() );

    // Past the Log Lines there are now, the view goes back to the top.
    if ( scrolling_.dataChanged( change ) ) {
        verticalScrollBar()->setValue( 0 );
        horizontalScrollBar()->setValue( 0 );
    }

    // Crop selection if it become out of range: it holds Log Lines, so it is
    // cropped to the Log File's.
    const auto logLineCount = lines_->logLineCount();
    selection_.crop( logLineCount.get() > 0 ? LineNumber( logLineCount.get() - 1 ) : 0_lnum );

    // Adapt the scroll bars to the new content
    updateScrollBars();

    // Reset the QuickFind in case we have new stuff to search into
    quickFind_->resetLimits();

    applyScroll( scrolling_.jumpToBottomIfFollowing() );

    // Update the overview if we have one
    if ( overview_ != nullptr ) {
        // Calculate the index of the last line shown
        const auto scrollPosition = scrolling_.position();
        const LineNumber lastLine
            = qMin( lastLineNumber, scrollPosition.lineNumber + getNbVisibleLines() );
        overview_->updateCurrentPosition( scrollPosition.lineNumber, lastLine );
    }

    // Not rereadLogLines(): scrolling was told already what changed.
    refresh( ViewportChange::Text );
}

void AbstractLogView::updateFont( const QFont& font )
{
    const QFont validatedFont = FontUtils::validatedFixedPitchFont( font );
    setFont( validatedFont );
    pixmapFontMetrics_ = pixmapFontMetrics( validatedFont );
    updateDisplaySize();
    update();
}

void AbstractLogView::updateDisplaySize()
{
    // Font is assumed to be mono-space (is restricted by options dialog)
    charHeight_ = std::max( pixmapFontMetrics_.height(), 1 );
    charWidth_ = std::max( textWidth( pixmapFontMetrics_, QString( "m" ) ), 1 );

    // A new width re-wraps the Log Line at the top, keeping the character
    // that was first on the top row there. Whether the view was at the bottom
    // is taken from before the new size, font or margins moved it.
    const bool wasAtBottom = scrolling_.viewportChanged();

    // Update the scroll bars
    updateScrollBars();
    verticalScrollBar()->setPageStep( static_cast<int>( getNbVisibleLines().get() ) );

    applyScroll( scrolling_.jumpToBottomIfFollowing( wasAtBottom ) );

    LOG_DEBUG << "viewport.width()=" << viewport()->width();
    LOG_DEBUG << "viewport.height()=" << viewport()->height();
    LOG_DEBUG << "width()=" << width();
    LOG_DEBUG << "height()=" << height();

    if ( overviewWidget_ )
        overviewWidget_->setGeometry( viewport()->width() + 2, 1, OverviewWidth - 1,
                                      viewport()->height() );

    // Our text area cache is now invalid
    textAreaCache_.invalid_ = true;
    textAreaCache_.pixmap_ = QPixmap{
        static_cast<int>( std::ceil( viewport()->width() * viewport()->devicePixelRatio() ) ),
        static_cast<int>( std::ceil( static_cast<int>( getNbVisibleLines().get() ) * charHeight_
                                     * viewport()->devicePixelRatio() ) )
    };
    textAreaCache_.pixmap_.setDevicePixelRatio( viewport()->devicePixelRatio() );
}

LineNumber AbstractLogView::getTopLine() const
{
    return scrolling_.position().lineNumber;
}

ScrollPosition AbstractLogView::scrollPosition() const
{
    return scrolling_.position();
}

QString AbstractLogView::getSelectedText() const
{
    return selection_.getSelectedText( *lines_, *logData_ );
}

bool AbstractLogView::isPartialSelection() const
{
    return selection_.isPortion();
}

void AbstractLogView::selectAll()
{
    const auto first = lines_->logLineAt( 0_lnum );
    const auto last = lastShownLogLine();
    if ( first.has_value() && last.has_value() ) {
        selection_.selectRange( *first, *last );
    }
    updateDecorations();
}

void AbstractLogView::trySelectLine( LineNumber lineToSelect )
{
    selectAndDisplayLine( lineToSelect );
}

void AbstractLogView::selectAndDisplayLine( LineNumber logLine )
{
    disableFollow();
    // A Log Line not shown selects the nearest one shown.
    const auto line = lines_->nearestShownLogLine( logLine ).value_or( logLine );
    selection_.selectLine( line );
    selectionStartPos_ = FilePosition{ line, 0_lcol };
    selectionCurrentEndPos_ = selectionStartPos_;
    displayLine( line );
    Q_EMIT newSelection( line, 1_lcount, 0_lcol, 0_length );
}

void AbstractLogView::selectPortionAndDisplayLine( LineNumber logLine, LinesCount nLines,
                                                   LineColumn startCol, LineLength nSymbols )
{
    disableFollow();
    const auto line = lines_->nearestShownLogLine( logLine ).value_or( logLine );
    selection_.selectLine( line );
    selectionStartPos_ = FilePosition{ line, startCol };
    selectionCurrentEndPos_ = FilePosition{ line, startCol + nSymbols };
    displayLine( line );
    Q_EMIT newSelection( line, nLines, startCol, nSymbols );
}

// The difference between this function and displayLine() is quite
// subtle: this one always jump, even if the line passed is visible.
void AbstractLogView::jumpToLine( LineNumber logLine )
{
    // Put the selected line in the middle if possible
    const auto newScrollPosition = ScrollPosition{
        lines_->nearestPositionOf( logLine ) - LinesCount( getNbVisibleLines().get() / 2 ), 0
    };
    applyScroll( scrolling_.scrollTo( newScrollPosition ) );
}

void AbstractLogView::setLineNumbersVisible( bool lineNumbersVisible )
{
    if ( lineNumbersVisible_ == lineNumbersVisible ) {
        return;
    }

    lineNumbersVisible_ = lineNumbersVisible;
    // Line numbers take their columns from the text, which re-wraps it.
    updateDisplaySize();
}

void AbstractLogView::updateDecorations()
{
    refresh( ViewportChange::Decorations );
}

void AbstractLogView::rereadLogLines()
{
    // Nothing scrolling counted for the Log Lines holds any longer.
    scrolling_.linesReread();
    refresh( ViewportChange::Text );
}

void AbstractLogView::refresh( ViewportChange change )
{
    switch ( change ) {
    case ViewportChange::Text:
        // The Log Lines are read, expanded and wrapped again when the Viewport
        // is next asked for, by painting or hit testing.
        ++viewportGeneration_;
        // The Log Lines a selection holds may read differently.
        selectedTextLength_.forget();
        [[fallthrough]];
    case ViewportChange::Decorations:
        // Painted again from the Log Lines the Viewport holds, each decorated
        // again.
        ++decorationGeneration_;
        textAreaCache_.invalid_ = true;
        break;
    }
    update();
}

void AbstractLogView::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    searchStart_ = startLine;
    searchEnd_ = endLine;

    updateDecorations();
}

//
// Private functions
//

// The viewport layout without the Visual Lines. Everything it answers -- margins,
// visible counts, scroll ranges -- is pure arithmetic over the widget's own
// geometry, so this is cheap enough to build on every call.
ViewportLayout AbstractLogView::viewportGeometry() const
{
    return scrolling_.geometry();
}

// The viewport layout including the Visual Lines in the Viewport. They come from the
// Log File, never from a paint, so a click or a hover before the first paint
// resolves correctly.
ViewportLayout AbstractLogView::viewportLayout() const
{
    return ViewportLayout{ viewportGeometry().input(), viewportContent().visualLines };
}

AbstractLogView::ViewportContentKey AbstractLogView::currentViewportContentKey() const
{
    return ViewportContentKey{ scrolling_.position(), scrolling_.firstColumn(),
                               logData_->getNbLine(), viewport()->width(),
                               viewport()->height(),  charWidth_,
                               charHeight_,           scrolling_.textWrap(),
                               lineNumbersVisible_,   viewportGeneration_ };
}

const AbstractLogView::ViewportContent& AbstractLogView::viewportContent() const
{
    const auto key = currentViewportContentKey();

    if ( !viewportContent_.has_value() || !( viewportContentKey_ == key ) ) {
        // After a scroll the Log Lines still in the Viewport are kept, with
        // their Decorations; anything else reads them all again.
        std::optional<ViewportContent> previous;
        if ( viewportContent_.has_value() && key.onlyScrolledFrom( viewportContentKey_ ) ) {
            previous = std::move( viewportContent_ );
        }
        viewportContentKey_ = key;
        viewportContent_ = buildViewportContent( std::move( previous ) );
    }

    return *viewportContent_;
}

AbstractLogView::ViewportContent
AbstractLogView::buildViewportContent( std::optional<ViewportContent> previous ) const
{
    ViewportContent content;

    const auto geometry = viewportGeometry();
    const auto linesInFile = logData_->getNbLine();
    if ( linesInFile.get() == 0 ) {
        return content;
    }

    const auto scrollPosition = geometry.clampScrollPosition( scrolling_.position(), linesInFile );
    // Every Log Line is at least one Visual Line, so this many Log Lines
    // always fill the Viewport.
    const auto nbLines = qMin( geometry.visibleLines(),
                               linesInFile - LinesCount( scrollPosition.lineNumber.get() ) );
    const auto visibleColumns = geometry.visibleColumns();
    // The Visual Lines from the Scroll Position down to the bottom of the
    // Viewport, and no more, however many a Log Line wraps into.
    const auto maxVisualLines = static_cast<size_t>( geometry.visibleLines().get() );

    // The Log Lines the previous content holds, by position: they were read,
    // expanded and wrapped for the same text and width.
    const auto kept = [ &previous ]( LineNumber position ) -> ViewportLogLine* {
        if ( !previous.has_value() || previous->logLines.empty() ) {
            return nullptr;
        }
        const auto first = previous->logLines.front().position;
        if ( position < first ) {
            return nullptr;
        }
        const auto index = position.get() - first.get();
        return index < previous->logLines.size() ? &previous->logLines[ index ] : nullptr;
    };

    content.logLines.reserve( nbLines.get() );
    content.visualLines.reserve( maxVisualLines );

    // Log Lines not kept, read together as far as the next one kept.
    logsquirl::vector<QString> readLines;
    LineNumber readFrom{ 0 };

    for ( size_t index = 0; index < nbLines.get() && content.visualLines.size() < maxVisualLines;
          ++index ) {
        const auto position = scrollPosition.lineNumber + LinesCount( index );

        if ( auto* keptLine = kept( position ); keptLine == nullptr ) {
            if ( readLines.empty() || position.get() - readFrom.get() >= readLines.size() ) {
                auto count = 1_lcount;
                while ( index + count.get() < nbLines.get()
                        && kept( position + count ) == nullptr ) {
                    count = count + 1_lcount;
                }
                readLines = logData_->getLines( position, count );
                readFrom = position;
            }
            if ( position.get() - readFrom.get() >= readLines.size() ) {
                // The Log File holds fewer Log Lines than it said.
                break;
            }
        }

        auto logLine = [ & ]() {
            if ( auto* keptLine = kept( position ); keptLine != nullptr ) {
                return std::move( *keptLine );
            }
            auto& text = readLines[ position.get() - readFrom.get() ];
            auto wrapped = scrolling_.wrap( text, visibleColumns );
            return ViewportLogLine{ position,
                                    lines_->logLineAt( position ).value_or( position ),
                                    std::move( text ),
                                    std::move( wrapped ),
                                    0,
                                    0,
                                    std::nullopt };
        }();

        const auto& wrappedLine = logLine.wrapped;
        const auto lineLength = LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
            wrappedLine.unwrappedLine().size() ) };

        const auto wrappedCount = wrappedLine.wrappedLinesCount();
        const auto visualLineLength = [ &wrappedLine ]( size_t wrappedLineIndex ) {
            return LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
                wrappedLine.wrappedLineLength( wrappedLineIndex ) ) };
        };

        // Only the Log Line at the top can start partway through. A Visual Line
        // a re-wrap has left past its end shows its last one; the Scroll
        // Position itself is corrected where the view scrolls, not here.
        const auto firstVisualLine
            = index == 0 ? std::min( scrollPosition.visualLineIndex, wrappedCount - 1 ) : 0;

        LineColumn visualLineStart = 0_lcol;
        for ( size_t wrappedLineIndex = 0; wrappedLineIndex < firstVisualLine;
              ++wrappedLineIndex ) {
            visualLineStart += visualLineLength( wrappedLineIndex );
        }

        size_t visualLineCount = 0;
        for ( auto wrappedLineIndex = firstVisualLine;
              wrappedLineIndex < wrappedCount && content.visualLines.size() < maxVisualLines;
              ++wrappedLineIndex ) {
            const auto length = visualLineLength( wrappedLineIndex );
            content.visualLines.push_back(
                VisualLine{ position, wrappedLineIndex, visualLineStart, length, lineLength } );
            visualLineStart += length;
            ++visualLineCount;
        }

        logLine.firstVisualLine = firstVisualLine;
        logLine.visualLineCount = visualLineCount;
        content.logLines.push_back( std::move( logLine ) );
    }

    return content;
}

AbstractLogView::DecorationKey AbstractLogView::decorationKey( LineNumber logLine ) const
{
    const auto portion = selection_.getPortionForLine( logLine );
    const bool lineSelected = selection_.isLineSelected( logLine );
    return DecorationKey{ .generation = decorationGeneration_,
                          .palette = viewport()->palette().cacheKey(),
                          .selectedAsWhole = lineSelected && !selection_.isSingleLine(),
                          .selectedAsSingleLine = lineSelected && selection_.isSingleLine(),
                          .selectionStart
                          = portion.isValid() ? portion.startColumn() : LineColumn{ -1 },
                          .selectionEnd
                          = portion.isValid() ? portion.endColumn() : LineColumn{ -1 } };
}

// Returns the number of lines visible in the viewport
LinesCount AbstractLogView::getNbVisibleLines() const
{
    return viewportGeometry().visibleLines();
}

// Returns the number of columns visible in the viewport
LineLength AbstractLogView::getNbVisibleCols() const
{
    return viewportGeometry().visibleColumns();
}

// Converts the mouse y coordinate to the line number in the file
OptionalLineNumber AbstractLogView::convertCoordToLine( int yPos ) const
{
    return viewportLayout().lineAtPoint( yPos );
}

// Converts the mouse x, y coordinates to the char coordinates (in the file)
FilePosition AbstractLogView::convertCoordToFilePos( const QPoint& pos ) const
{
    return viewportLayout().filePositionAtPoint( pos.x(), pos.y() );
}

OptionalLineNumber AbstractLogView::logLineAtY( int yPos ) const
{
    const auto position = convertCoordToLine( yPos );
    if ( !position.has_value() ) {
        return std::nullopt;
    }
    return lines_->logLineAt( *position );
}

FilePosition AbstractLogView::logLineFilePosAt( const QPoint& pos ) const
{
    const auto position = convertCoordToFilePos( pos );
    return FilePosition{ lines_->logLineAt( position.line() ).value_or( position.line() ),
                         position.column() };
}

void AbstractLogView::displayLine( LineNumber logLine )
{
    displayPosition( FilePosition{ lines_->nearestPositionOf( logLine ), 0_lcol } );
}

// Makes the widget adjust itself to display the passed position.
// Doing so, it may throw itself a scrollContents event.
void AbstractLogView::displayPosition( FilePosition position )
{
    const auto target = scrolling_.visualLineOf( position );
    if ( viewportLayout().showsWholeVisualLine( target ) ) {
        // The view stays; only the selection is painted anew.
        updateDecorations();
    }
    else {
        applyScroll( scrolling_.scrollTo( target ) );
    }

    const auto logLine = lines_->logLineAt( position.line() );
    const auto portion = logLine.has_value() ? selection_.getPortionForLine( *logLine ) : Portion{};
    if ( portion.isValid() ) {
        horizontalScrollBar()->setValue( type_safe::narrow_cast<int>(
            portion.endColumn().get() - getNbVisibleCols().get() + 1 ) );
    }
}

// Move the selection up and down by the passed number of lines
void AbstractLogView::moveSelection( LinesCount delta, bool isDeltaNegative )
{
    LOG_DEBUG << "AbstractLogView::moveSelection delta=" << delta;

    auto selection = selection_.getLines( *lines_ );
    // The selection moves by lines shown, from the first line shown when
    // nothing is selected.
    OptionalLineNumber newLine;
    if ( selection.empty() ) {
        newLine = shownLogLineMovedBy( 0_lnum, 0 );
    }
    else if ( isDeltaNegative ) {
        newLine = shownLogLineMovedBy( selection.front(), -static_cast<int64_t>( delta.get() ) );
    }
    else {
        newLine = shownLogLineMovedBy( selection.back(), static_cast<int64_t>( delta.get() ) );
    }

    if ( !newLine.has_value() ) {
        return;
    }

    // Select and display the new line
    selection_.selectLine( *newLine );
    displayLine( *newLine );
    selectionStartPos_ = FilePosition{ *newLine, 0_lcol };
    selectionCurrentEndPos_ = selectionStartPos_;
    Q_EMIT newSelection( *newLine, selection_.getSelectedLinesCount( *lines_ ), 0_lcol,
                         selectedTextLength() );
}

// Make the start of the lines visible
void AbstractLogView::jumpToStartOfLine()
{
    horizontalScrollBar()->setValue( 0 );
}

LineLength AbstractLogView::maxLineLength( const logsquirl::vector<LineNumber>& lines ) const
{
    const auto& logFile = lines_->logFile();
    auto longest = 0_length;
    for ( const auto line : lines ) {
        longest = std::max( longest, logFile.getLineLength( line ) );
    }
    return longest;
}

// Make the end of the lines in the selection visible
void AbstractLogView::jumpToEndOfLine()
{
    const auto selection = selection_.getLines( *lines_ );
    horizontalScrollBar()->setValue( type_safe::narrow_cast<int>( maxLineLength( selection ).get()
                                                                  - getNbVisibleCols().get() ) );
}

// Make the end of the lines on the screen visible
void AbstractLogView::jumpToRightOfScreen()
{
    const auto nbVisibleLines = getNbVisibleLines();

    logsquirl::vector<LineNumber::UnderlyingType> visibleLinesNumbers( nbVisibleLines.get() );
    std::iota( visibleLinesNumbers.begin(), visibleLinesNumbers.end(),
               scrolling_.position().lineNumber.get() );

    logsquirl::vector<LineNumber> visibleLines;
    visibleLines.reserve( nbVisibleLines.get() );
    for ( const auto number : visibleLinesNumbers ) {
        const auto logLine = lines_->logLineAt( LineNumber{ number } );
        if ( logLine.has_value() ) {
            visibleLines.push_back( *logLine );
        }
    }
    horizontalScrollBar()->setValue( type_safe::narrow_cast<int>(
        maxLineLength( visibleLines ).get() - getNbVisibleCols().get() ) );
}

// Jump to the last line
void AbstractLogView::jumpToBottom()
{
    applyScroll( scrolling_.jumpToBottom() );
}

// Select the word under the given position
void AbstractLogView::selectWordAtPosition( const FilePosition& pos )
{
    const QString line = lines_->logFile().getExpandedLineString( pos.line() );

    const int clickPos = type_safe::narrow_cast<int>( pos.column().get() );

    const auto isWordSeparator = []( QChar c ) {
        return !c.isLetterOrNumber() && c.category() != QChar::Punctuation_Connector;
    };

    if ( line.isEmpty() || isWordSeparator( line[ clickPos ] ) ) {
        return;
    }

    const auto wordStart
        = std::find_if( line.rbegin() + line.size() - clickPos, line.rend(), isWordSeparator );
    const auto selectionStart = LineColumn{ type_safe::narrow_cast<LineColumn::UnderlyingType>(
        std::distance( line.begin(), wordStart.base() ) ) };

    const auto wordEnd = std::find_if( line.begin() + clickPos, line.end(), isWordSeparator );
    const auto selectionEnd = LineColumn{ type_safe::narrow_cast<LineColumn::UnderlyingType>(
        std::distance( line.begin(), wordEnd ) - 1 ) };

    selection_.selectPortion( pos.line(), selectionStart, selectionEnd );
    updateGlobalSelection();
    updateDecorations();
}

// Update the system global (middle click) selection (X11 only)
void AbstractLogView::updateGlobalSelection()
{
    // Updating it only for "non-trivial" (range or portion) selections
    if ( selection_.isSingleLine() ) {
        return;
    }
    // Where there is no selection clipboard, the selected text is not built.
    sendTextToSelectionClipboard( QApplication::clipboard(), [ this ]() {
        return selection_.getSelectedText( *lines_, *logData_ );
    } );
}

LineLength AbstractLogView::selectedTextLength()
{
    return selectedTextLength_.of( selection_, *lines_, *logData_ );
}

void AbstractLogView::selectAndDisplayRange( FilePosition pos )
{
    disableFollow();
    selection_.selectRange( selectionStartPos_.line(), pos.line() );
    selectionCurrentEndPos_ = pos;
    displayLine( pos.line() );
    Q_EMIT newSelection( pos.line(), selection_.getSelectedLinesCount( *lines_ ), 0_lcol,
                         selectedTextLength() );
}

std::unique_ptr<QMenu> AbstractLogView::createContextMenu( const QPoint& pos )
{
    PresentationMenu::Report report;

    const auto lines = selection_.getLines( *lines_ );
    report.selectedLogLines = lines;
    report.textWithinLogLine = selection_.isPortion();
    if ( selection_.isPortion() || selection_.isSingleLine() ) {
        report.selectedText = selection_.getSelectedText( *lines_, *logData_ );
    }

    report.logLineUnderCursor = logLineAtY( pos.y() );

    report.hasUnmarkedLogLines = std::any_of( lines.begin(), lines.end(), [ this ]( auto line ) {
        return !lines_->lineType( line ).testFlag( AbstractLogData::LineTypeFlags::Mark );
    } );
    report.colorLabels = quickHighlighters_;
    report.selectionStartSet = selectionStart_.has_value();
    report.drawnLikeTextView = true;

    PresentationMenu::Entries entries;
    entries.highlightersChange = [ this ]() { Q_EMIT highlightersChange(); };
    entries.addColorLabel = [ this ]( size_t label ) { Q_EMIT addColorLabel( label ); };
    entries.clearColorLabels = [ this ]() { Q_EMIT clearColorLabels(); };
    entries.mark = [ this ]() { markSelected(); };
    entries.copy = [ this ]() { copy(); };
    entries.copyWithLineNumbers = [ this ]() { copyWithLineNumbers(); };
    entries.sendToScratchpad = [ this ]() { Q_EMIT sendSelectionToScratchpad(); };
    entries.replaceScratchpad = [ this ]() { Q_EMIT replaceScratchpadWithSelection(); };
    entries.findNext = [ this ]() { findNextSelected(); };
    entries.findPrevious = [ this ]() { findPreviousSelected(); };
    entries.replaceSearch = [ this ]() { replaceSearch(); };
    entries.addToSearch = [ this ]() { addToSearch(); };
    entries.excludeFromSearch = [ this ]() { excludeFromSearch(); };
    entries.setSearchStart = [ this ]( LineNumber logLine ) { setSearchStart( logLine ); };
    entries.setSearchEnd = [ this ]( LineNumber logLine ) { setSearchEnd( logLine ); };
    entries.clearSearchLimits = [ this ]() { Q_EMIT clearSearchLimits(); };
    entries.setSelectionStart = [ this ]() { setSelectionStart(); };
    entries.setSelectionEnd = [ this ]() { setSelectionEnd(); };
    entries.saveSplitterPosition = [ this ]() { Q_EMIT saveDefaultSplitterSizes(); };
    entries.saveToFile = [ this ]() { saveToFile(); };
    entries.saveSelectedToFile = [ this ]() { saveSelectedToFile(); };

    return PresentationMenu::create( this, report, entries );
}

void AbstractLogView::considerMouseHovering( int xPos, int yPos )
{
    const auto line = logLineAtY( yPos );
    if ( ( xPos < viewportGeometry().leftMarginPx() ) && ( line.has_value() ) ) {
        // Mouse moved in the margin, send event up
        // (possibly to highlight the overview)
        if ( line != lastHoveredLine_ ) {
            LOG_DEBUG << "Mouse moved in margin line: " << *line;
            Q_EMIT mouseHoveredOverLine( *line );
            lastHoveredLine_ = line;
        }
    }
    else {
        if ( lastHoveredLine_.has_value() ) {
            Q_EMIT mouseLeftHoveringZone();
            lastHoveredLine_ = {};
        }
    }
}

void AbstractLogView::updateScrollBars()
{
    // The margin arithmetic the ranges need lives in the layout, so this is
    // right even before the first paint.
    const auto ranges = scrolling_.updateScrollBarRanges( logData_->getMaxLength() );

    // Lowering the maximum below the scrollbar's value moves the view to the
    // bottom Scroll Position, through scrollContentsBy().
    verticalScrollBar()->setRange( 0, ranges.verticalMaximum );

    horizontalScrollBar()->setRange( 0, ranges.horizontalMaximum );
    horizontalScrollBar()->setPageStep( ranges.horizontalPageStep );

    applyScroll( scrolling_.keepAboveBottom() );
}

bool AbstractLogView::scrollTextArea( ScrollPosition scrollPosition )
{
    auto& pixmap = textAreaCache_.pixmap_;
    const auto before = textAreaCache_.scroll_position_;
    if ( scrolling_.textWrap() || pixmap.isNull() || before.visualLineIndex != 0
         || scrollPosition.visualLineIndex != 0 ) {
        return false;
    }

    // Moved in device pixels, so a row must be a whole number of them.
    const auto pixelRatio = pixmap.devicePixelRatio();
    const auto rowHeightPx = static_cast<double>( charHeight_ ) * pixelRatio;
    const auto rowDevicePx = static_cast<int>( std::lround( rowHeightPx ) );
    const int rows = static_cast<int>( getNbVisibleLines().get() );
    if ( rowDevicePx <= 0 || std::abs( rowHeightPx - rowDevicePx ) > 1e-6
         || pixmap.height() != rows * rowDevicePx
         || !qFuzzyCompare( pixelRatio, viewport()->devicePixelRatio() ) ) {
        return false;
    }

    const auto rowsMoved = static_cast<int64_t>( scrollPosition.lineNumber.get() )
                           - static_cast<int64_t>( before.lineNumber.get() );
    if ( rowsMoved == 0 || std::abs( rowsMoved ) >= rows ) {
        return false;
    }

    // The Log Lines still in view must be the ones painted, read for the same
    // text and layout and decorated the same.
    const auto& content = viewportContent();
    if ( !viewportContentKey_.onlyScrolledFrom( textAreaCache_.content_key_ ) ) {
        return false;
    }
    const auto keptFirst = rowsMoved > 0 ? 0 : static_cast<int>( -rowsMoved );
    const auto keptEnd = rowsMoved > 0 ? rows - static_cast<int>( rowsMoved ) : rows;
    for ( int row = keptFirst; row < keptEnd && row < logsquirl::isize( content.logLines );
          ++row ) {
        const auto& logLine = content.logLines[ static_cast<size_t>( row ) ];
        if ( !logLine.decorated.has_value()
             || !( logLine.decorated->key == decorationKey( logLine.lineNumber ) ) ) {
            return false;
        }
    }

    const auto shiftPx = static_cast<int>( -rowsMoved ) * rowDevicePx;
    pixmap.scroll( 0, shiftPx, pixmap.rect() );

    // The rows that came into view. The first and the last row are painted
    // again too: the margins' lines start and end in them, and moved
    // elsewhere those ends would show.
    if ( rowsMoved > 0 ) {
        drawTextArea( &pixmap, 0, 1 );
        drawTextArea( &pixmap, keptEnd - 1, rows );
    }
    else {
        drawTextArea( &pixmap, 0, keptFirst + 1 );
        drawTextArea( &pixmap, rows - 1, rows );
    }
    return true;
}

void AbstractLogView::drawTextArea( QPaintDevice* paintDevice, int firstRow,
                                    std::optional<int> endRow )
{
    // LOG_DEBUG << "devicePixelRatio: " << viewport()->devicePixelRatio();
    // LOG_DEBUG << "viewport size: " << viewport()->size().width();
    // LOG_DEBUG << "pixmap size: " << textPixmap.width();
    // Repaint the viewport
    auto painter = pixmapPainter( paintDevice, this->font() );
    // LOG_DEBUG << "font: " << viewport()->font().family().toStdString();
    // LOG_DEBUG << "font painter: " << painter->font().family().toStdString();

    const int fontHeight = charHeight_;
    const int fontAscent = painter->fontMetrics().ascent();
    const LineLength nbVisibleCols = getNbVisibleCols();
    const bool textWrap = scrolling_.textWrap();
    const auto firstColumn = scrolling_.firstColumn();

    const int paintDeviceHeight
        = static_cast<int>( std::floor( paintDevice->height() / viewport()->devicePixelRatio() ) );
    const int paintDeviceWidth
        = static_cast<int>( std::floor( paintDevice->width() / viewport()->devicePixelRatio() ) );

    const QPalette& palette = viewport()->palette();
    const HighlighterSet& highlighterSet = HighlighterSetCollection::get().currentActiveSet();

    static const QBrush normalBulletBrush = QBrush( Qt::white );
    // What a Log Line is -- Match, Mark, or both -- is shown in the colors
    // defined once beside the Line Decorator, so the gutter bullets here and
    // the Table View's row backgrounds cannot drift apart.
    static const QBrush matchBulletBrush = QBrush( LineStatusColors::match() );
    static const QBrush markBrush = QBrush( LineStatusColors::mark() );
    static const QBrush markedMatchBrush = QBrush( LineStatusColors::markedMatch() );

    // Layout constants moved to anonymous namespace (see top of file).

    // The layout owns the margin arithmetic; painting only reads it.
    const auto layout = viewportGeometry();

    // The Log Lines to draw, already expanded and wrapped into the same Visual
    // Lines hit testing resolves points against. Painting builds none of its own.
    const auto& content = viewportContent();

    LOG_DEBUG << "drawing " << content.logLines.size() << " Log Lines";
    LOG_DEBUG << "Height: " << paintDeviceHeight;

    // Rows outside [firstRow, endRow) keep what they show.
    const int lastRow = endRow.value_or( std::numeric_limits<int>::max() / fontHeight );
    if ( firstRow > 0 || endRow.has_value() ) {
        painter->setClipRect( 0, firstRow * fontHeight, paintDeviceWidth,
                              ( lastRow - firstRow ) * fontHeight );
    }

    painter->fillRect( 0, 0, paintDeviceWidth, paintDeviceHeight,
                       palette.color( QPalette::Window ) );

    // First draw the bullet left margin
    painter->setPen( palette.color( QPalette::Text ) );
    painter->fillRect( 0, 0, BulletAreaWidth, paintDeviceHeight, Qt::darkGray );

    // Column at which the content should start (pixels)
    int contentStartPosX = layout.bulletZoneWidthPx();

    // Update the length of line numbers
    const int nbDigitsInLineNumber = layout.lineNumberDigits();

    // Draw the line numbers area
    int lineNumberAreaStartX = 0;
    if ( lineNumbersVisible_ ) {
        const auto lineNumberAreaWidth = layout.lineNumberAreaWidthPx();
        lineNumberAreaStartX = contentStartPosX;

        painter->setPen( palette.color( QPalette::Text ) );
        painter->fillRect( contentStartPosX - SeparatorWidth, 0,
                           lineNumberAreaWidth + SeparatorWidth, paintDeviceHeight, Qt::darkGray );

        painter->drawLine( contentStartPosX + lineNumberAreaWidth - SeparatorWidth, 0,
                           contentStartPosX + lineNumberAreaWidth - SeparatorWidth,
                           paintDeviceHeight );

        // Update for drawing the actual text
        contentStartPosX += lineNumberAreaWidth;
    }
    else {
        painter->fillRect( contentStartPosX - SeparatorWidth, 0, SeparatorWidth + 1,
                           paintDeviceHeight, palette.color( QPalette::Disabled, QPalette::Text ) );
        // contentStartPosX += SEPARATOR_WIDTH;
    }

    painter->drawLine( BulletAreaWidth, 0, BulletAreaWidth, paintDeviceHeight - 1 );

    // The Line Decorator owns every colour decision: the line's own
    // colours, and which of the whole-line Highlighter, main search, Color
    // Labels, QuickFind and selection wins where. It is constructed once per
    // repaint with the stable context, not once per line. That context is
    // built by the Decoration Setup, the one module that builds one for
    // either Presentation -- painting reads no setting and builds no
    // Highlighter itself. Only what the setup cannot know before the
    // repaint is passed in: the active Highlighter Set, the Search Limits --
    // Log Lines, as every Line Verdict is decided for a Log Line -- and this
    // view's palette.
    //
    // Every source it matches works against the raw line, so its Decoration
    // is in raw columns too -- the loop below moves a line's selection from
    // display to raw columns before decorating, and the finished Decoration
    // to display columns once afterwards. Tab expansion stays this view's
    // step.
    const LineDecorator lineDecorator{ decorationSetup_.context(
        highlighterSet, SearchLimits{ searchStart_, searchEnd_ },
        LinePalette::fromPalette( palette ), LineStatusDisplay::InGutter ) };

    // Position in pixel of the base line of the line to print
    int yPos = 0;
    // The row of the Visual Line drawn at yPos.
    int row = 0;
    for ( const auto& viewportLogLine : content.logLines ) {
        const auto lineNumber = viewportLogLine.lineNumber;
        const QString& logLine = viewportLogLine.text;

        const int lineRows = static_cast<int>( viewportLogLine.visualLineCount );
        if ( row + lineRows <= firstRow || row >= lastRow ) {
            // Not painted, so not decorated either.
            row += lineRows;
            yPos += fontHeight * lineRows;
            continue;
        }
        row += lineRows;

        const int xPos = layout.textOriginX();

        const auto& wrappedLineView = viewportLogLine.wrapped;
        const QStringView expandedLine = wrappedLineView.unwrappedLine();

        // A Log Line is decorated once for as long as nothing its Decoration
        // depends on changes, however often it is painted as the view scrolls.
        const auto key = decorationKey( lineNumber );
        if ( !viewportLogLine.decorated.has_value()
             || !( viewportLogLine.decorated->key == key ) ) {
            const auto lineType = lines_->lineType( lineNumber );
            const auto verdict = lineDecorator.verdictFor( LogLine{ lineNumber, logLine }, lineType,
                                                           key.selectedAsWhole );

            // Is there something selected in the line? Selection columns come
            // from mouse/pixel positions against the rendered (tab-expanded)
            // text, so they are moved to raw columns for the Line Decorator.
            const auto selectionPortion = selection_.getPortionForLine( lineNumber );
            std::optional<HighlightedMatch> rawSelection;
            if ( selectionPortion.isValid() ) {
                rawSelection = inRawColumns(
                    logLine,
                    HighlightedMatch{ selectionPortion.startColumn(), selectionPortion.size(),
                                      palette.color( QPalette::HighlightedText ),
                                      palette.color( QPalette::Highlight ) } );
            }

            viewportLogLine.decorated
                = DecoratedLogLine{ key, lineType,
                                    lineDecorator.decorate( logLine, verdict, rawSelection )
                                        .inDisplayColumns( logLine,
                                                           LineLength{ expandedLine.size() } ) };
        }

        using LineTypeFlags = AbstractLogData::LineTypeFlags;
        const auto currentLineType = viewportLogLine.decorated->lineType;
        const auto& decoration = viewportLogLine.decorated->decoration;
        const auto& lineColors = decoration.lineColors();

        // Only the Visual Lines in the Viewport are drawn: the Log Line at the
        // top can start partway through, the one at the bottom can be cut off.
        const auto firstVisualLine = viewportLogLine.firstVisualLine;
        const auto visualLineCount = viewportLogLine.visualLineCount;
        const bool showsFirstVisualLine = firstVisualLine == 0;
        const bool showsLastVisualLine
            = firstVisualLine + visualLineCount == wrappedLineView.wrappedLinesCount();
        const auto finalLineHeight = fontHeight * static_cast<int>( visualLineCount );

        painter->fillRect( xPos - ContentMarginWidth, yPos,
                           viewport()->width() - xPos + ContentMarginWidth, finalLineHeight,
                           lineColors.backColor );

        // The Decoration covers the whole text; only the part of it in view
        // is drawn, each span as it is.
        LineDrawer lineDrawer( lineColors.backColor );
        const auto firstVisibleColumn
            = std::clamp( textWrap ? 0_lcol : firstColumn, 0_lcol,
                          LineColumn{ logsquirl::isize( expandedLine ) } );
        const auto lastVisibleColumn = textWrap ? LineColumn{ logsquirl::isize( expandedLine ) }
                                                : firstColumn + nbVisibleCols;
        for ( const auto& span : decoration.spans() ) {
            if ( span.size() == 0_length || span.endColumn() < firstVisibleColumn
                 || span.startColumn() > lastVisibleColumn ) {
                continue;
            }
            lineDrawer.addChunk( std::max( span.startColumn(), firstVisibleColumn ),
                                 std::min( span.endColumn(), lastVisibleColumn ), span.foreColor(),
                                 span.backColor() );
        }
        lineDrawer.draw( painter.get(), xPos, yPos, viewport()->width(), wrappedLineView,
                         firstVisualLine, visualLineCount, ContentMarginWidth );

        if ( key.selectedAsSingleLine || key.selectionStart >= 0_lcol ) {
            auto selectionPen = QPen( palette.color( QPalette::Highlight ) );
            selectionPen.setWidth( 1 );
            painter->setPen( selectionPen );
            if ( showsFirstVisualLine ) {
                painter->drawLine( xPos - ContentMarginWidth + 1, yPos, viewport()->width() - 1,
                                   yPos );
            }
            if ( showsLastVisualLine ) {
                painter->drawLine( xPos - ContentMarginWidth + 1, yPos + finalLineHeight - 1,
                                   viewport()->width() - 1, yPos + finalLineHeight - 1 );
            }
        }

        const int lineTopY = yPos;
        yPos += finalLineHeight;

        if ( !showsFirstVisualLine ) {
            // The bullet and the line number sit beside a Log Line's first
            // Visual Line, which is above the Viewport.
            continue;
        }

        // Then draw the bullet
        painter->setPen( Qt::black );
        const int circleSize = 3;
        const int arrowHeight = 4;
        const int middleXLine = BulletAreaWidth / 2;
        const int middleYLine = lineTopY + ( fontHeight / 2 );

        if ( currentLineType.testFlag( LineTypeFlags::Mark ) ) {
            // A pretty arrow if the line is marked
            const QPointF points[ 7 ] = {
                QPointF( 1, middleYLine - 2 ),
                QPointF( middleXLine, middleYLine - 2 ),
                QPointF( middleXLine, middleYLine - arrowHeight ),
                QPointF( BulletAreaWidth - 1, middleYLine ),
                QPointF( middleXLine, middleYLine + arrowHeight ),
                QPointF( middleXLine, middleYLine + 2 ),
                QPointF( 1, middleYLine + 2 ),
            };

            painter->setBrush( currentLineType.testFlag( LineTypeFlags::Match ) ? markedMatchBrush
                                                                                : markBrush );
            painter->drawPolygon( points, 7 );
        }
        else {
            // For pretty circles
            painter->setRenderHint( QPainter::Antialiasing );

            QBrush brush = normalBulletBrush;
            if ( currentLineType.testFlag( LineTypeFlags::Match ) )
                brush = matchBulletBrush;
            painter->setBrush( brush );
            painter->drawEllipse( middleXLine - circleSize, middleYLine - circleSize,
                                  circleSize * 2, circleSize * 2 );
        }

        // Draw the line number
        if ( lineNumbersVisible_ ) {
            static const QString lineNumberFormat( "%1" );
            // Shown from 1.
            const QString& lineNumberStr
                = lineNumberFormat.arg( lineNumber.get() + 1, nbDigitsInLineNumber );
            painter->setPen( Qt::white );
            painter->drawText( lineNumberAreaStartX + LineNumberPadding, lineTopY + fontAscent,
                               lineNumberStr );
        }
    } // For each line
}

// Draw the "pull to follow" bar and return a pixmap.
// The width is passed in "logic" pixels.
QPixmap AbstractLogView::drawPullToFollowBar( int width, qreal pixelRatio )
{
    static constexpr int barWidth = 40;
    QPixmap pixmap( static_cast<int>( width * pixelRatio ), static_cast<int>( barWidth * 6.0 ) );
    pixmap.setDevicePixelRatio( pixelRatio );
    pixmap.fill( this->palette().color( this->backgroundRole() ) );
    const int nbBars = width / ( barWidth * 2 ) + 1;

    QPainter painter( &pixmap );
    painter.setPen( QPen( QColor( 0, 0, 0, 0 ) ) );
    painter.setBrush( QBrush( QColor( "lightyellow" ) ) );

    for ( int i = 0; i < nbBars; ++i ) {
        QPoint points[ 4 ] = { { ( i * 2 + 1 ) * barWidth, 0 },
                               { 0, ( i * 2 + 1 ) * barWidth },
                               { 0, ( i + 1 ) * 2 * barWidth },
                               { ( i + 1 ) * 2 * barWidth, 0 } };
        painter.drawConvexPolygon( points, 4 );
    }

    return pixmap;
}

void AbstractLogView::disableFollow()
{
    applyScroll( scrolling_.leaveFollow() );
}
