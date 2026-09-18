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

// The scrolling rules of a text view, without a widget (#246): scrolling by
// Visual Lines, the bottom, follow and re-wrapping (#153, #154, #155). The
// text view's own tests keep a few of these to check that it adapts Qt events
// to them.
//
// A scrollbar is simulated as Qt's behaves: it clamps its value to its range,
// and tells scrolling whenever its value changes, also when scrolling moved it.

#include "log_view_log_files.h"
#include "textviewscrolling.h"

#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <catch2/catch.hpp>

namespace {

using namespace logviewscrolling;

// Visual Lines of one column shown one column wide, 20 rows high.
constexpr int CharWidthPx = 8;
constexpr int CharHeightPx = 10;
constexpr int Rows = 20;
constexpr int OneColumnWidePx
    = ViewportLayout::BulletAreaWidth + 2 * ViewportLayout::SeparatorWidth + 7;
// Wider by at least six columns.
constexpr int WideWidthPx = OneColumnWidePx + 200;

class FakeText final : public ScrolledText {
public:
    explicit FakeText( QStringList lines )
        : lines_( std::move( lines ) )
    {
    }

    LinesCount lineCount() const override
    {
        return LinesCount( static_cast<LinesCount::UnderlyingType>( lines_.size() ) );
    }
    QString lineText( LineNumber position ) const override
    {
        ++linesRead;
        ++readsOfLines;
        return lines_.at( static_cast<qsizetype>( position.get() ) );
    }
    logsquirl::vector<QString> lineTexts( LineNumber first, LinesCount count ) const override
    {
        ++readsOfLines;
        logsquirl::vector<QString> texts;
        for ( auto position = first.get(); position < first.get() + count.get(); ++position ) {
            ++linesRead;
            texts.push_back( lines_.at( static_cast<qsizetype>( position ) ) );
        }
        return texts;
    }
    ScrollingViewport viewport() const override
    {
        return viewport_;
    }

    QStringList lines_;
    ScrollingViewport viewport_{ .charWidthPx = CharWidthPx,
                                 .charHeightPx = CharHeightPx,
                                 .widthPx = OneColumnWidePx,
                                 .heightPx = Rows * CharHeightPx };
    mutable uint64_t linesRead = 0;
    // How often lines were read: one at a time or several together.
    mutable uint64_t readsOfLines = 0;
};

// A text view reduced to what scrolling needs of it: its lines, its Viewport
// and a vertical scrollbar.
class View {
public:
    explicit View( QStringList lines, bool textWrap = true )
        : text( std::move( lines ) )
        , scrolling( text, textWrap )
    {
        text.viewport_.largestDisplayLineNumber = text.lineCount().get();
        QObject::connect( &scrolling.elasticHook(), &ElasticHook::hooked,
                          [ this ]( bool hooked ) { hookedSignals.push_back( hooked ); } );
        // As a view shown for the first time is resized.
        changeViewport( text.viewport_.widthPx );
    }

    // --- the scrollbar ---------------------------------------------------

    void setScrollBarValue( int value )
    {
        value = std::clamp( value, 0, scrollBarMaximum );
        if ( value != scrollBarValue ) {
            scrollBarValue = value;
            scrolling.scrollBarMoved( value, 0 );
        }
    }

    void updateScrollBars()
    {
        const auto ranges = scrolling.updateScrollBarRanges( 0_length );
        scrollBarMaximum = ranges.verticalMaximum;
        setScrollBarValue( scrollBarValue );
        apply( scrolling.keepAboveBottom() );
    }

    // What the text view does with an answer.
    void apply( const ScrollAnswer& answer )
    {
        if ( answer.followChange != FollowChange::None ) {
            followChanges.push_back( answer.followChange );
        }
        if ( answer.scrolled ) {
            setScrollBarValue( answer.scrollBarValue );
        }
    }

    // --- input -----------------------------------------------------------

    void turnWheel( int angleDeltaY, bool fastScrollHeld = false )
    {
        apply( scrolling.turnWheel( WheelTurn{
            .angleDeltaY = angleDeltaY, .fastScrollHeld = fastScrollHeld, .linesPerNotch = 3 } ) );
    }

    // Pulls pixels down, a finger on the trackpad.
    void pull( int pixels )
    {
        apply( scrolling.turnWheel(
            WheelTurn{ .pixelDeltaY = -pixels, .phase = WheelPhase::Begin, .linesPerNotch = 3 } ) );
    }

    void step( int64_t visualLines, int times = 1 )
    {
        for ( int i = 0; i < times; ++i ) {
            apply( scrolling.stepVisualLines( visualLines ) );
        }
    }

    void page( bool down )
    {
        apply( scrolling.stepPage( down ) );
    }

    // The scrollbar dragged to the top, then all the way down.
    void dragToScrollBarMaximum()
    {
        setScrollBarValue( 0 );
        setScrollBarValue( scrollBarMaximum );
    }

    void moveTo( ScrollPosition position )
    {
        const auto line = static_cast<int>( position.lineNumber.get() );
        setScrollBarValue( line == 0 ? 1 : 0 );
        setScrollBarValue( line );
        step( 1, static_cast<int>( position.visualLineIndex ) );
        REQUIRE( scrolling.position() == position );
    }

    // The Log File changed to lines; change says how.
    void setLines( QStringList lines, LinesChange change = LinesChange::Any )
    {
        text.lines_ = std::move( lines );
        text.viewport_.largestDisplayLineNumber = text.lineCount().get();
        if ( scrolling.dataChanged( change ) ) {
            setScrollBarValue( 0 );
        }
        updateScrollBars();
        apply( scrolling.jumpToBottomIfFollowing() );
    }

    void changeViewport( int widthPx, bool lineNumbersVisible = false )
    {
        text.viewport_.widthPx = widthPx;
        text.viewport_.lineNumbersVisible = lineNumbersVisible;
        const bool wasAtBottom = scrolling.viewportChanged();
        updateScrollBars();
        apply( scrolling.jumpToBottomIfFollowing( wasAtBottom ) );
    }

    // --- what it shows ---------------------------------------------------

    LineLength columns() const
    {
        return scrolling.geometry().visibleColumns();
    }

    size_t visualLines( LineNumber position ) const
    {
        return scrolling
            .wrap( text.lines_.at( static_cast<qsizetype>( position.get() ) ), columns() )
            .wrappedLinesCount();
    }

    // The display column the top row starts at.
    LineColumn topRowColumn() const
    {
        const auto position = scrolling.position();
        return scrolling
            .wrap( text.lines_.at( static_cast<qsizetype>( position.lineNumber.get() ) ),
                   columns() )
            .wrappedLineStart( position.visualLineIndex );
    }

    // Whether the Viewport's last row shows the last Visual Line of the Log
    // File: counted from the Scroll Position, the Visual Lines down to the end
    // fill exactly the rows, and the view aligns them to the last one.
    void requireLogFileEndsOnLastRow() const
    {
        const auto position = scrolling.position();
        size_t below = visualLines( position.lineNumber ) - position.visualLineIndex;
        for ( auto line = position.lineNumber + 1_lcount;
              line < LineNumber( text.lineCount().get() ); line = line + 1_lcount ) {
            below += visualLines( line );
        }
        REQUIRE( below == static_cast<size_t>( Rows ) );
        REQUIRE( scrolling.pullToFollowState().atBottom );
    }

    FakeText text;
    TextViewScrolling scrolling;
    int scrollBarValue = 0;
    int scrollBarMaximum = 0;
    std::vector<FollowChange> followChanges;
    std::vector<bool> hookedSignals;
};

constexpr int Notch = 120;

} // namespace

SCENARIO( "Scrolling moves a text view by Visual Lines", "[textviewscrolling][scrollposition]" )
{
    GIVEN( "a Log Line taller than the Viewport in the middle of the Log File" )
    {
        View view{ tallLogLines() };

        THEN( "a notch of the wheel moves as far as three arrow steps, inside it and across Log "
              "Lines, and reaches its last Visual Line" )
        {
            bool reachedLastVisualLine = false;
            while ( view.scrolling.position().lineNumber.get() <= LinesBeforeTallLine + 5 ) {
                const auto before = view.scrolling.position();
                view.turnWheel( -Notch );
                const auto after = view.scrolling.position();
                view.step( -1, 3 );
                REQUIRE( view.scrolling.position() == before );
                view.step( 1, 3 );
                REQUIRE( view.scrolling.position() == after );
                reachedLastVisualLine
                    = reachedLastVisualLine
                      || after == ScrollPosition{ TallLine, TallLineVisualLines - 1 };
            }
            REQUIRE( reachedLastVisualLine );

            while ( view.scrolling.position() != ScrollPosition{} ) {
                const auto before = view.scrolling.position();
                view.turnWheel( Notch );
                const auto after = view.scrolling.position();
                view.step( 1, 3 );
                REQUIRE( view.scrolling.position() == before );
                view.step( -1, 3 );
                REQUIRE( view.scrolling.position() == after );
            }
        }

        THEN( "an arrow step moves one Visual Line, into the next Log Line and back" )
        {
            const ScrollPosition lastOfTallLine{ TallLine, TallLineVisualLines - 1 };
            view.moveTo( lastOfTallLine );
            view.step( 1 );
            REQUIRE( view.scrolling.position() == ScrollPosition{ TallLine + 1_lcount, 0 } );
            view.step( -1 );
            REQUIRE( view.scrolling.position() == lastOfTallLine );
        }

        THEN( "a page down followed by a page up returns to the same Scroll Position" )
        {
            for ( const auto start : { ScrollPosition{ 5_lnum, 0 }, ScrollPosition{ TallLine, 150 },
                                       ScrollPosition{ TallLine, TallLineVisualLines - 2 } } ) {
                view.moveTo( start );
                view.page( true );
                REQUIRE( view.scrolling.position() > start );
                view.page( false );
                REQUIRE( view.scrolling.position() == start );
            }
        }

        THEN( "turning wrapping off and on keeps the same Log Line at the top" )
        {
            view.moveTo( ScrollPosition{ TallLine, 150 } );
            view.scrolling.setTextWrap( false );
            view.updateScrollBars();
            REQUIRE( view.scrolling.position() == ScrollPosition{ TallLine, 0 } );
            view.scrolling.setTextWrap( true );
            view.updateScrollBars();
            REQUIRE( view.scrolling.position() == ScrollPosition{ TallLine, 0 } );
        }
    }

    GIVEN( "a Log Line of more than 10,000 Visual Lines" )
    {
        // 2,000 numbered words of six characters: 12,000 Visual Lines one column wide.
        QString numberedWords;
        for ( int word = 0; word < 2000; ++word ) {
            numberedWords += QStringLiteral( "%1 " ).arg( word, 5, 10, QLatin1Char( '0' ) );
        }
        QStringList lines{ QStringLiteral( "a" ), numberedWords };
        for ( int line = 0; line < 100; ++line ) {
            lines << QStringLiteral( "b" );
        }
        View view{ lines };

        THEN( "pages and then single steps reach any of them" )
        {
            const ScrollPosition lastDigit{ 1_lnum, 11998 };
            for ( int page = 0; page < 20000 && view.scrolling.position() < lastDigit; ++page ) {
                view.page( true );
            }
            for ( int step = 0; step < 20000 && view.scrolling.position() > lastDigit; ++step ) {
                view.step( -1 );
            }
            REQUIRE( view.scrolling.position() == lastDigit );
            REQUIRE( view.topRowColumn() == 11998_lcol );
        }
    }
}

SCENARIO( "The vertical scrollbar counts whole Log Lines", "[textviewscrolling][scrollbar]" )
{
    View view{ tallLogLines() };
    const auto tallLineValue = static_cast<int>( TallLine.get() );

    GIVEN( "a view moved partway through a Log Line" )
    {
        const auto answer = view.scrolling.scrollTo( ScrollPosition{ TallLine, 150 } );

        THEN( "the answer puts the scrollbar on that Log Line" )
        {
            REQUIRE( answer.scrolled );
            REQUIRE( answer.scrollBarValue == tallLineValue );
        }

        WHEN( "the scrollbar only catches up with that Log Line" )
        {
            view.scrolling.scrollBarMoved( answer.scrollBarValue, 0 );

            THEN( "the Scroll Position stands, Visual Line and all" )
            {
                REQUIRE( view.scrolling.position() == ScrollPosition{ TallLine, 150 } );
            }
        }

        WHEN( "the scrollbar is moved to another Log Line" )
        {
            view.scrolling.scrollBarMoved( 20, 0 );

            THEN( "the view lands on its first Visual Line" )
            {
                REQUIRE( view.scrolling.position() == ScrollPosition{ 20_lnum, 0 } );
            }

            AND_WHEN( "it is moved back to the Log Line partway through" )
            {
                view.scrolling.scrollBarMoved( tallLineValue, 0 );

                THEN( "the view lands on its first Visual Line too" )
                {
                    REQUIRE( view.scrolling.position() == ScrollPosition{ TallLine, 0 } );
                }
            }
        }
    }

    GIVEN( "a view moving between Visual Lines of one Log Line" )
    {
        view.moveTo( ScrollPosition{ TallLine, 150 } );
        view.step( 1 );
        view.turnWheel( -Notch );

        THEN( "the scrollbar stays on the Log Line, and does not pull the view back" )
        {
            REQUIRE( view.scrolling.position() == ScrollPosition{ TallLine, 154 } );
            REQUIRE( view.scrollBarValue == tallLineValue );
        }
    }

    GIVEN( "a view scrolled by the horizontal scrollbar" )
    {
        View unwrapped{ QStringList{ QString( 400, QLatin1Char( 'x' ) ) }, false };
        unwrapped.scrolling.scrollBarMoved( 0, -30 );
        REQUIRE( unwrapped.scrolling.firstColumn() == 30_lcol );

        THEN( "the first column goes no further left than the first one" )
        {
            unwrapped.scrolling.scrollBarMoved( 0, 50 );
            REQUIRE( unwrapped.scrolling.firstColumn() == 0_lcol );
        }
    }
}

SCENARIO( "The bottom of a text view shows exactly the last Visual Line of the Log File",
          "[textviewscrolling][bottom]" )
{
    GIVEN( "a Log File whose last Log Lines are one Visual Line each" )
    {
        View view{ tallLogLines() };

        THEN( "at the scrollbar's maximum its last Visual Line is on the last row" )
        {
            REQUIRE( view.scrollBarMaximum > 0 );
            view.dragToScrollBarMaximum();
            REQUIRE( view.scrolling.position().lineNumber.get()
                     == static_cast<uint64_t>( view.scrollBarMaximum ) );
            view.requireLogFileEndsOnLastRow();
        }

        THEN( "steps, pages and the wheel go no further" )
        {
            view.dragToScrollBarMaximum();
            const auto bottom = view.scrolling.position();
            view.step( 1 );
            view.page( true );
            view.turnWheel( -Notch );
            REQUIRE( view.scrolling.position() == bottom );
            view.requireLogFileEndsOnLastRow();
        }

        THEN( "a move past it goes no further than the bottom" )
        {
            view.apply( view.scrolling.scrollTo( ScrollPosition{ LineNumber( 5000 ), 0 } ) );
            REQUIRE( view.scrolling.position() == view.scrolling.bottomScrollPosition() );
        }
    }

    GIVEN( "a Log File whose last Log Line is taller than the Viewport" )
    {
        View view{ tallLastLogLines() };
        view.dragToScrollBarMaximum();
        const auto bottom = view.scrolling.position();
        REQUIRE( bottom.visualLineIndex > 5 );

        THEN( "at the scrollbar's maximum its last Visual Line is on the last row" )
        {
            view.requireLogFileEndsOnLastRow();
        }

        THEN( "pages, the wheel and the scrollbar all reach that same bottom from the top" )
        {
            view.moveTo( ScrollPosition{} );
            for ( int page = 0; page < 1000 && view.scrolling.position() != bottom; ++page ) {
                view.page( true );
            }
            REQUIRE( view.scrolling.position() == bottom );

            view.moveTo( ScrollPosition{} );
            for ( int notch = 0; notch < 1000 && view.scrolling.position() != bottom; ++notch ) {
                view.turnWheel( -Notch );
            }
            REQUIRE( view.scrolling.position() == bottom );
        }

        WHEN( "the view moves partway up that Log Line, where the scrollbar is at its maximum" )
        {
            while ( view.scrolling.position().visualLineIndex > 5 ) {
                view.step( -1 );
            }
            REQUIRE( view.scrollBarValue == view.scrollBarMaximum );

            THEN( "the scrollbar's own action to its maximum lands at the bottom" )
            {
                view.apply( view.scrolling.scrollBarActionTriggered( false, view.scrollBarMaximum,
                                                                     view.scrollBarValue ) );
                REQUIRE( view.scrolling.position() == bottom );
                view.requireLogFileEndsOnLastRow();
            }

            THEN( "releasing the thumb at its maximum lands at the bottom" )
            {
                view.apply( view.scrolling.scrollBarReleased( view.scrollBarMaximum,
                                                              view.scrollBarValue ) );
                REQUIRE( view.scrolling.position() == bottom );
            }

            THEN( "an action that leaves the thumb elsewhere does not move the view" )
            {
                const auto partway = view.scrolling.position();
                const auto answer = view.scrolling.scrollBarActionTriggered(
                    false, view.scrollBarMaximum - 1, view.scrollBarValue );
                REQUIRE( !answer.scrolled );
                REQUIRE( view.scrolling.position() == partway );
            }
        }
    }

    GIVEN( "a Log File of fewer Log Lines than rows, one of them taller than the Viewport" )
    {
        View view{ fewLogLinesOneTallerThanTheViewport() };

        THEN( "it can be scrolled, down to its last Visual Line on the last row" )
        {
            REQUIRE( view.scrollBarMaximum > 0 );
            view.dragToScrollBarMaximum();
            view.requireLogFileEndsOnLastRow();
        }
    }

    GIVEN( "a Log File with fewer Visual Lines than the Viewport has rows" )
    {
        View view{ fewerVisualLinesThanRows() };

        THEN( "it shows from its top, with no scroll range, and does not align to the last row" )
        {
            REQUIRE( view.scrollBarMaximum == 0 );
            view.page( true );
            view.turnWheel( -Notch );
            REQUIRE( view.scrolling.position() == ScrollPosition{} );
            REQUIRE( !view.scrolling.pullToFollowState().atBottom );
        }
    }
}

SCENARIO( "A text view at the bottom as its Log File changes", "[textviewscrolling][follow]" )
{
    GIVEN( "Log Lines appended to the Log File" )
    {
        View view{ tallLogLines() };
        auto grown = tallLogLines();
        grown << QStringLiteral( "b" ) << tallLine();

        THEN( "follow keeps the new last Visual Line on the last row" )
        {
            view.apply( view.scrolling.followSet( true ) );
            view.setLines( grown );
            view.requireLogFileEndsOnLastRow();
            REQUIRE( view.scrolling.pullToFollowState().hooked );
            REQUIRE( view.scrollBarValue == view.scrollBarMaximum );
        }

        THEN( "without follow, a view at the bottom stays where it is" )
        {
            view.dragToScrollBarMaximum();
            const auto before = view.scrolling.position();
            view.setLines( grown );
            REQUIRE( view.scrolling.position() == before );
            REQUIRE( view.scrollBarMaximum > static_cast<int>( before.lineNumber.get() ) );
            // Still aligned as it was: only scrolling changes that.
            REQUIRE( view.scrolling.pullToFollowState().atBottom );
        }

        THEN( "without follow, a view partway through a Log Line stays where it is" )
        {
            view.moveTo( ScrollPosition{ TallLine, 150 } );
            view.setLines( grown );
            REQUIRE( view.scrolling.position() == ScrollPosition{ TallLine, 150 } );
        }
    }

    GIVEN( "the last Log Line growing longer" )
    {
        View view{ tallLastLogLines() };
        auto grown = tallLastLogLines();
        grown.last() += QStringLiteral( " x x x x" );

        THEN( "follow keeps its new last Visual Line on the last row" )
        {
            view.apply( view.scrolling.followSet( true ) );
            view.setLines( grown );
            view.requireLogFileEndsOnLastRow();
        }

        THEN( "follow keeps its new last Visual Line on the last row when told Log Lines were "
              "only appended" )
        {
            view.apply( view.scrolling.followSet( true ) );
            view.setLines( grown, LinesChange::Appended );
            view.requireLogFileEndsOnLastRow();
        }
    }

    GIVEN( "Log Lines appended, and the view told so" )
    {
        View view{ fewerVisualLinesThanRows() };
        view.apply( view.scrolling.followSet( true ) );

        THEN( "follow keeps the new last Visual Line on the last row, append after append" )
        {
            auto grown = fewerVisualLinesThanRows();
            for ( int append = 0; append < 30; ++append ) {
                grown << ( append % 5 == 0 ? tallLine() : QStringLiteral( "c" ) );
                view.setLines( grown, LinesChange::Appended );
                if ( view.scrolling.bottomScrollPosition() != ScrollPosition{} ) {
                    view.requireLogFileEndsOnLastRow();
                }
                REQUIRE( view.scrollBarValue == view.scrollBarMaximum );
            }
        }
    }

    GIVEN( "a view standing past the Log Lines there are after a change" )
    {
        View view{ tallLogLines() };
        view.moveTo( ScrollPosition{ TallLine + 50_lcount, 0 } );

        WHEN( "the Log File shrinks" )
        {
            view.setLines( fewerVisualLinesThanRows() );

            THEN( "the view goes back to the top" )
            {
                REQUIRE( view.scrolling.position() == ScrollPosition{} );
                REQUIRE( view.scrollBarValue == 0 );
            }
        }
    }
}

SCENARIO( "Follow is left by moving up and engaged by pulling past the bottom",
          "[textviewscrolling][follow]" )
{
    View view{ tallLogLines() };

    GIVEN( "a view that follows" )
    {
        view.apply( view.scrolling.followSet( true ) );
        REQUIRE( view.scrolling.follows() );
        REQUIRE( view.scrolling.position() == view.scrolling.bottomScrollPosition() );

        THEN( "a step up asks to leave follow, a step down does not" )
        {
            view.step( 1 );
            REQUIRE( view.followChanges.empty() );
            view.step( -1 );
            REQUIRE( view.followChanges == std::vector{ FollowChange::Leave } );
        }

        THEN( "a step or page up by the scrollbar asks to leave follow, and so does any other move "
              "away" )
        {
            view.apply( view.scrolling.scrollBarActionTriggered( true, view.scrollBarValue - 1,
                                                                 view.scrollBarValue ) );
            REQUIRE( view.followChanges == std::vector{ FollowChange::Leave } );
            view.apply( view.scrolling.leaveFollow() );
            REQUIRE( view.followChanges.size() == 2 );
            REQUIRE( !view.scrolling.pullToFollowState().hooked );
        }

        THEN( "the wheel takes a view the scrollbar moved away back to the bottom first" )
        {
            view.setScrollBarValue( 3 );
            REQUIRE( view.scrolling.position() == ScrollPosition{ 3_lnum, 0 } );
            view.turnWheel( -Notch );
            REQUIRE( view.scrolling.position() == view.scrolling.bottomScrollPosition() );
        }
    }

    GIVEN( "a view at the bottom, under a Policy that lets scrolling engage follow" )
    {
        auto policy = view.scrolling.presentationPolicy();
        policy.allowFollowOnScroll = true;
        view.scrolling.setPresentationPolicy( policy );
        view.dragToScrollBarMaximum();
        const auto bottom = view.scrolling.position();

        WHEN( "it is pulled past the bottom as far as the hook's threshold" )
        {
            view.pull( TextViewScrolling::HookThreshold );

            THEN( "the elastic hook hooks, and the view does not move" )
            {
                REQUIRE( view.hookedSignals == std::vector{ true } );
                REQUIRE( view.scrolling.pullToFollowState().hooked );
                REQUIRE( view.scrolling.position() == bottom );
            }
        }

        WHEN( "it is pulled less than that" )
        {
            view.pull( 112 );

            THEN( "the elastic stretches without hooking" )
            {
                REQUIRE( view.hookedSignals.empty() );
                REQUIRE( view.scrolling.pullToFollowState().elasticHookLength == 112 );
                REQUIRE( view.scrolling.position() == bottom );
            }
        }

        WHEN( "follow is not allowed for the Log File" )
        {
            view.scrolling.allowFollow( false );
            view.hookedSignals.clear();
            view.pull( TextViewScrolling::HookThreshold );

            THEN( "the pull does not hook" )
            {
                REQUIRE( view.hookedSignals.empty() );
                REQUIRE( !view.scrolling.pullToFollowState().hooked );
            }
        }
    }

    GIVEN( "a view at the bottom, under a Policy that does not let scrolling engage follow" )
    {
        view.dragToScrollBarMaximum();
        view.pull( TextViewScrolling::HookThreshold );

        THEN( "a pull down neither stretches nor hooks the elastic" )
        {
            REQUIRE( view.hookedSignals.empty() );
            REQUIRE( view.scrolling.pullToFollowState().elasticHookLength == 0 );
        }
    }

    GIVEN( "a view at the bottom" )
    {
        view.dragToScrollBarMaximum();

        THEN( "engaging follow from there asks for it" )
        {
            view.apply( view.scrolling.engageFollow() );
            REQUIRE( view.followChanges == std::vector{ FollowChange::Engage } );
            REQUIRE( view.scrolling.pullToFollowState().hooked );
        }
    }
}

SCENARIO( "Fast scrolling follows the Presentation Policy", "[textviewscrolling][policy]" )
{
    View view{ tallLogLines() };
    view.turnWheel( -Notch );
    const auto afterAPlainNotch = view.scrolling.position();
    REQUIRE( afterAPlainNotch > ScrollPosition{} );

    auto policy = view.scrolling.presentationPolicy();

    GIVEN( "a Policy that turns fast scrolling on" )
    {
        policy.fastScrollEnabled = true;
        policy.fastScrollMultiplier = 5;
        view.scrolling.setPresentationPolicy( policy );

        THEN( "a notch with the modifier held moves further than without it" )
        {
            view.moveTo( ScrollPosition{} );
            view.turnWheel( -Notch, /* fastScrollHeld */ true );
            REQUIRE( view.scrolling.position() > afterAPlainNotch );
        }
    }

    GIVEN( "a Policy that turns fast scrolling off" )
    {
        policy.fastScrollEnabled = false;
        view.scrolling.setPresentationPolicy( policy );

        THEN( "a notch with the modifier held moves exactly as far as without it" )
        {
            view.moveTo( ScrollPosition{} );
            view.turnWheel( -Notch, /* fastScrollHeld */ true );
            REQUIRE( view.scrolling.position() == afterAPlainNotch );
        }
    }
}

SCENARIO( "A re-wrap keeps the text on the top row", "[textviewscrolling][rewrap]" )
{
    GIVEN( "a view one column wide, partway down a Log Line taller than the Viewport" )
    {
        View view{ tallLogLines() };
        constexpr size_t PartwayDown = 224;
        const LineColumn partway{ static_cast<LineColumn::UnderlyingType>( PartwayDown ) };
        view.moveTo( ScrollPosition{ TallLine, PartwayDown } );
        REQUIRE( view.topRowColumn() == partway );

        WHEN( "it is widened" )
        {
            view.changeViewport( WideWidthPx );

            THEN( "the top row holds the same character" )
            {
                REQUIRE( view.scrolling.position().lineNumber == TallLine );
                const auto top = view.topRowColumn();
                REQUIRE( top <= partway );
                REQUIRE( partway < top + view.columns() );

                AND_WHEN( "it is narrowed again" )
                {
                    view.changeViewport( OneColumnWidePx );

                    THEN( "the top row holds the first character of the wide one" )
                    {
                        REQUIRE( view.scrolling.position()
                                 == ScrollPosition{ TallLine, static_cast<size_t>( top.get() ) } );
                    }
                }
            }
        }
    }

    GIVEN( "a wide view partway down a Log Line taller than the Viewport" )
    {
        View view{ tallLogLines() };
        view.changeViewport( WideWidthPx );
        view.moveTo( ScrollPosition{ TallLine, 5 } );
        auto topColumn = view.topRowColumn();
        REQUIRE( topColumn > 0_lcol );

        for ( const bool lineNumbersVisible : { true, false } ) {
            WHEN( "line numbers are " << ( lineNumbersVisible ? "shown" : "hidden" ) )
            {
                if ( !lineNumbersVisible ) {
                    view.changeViewport( WideWidthPx, true );
                    view.moveTo( ScrollPosition{ TallLine, 5 } );
                    topColumn = view.topRowColumn();
                }
                view.changeViewport( WideWidthPx, lineNumbersVisible );

                THEN( "the top row still holds the same character" )
                {
                    const auto top = view.topRowColumn();
                    REQUIRE( top <= topColumn );
                    REQUIRE( topColumn < top + view.columns() );
                }
            }
        }
    }

    GIVEN( "a view at the bottom of a Log File whose last Log Line is taller than the Viewport" )
    {
        View view{ tallLastLogLines() };
        view.changeViewport( WideWidthPx );
        view.dragToScrollBarMaximum();
        view.requireLogFileEndsOnLastRow();

        THEN( "it stays at the bottom through a resize" )
        {
            view.changeViewport( OneColumnWidePx );
            view.requireLogFileEndsOnLastRow();
            REQUIRE( view.scrollBarValue == view.scrollBarMaximum );

            view.changeViewport( WideWidthPx );
            view.requireLogFileEndsOnLastRow();
            REQUIRE( view.scrollBarValue == view.scrollBarMaximum );
        }
    }
}

SCENARIO( "No scrolling step reads more than the Viewport and what it passes over",
          "[textviewscrolling][cost]" )
{
    GIVEN( "a wrapped view of 100,000 Log Lines of one Visual Line each" )
    {
        QStringList lines;
        for ( int line = 0; line < 100000; ++line ) {
            lines << QStringLiteral( "b" );
        }
        View view{ lines };

        THEN( "the Log File changing reads no more Log Lines than the Viewport has rows" )
        {
            view.text.linesRead = 0;
            view.setLines( view.text.lines_ );
            REQUIRE( view.text.linesRead > 0 );
            REQUIRE( view.text.linesRead <= static_cast<uint64_t>( Rows ) );
        }

        THEN( "the scrollbar moved to the middle reads no Log Line" )
        {
            view.text.linesRead = 0;
            view.setScrollBarValue( 50000 );
            REQUIRE( view.scrolling.position() == ScrollPosition{ 50000_lnum, 0 } );
            REQUIRE( view.text.linesRead == 0 );
        }

        THEN( "a notch of the wheel reads only the Log Lines it passes over" )
        {
            view.setScrollBarValue( 50000 );
            view.text.linesRead = 0;
            view.turnWheel( -Notch );
            REQUIRE( view.text.linesRead <= 3 );
        }

        THEN( "a Log Line appended, the view told so, reads only it and the Log Line before it" )
        {
            view.setLines( view.text.lines_ );
            auto grown = view.text.lines_;
            grown << QStringLiteral( "c" );
            view.text.linesRead = 0;
            view.setLines( grown, LinesChange::Appended );
            REQUIRE( view.text.linesRead <= 2 );
        }

        THEN( "a page down reads the Log Lines it passes over together, not one by one" )
        {
            view.setScrollBarValue( 50000 );
            view.text.linesRead = 0;
            view.text.readsOfLines = 0;
            view.page( true );
            REQUIRE( view.scrolling.position() == ScrollPosition{ 50020_lnum, 0 } );
            REQUIRE( view.text.linesRead <= static_cast<uint64_t>( Rows ) );
            // Reads that double in size: 1, 1 (two lines are read one by one),
            // 4, 8 and the 6 left.
            REQUIRE( view.text.readsOfLines <= 5 );
        }

        THEN( "a page up reads the Log Lines it passes over together, not one by one" )
        {
            view.setScrollBarValue( 50000 );
            view.text.linesRead = 0;
            view.text.readsOfLines = 0;
            view.page( false );
            REQUIRE( view.scrolling.position() == ScrollPosition{ 49980_lnum, 0 } );
            REQUIRE( view.text.linesRead <= static_cast<uint64_t>( Rows ) );
            REQUIRE( view.text.readsOfLines <= 5 );
        }

        THEN( "the Log File changing reads the Log Lines at its end together" )
        {
            view.text.readsOfLines = 0;
            view.setLines( view.text.lines_ );
            REQUIRE( view.text.readsOfLines <= 5 );
        }

        THEN( "a resize reads no more Log Lines than the Viewport has rows" )
        {
            view.setScrollBarValue( 50000 );
            view.text.linesRead = 0;
            view.changeViewport( WideWidthPx );
            REQUIRE( view.text.linesRead <= static_cast<uint64_t>( Rows ) );
        }
    }

    GIVEN( "a wrapped view of 100,000 Log Lines of two Visual Lines each" )
    {
        QStringList lines;
        for ( int line = 0; line < 100000; ++line ) {
            lines << QStringLiteral( "bb" );
        }
        View view{ lines };
        view.setScrollBarValue( 50000 );
        view.text.linesRead = 0;

        THEN( "a page down reads no Log Line past the ones it passes over" )
        {
            view.page( true );
            REQUIRE( view.scrolling.position() == ScrollPosition{ 50010_lnum, 0 } );
            REQUIRE( view.text.linesRead <= static_cast<uint64_t>( Rows / 2 ) );
        }

        THEN( "a page up reads no Log Line past the ones it passes over" )
        {
            view.page( false );
            REQUIRE( view.scrolling.position() == ScrollPosition{ 49990_lnum, 0 } );
            REQUIRE( view.text.linesRead <= static_cast<uint64_t>( Rows / 2 ) );
        }

        THEN( "a notch of the wheel reads its Log Lines one at a time" )
        {
            view.text.readsOfLines = 0;
            view.turnWheel( -Notch );
            REQUIRE( view.text.linesRead <= 2 );
            REQUIRE( view.text.readsOfLines == view.text.linesRead );
        }
    }

    GIVEN( "a wrapped view whose last Log Line is taller than the Viewport" )
    {
        View view{ tallLastLogLines() };

        THEN( "the Log File changing reads only that last Log Line" )
        {
            view.text.linesRead = 0;
            view.setLines( view.text.lines_ );
            REQUIRE( view.text.linesRead == 1 );
        }
    }
}

// A column is as wide as the font paints it (#352). Qt adds advances up in
// 1/64 pixels; a layout that counts columns in whole pixels loses a fraction
// of a pixel per column, and over a Viewport's width that becomes characters
// the view never shows and clicks that land beside the character.
SCENARIO( "columns are as wide as the font paints them", "[textviewscrolling][viewportlayout]" )
{
    // What Qt measures for Menlo at 16pt: 9.625 px per character, which
    // QFontMetrics::horizontalAdvance() reports as 10.
    constexpr double PaintedAdvancePx = 9.625;
    constexpr int RoundedAdvancePx = 10;
    constexpr int FontHeightPx = 16;
    // The text area of the Viewport the bug was measured in.
    constexpr int TextAreaPx = 2275;
    constexpr int64_t LongLineColumns = 100000;

    const auto marginsOnly = ViewportLayout{ ViewportLayoutInput{} };

    ViewportLayoutInput input;
    input.charWidthPx = PaintedAdvancePx;
    input.charHeightPx = FontHeightPx;
    input.viewportWidthPx = marginsOnly.leftMarginPx() + TextAreaPx;
    input.viewportHeightPx = 10 * FontHeightPx;
    input.textWrap = false;

    GIVEN( "a Viewport measured in a font whose advance is fractional" )
    {
        const ViewportLayout layout{ input };
        const auto columns = static_cast<double>( layout.visibleColumns().get() );

        THEN( "it shows every column that fits, and none that does not" )
        {
            REQUIRE( columns * PaintedAdvancePx <= TextAreaPx );
            REQUIRE( ( columns + 1 ) * PaintedAdvancePx > TextAreaPx );
        }

        THEN( "it shows the columns a whole-pixel width would leave blank" )
        {
            // 236 columns rather than 227: the eight characters that used to
            // stay empty at the right edge of the Viewport.
            REQUIRE( columns > TextAreaPx / RoundedAdvancePx );
        }
    }

    GIVEN( "a Log Line longer than the Viewport is wide" )
    {
        VisualLines visualLines{ VisualLine{ .lineNumber = 0_lnum,
                                             .wrappedLineIndex = 0,
                                             .firstColumn = 0_lcol,
                                             .length = LineLength{ LongLineColumns },
                                             .lineLength = LineLength{ LongLineColumns } } };
        const ViewportLayout layout{ input, std::move( visualLines ) };
        // One type for every column below: a braced list of mixed integer
        // types deduces nothing, and which of them int64_t is differs between
        // the platforms (macOS builds it, GCC on Linux does not).
        using Column = decltype( layout.visibleColumns().get() );
        const Column lastColumn = layout.visibleColumns().get() - 1;

        THEN( "a click lands on the character under it, at either edge" )
        {
            for ( const Column column :
                  { Column{ 0 }, Column{ 1 }, Column{ 100 }, Column{ 200 }, lastColumn } ) {
                const auto centreOfColumnPx
                    = layout.textOriginX()
                      + static_cast<int>( PaintedAdvancePx * static_cast<double>( column )
                                          + PaintedAdvancePx / 2 );
                const auto position = layout.filePositionAtPoint( centreOfColumnPx, 0 );
                REQUIRE( position.column().get() == column );
            }
        }

        THEN( "a character's cell sits where the character is painted" )
        {
            for ( const Column column : { Column{ 0 }, Column{ 100 }, lastColumn } ) {
                const auto rect = layout.rectForColumn(
                    0_lnum, LineColumn{ static_cast<LineColumn::UnderlyingType>( column ) } );
                REQUIRE( rect.x
                         == layout.textOriginX() + columnsWidthPx( PaintedAdvancePx, column ) );
                // A cell covers a whole character, never a sliver of one.
                REQUIRE( rect.width >= static_cast<int>( PaintedAdvancePx ) );
            }
        }
    }
}
