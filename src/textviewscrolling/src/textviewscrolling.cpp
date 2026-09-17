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

#include "textviewscrolling.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {

// Wheel angle deltas of one notch, as QWheelEvent::DefaultDeltasPerStep.
constexpr int DeltasPerNotch = 120;

} // namespace

int WheelTurn::pixels() const
{
    if ( pixelDeltaX == 0 && pixelDeltaY == 0 ) {
        return static_cast<int>( std::floor( static_cast<float>( angleDeltaY ) / 0.7f ) );
    }
    return pixelDeltaY;
}

WrappedString wrapLogLine( QString text, LineLength columns, bool textWrap )
{
    auto expandedText = untabify( std::move( text ) );
    const auto wrapColumns
        = textWrap ? columns : LineLength{ logsquirl::isize( expandedText ) } + 1_length;
    WrappedString visualLines{ std::move( expandedText ), wrapColumns };
    // What finds a Scroll Position's last Visual Line relies on it.
    assert( visualLines.wrappedLinesCount() > 0 );
    return visualLines;
}

logsquirl::vector<QString> ScrolledText::lineTexts( LineNumber first, LinesCount count ) const
{
    logsquirl::vector<QString> texts;
    texts.reserve( count.get() );
    for ( auto line = first; line < first + count; line = line + 1_lcount ) {
        texts.push_back( lineText( line ) );
    }
    return texts;
}

TextViewScrolling::TextViewScrolling( const ScrolledText& text, bool textWrap )
    : text_( text )
    , textWrap_( textWrap )
{
}

//
// What it answers
//

ViewportLayoutInput TextViewScrolling::layoutInput() const
{
    const auto viewport = text_.viewport();
    ViewportLayoutInput input;
    input.charWidthPx = viewport.charWidthPx;
    input.charHeightPx = viewport.charHeightPx;
    input.viewportWidthPx = viewport.widthPx;
    input.viewportHeightPx = viewport.heightPx;
    input.scrollPosition = position_;
    input.firstColumn = firstColumn_;
    input.lineNumbersVisible = viewport.lineNumbersVisible;
    input.largestDisplayLineNumber = viewport.largestDisplayLineNumber;
    input.textWrap = textWrap_;
    return input;
}

ViewportLayout TextViewScrolling::geometry() const
{
    auto input = layoutInput();
    // The pull-to-follow geometry does not read the offset, so the layout
    // built so far can already say what the offset is.
    input.drawingTopOffsetPx
        = ViewportLayout{ input }.pullToFollowGeometry( pullToFollowState() ).textTopPx;
    return ViewportLayout{ input };
}

PullToFollowState TextViewScrolling::pullToFollowState() const
{
    return PullToFollowState{ .elasticHookLength = elasticHook_.size(),
                              .hooked = elasticHook_.isHooked(),
                              .atBottom = atBottom_,
                              .bottomVisualLines = logFileBottom().visualLines };
}

const LogFileBottom& TextViewScrolling::logFileBottom() const
{
    const auto viewport = text_.viewport();
    const LogFileBottomKey key{ text_.lineCount(),          viewport.widthPx,
                                viewport.heightPx,          viewport.charWidthPx,
                                viewport.charHeightPx,      textWrap_,
                                viewport.lineNumbersVisible };

    if ( !logFileBottom_.has_value() || !( logFileBottomKey_ == key ) ) {
        logFileBottomKey_ = key;
        // Not geometry(): its drawing offset depends on the bottom.
        const ViewportLayout layout{ layoutInput() };
        const auto columns = layout.visibleColumns();
        // Wrapped backwards from the end, no more lines than the rows. After
        // lines were only appended, the lines counted before but the last of
        // them are not read again.
        const auto read = batchedVisualLineCounter( columns, /* downwards */ false,
                                                    layout.viewportRows().get() );
        std::optional<BottomLines> before;
        if ( linesOnlyAppended_ && bottomLines_.has_value() && bottomLines_->columns == columns
             && textWrap_ && bottomLines_->end() <= LineNumber( key.totalLines.get() ) ) {
            before = std::move( bottomLines_ );
        }

        // The lines appended and the last one before them, read together.
        const auto readAppended
            = before.has_value()
                  ? batchedVisualLineCounter( columns, /* downwards */ false,
                                              key.totalLines.get() - before->end().get() + 1 )
                  : read;

        BottomLines counted{ columns, LineNumber( key.totalLines.get() ), {} };
        logFileBottom_ = layout.logFileBottom(
            key.totalLines, [ &read, &readAppended, &before, &counted ]( LineNumber line ) {
                auto count = size_t{ 0 };
                if ( !before.has_value() || line < before->first ) {
                    count = read( line );
                }
                else if ( line + 1_lcount < before->end() ) {
                    count = before->visualLineCounts[ line.get() - before->first.get() ];
                }
                else {
                    count = readAppended( line );
                }
                // Walked backwards, one line after the other.
                counted.first = line;
                counted.visualLineCounts.insert( counted.visualLineCounts.begin(), count );
                return count;
            } );
        bottomLines_ = std::move( counted );
    }

    return *logFileBottom_;
}

std::int64_t TextViewScrolling::visualLinesPerPage() const
{
    return static_cast<std::int64_t>( ViewportLayout{ layoutInput() }.visualLinesPerPage().get() );
}

std::size_t TextViewScrolling::visualLineCount( LineNumber line, LineLength columns ) const
{
    if ( !textWrap_ ) {
        return 1;
    }

    return wrap( text_.lineText( line ), columns ).wrappedLinesCount();
}

VisualLineCounter TextViewScrolling::batchedVisualLineCounter( LineLength columns, bool downwards,
                                                               std::uint64_t mostLines ) const
{
    if ( !textWrap_ ) {
        return []( LineNumber ) { return size_t{ 1 }; };
    }

    // The counts of the last batch read, from its first line on.
    struct Batch {
        LineNumber first{ 0 };
        std::vector<std::size_t> counts;
        std::uint64_t nextSize = 1;
        std::uint64_t linesLeft = 0;
    };
    auto batch = std::make_shared<Batch>();
    batch->linesLeft = std::max<std::uint64_t>( mostLines, 1 );

    return [ this, columns, downwards, batch ]( LineNumber line ) -> std::size_t {
        const auto& cached = *batch;
        if ( line >= cached.first && line.get() - cached.first.get() < cached.counts.size() ) {
            return cached.counts[ line.get() - cached.first.get() ];
        }

        const auto lineCount = text_.lineCount().get();
        if ( line.get() >= lineCount ) {
            return 1;
        }

        const auto size
            = std::max<std::uint64_t>( std::min( batch->nextSize, batch->linesLeft ), 1 );
        batch->nextSize *= 2;
        batch->linesLeft -= std::min( batch->linesLeft, size );

        const auto first
            = downwards ? line.get() : line.get() + 1 - std::min( line.get() + 1, size );
        const auto end = downwards ? std::min( line.get() + size, lineCount ) : line.get() + 1;
        const auto texts = text_.lineTexts( LineNumber( first ), LinesCount( end - first ) );

        batch->first = LineNumber( first );
        batch->counts.clear();
        for ( const auto& text : texts ) {
            batch->counts.push_back( wrap( text, columns ).wrappedLinesCount() );
        }
        const auto index = line.get() - first;
        return index < batch->counts.size() ? batch->counts[ index ] : 1;
    };
}

WrappedString TextViewScrolling::wrap( QString text, LineLength columns ) const
{
    return wrapLogLine( std::move( text ), columns, textWrap_ );
}

ScrollPosition TextViewScrolling::withinLogLine( ScrollPosition position ) const
{
    if ( position.visualLineIndex > 0 && position.lineNumber < text_.lineCount() ) {
        const auto columns = ViewportLayout{ layoutInput() }.visibleColumns();
        position.visualLineIndex = std::min( position.visualLineIndex,
                                             visualLineCount( position.lineNumber, columns ) - 1 );
    }
    return position;
}

ScrollPosition TextViewScrolling::visualLineOf( FilePosition position ) const
{
    if ( !textWrap_ || position.column() <= 0_lcol || position.line() >= text_.lineCount() ) {
        return ScrollPosition{ position.line(), 0 };
    }

    const auto visualLines = wrap( text_.lineText( position.line() ),
                                   ViewportLayout{ layoutInput() }.visibleColumns() );
    return ScrollPosition{ position.line(), visualLines.wrappedLineIndexOf( position.column() ) };
}

//
// What it is handed
//

void TextViewScrolling::setPresentationPolicy( const PresentationPolicy& policy )
{
    presentationPolicy_ = policy;
}

void TextViewScrolling::allowFollow( bool allow )
{
    elasticHook_.allowHook( allow );
}

//
// Moves
//

ScrollAnswer TextViewScrolling::answer( bool scrolled ) const
{
    ScrollAnswer result;
    result.scrolled = scrolled;
    result.position = position_;
    result.scrollBarValue = lineToScrollBar( position_.lineNumber );
    return result;
}

void TextViewScrolling::merge( ScrollAnswer& into, const ScrollAnswer& step )
{
    into.scrolled = into.scrolled || step.scrolled;
    if ( step.scrolled ) {
        into.position = step.position;
        into.scrollBarValue = step.scrollBarValue;
    }
    if ( step.followChange != FollowChange::None ) {
        into.followChange = step.followChange;
    }
    into.redraw = into.redraw || step.redraw;
    into.scrollHorizontally = into.scrollHorizontally || step.scrollHorizontally;
}

ScrollAnswer TextViewScrolling::scrollTo( ScrollPosition position )
{
    const ViewportLayout layout{ layoutInput() };
    position_ = std::min( layout.clampScrollPosition( position, text_.lineCount() ),
                          bottomScrollPosition() );
    updateAtBottom();
    return answer( true );
}

ScrollAnswer TextViewScrolling::scrollByVisualLines( std::int64_t visualLines )
{
    const auto columns = ViewportLayout{ layoutInput() }.visibleColumns();
    // Every line passed is at least one Visual Line, so a move asks about no
    // more lines than it moves Visual Lines.
    const auto mostLines = static_cast<std::uint64_t>( std::abs( visualLines ) );
    return scrollTo( moveScrollPosition(
        position_, visualLines, bottomScrollPosition(),
        batchedVisualLineCounter( columns, /* downwards */ visualLines > 0, mostLines ) ) );
}

ScrollAnswer TextViewScrolling::stepVisualLines( std::int64_t visualLines )
{
    ScrollAnswer result;
    if ( visualLines < 0 && follow_ ) {
        merge( result, leaveFollow() );
    }
    merge( result, scrollByVisualLines( visualLines ) );
    return result;
}

ScrollAnswer TextViewScrolling::stepPage( bool down )
{
    const auto page = visualLinesPerPage();
    return stepVisualLines( down ? page : -page );
}

ScrollAnswer TextViewScrolling::turnWheel( const WheelTurn& turn )
{
    ScrollAnswer result;
    auto yDelta = turn.pixels();

    // Fast scroll: the delta is multiplied while the modifier is held.
    const bool isFastScroll = turn.fastScrollHeld && presentationPolicy_.fastScrollEnabled;
    if ( isFastScroll ) {
        yDelta *= presentationPolicy_.fastScrollMultiplier;
    }

    // Follow is on, but the view may have been moved by the scrollbar: it is
    // taken back to the bottom.
    if ( follow_ ) {
        merge( result, jumpToBottom() );
    }

    const auto allowFollowOnScroll = presentationPolicy_.allowFollowOnScroll;
    if ( position_ == bottomScrollPosition() ) {
        if ( allowFollowOnScroll || yDelta > 0 ) {
            // A finger on the trackpad holds the elastic.
            if ( turn.phase == WheelPhase::Begin ) {
                elasticHook_.hold();
            }
            else if ( turn.phase == WheelPhase::End || turn.phase == WheelPhase::Momentum ) {
                elasticHook_.release();
            }

            // Pulling may engage or leave follow, whose holder can hand it
            // back at once: nothing read before this is relied on after it.
            elasticHook_.move( -yDelta );
        }
    }

    if ( !allowFollowOnScroll || ( elasticHook_.size() == 0 && !elasticHook_.isHooked() ) ) {
        if ( isFastScroll ) {
            // The multiplied delta, as the turn itself carries the plain one.
            merge( result, scrollByVisualLines( -yDelta ) );
        }
        else if ( std::abs( turn.angleDeltaX ) > std::abs( turn.angleDeltaY ) ) {
            // Mostly sideways: the scroll area scrolls horizontally.
            result.scrollHorizontally = true;
        }
        else {
            merge( result, scrollByVisualLines( wheelVisualLines( turn ) ) );
        }
    }

    if ( result.scrolled ) {
        // Where it stands now, whatever a change of follow did meanwhile.
        const auto now = answer( true );
        result.position = now.position;
        result.scrollBarValue = now.scrollBarValue;
    }
    return result;
}

std::int64_t TextViewScrolling::wheelVisualLines( const WheelTurn& turn )
{
    // What QScrollBar makes of a wheel turn, with Visual Lines for its steps:
    // linesPerNotch per notch, a fraction of a step carried over to the next
    // turn, and never more than a page at once.
    const auto page = visualLinesPerPage();
    const auto notches
        = static_cast<double>( turn.angleDeltaY ) / static_cast<double>( DeltasPerNotch );

    std::int64_t visualLinesUp = 0;
    if ( turn.pageHeld ) {
        wheelVisualLinesPending_ = 0;
        visualLinesUp = static_cast<std::int64_t>( notches * static_cast<double>( page ) );
    }
    else {
        const auto turned = turn.linesPerNotch * notches;
        if ( wheelVisualLinesPending_ != 0 && turned / wheelVisualLinesPending_ < 0 ) {
            // The wheel changed direction.
            wheelVisualLinesPending_ = 0;
        }
        wheelVisualLinesPending_ += turned;
        visualLinesUp = static_cast<std::int64_t>( wheelVisualLinesPending_ );
        wheelVisualLinesPending_ -= static_cast<double>( visualLinesUp );
    }

    // Turning the wheel away (a positive delta) moves up the Log File.
    return -std::clamp( visualLinesUp, -page, page );
}

ScrollAnswer TextViewScrolling::jumpToBottom()
{
    auto result = scrollTo( bottomScrollPosition() );
    // Redrawn in case the view has not moved.
    result.redraw = true;
    return result;
}

ScrollAnswer TextViewScrolling::jumpToBottomIfFollowing( bool orAtBottom )
{
    if ( follow_ || orAtBottom ) {
        return jumpToBottom();
    }
    return ScrollAnswer{};
}

ScrollAnswer TextViewScrolling::keepAboveBottom()
{
    // The bottom can move up within the line the view stands on, as when the
    // Viewport grows; lines added never move it up.
    const auto bottom = bottomScrollPosition();
    if ( position_ > bottom ) {
        return scrollTo( bottom );
    }
    return ScrollAnswer{};
}

//
// Follow
//

ScrollAnswer TextViewScrolling::followSet( bool checked )
{
    follow_ = checked;
    elasticHook_.hook( checked );

    ScrollAnswer result;
    result.redraw = true;
    if ( checked ) {
        merge( result, jumpToBottom() );
    }
    return result;
}

ScrollAnswer TextViewScrolling::leaveFollow()
{
    elasticHook_.hook( false );
    ScrollAnswer result;
    result.followChange = FollowChange::Leave;
    return result;
}

ScrollAnswer TextViewScrolling::engageFollow()
{
    elasticHook_.hook( true );
    ScrollAnswer result;
    result.followChange = FollowChange::Engage;
    return result;
}

//
// The scrollbars
//

void TextViewScrolling::scrollBarMoved( int value, int dx )
{
    if ( value == lineToScrollBar( position_.lineNumber ) ) {
        // The scrollbar only caught up with the line the view moved to by
        // itself, so the Scroll Position stands, Visual Line and all.
    }
    else if ( value == scrollBarMaximum_ ) {
        // The scrollbar was moved to its maximum. That is the bottom Scroll
        // Position, the one place the scrollbar does not land on the first
        // Visual Line of a line.
        position_ = bottomScrollPosition();
    }
    else {
        // The scrollbar was moved. It counts whole lines, so the view lands on
        // the first Visual Line of the line it maps to, brought back into the
        // range the Log File actually has: scrolling is where the Scroll
        // Position gets clamped, painting never moves it.
        position_ = ViewportLayout{ layoutInput() }.clampScrollPosition(
            ScrollPosition{ scrollBarToLine( value ), 0 }, text_.lineCount() );
    }
    updateAtBottom();

    firstColumn_
        = ( firstColumn_.get() - dx ) >= 0 ? LineColumn{ firstColumn_.get() - dx } : 0_lcol;
}

ScrollAnswer TextViewScrolling::landAtBottomOnMaximum( int sliderPosition, int value )
{
    // Only the scrollbar's own actions and releasing its thumb come here: a
    // move between Visual Lines sets no value and triggers neither.
    if ( sliderPosition == scrollBarMaximum_ && value == scrollBarMaximum_
         && position_ != bottomScrollPosition() ) {
        return scrollTo( bottomScrollPosition() );
    }
    return ScrollAnswer{};
}

ScrollAnswer TextViewScrolling::scrollBarActionTriggered( bool steppedUp, int sliderPosition,
                                                          int value )
{
    ScrollAnswer result;
    if ( follow_ && steppedUp ) {
        merge( result, leaveFollow() );
    }
    merge( result, landAtBottomOnMaximum( sliderPosition, value ) );
    return result;
}

ScrollAnswer TextViewScrolling::scrollBarReleased( int sliderPosition, int value )
{
    return landAtBottomOnMaximum( sliderPosition, value );
}

ScrollBarRanges TextViewScrolling::updateScrollBarRanges( LineLength maxLineLength )
{
    // No more lines than the Viewport has rows are read.
    logFileBottom_.reset();
    const auto bottom = logFileBottom();
    // What was appended is counted now; a later change is not known to be one.
    linesOnlyAppended_ = false;
    const ViewportLayout layout{ layoutInput() };

    ScrollBarRanges ranges;
    // Lowering the maximum below the scrollbar's value moves the view to the
    // bottom Scroll Position, through scrollBarMoved(). It is known here
    // before the scrollbar says so.
    ranges.verticalMaximum = layout.verticalScrollRange( bottom.scrollPosition );
    scrollBarMaximum_ = ranges.verticalMaximum;
    ranges.horizontalMaximum = layout.horizontalScrollRange( maxLineLength );
    ranges.horizontalPageStep
        = type_safe::narrow_cast<int>( layout.visibleColumns().get() * 7 / 8 );
    return ranges;
}

int TextViewScrolling::lineToScrollBar( LineNumber line ) const
{
    // Clamp the result to the representable range of int before casting.
    // line.get() can be a sentinel value (e.g., max LineNumber::UnderlyingType
    // for an invalid/unset line), and the floating-point product can exceed
    // INT_MAX, making the conversion undefined behavior.
    const double value = std::round( static_cast<double>( line.get() ) * scrollBarMultiplicator() );
    constexpr double IntMax = static_cast<double>( std::numeric_limits<int>::max() );
    constexpr double IntMin = static_cast<double>( std::numeric_limits<int>::min() );
    return static_cast<int>( std::clamp( value, IntMin, IntMax ) );
}

LineNumber TextViewScrolling::scrollBarToLine( int value ) const
{
    return LineNumber( static_cast<LineNumber::UnderlyingType>(
        std::round( static_cast<double>( value ) / scrollBarMultiplicator() ) ) );
}

double TextViewScrolling::scrollBarMultiplicator() const
{
    return scrollBarMaximum_ < std::numeric_limits<int>::max()
               ? 1.0
               : static_cast<double>( std::numeric_limits<int>::max() )
                     / static_cast<double>( text_.lineCount().get() );
}

//
// Changes
//

void TextViewScrolling::linesReread()
{
    bottomLines_.reset();
    linesOnlyAppended_ = false;
}

bool TextViewScrolling::dataChanged( LinesChange change )
{
    if ( change == LinesChange::Appended ) {
        linesOnlyAppended_ = true;
    }
    else {
        linesReread();
    }

    bool backToTop = false;
    if ( position_.lineNumber >= LineNumber( text_.lineCount().get() ) ) {
        position_ = ScrollPosition{};
        firstColumn_ = 0_lcol;
        backToTop = true;
    }
    // The line at the top may now wrap into fewer Visual Lines, and more lines
    // can take columns from the text for their line numbers.
    rewrap();
    return backToTop;
}

bool TextViewScrolling::viewportChanged()
{
    // Taken from the bottom as it was before the new size, font or margins
    // move it.
    const bool wasAtBottom
        = logFileBottom_.has_value() && logFileBottom_->alignsLastVisualLineAt( position_ );
    // A new width re-wraps the line at the top: keep the character that was
    // first on the top row there.
    rewrap();
    return wasAtBottom;
}

void TextViewScrolling::setTextWrap( bool textWrap )
{
    textWrap_ = textWrap;
    // Without text wrapping the line is a single Visual Line, and with it the
    // view starts again at its first one.
    position_.visualLineIndex = 0;
    rewrap();
}

void TextViewScrolling::rewrap()
{
    const auto columns = ViewportLayout{ layoutInput() }.visibleColumns();
    const auto before = position_;
    // No columns counted means none was ever counted: the Visual Line was
    // not reached through a width.
    if ( textWrap_ && columns != positionColumns_ && positionColumns_ > 0_length
         && position_.visualLineIndex > 0 && position_.lineNumber < text_.lineCount() ) {
        // Wrapped at the width the Visual Line was counted at and at the new one.
        const auto text = text_.lineText( position_.lineNumber );
        const auto countedAt = wrap( text, positionColumns_ );
        const auto firstOnTopRow = countedAt.wrappedLineStart(
            std::min( position_.visualLineIndex, countedAt.wrappedLinesCount() - 1 ) );
        position_.visualLineIndex = wrap( text, columns ).wrappedLineIndexOf( firstOnTopRow );
    }
    else {
        // The same width, but the line at the top may now wrap into fewer
        // Visual Lines.
        position_ = withinLogLine( position_ );
    }
    positionColumns_ = columns;
    // Only a move changes what is aligned: lines appended below a view that is
    // not following leave it as it is.
    if ( position_ != before ) {
        updateAtBottom();
    }
}

void TextViewScrolling::updateAtBottom()
{
    atBottom_ = logFileBottom().alignsLastVisualLineAt( position_ );
}
