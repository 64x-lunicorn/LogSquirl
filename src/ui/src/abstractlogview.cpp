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
// Most of the actual drawing and event management common to the two views
// is implemented in this class.  The class only calls protected virtual
// functions when view specific behaviour is desired, using the template
// pattern.

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
    : QAbstractScrollArea( parent )
    , followElasticHook_( HookThreshold )
    , logData_( newLogData )
    , useTextWrap_( initialTextWrap )
    , searchEnd_( newLogData->getNbLine().get() )
    , quickFindPattern_( quickFindPattern )
    , quickFind_(
          new QuickFind( [ this ]() { return quickFindLines(); },
                         [ this ]( LineNumber logLine ) { return displaysLogLine( logLine ); } ) )
    , pixmapFontMetrics_( pixmapFontMetrics( parent ? parent->font() : QFont() ) )
{
    setViewport( nullptr );

    // Initialise char dimensions from the pixmap-based font metrics so that
    // updateScrollBars() computes sensible values even before the first
    // resizeEvent() (which calls updateDisplaySize()).
    charHeight_ = std::max( pixmapFontMetrics_.height(), 1 );
    charWidth_ = std::max( textWidth( pixmapFontMetrics_, QString( "m" ) ), 1 );

    // Hovering
    setMouseTracking( true );

    createMenu();

    connect( quickFindPattern_, SIGNAL( patternUpdated() ), this, SLOT( handlePatternUpdated() ) );
    connect( quickFind_, SIGNAL( notify( const QFNotification& ) ), this,
             SIGNAL( notifyQuickFind( const QFNotification& ) ) );
    connect( quickFind_, SIGNAL( clearNotification() ), this,
             SIGNAL( clearQuickFindNotification() ) );

    // Direct: QuickFind checked that the result's Log Line is displayed in the
    // same call, so the displayed lines cannot change before it is converted
    // to this view's line numbers.
    connect( quickFind_, &QuickFind::searchDone, this, &AbstractLogView::setQuickFindResult,
             Qt::DirectConnection );

    connect( &followElasticHook_, SIGNAL( lengthChanged() ), this, SLOT( repaint() ) );
    connect( &followElasticHook_, SIGNAL( hooked( bool ) ), this,
             SIGNAL( followModeChanged( bool ) ) );

    connect( verticalScrollBar(), &QAbstractSlider::actionTriggered, this, [ this ]( int action ) {
        if ( !followMode_ ) {
            return;
        }

        if ( action == QAbstractSlider::SliderPageStepSub
             || action == QAbstractSlider::SliderSingleStepSub ) {
            disableFollow();
        }
    } );

    // Moving the scrollbar to the maximum it is already at changes no value,
    // so scrollContentsBy() never hears of it. Such a move still lands at the
    // bottom Scroll Position, also from partway up the last Log Line. Only the
    // scrollbar's own actions and releasing its thumb count: a move between
    // Visual Lines sets no value and triggers neither.
    const auto landAtBottomOnMaximum = [ this ]() {
        const auto* scrollBar = verticalScrollBar();
        if ( scrollBar->sliderPosition() == scrollBar->maximum()
             && scrollBar->value() == scrollBar->maximum()
             && scrollPosition_ != bottomScrollPosition() ) {
            scrollTo( bottomScrollPosition() );
        }
    };
    connect( verticalScrollBar(), &QAbstractSlider::actionTriggered, this, landAtBottomOnMaximum );
    connect( verticalScrollBar(), &QAbstractSlider::sliderReleased, this, landAtBottomOnMaximum );
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
    auto line = convertCoordToLine( mouseEvent->pos().y() );

    if ( mouseEvent->button() == Qt::LeftButton ) {
        // Invalidate our cache
        textAreaCache_.invalid_ = true;

        if ( line.has_value() && mouseEvent->modifiers() & Qt::ShiftModifier ) {
            selection_.selectRangeFromPrevious( *line );
            selectionCurrentEndPos_ = convertCoordToFilePos( mouseEvent->pos() );
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
                if ( *line < logData_->getNbLine() ) {
                    selection_.selectLine( *line );
                    Q_EMIT newSelection( *line, 1_lcount, 0_lcol, 0_length );
                }

                // Remember the click in case we're starting a selection
                selectionStarted_ = true;
                selectionStartPos_ = convertCoordToFilePos( mouseEvent->pos() );
                selectionCurrentEndPos_ = selectionStartPos_;
            }
        }
    }
    else if ( mouseEvent->button() == Qt::RightButton ) {
        if ( line.has_value() && line >= logData_->getNbLine() ) {
            line = {};
        }

        const auto filePos = convertCoordToFilePos( mouseEvent->pos() );

        if ( line.has_value()
             && !selection_.isPortionSelected( *line, filePos.column(), filePos.column() ) ) {
            selection_.selectLine( *line );
            Q_EMIT newSelection( *line, 1_lcount, 0_lcol, 0_length );
            textAreaCache_.invalid_ = true;
        }

        if ( selection_.isSingleLine() ) {
            copyAction_->setText( tr( "&Copy this line" ) );
            copyWithLineNumbersAction_->setText( tr( "Copy this line with line number" ) );

            setSearchStartAction_->setEnabled( true );
            setSearchEndAction_->setEnabled( true );

            setSelectionStartAction_->setEnabled( true );
            setSelectionEndAction_->setEnabled( !!selectionStart_ );
        }
        else {
            copyAction_->setText( tr( "&Copy" ) );
            copyAction_->setStatusTip( tr( "Copy the selection" ) );

            copyWithLineNumbersAction_->setText( tr( "Copy with line numbers" ) );

            setSearchStartAction_->setEnabled( false );
            setSearchEndAction_->setEnabled( false );

            setSelectionStartAction_->setEnabled( false );
            setSelectionEndAction_->setEnabled( false );
        }

        bool hasUnmarkedLines = false;
        auto lines = selection_.getLines();
        for ( auto i = 0u; i < lines.size(); ++i ) {
            using LineTypeFlags = AbstractLogData::LineTypeFlags;
            const auto currentLineType = lineType( lines[ i ] );
            if ( !currentLineType.testFlag( LineTypeFlags::Mark ) ) {
                hasUnmarkedLines = true;
                break;
            }
        }
        markAction_->setText( hasUnmarkedLines ? tr( "&Mark" ) : tr( "Unmark" ) );

        if ( selection_.isPortion() ) {
            findNextAction_->setEnabled( true );
            findPreviousAction_->setEnabled( true );
            addToSearchAction_->setEnabled( true );
            replaceSearchAction_->setEnabled( true );
        }
        else {
            findNextAction_->setEnabled( false );
            findPreviousAction_->setEnabled( false );
            addToSearchAction_->setEnabled( false );
            replaceSearchAction_->setEnabled( false );
        }

        highlightersMenu_->createHighlightersMenu();
        highlightersMenu_->populateHighlightersMenu();
        highlightersMenu_->setApplyChange( [ this ]() { Q_EMIT highlightersChange(); } );

        auto colorLabelsActionGroup = new QActionGroup( this );
        connect( colorLabelsActionGroup, &QActionGroup::triggered, this,
                 &AbstractLogView::setColorLabel );
        colorLabelsMenu_->clear();
        colorLabelsMenu_->setEnabled( selection_.isPortion() || selection_.isSingleLine() );
        if ( colorLabelsMenu_->isEnabled() ) {
            auto selectedText = selection_.getSelectedText( logData_ );
            std::optional<size_t> currentLabel;
            for ( auto i = 0u; i < quickHighlighters_.size(); ++i ) {
                if ( quickHighlighters_[ i ].contains( selectedText ) ) {
                    currentLabel = i;
                    break;
                }
            }

            auto noneAction = colorLabelsMenu_->addAction( tr( "None" ) );
            noneAction->setActionGroup( colorLabelsActionGroup );
            noneAction->setCheckable( true );
            noneAction->setChecked( !currentLabel.has_value() );
            if ( currentLabel ) {
                noneAction->setData( static_cast<unsigned>( *currentLabel ) );
            }

            const auto& quickHighlightersConfiguration
                = HighlighterSetCollection::get().quickHighlighters();

            colorLabelsMenu_->addSeparator();
            const auto maxLabel
                = std::min( quickHighlighters_.size(),
                            static_cast<size_t>( quickHighlightersConfiguration.size() ) );
            for ( auto i = 0u; i < maxLabel; ++i ) {

                const auto& currentLabelConfiguration
                    = quickHighlightersConfiguration.at( static_cast<int>( i ) );
                auto colorLabelAction
                    = colorLabelsMenu_->addAction( currentLabelConfiguration.name );
                colorLabelAction->setActionGroup( colorLabelsActionGroup );
                colorLabelAction->setCheckable( true );
                colorLabelAction->setChecked( currentLabel == i );
                colorLabelAction->setData( i );

                QPixmap pixmap( 20, 10 );
                auto fillColor = currentLabelConfiguration.color.backColor;
                fillColor.setAlphaF( 1.0 );
                pixmap.fill( fillColor );
                colorLabelAction->setIcon( QIcon( pixmap ) );
                colorLabelAction->setIconVisibleInMenu( true );
            }
            colorLabelsMenu_->addSeparator();
            auto clearAllAction = colorLabelsMenu_->addAction( tr( "Clear all" ) );
            connect( clearAllAction, &QAction::triggered, this,
                     &AbstractLogView::clearColorLabels );
        }
        // Display the popup (blocking)
        popupMenu_->exec( QCursor::pos( activeScreen( this ) ) );

        highlightersMenu_->clearHighlightersMenu();
        colorLabelsActionGroup->deleteLater();
    }

    Q_EMIT activity();
}

void AbstractLogView::mouseMoveEvent( QMouseEvent* mouseEvent )
{
    // Selection implementation
    if ( selectionStarted_ ) {
        // Invalidate our cache
        textAreaCache_.invalid_ = true;

        const auto thisEndPos = convertCoordToFilePos( mouseEvent->pos() );

        if ( thisEndPos != selectionCurrentEndPos_ ) {
            const auto lineNumber = thisEndPos.line();
            // Are we on a different line?
            if ( selectionStartPos_.line() != thisEndPos.line() ) {
                if ( thisEndPos.line() != selectionCurrentEndPos_.line() ) {
                    // This is a 'range' selection
                    selection_.selectRange( selectionStartPos_.line(), lineNumber );

                    Q_EMIT newSelection(
                        lineNumber, selection_.getSelectedLinesCount(),
                        0_lcol, // portion selection always starts from the first column
                        LineLength{ getSelectedText().size() } );

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
        const auto line = convertCoordToLine( mouseEvent->pos().y() );
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

        const auto pos = convertCoordToFilePos( mouseEvent->pos() );
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
                stepVisualLines( pos.y() < visible.center().y() ? -1 : 1 );
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
    doRegisterShortcuts();
}

void AbstractLogView::doRegisterShortcuts()
{
    LOG_INFO << "Reloading shortcuts";

    for ( auto& shortcut : shortcuts_ ) {
        shortcut.second->deleteLater();
    }

    shortcuts_.clear();

    registerShortcut( ShortcutAction::LogViewSelectionUp, [ this ]() { moveSelectionUp(); } );
    registerShortcut( ShortcutAction::LogViewSelectionDown, [ this ]() { moveSelectionDown(); } );

    registerShortcut( ShortcutAction::LogViewScrollUp,
                      [ this ]() { stepVisualLines( -visualLinesPerPage() ); } );
    registerShortcut( ShortcutAction::LogViewScrollDown,
                      [ this ]() { stepVisualLines( visualLinesPerPage() ); } );
    registerShortcut( ShortcutAction::LogViewScrollLeft, [ this ]() {
        horizontalScrollBar()->triggerAction( QScrollBar::SliderPageStepSub );
    } );
    registerShortcut( ShortcutAction::LogViewScrollRight, [ this ]() {
        horizontalScrollBar()->triggerAction( QScrollBar::SliderPageStepAdd );
    } );

    registerShortcut( ShortcutAction::LogViewJumpToTop,
                      [ this ]() { selectAndDisplayLine( 0_lnum ); } );
    registerShortcut( ShortcutAction::LogViewJumpToBottom, [ this ]() {
        const bool wasAtBottom = scrollPosition_ == bottomScrollPosition();
        if ( !wasAtBottom ) {
            selectAndDisplayLine( maxDisplayLineNumber() - 1_lcount );
            jumpToBottom();
        }
        else {
            Q_EMIT followModeChanged( true );
            followElasticHook_.hook( true );
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

    registerShortcut( ShortcutAction::LogViewJumpToLineNumber, [ this ]() {
        const auto newLine = qMax( 0ull, digitsBuffer_.content() - 1ull );
        trySelectLine( LineNumber( newLine ) );
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

    registerShortcut( ShortcutAction::LogViewSelectLinesUp, [ this ]() {
        auto newPosition = selectionCurrentEndPos_;
        if ( newPosition.line() == 0_lnum ) {
            // Reached the begin
            return;
        }
        newPosition = FilePosition( selectionCurrentEndPos_.line() - 1_lcount,
                                    selectionCurrentEndPos_.column() );
        selectAndDisplayRange( newPosition );
    } );

    registerShortcut( ShortcutAction::LogViewSelectLinesDown, [ this ]() {
        auto newPosition = selectionCurrentEndPos_;
        if ( newPosition.line() >= maxDisplayLineNumber() - 1_lcount ) {
            // Reached the end
            return;
        }
        newPosition = FilePosition( selectionCurrentEndPos_.line() + 1_lcount,
                                    selectionCurrentEndPos_.column() );
        selectAndDisplayRange( newPosition );
    } );
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
                stepVisualLines( -1 );
                break;
            case Qt::Key_Down:
                stepVisualLines( 1 );
                break;
            case Qt::Key_PageUp:
                stepVisualLines( -visualLinesPerPage() );
                break;
            case Qt::Key_PageDown:
                stepVisualLines( visualLinesPerPage() );
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

    int yDelta = 0;
    const auto pixelDelta = wheelEvent->pixelDelta();

    if ( pixelDelta.isNull() ) {
        yDelta = static_cast<int>(
            std::floor( static_cast<float>( wheelEvent->angleDelta().y() ) / 0.7f ) );
    }
    else {
        yDelta = pixelDelta.y();
    }

    if ( yDelta == 0 ) {
        QAbstractScrollArea::wheelEvent( wheelEvent );
        return;
    }

    if ( wheelEvent->modifiers().testFlag( Qt::ControlModifier ) ) {
        Q_EMIT changeFontSize( yDelta > 0 );
        return;
    }

    // Fast scroll: multiply scroll delta when Alt (Option on macOS) is held
    const bool isFastScroll = wheelEvent->modifiers().testFlag( Qt::AltModifier )
                              && Configuration::get().fastScrollEnabled();
    if ( isFastScroll ) {
        yDelta *= Configuration::get().fastScrollMultiplier();
    }

    // LOG_DEBUG << "wheelEvent";

    // This is to handle the case where follow mode is on, but the user
    // has moved using the scroll bar. We take them back to the bottom.
    if ( followMode_ )
        jumpToBottom();

    const auto allowFollowOnScroll = Configuration::get().allowFollowOnScroll();
    if ( scrollPosition_ == bottomScrollPosition() ) {
        if ( allowFollowOnScroll || yDelta > 0 ) {
            // First see if we need to block the elastic (on Mac)
            if ( wheelEvent->phase() == Qt::ScrollBegin ) {
                followElasticHook_.hold();
            }
            else if ( wheelEvent->phase() == Qt::ScrollEnd
                      || wheelEvent->phase() == Qt::ScrollMomentum ) {
                followElasticHook_.release();
            }

            followElasticHook_.move( -yDelta );
        }

        // LOG_DEBUG << "Elastic " << y_delta;
    }

    // LOG_DEBUG << "Length = " << followElasticHook_.size();
    if ( !allowFollowOnScroll
         || ( followElasticHook_.size() == 0 && !followElasticHook_.isHooked() ) ) {
        if ( isFastScroll ) {
            // Apply multiplied delta directly since the original event has the unmultiplied value
            scrollByVisualLines( -yDelta );
        }
        else if ( std::abs( wheelEvent->angleDelta().x() )
                  > std::abs( wheelEvent->angleDelta().y() ) ) {
            // Mostly sideways: the scroll area scrolls horizontally.
            QAbstractScrollArea::wheelEvent( wheelEvent );
        }
        else {
            scrollByVisualLines( wheelVisualLines( *wheelEvent ) );
        }
    }
}

int64_t AbstractLogView::wheelVisualLines( const QWheelEvent& wheelEvent )
{
    // What QScrollBar makes of a wheel turn, with Visual Lines for its steps:
    // wheelScrollLines() per notch, a fraction of a step carried over to the
    // next event, and never more than a page at once.
    const auto page = visualLinesPerPage();
    const auto notches = static_cast<double>( wheelEvent.angleDelta().y() )
                         / static_cast<double>( QWheelEvent::DefaultDeltasPerStep );

    int64_t visualLinesUp = 0;
    if ( wheelEvent.modifiers().testFlag( Qt::ShiftModifier ) ) {
        wheelVisualLinesPending_ = 0;
        visualLinesUp = static_cast<int64_t>( notches * static_cast<double>( page ) );
    }
    else {
        const auto turned = QApplication::wheelScrollLines() * notches;
        if ( wheelVisualLinesPending_ != 0 && turned / wheelVisualLinesPending_ < 0 ) {
            // The wheel changed direction.
            wheelVisualLinesPending_ = 0;
        }
        wheelVisualLinesPending_ += turned;
        visualLinesUp = static_cast<int64_t>( wheelVisualLinesPending_ );
        wheelVisualLinesPending_ -= static_cast<double>( visualLinesUp );
    }

    // Turning the wheel away (a positive delta) moves up the Log File.
    return -std::clamp( visualLinesUp, -page, page );
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

int AbstractLogView::lineNumberToVerticalScroll( LineNumber line ) const
{
    // Clamp the result to the representable range of int before casting.
    // line.get() can be a sentinel value (e.g., max LineNumber::UnderlyingType for an
    // invalid/unset line), and the floating-point product can exceed INT_MAX,
    // making the conversion undefined behavior. See UBSan finding.
    const double value
        = std::round( static_cast<double>( line.get() ) * verticalScrollMultiplicator() );
    constexpr double IntMax = static_cast<double>( std::numeric_limits<int>::max() );
    constexpr double IntMin = static_cast<double>( std::numeric_limits<int>::min() );
    const double clamped = std::clamp( value, IntMin, IntMax );
    return static_cast<int>( clamped );
}

LineNumber AbstractLogView::verticalScrollToLineNumber( int scrollPosition ) const
{
    return LineNumber( static_cast<LineNumber::UnderlyingType>(
        std::round( static_cast<double>( scrollPosition ) / verticalScrollMultiplicator() ) ) );
}

double AbstractLogView::verticalScrollMultiplicator() const
{
    return verticalScrollBar()->maximum() < std::numeric_limits<int>::max()
               ? 1.0
               : static_cast<double>( std::numeric_limits<int>::max() )
                     / static_cast<double>( logData_->getNbLine().get() );
}

void AbstractLogView::scrollContentsBy( int dx, int dy )
{
    LOG_DEBUG << "scrollContentsBy received " << dy << "position " << verticalScrollBar()->value();

    const auto scrollBarValue = verticalScrollBar()->value();

    if ( scrollBarValue == lineNumberToVerticalScroll( scrollPosition_.lineNumber ) ) {
        // The scrollbar only caught up with the Log Line the view moved to
        // by itself, so the Scroll Position stands, Visual Line and all.
    }
    else if ( scrollBarValue == verticalScrollBar()->maximum() ) {
        // The scrollbar was moved to its maximum. That is the bottom Scroll
        // Position, the one place the scrollbar does not land on the first
        // Visual Line of a Log Line.
        scrollPosition_ = bottomScrollPosition();
    }
    else {
        // The scrollbar was moved. It counts whole Log Lines, so the view
        // lands on the first Visual Line of the Log Line it maps to, brought
        // back into the range the Log File actually has: scrolling is where
        // the Scroll Position gets clamped, painting never moves it.
        const auto scrollBarLine = verticalScrollToLineNumber( scrollBarValue );
        scrollPosition_ = viewportGeometry().clampScrollPosition(
            ScrollPosition{ scrollBarLine, 0 }, logData_->getNbLine() );
    }
    updateAtBottom();

    firstCol_ = ( firstCol_.get() - dx ) >= 0 ? LineColumn{ firstCol_.get() - dx } : 0_lcol;

    scrollPositionMoved();
}

void AbstractLogView::updateAtBottom()
{
    atBottom_ = logFileBottom().alignsLastVisualLineAt( scrollPosition_ );
}

void AbstractLogView::scrollPositionMoved()
{
    // Update the overview if we have one
    if ( overview_ != nullptr ) {
        const auto lastLine = scrollPosition_.lineNumber + getNbVisibleLines();
        overview_->updateCurrentPosition( scrollPosition_.lineNumber, lastLine );
    }

    // Are we hovering over a new line?
    const auto mousePos = mapFromGlobal( QCursor::pos( activeScreen( this ) ) );
    considerMouseHovering( mousePos.x(), mousePos.y() );

    // Redraw
    update();
}

void AbstractLogView::scrollTo( ScrollPosition position )
{
    scrollPosition_
        = std::min( viewportGeometry().clampScrollPosition( position, logData_->getNbLine() ),
                    bottomScrollPosition() );

    const auto scrollBarValue = lineNumberToVerticalScroll( scrollPosition_.lineNumber );
    if ( verticalScrollBar()->value() != scrollBarValue ) {
        // scrollContentsBy() follows, and keeps this Scroll Position.
        verticalScrollBar()->setValue( scrollBarValue );
    }
    else {
        updateAtBottom();
        scrollPositionMoved();
    }
}

void AbstractLogView::scrollByVisualLines( int64_t visualLines )
{
    scrollTo(
        moveScrollPosition( scrollPosition_, visualLines, bottomScrollPosition(),
                            [ this ]( LineNumber line ) { return visualLineCount( line ); } ) );
}

void AbstractLogView::stepVisualLines( int64_t visualLines )
{
    if ( visualLines < 0 && followMode_ ) {
        disableFollow();
    }
    scrollByVisualLines( visualLines );
}

int64_t AbstractLogView::visualLinesPerPage() const
{
    return static_cast<int64_t>( viewportGeometry().visualLinesPerPage().get() );
}

const LogFileBottom& AbstractLogView::logFileBottom() const
{
    const LogFileBottomKey key{ logData_->getNbLine(), viewport()->width(),
                                viewport()->height(),  charWidth_,
                                charHeight_,           useTextWrap_,
                                lineNumbersVisible_ };

    if ( !logFileBottom_.has_value() || !( logFileBottomKey_ == key ) ) {
        logFileBottomKey_ = key;
        // Not viewportGeometry(): its drawing offset depends on the bottom.
        const ViewportLayout layout{ viewportInput() };
        const auto columns = layout.visibleColumns();
        logFileBottom_
            = layout.logFileBottom( key.totalLines, [ this, columns ]( LineNumber line ) {
                  return visualLineCount( line, columns );
              } );
    }

    return *logFileBottom_;
}

ScrollPosition AbstractLogView::bottomScrollPosition() const
{
    return logFileBottom().scrollPosition;
}

size_t AbstractLogView::visualLineCount( LineNumber line ) const
{
    return visualLineCount( line, getNbVisibleCols() );
}

size_t AbstractLogView::visualLineCount( LineNumber line, LineLength columns ) const
{
    if ( !useTextWrap_ ) {
        return 1;
    }

    return wrapLogLine( logData_->getLineString( line ), columns ).wrappedLinesCount();
}

WrappedString AbstractLogView::wrapLogLine( QString text, LineLength columns ) const
{
    auto expandedText = untabify( std::move( text ) );
    const auto wrapColumns
        = useTextWrap_ ? columns : LineLength{ logsquirl::isize( expandedText ) } + 1_length;
    WrappedString visualLines{ std::move( expandedText ), wrapColumns };
    // What finds a Scroll Position's last Visual Line relies on it.
    assert( visualLines.wrappedLinesCount() > 0 );
    return visualLines;
}

ScrollPosition AbstractLogView::withinLogLine( ScrollPosition position ) const
{
    if ( position.visualLineIndex > 0 && position.lineNumber < logData_->getNbLine() ) {
        position.visualLineIndex
            = std::min( position.visualLineIndex, visualLineCount( position.lineNumber ) - 1 );
    }
    return position;
}

void AbstractLogView::rewrapScrollPosition()
{
    const auto columns = getNbVisibleCols();
    const auto before = scrollPosition_;
    if ( useTextWrap_ && columns != scrollPositionColumns_ && scrollPosition_.visualLineIndex > 0
         && scrollPosition_.lineNumber < logData_->getNbLine() ) {
        // Wrapped at the width the Visual Line was counted at and at the new one.
        const auto text = logData_->getLineString( scrollPosition_.lineNumber );
        const auto countedAt = wrapLogLine( text, scrollPositionColumns_ );
        const auto firstOnTopRow = countedAt.wrappedLineStart(
            std::min( scrollPosition_.visualLineIndex, countedAt.wrappedLinesCount() - 1 ) );
        scrollPosition_.visualLineIndex
            = wrapLogLine( text, columns ).wrappedLineIndexOf( firstOnTopRow );
    }
    else {
        // The same width, but the Log Line at the top may now wrap into fewer
        // Visual Lines.
        scrollPosition_ = withinLogLine( scrollPosition_ );
    }
    scrollPositionColumns_ = columns;
    // Only a move changes what is aligned: Log Lines appended below a view
    // that is not following leave it as it is.
    if ( scrollPosition_ != before ) {
        updateAtBottom();
    }
}

ScrollPosition AbstractLogView::visualLineOf( FilePosition position ) const
{
    if ( !useTextWrap_ || position.column() <= 0_lcol
         || position.line() >= logData_->getNbLine() ) {
        return ScrollPosition{ position.line(), 0 };
    }

    const auto visualLines
        = wrapLogLine( logData_->getLineString( position.line() ), getNbVisibleCols() );
    return ScrollPosition{ position.line(), visualLines.wrappedLineIndexOf( position.column() ) };
}

void AbstractLogView::paintEvent( QPaintEvent* paintEvent )
{
    const QRect invalidRect = paintEvent->rect();
    if ( ( invalidRect.isEmpty() ) || ( logData_ == nullptr ) )
        return;

    LOG_DEBUG << "paintEvent received, scrollPosition_=" << scrollPosition_.lineNumber << ":"
              << scrollPosition_.visualLineIndex << " atBottom_=" << atBottom_
              << " rect: " << invalidRect.topLeft().x() << ", " << invalidRect.topLeft().y() << ", "
              << invalidRect.bottomRight().x() << ", " << invalidRect.bottomRight().y();

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
    if ( textAreaCache_.invalid_ || ( textAreaCache_.first_column_ != firstCol_ )
         || ( textAreaCache_.scroll_position_ != scrollPosition_ ) ) {
        // Full redraw
        drawTextArea( &textAreaCache_.pixmap_ );

        textAreaCache_.invalid_ = false;
        textAreaCache_.scroll_position_ = scrollPosition_;
        textAreaCache_.first_column_ = firstCol_;

        LOG_DEBUG << "End of writing "
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now() - start )
                         .count();
    }
    else {
        // Use the cache as is: nothing to do!
    }

    // The same geometry hit testing places the text by.
    const auto pullToFollow = viewportGeometry().pullToFollowGeometry( pullToFollowState() );

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

// These two functions are virtual and this implementation is clearly
// only valid for a non-filtered display.
// We count on the 'filtered' derived classes to override them.
LineNumber AbstractLogView::displayLineNumber( LineNumber lineNumber ) const
{
    return lineNumber + 1_lcount; // show a 1-based index
}

LineNumber AbstractLogView::lineIndex( LineNumber lineNumber ) const
{
    return lineNumber;
}

LineNumber AbstractLogView::maxDisplayLineNumber() const
{
    return LineNumber( logData_->getNbLine().get() );
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

LineNumber AbstractLogView::getViewPosition() const
{
    LineNumber line;

    const auto selectedLine = selection_.selectedLine();
    if ( selectedLine.has_value() ) {
        line = *selectedLine;
    }
    else {
        // Middle of the view
        line = scrollPosition_.lineNumber + LinesCount( getNbVisibleLines().get() / 2 );
    }

    return line;
}

void AbstractLogView::searchUsingFunction( QuickFindSearchFn searchFunction )
{
    disableFollow();
    ( quickFind_->*searchFunction )( toLogLines( selection_ ), quickFindPattern_->getMatcher() );
}

QuickFindLines AbstractLogView::quickFindLines() const
{
    return QuickFindLines::everyLogLine( *logData_ );
}

Selection AbstractLogView::toLogLines( const Selection& selection ) const
{
    return selection.mapLines( [ this ]( LineNumber line ) { return logLineAt( line ); } );
}

Selection AbstractLogView::toViewLines( const Selection& selection ) const
{
    return selection.mapLines( [ this ]( LineNumber logLine ) { return lineIndex( logLine ); } );
}

bool AbstractLogView::displaysLogLine( LineNumber logLine ) const
{
    // lineIndex() gives the nearest displayed line for one that isn't.
    return logLineAt( lineIndex( logLine ) ) == logLine;
}

LineNumber AbstractLogView::logLineAt( LineNumber viewLine ) const
{
    // displayLineNumber() is the 1-based number shown in the margin.
    return displayLineNumber( viewLine ) - 1_lcount;
}

void AbstractLogView::setQuickFindResult( bool hasMatch, const Portion& logLinePortion )
{
    // QuickFind reports a Log Line; this view may number it differently.
    const auto portion = logLinePortion.isValid()
                             ? Portion{ lineIndex( logLinePortion.line() ),
                                        logLinePortion.startColumn(), logLinePortion.endColumn() }
                             : logLinePortion;
    if ( portion.isValid() ) {
        LOG_DEBUG << "search " << portion.line();
        // The Visual Line holding the start of the found text.
        displayPosition( FilePosition{ portion.line(), portion.startColumn() } );
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
    selection_ = toViewLines( quickFind_->incrementalSearchAbort() );
    Q_EMIT changeQuickFind( "", QuickFindMux::Forward );
}

void AbstractLogView::incrementalSearchStop()
{
    auto oldSelection = toViewLines( quickFind_->incrementalSearchStop() );
    if ( selection_.isEmpty() ) {
        selection_ = oldSelection;
    }
}

void AbstractLogView::allowFollowMode( bool allow )
{
    followElasticHook_.allowHook( allow );
}

void AbstractLogView::setSearchPattern( const RegularExpressionPattern& pattern )
{
    searchPattern_ = pattern;
    forceRefresh();
}

void AbstractLogView::setQuickHighlighters(
    const std::vector<QuickHighlighters>& quickHighlighters )
{
    quickHighlighters_ = quickHighlighters;
    forceRefresh();
}

void AbstractLogView::followSet( bool checked )
{
    followMode_ = checked;
    followElasticHook_.hook( checked );
    forceRefresh();

    if ( checked )
        jumpToBottom();
}

void AbstractLogView::textWrapSet( bool checked )
{
    useTextWrap_ = checked;
    // The Log Line at the top stays. Without text wrapping it is a single
    // Visual Line, and with it the view starts again at its first one.
    scrollPosition_.visualLineIndex = 0;
    rewrapScrollPosition();
    updateScrollBars();
    forceRefresh();
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
    forceRefresh();
}

// OR the current selection with the current search expression
void AbstractLogView::addToSearch()
{
    if ( selection_.isPortion() ) {
        LOG_DEBUG << "AbstractLogView::addToSearch()";
        Q_EMIT addToSearch( selection_.getSelectedText( logData_ ) );
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
        Q_EMIT replaceSearch( selection_.getSelectedText( logData_ ) );
    }
    else {
        LOG_ERROR << "AbstractLogView::replaceSearch called for a wrong type of selection";
    }
}

void AbstractLogView::excludeFromSearch()
{
    if ( selection_.isPortion() ) {
        LOG_DEBUG << "AbstractLogView::excludeFromSearch()";
        Q_EMIT excludeFromSearch( selection_.getSelectedText( logData_ ) );
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
        Q_EMIT changeQuickFind( selection_.getSelectedText( logData_ ), QuickFindMux::Forward );
        Q_EMIT searchNext();
    }
}

// Find next previous of the selected text (#)
void AbstractLogView::findPreviousSelected()
{
    if ( selection_.isPortion() ) {
        Q_EMIT changeQuickFind( selection_.getSelectedText( logData_ ), QuickFindMux::Backward );
        Q_EMIT searchNext();
    }
}

// Copy the selection to the clipboard
void AbstractLogView::copy()
{

    try {
        auto text = selection_.getSelectedText( logData_ );
        text.replace( QChar::Null, QChar::Space );
        sendTextToClipboard( text );
    } catch ( std::exception& err ) {
        LOG_ERROR << "failed to copy data to clipboard " << err.what();
    }
}

// Copy the selection with line numbers to the clipboard
void AbstractLogView::copyWithLineNumbers()
{
    try {
        auto text = selection_.getSelectedText( logData_, true );
        text.replace( QChar::Null, QChar::Space );
        sendTextToClipboard( text );
    } catch ( std::exception& err ) {
        LOG_ERROR << "failed to copy data to clipboard " << err.what();
    }
}

void AbstractLogView::markSelected()
{
    auto lines = selection_.getLines();
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
    const auto selectedLines = selection_.getLines();
    if ( selectedLines.empty() ) {
        return;
    }

    const auto start = selectedLines.front();
    const auto lastLine = selectedLines.back();
    const auto end = lastLine + 1_lcount;
    saveLinesToFile( start, end );
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
    QSaveFile saveFile{ filename };
    if ( !saveFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        LOG_ERROR << "Failed to open file to save";
        return;
    }

    // The lines are read, encoded and written off the UI thread, while the UI
    // thread runs the progress dialog: the save's progress and its end reach
    // the dialog as signals on the UI thread, and leaving the dialog any other
    // way (Cancel, Escape) interrupts the save. The dialog is application
    // modal, so the user can't change the view while the save runs, and the
    // lines are read through a copy of what the view displays now.
    AtomicFlag interruptRequest;
    LinesSaver linesSaver;

    QProgressDialog progressDialog( this );
    progressDialog.setLabelText( tr( "Saving content to %1" ).arg( filename ) );
    progressDialog.setRange( 0, 1000 );
    progressDialog.setWindowModality( Qt::ApplicationModal );

    connect( &linesSaver, &LinesSaver::progressed, &progressDialog, &QProgressDialog::setValue );
    connect( &linesSaver, &LinesSaver::finished, &progressDialog,
             [ &progressDialog ]() { progressDialog.done( QDialog::Accepted ); } );

    linesSaver.save( linesToSave(), begin, end, logData_->getDisplayEncoding(), &saveFile,
                     interruptRequest );

    if ( progressDialog.exec() != QDialog::Accepted ) {
        interruptRequest.set();
    }

    if ( linesSaver.waitForResult() && !saveFile.commit() ) {
        LOG_ERROR << "Failed to replace the saved file: " << saveFile.errorString();
    }
}

DisplayedLinesReader AbstractLogView::linesToSave() const
{
    return [ logFile = logData_ ]( LineNumber first, LinesCount count ) {
        return logFile->getLines( first, count );
    };
}

void AbstractLogView::updateSearchLimits()
{
    forceRefresh();

    Q_EMIT changeSearchLimits( searchStart_, searchEnd_ );
}

void AbstractLogView::setSearchStart()
{
    const auto selectedLine = selection_.selectedLine();
    searchStart_
        = selectedLine.has_value() ? displayLineNumber( *selectedLine ) - 1_lcount : 0_lnum;
    updateSearchLimits();
}

void AbstractLogView::setSearchEnd()
{
    const auto selectedLine = selection_.selectedLine();
    searchEnd_ = selectedLine.has_value() ? displayLineNumber( *selectedLine )
                                          : LineNumber( logData_->getNbLine().get() );
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

        forceRefresh();
    }
}

//
// Public functions
//

void AbstractLogView::updateData()
{
    LOG_DEBUG << "AbstractLogView::updateData";

    const auto lastLineNumber = LineNumber( logData_->getNbLine().get() );

    // Check the top Line is within range
    if ( scrollPosition_.lineNumber >= lastLineNumber ) {
        scrollPosition_ = ScrollPosition{};
        firstCol_ = 0_lcol;
        verticalScrollBar()->setValue( 0 );
        horizontalScrollBar()->setValue( 0 );
    }
    // The Log Line at the top may now wrap into fewer Visual Lines, and more
    // Log Lines can take columns from the text for their line numbers.
    rewrapScrollPosition();

    // Crop selection if it become out of range
    selection_.crop( lastLineNumber - 1_lcount );

    // Adapt the scroll bars to the new content
    updateScrollBars();

    // Reset the QuickFind in case we have new stuff to search into
    quickFind_->resetLimits();

    if ( followMode_ )
        jumpToBottom();

    // Update the overview if we have one
    if ( overview_ != nullptr ) {
        // Calculate the index of the last line shown
        const LineNumber lastLine
            = qMin( lastLineNumber, scrollPosition_.lineNumber + getNbVisibleLines() );
        overview_->updateCurrentPosition( scrollPosition_.lineNumber, lastLine );
    }

    forceRefresh();
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
    // Whether the view is at the bottom, taken from the bottom as it was
    // before the new size, font or margins move it.
    const bool atBottom
        = logFileBottom_.has_value() && logFileBottom_->alignsLastVisualLineAt( scrollPosition_ );

    // Font is assumed to be mono-space (is restricted by options dialog)
    charHeight_ = std::max( pixmapFontMetrics_.height(), 1 );
    charWidth_ = std::max( textWidth( pixmapFontMetrics_, QString( "m" ) ), 1 );

    // A new width re-wraps the Log Line at the top: keep the character that
    // was first on the top row there.
    rewrapScrollPosition();

    // Update the scroll bars
    updateScrollBars();
    verticalScrollBar()->setPageStep( static_cast<int>( getNbVisibleLines().get() ) );

    if ( followMode_ || atBottom )
        jumpToBottom();

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
    return scrollPosition_.lineNumber;
}

ScrollPosition AbstractLogView::scrollPosition() const
{
    return scrollPosition_;
}

QString AbstractLogView::getSelectedText() const
{
    return selection_.getSelectedText( logData_ );
}

bool AbstractLogView::isPartialSelection() const
{
    return selection_.isPortion();
}

void AbstractLogView::selectAll()
{
    selection_.selectRange( 0_lnum, LineNumber( logData_->getNbLine().get() ) - 1_lcount );
    forceRefresh();
}

void AbstractLogView::trySelectLine( LineNumber lineToSelect )
{
    if ( lineToSelect >= logData_->getNbLine() ) {
        lineToSelect = lineToSelect - 1_lcount;
    }

    selectAndDisplayLine( lineToSelect );
}

void AbstractLogView::selectAndDisplayLine( LineNumber line )
{
    disableFollow();
    selection_.selectLine( line );
    selectionStartPos_ = FilePosition{ line, 0_lcol };
    selectionCurrentEndPos_ = selectionStartPos_;
    displayLine( line );
    Q_EMIT newSelection( line, 1_lcount, 0_lcol, 0_length );
}

void AbstractLogView::selectPortionAndDisplayLine( LineNumber line, LinesCount nLines,
                                                   LineColumn startCol, LineLength nSymbols )
{
    disableFollow();
    selection_.selectLine( line );
    selectionStartPos_ = FilePosition{ line, startCol };
    selectionCurrentEndPos_ = FilePosition{ line, startCol + nSymbols };
    displayLine( line );
    Q_EMIT newSelection( line, nLines, startCol, nSymbols );
}

// The difference between this function and displayLine() is quite
// subtle: this one always jump, even if the line passed is visible.
void AbstractLogView::jumpToLine( LineNumber line )
{
    // Put the selected line in the middle if possible
    const auto newTopLine = line - LinesCount( getNbVisibleLines().get() / 2 );
    scrollTo( ScrollPosition{ newTopLine, 0 } );
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

void AbstractLogView::forceRefresh()
{
    // Invalidate our caches
    textAreaCache_.invalid_ = true;
    ++viewportGeneration_;
    update();
}

void AbstractLogView::setSearchLimits( LineNumber startLine, LineNumber endLine )
{
    searchStart_ = startLine;
    searchEnd_ = endLine;

    forceRefresh();
}

//
// Private functions
//

// The viewport layout without the Visual Lines. Everything it answers -- margins,
// visible counts, scroll ranges -- is pure arithmetic over the widget's own
// geometry, so this is cheap enough to build on every call.
ViewportLayout AbstractLogView::viewportGeometry() const
{
    auto input = viewportInput();
    // The pull-to-follow geometry does not read the offset, so the layout
    // built so far can already say what the offset is.
    input.drawingTopOffsetPx
        = ViewportLayout{ input }.pullToFollowGeometry( pullToFollowState() ).textTopPx;
    return ViewportLayout{ input };
}

ViewportLayoutInput AbstractLogView::viewportInput() const
{
    ViewportLayoutInput input;
    input.charWidthPx = charWidth_;
    input.charHeightPx = charHeight_;
    input.viewportWidthPx = viewport()->width();
    input.viewportHeightPx = viewport()->height();
    input.scrollPosition = scrollPosition_;
    input.firstColumn = firstCol_;
    input.lineNumbersVisible = lineNumbersVisible_;
    input.largestDisplayLineNumber = maxDisplayLineNumber().get();
    input.textWrap = useTextWrap_;
    return input;
}

PullToFollowState AbstractLogView::pullToFollowState() const
{
    return PullToFollowState{ .elasticHookLength = followElasticHook_.size(),
                              .hooked = followElasticHook_.isHooked(),
                              .atBottom = atBottom_,
                              .bottomVisualLines = logFileBottom().visualLines };
}

// The viewport layout including the Visual Lines in the Viewport. They come from the
// Log File, never from a paint, so a click or a hover before the first paint
// resolves correctly.
ViewportLayout AbstractLogView::viewportLayout() const
{
    return ViewportLayout{ viewportGeometry().input(), viewportContent().visualLines };
}

const AbstractLogView::ViewportContent& AbstractLogView::viewportContent() const
{
    const ViewportContentKey key{ scrollPosition_,       firstCol_,
                                  logData_->getNbLine(), viewport()->width(),
                                  viewport()->height(),  charWidth_,
                                  charHeight_,           useTextWrap_,
                                  lineNumbersVisible_,   viewportGeneration_ };

    if ( !viewportContent_.has_value() || !( viewportContentKey_ == key ) ) {
        viewportContentKey_ = key;
        viewportContent_ = buildViewportContent();
    }

    return *viewportContent_;
}

AbstractLogView::ViewportContent AbstractLogView::buildViewportContent() const
{
    ViewportContent content;

    const auto geometry = viewportGeometry();
    const auto linesInFile = logData_->getNbLine();
    if ( linesInFile.get() == 0 ) {
        return content;
    }

    const auto scrollPosition = geometry.clampScrollPosition( scrollPosition_, linesInFile );
    // Every Log Line is at least one Visual Line, so this many Log Lines
    // always fill the Viewport.
    const auto nbLines = qMin( geometry.visibleLines(),
                               linesInFile - LinesCount( scrollPosition.lineNumber.get() ) );
    const auto visibleColumns = geometry.visibleColumns();
    // The Visual Lines from the Scroll Position down to the bottom of the
    // Viewport, and no more, however many a Log Line wraps into.
    const auto maxVisualLines = static_cast<size_t>( geometry.visibleLines().get() );

    auto rawLines = logData_->getLines( scrollPosition.lineNumber, nbLines );
    content.logLines.reserve( rawLines.size() );
    content.visualLines.reserve( maxVisualLines );

    for ( size_t index = 0; index < rawLines.size() && content.visualLines.size() < maxVisualLines;
          ++index ) {
        auto wrappedLine = wrapLogLine( rawLines[ index ], visibleColumns );
        const auto lineLength = LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
            wrappedLine.unwrappedLine().size() ) };

        const auto lineNumber = scrollPosition.lineNumber + LinesCount( index );
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
                VisualLine{ lineNumber, wrappedLineIndex, visualLineStart, length, lineLength } );
            visualLineStart += length;
            ++visualLineCount;
        }

        content.logLines.push_back( ViewportLogLine{ lineNumber, std::move( rawLines[ index ] ),
                                                     std::move( wrappedLine ), firstVisualLine,
                                                     visualLineCount } );
    }

    return content;
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

void AbstractLogView::displayLine( LineNumber line )
{
    displayPosition( FilePosition{ line, 0_lcol } );
}

// Makes the widget adjust itself to display the passed position.
// Doing so, it may throw itself a scrollContents event.
void AbstractLogView::displayPosition( FilePosition position )
{
    const auto target = visualLineOf( position );
    if ( viewportLayout().showsWholeVisualLine( target ) ) {
        // Invalidate our cache
        forceRefresh();
    }
    else {
        scrollTo( target );
    }

    const auto portion = selection_.getPortionForLine( position.line() );
    if ( portion.isValid() ) {
        horizontalScrollBar()->setValue( type_safe::narrow_cast<int>(
            portion.endColumn().get() - getNbVisibleCols().get() + 1 ) );
    }
}

// Move the selection up and down by the passed number of lines
void AbstractLogView::moveSelection( LinesCount delta, bool isDeltaNegative )
{
    LOG_DEBUG << "AbstractLogView::moveSelection delta=" << delta;

    auto selection = selection_.getLines();
    LineNumber newLine;

    if ( !selection.empty() ) {
        if ( isDeltaNegative )
            newLine = selection.front() - delta;
        else
            newLine = selection.back() + delta;
    }

    if ( newLine >= logData_->getNbLine() ) {
        newLine = LineNumber( logData_->getNbLine().get() ) - 1_lcount;
    }

    // Select and display the new line
    selection_.selectLine( newLine );
    displayLine( newLine );
    selectionStartPos_ = FilePosition{ newLine, 0_lcol };
    selectionCurrentEndPos_ = selectionStartPos_;
    Q_EMIT newSelection( newLine, selection_.getSelectedLinesCount(), 0_lcol,
                         LineLength{ getSelectedText().size() } );
}

// Make the start of the lines visible
void AbstractLogView::jumpToStartOfLine()
{
    horizontalScrollBar()->setValue( 0 );
}

LineLength AbstractLogView::maxLineLength( const logsquirl::vector<LineNumber>& lines ) const
{
    const auto longestLine = std::max_element(
        lines.cbegin(), lines.cend(), [ this ]( const auto& lhs, const auto& rhs ) {
            const auto lhsLength = logData_->getLineLength( lhs );
            const auto rhsLength = logData_->getLineLength( rhs );
            return lhsLength < rhsLength;
        } );

    return logData_->getLineLength( LineNumber( *longestLine ) );
}

// Make the end of the lines in the selection visible
void AbstractLogView::jumpToEndOfLine()
{
    const auto selection = selection_.getLines();
    horizontalScrollBar()->setValue( type_safe::narrow_cast<int>( maxLineLength( selection ).get()
                                                                  - getNbVisibleCols().get() ) );
}

// Make the end of the lines on the screen visible
void AbstractLogView::jumpToRightOfScreen()
{
    const auto nbVisibleLines = getNbVisibleLines();

    logsquirl::vector<LineNumber::UnderlyingType> visibleLinesNumbers( nbVisibleLines.get() );
    std::iota( visibleLinesNumbers.begin(), visibleLinesNumbers.end(),
               scrollPosition_.lineNumber.get() );

    logsquirl::vector<LineNumber> visibleLines( nbVisibleLines.get() );
    std::transform( visibleLinesNumbers.cbegin(), visibleLinesNumbers.cend(), visibleLines.begin(),
                    []( auto number ) { return LineNumber{ number }; } );
    horizontalScrollBar()->setValue( type_safe::narrow_cast<int>(
        maxLineLength( visibleLines ).get() - getNbVisibleCols().get() ) );
}

// Jump to the first line
void AbstractLogView::jumpToTop()
{
    scrollTo( ScrollPosition{} );
    forceRefresh(); // in case the screen hasn't moved
}

// Jump to the last line
void AbstractLogView::jumpToBottom()
{
    scrollTo( bottomScrollPosition() );

    forceRefresh();
}

// Select the word under the given position
void AbstractLogView::selectWordAtPosition( const FilePosition& pos )
{
    const QString line = logData_->getExpandedLineString( pos.line() );

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
    forceRefresh();
}

// Update the system global (middle click) selection (X11 only)
void AbstractLogView::updateGlobalSelection()
{
    try {
        auto clipboard = QApplication::clipboard();
        // Updating it only for "non-trivial" (range or portion) selections
        if ( !selection_.isSingleLine() )
            clipboard->setText( selection_.getSelectedText( logData_ ), QClipboard::Selection );
    } catch ( std::exception& err ) {
        LOG_ERROR << "failed to copy data to clipboard " << err.what();
    }
}

void AbstractLogView::selectAndDisplayRange( FilePosition pos )
{
    disableFollow();
    selection_.selectRange( selectionStartPos_.line(), pos.line() );
    selectionCurrentEndPos_ = pos;
    displayLine( pos.line() );
    Q_EMIT newSelection( pos.line(), selection_.getSelectedLinesCount(), 0_lcol,
                         LineLength{ getSelectedText().size() } );
}

// Create the pop-up menu
void AbstractLogView::createMenu()
{
    copyAction_ = new QAction( tr( "&Copy" ), this );
    // No text as this action title depends on the type of selection
    connect( copyAction_, &QAction::triggered, this, [ this ]( auto ) { this->copy(); } );

    copyWithLineNumbersAction_ = new QAction( tr( "Copy with line numbers" ), this );
    // No text as this action title depends on the type of selection
    connect( copyWithLineNumbersAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->copyWithLineNumbers(); } );

    markAction_ = new QAction( tr( "&Mark" ), this );
    connect( markAction_, &QAction::triggered, this, [ this ]( auto ) { this->markSelected(); } );

    saveToFileAction_ = new QAction( tr( "Save to file" ), this );
    connect( saveToFileAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->saveToFile(); } );

    saveSelectedToFileAction_ = new QAction( tr( "Save selected to file" ), this );
    connect( saveSelectedToFileAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->saveSelectedToFile(); } );

    // For '#' and '*', shortcuts doesn't seem to work but
    // at least it displays them in the menu, we manually handle those keys
    // as keys event anyway (in keyPressEvent).
    findNextAction_ = new QAction( tr( "Find &next" ), this );
    findNextAction_->setShortcut( Qt::Key_Asterisk );
    findNextAction_->setStatusTip( tr( "Find the next occurrence" ) );
    connect( findNextAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->findNextSelected(); } );

    findPreviousAction_ = new QAction( tr( "Find &previous" ), this );
    findPreviousAction_->setShortcut( tr( "/" ) );
    findPreviousAction_->setStatusTip( tr( "Find the previous occurrence" ) );
    connect( findPreviousAction_, &QAction::triggered,
             [ this ]( auto ) { this->findPreviousSelected(); } );

    replaceSearchAction_ = new QAction( tr( "&Replace search" ), this );
    replaceSearchAction_->setStatusTip( tr( "Replace the search expression with the selection" ) );
    connect( replaceSearchAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->replaceSearch(); } );

    addToSearchAction_ = new QAction( tr( "&Add to search" ), this );
    addToSearchAction_->setStatusTip( tr( "Add the selection to the current search" ) );
    connect( addToSearchAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->addToSearch(); } );

    excludeFromSearchAction_ = new QAction( tr( "&Exclude from search" ), this );
    excludeFromSearchAction_->setStatusTip( tr( "Excludes the selection from search" ) );
    connect( excludeFromSearchAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->excludeFromSearch(); } );

    setSearchStartAction_ = new QAction( tr( "Set search start" ), this );
    connect( setSearchStartAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->setSearchStart(); } );

    setSearchEndAction_ = new QAction( tr( "Set search end" ), this );
    connect( setSearchEndAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->setSearchEnd(); } );

    clearSearchLimitAction_ = new QAction( tr( "Clear search limits" ), this );
    connect( clearSearchLimitAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->clearSearchLimits(); } );

    setSelectionStartAction_ = new QAction( tr( "Set selection start" ), this );
    connect( setSelectionStartAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->setSelectionStart(); } );

    setSelectionEndAction_ = new QAction( tr( "Set selection end" ), this );
    connect( setSelectionEndAction_, &QAction::triggered, this,
             [ this ]( auto ) { this->setSelectionEnd(); } );

    saveDefaultSplitterSizesAction_ = new QAction( tr( "Save splitter position" ), this );
    connect( saveDefaultSplitterSizesAction_, &QAction::triggered, this,
             [ this ]( auto ) { Q_EMIT saveDefaultSplitterSizes(); } );

    sendToScratchpadAction_ = new QAction( tr( "Send to scratchpad" ), this );
    connect( sendToScratchpadAction_, &QAction::triggered, this,
             [ this ]( auto ) { Q_EMIT sendSelectionToScratchpad(); } );

    replaceInScratchpadAction_ = new QAction( tr( "Replace scratchpad" ), this );
    connect( replaceInScratchpadAction_, &QAction::triggered, this,
             [ this ]( auto ) { Q_EMIT replaceScratchpadWithSelection(); } );

    popupMenu_ = new QMenu( this );
    highlightersMenu_ = new HighlightersMenu( tr( "Highlighters" ) );
    popupMenu_->addMenu( highlightersMenu_ );
    colorLabelsMenu_ = popupMenu_->addMenu( tr( "Color labels" ) );

    popupMenu_->addSeparator();
    popupMenu_->addAction( markAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( copyAction_ );
    popupMenu_->addAction( copyWithLineNumbersAction_ );
    popupMenu_->addAction( sendToScratchpadAction_ );
    popupMenu_->addAction( replaceInScratchpadAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( findNextAction_ );
    popupMenu_->addAction( findPreviousAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( replaceSearchAction_ );
    popupMenu_->addAction( addToSearchAction_ );
    popupMenu_->addAction( excludeFromSearchAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( setSearchStartAction_ );
    popupMenu_->addAction( setSearchEndAction_ );
    popupMenu_->addAction( clearSearchLimitAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( setSelectionStartAction_ );
    popupMenu_->addAction( setSelectionEndAction_ );
    popupMenu_->addSeparator();
    popupMenu_->addAction( saveDefaultSplitterSizesAction_ );
    popupMenu_->addAction( saveToFileAction_ );
    popupMenu_->addAction( saveSelectedToFileAction_ );
}

void AbstractLogView::considerMouseHovering( int xPos, int yPos )
{
    const auto line = convertCoordToLine( yPos );
    if ( ( xPos < viewportGeometry().leftMarginPx() ) && ( line.has_value() )
         && ( *line < logData_->getNbLine() ) ) {
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
    // The Log Lines at the end may have changed, not only how many there are,
    // so the bottom is wrapped again: no more Log Lines than the Viewport has
    // rows are read.
    logFileBottom_.reset();
    const auto bottom = logFileBottom();

    // The margin arithmetic the visible column count needs lives in the
    // layout, so this is right even before the first paint.
    const auto layout = viewportGeometry();

    // Lowering the maximum below the scrollbar's value moves the view to the
    // bottom Scroll Position, through scrollContentsBy().
    verticalScrollBar()->setRange( 0, layout.verticalScrollRange( bottom.scrollPosition ) );

    horizontalScrollBar()->setRange( 0, layout.horizontalScrollRange( logData_->getMaxLength() ) );
    horizontalScrollBar()->setPageStep(
        type_safe::narrow_cast<int>( layout.visibleColumns().get() * 7 / 8 ) );

    // The bottom can move up within the Log Line the view stands on, as when
    // the Viewport grows; a Log File growing never moves it up.
    if ( scrollPosition_ > bottom.scrollPosition ) {
        scrollTo( bottom.scrollPosition );
    }
}

void AbstractLogView::drawTextArea( QPaintDevice* paintDevice )
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

    const int paintDeviceHeight
        = static_cast<int>( std::floor( paintDevice->height() / viewport()->devicePixelRatio() ) );
    const int paintDeviceWidth
        = static_cast<int>( std::floor( paintDevice->width() / viewport()->devicePixelRatio() ) );

    const QPalette& palette = viewport()->palette();
    const HighlighterSet& highlighterSet = HighlighterSetCollection::get().currentActiveSet();
    const auto& quickHighlighters = HighlighterSetCollection::get().quickHighlighters();
    QColor foreColor, backColor;

    static const QBrush normalBulletBrush = QBrush( Qt::white );
    static const QBrush matchBulletBrush = QBrush( Qt::red );
    static const QBrush markBrush = QBrush( "dodgerblue" );
    static const QBrush markedMatchBrush = QBrush( "violet" );

    // Layout constants moved to anonymous namespace (see top of file).

    // The layout owns the margin arithmetic; painting only reads it.
    const auto layout = viewportGeometry();

    // The Log Lines to draw, already expanded and wrapped into the same Visual
    // Lines hit testing resolves points against. Painting builds none of its own.
    const auto& content = viewportContent();

    LOG_DEBUG << "drawing " << content.logLines.size() << " Log Lines";
    LOG_DEBUG << "Height: " << paintDeviceHeight;

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

    const auto searchStartIndex = lineIndex( searchStart_ );
    const auto searchEndIndex = [ this ] {
        auto index = lineIndex( searchEnd_ );
        if ( searchEnd_ + 1_lcount != displayLineNumber( index ) ) {
            // in filtered view lineIndex for "past the end" returns last line
            // it should not be marked as excluded
            index = index + 1_lcount;
        }

        return index;
    }();

    const auto highlightPatternMatches = Configuration::get().mainSearchHighlight();
    const auto variateHighlightPatternMatches = Configuration::get().variateMainSearchHighlight();

    std::optional<Highlighter> patternHighlight;
    if ( highlightPatternMatches && !searchPattern_.isBoolean && !searchPattern_.isExclude
         && !searchPattern_.pattern.isEmpty() ) {
        const auto mainSearchBackColor = Configuration::get().mainSearchBackColor();
        patternHighlight = Highlighter{};
        patternHighlight->setHighlightOnlyMatch( true );
        patternHighlight->setVariateColors( variateHighlightPatternMatches );
        patternHighlight->setPattern( searchPattern_.pattern );
        patternHighlight->setIgnoreCase( !searchPattern_.isCaseSensitive );
        patternHighlight->setUseRegex( !searchPattern_.isPlainText );

        patternHighlight->setBackColor( mainSearchBackColor );
        patternHighlight->setForeColor( Qt::black );
    }

    logsquirl::vector<Highlighter> additionalHighlighters;
    for ( auto i = 0u; i < quickHighlighters_.size(); ++i ) {
        const auto quickHighlighterIndex = static_cast<int>( i );
        if ( quickHighlighterIndex >= quickHighlighters.size() ) {
            LOG_WARNING << "Not enough quickHighlighters configured";
            break;
        }

        const auto quickHighlighter = quickHighlighters.at( quickHighlighterIndex );

        std::transform( quickHighlighters_[ i ].begin(), quickHighlighters_[ i ].end(),
                        std::back_inserter( additionalHighlighters ),
                        [ quickHighlighter ]( const QString& word ) {
                            Highlighter h{ word, false, true, quickHighlighter.color.foreColor,
                                           quickHighlighter.color.backColor };
                            h.setUseRegex( false );
                            return h;
                        } );
    }

    // The Line Decorator owns the colour precedence rule (whole-line
    // Highlighter, main search, Color Labels, QuickFind); it is
    // constructed once per repaint with the stable context, not once per
    // line. Every source it matches works against the raw line, so its
    // Decoration is in raw-space too -- the loop below maps that Decoration
    // to display columns once per line rather than once per match.
    // Selection is the one source left out of its context: it comes from
    // mouse/pixel positions against the already-rendered (tab-expanded)
    // text, so it is display-space already and needs no translation.
    const LineDecorator lineDecorator{ LineDecorator::Context{
        highlighterSet,
        patternHighlight,
        additionalHighlighters,
        quickFindPattern_->getMatcher(),
        Configuration::get().qfBackColor(),
        SearchLimits{ searchStartIndex, searchEndIndex - 1_lcount },
    } };

    // A Line Verdict for a line that should show no Highlighter/main-search/
    // Color Label colour -- used for the reversed-selection line below,
    // which already gets a uniform selection background instead. This
    // reuses decorate()'s isOutsideSearchLimits gate for that tier (the
    // line is not actually outside the search limits); QuickFind is
    // unconditional in decorate() regardless, so it still applies.
    const auto quickFindOnlyVerdict = []( AbstractLogData::LineType lineType ) {
        return LineVerdict{ std::nullopt, lineType, /* isOutsideSearchLimits = */ true };
    };

    // Position in pixel of the base line of the line to print
    int yPos = 0;
    logsquirl::vector<std::pair<QColor, QColor>> highlightColors;
    for ( const auto& viewportLogLine : content.logLines ) {
        const auto lineNumber = viewportLogLine.lineNumber;
        const QString& logLine = viewportLogLine.text;

        const int xPos = layout.textOriginX();

        using LineTypeFlags = AbstractLogData::LineTypeFlags;
        const auto currentLineType = lineType( lineNumber );

        LineVerdict verdict;

        if ( selection_.isLineSelected( lineNumber ) && !selection_.isSingleLine() ) {
            // Reverse the selected line. No Highlighter/main-search/Color
            // Label colour is shown over it, same as before.
            foreColor = palette.color( QPalette::HighlightedText );
            backColor = palette.color( QPalette::Highlight );
            painter->setPen( palette.color( QPalette::Text ) );
            verdict = quickFindOnlyVerdict( currentLineType );
        }
        else {
            foreColor = palette.color( QPalette::Text );
            backColor = palette.color( QPalette::Base );

            verdict = lineDecorator.verdictFor( LogLine{ lineNumber, logLine }, currentLineType );

            if ( verdict.isOutsideSearchLimits() ) {
                foreColor = palette.brush( QPalette::Disabled, QPalette::Text ).color();
            }
            else if ( const auto wholeLine = verdict.wholeLineHighlight(); wholeLine.has_value() ) {
                // color applies to whole line
                foreColor = wholeLine->foreColor;
                backColor = wholeLine->backColor;
            }
        }

        // Dim context (breadcrumb) lines
        if ( currentLineType.testFlag( LineTypeFlags::Context ) ) {
            foreColor.setAlpha( 128 );
        }

        // Every colour source the Decorator owns -- Highlighters, main
        // search, Color Labels and QuickFind -- is matched against the raw
        // line in this one call, so the returned spans are all in raw
        // column space.
        auto rawSpans = lineDecorator.decorate( logLine, verdict ).spans();

        if ( !rawSpans.empty() ) {
            // The raw-to-display mapping runs once per line here, instead
            // of re-expanding the prefix from scratch for every match --
            // but only up to the furthest raw column any match actually
            // reaches, so a long line with only a few early matches isn't
            // mapped past where any of them need it.
            int furthestRawColumn = 0;
            for ( const auto& match : rawSpans ) {
                furthestRawColumn
                    = std::max<int>( furthestRawColumn, static_cast<int>( match.startColumn().get()
                                                                          + match.size().get() ) );
            }
            const auto rawToDisplay
                = rawToDisplayColumns( QStringView{ logLine }.left( furthestRawColumn ) );
            std::transform(
                rawSpans.begin(), rawSpans.end(), rawSpans.begin(),
                [ &rawToDisplay ]( const HighlightedMatch& match ) {
                    const auto rawStart = static_cast<size_t>( match.startColumn().get() );
                    const auto rawEnd = rawStart + static_cast<size_t>( match.size().get() );
                    const auto displayStart = rawToDisplay[ rawStart ];
                    const auto displayEnd = rawToDisplay[ rawEnd ];
                    return HighlightedMatch{
                        LineColumn{
                            type_safe::narrow_cast<LineColumn::UnderlyingType>( displayStart ) },
                        LineLength{ type_safe::narrow_cast<LineLength::UnderlyingType>(
                            displayEnd - displayStart ) },
                        match.foreColor(), match.backColor()
                    };
                } );
        }

        HighlightedMatchRanges allHighlights{ std::move( rawSpans ) };

        const auto& wrappedLineView = viewportLogLine.wrapped;
        const QStringView expandedLine = wrappedLineView.unwrappedLine();

        // Is there something selected in the line? Selection columns come
        // from mouse/pixel positions against the rendered (tab-expanded)
        // text, so they are already display-space and need no translation.
        const auto selectionPortion = selection_.getPortionForLine( lineNumber );
        if ( selectionPortion.isValid() ) {
            allHighlights.addMatch( HighlightedMatch{ selectionPortion.startColumn(),
                                                      selectionPortion.size(),
                                                      palette.color( QPalette::HighlightedText ),
                                                      palette.color( QPalette::Highlight ) } );
        }

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
                           backColor );

        LineDrawer lineDrawer( backColor );
        const auto firstVisibleColumn
            = std::clamp( useTextWrap_ ? 0_lcol : firstCol_, 0_lcol,
                          LineColumn{ logsquirl::isize( expandedLine ) } );
        const auto lastVisibleColumn = useTextWrap_ ? LineColumn{ logsquirl::isize( expandedLine ) }
                                                    : firstCol_ + nbVisibleCols;
        allHighlights.clamp( firstVisibleColumn, lastVisibleColumn );

        if ( !allHighlights.empty() && !expandedLine.isEmpty() ) {
            // first part without highlight
            if ( allHighlights.front().startColumn() > firstVisibleColumn ) {
                lineDrawer.addChunk( firstVisibleColumn,
                                     allHighlights.front().startColumn() - 1_length, foreColor,
                                     backColor );
            }

            for ( const auto& match : allHighlights.matches() ) {
                const auto matchStart = match.startColumn();

                // a part between two highlight regions
                if ( !lineDrawer.empty() && matchStart - lineDrawer.endColumn() > 1_length ) {
                    lineDrawer.addChunk( lineDrawer.endColumn() + 1_length, matchStart - 1_length,
                                         foreColor, backColor );
                }

                const auto matchEnd = match.endColumn();
                auto matchLengthInString = match.size();
                if ( matchEnd >= LineColumn{ expandedLine.size() } ) {
                    matchLengthInString = LineLength{ logsquirl::isize( expandedLine )
                                                      - match.startColumn().get() };
                }
                if ( matchLengthInString > 0_length ) {
                    lineDrawer.addChunk( match.startColumn(), matchEnd, match.foreColor(),
                                         match.backColor() );
                }
            }

            // last part without highlight
            const auto lastHighlightColumn = allHighlights.back().endColumn();
            if ( lastHighlightColumn < lastVisibleColumn ) {
                lineDrawer.addChunk( lastHighlightColumn + 1_length, lastVisibleColumn, foreColor,
                                     backColor );
            }
        }
        else {
            if ( useTextWrap_ ) {
                lineDrawer.addChunk( 0_lcol, LineColumn{ expandedLine.size() }, foreColor,
                                     backColor );
            }
            else {
                lineDrawer.addChunk( firstCol_, firstCol_ + nbVisibleCols, foreColor, backColor );
            }
        }
        lineDrawer.draw( painter.get(), xPos, yPos, viewport()->width(), wrappedLineView,
                         firstVisualLine, visualLineCount, ContentMarginWidth );

        if ( ( selection_.isLineSelected( lineNumber ) && selection_.isSingleLine() )
             || selection_.getPortionForLine( lineNumber ).isValid() ) {
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
            const QString& lineNumberStr = lineNumberFormat.arg(
                displayLineNumber( lineNumber ).get(), nbDigitsInLineNumber );
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
    Q_EMIT followModeChanged( false );
    followElasticHook_.hook( false );
}

void AbstractLogView::setColorLabel( QAction* action )
{
    if ( action->data().isValid() ) {
        Q_EMIT addColorLabel( static_cast<size_t>( action->data().toInt() ) );
    }
    else {
        Q_EMIT clearColorLabels();
    }
}
