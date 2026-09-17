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

// Micro-benchmarks for the scrolling rules of a text view (#246), without a
// widget: a wheel turn, the scrollbar moved, the data changed and the width
// changed, on a million wrapped Log Lines. Links logsquirl_textviewscrolling
// only and creates a QCoreApplication, for the elastic hook's timer.
//
// See tests/benchmarks/README.md for how to run this and compare two runs.

#include "generated_log_lines.h"
#include "textviewscrolling.h"

#include <QCoreApplication>

#include <algorithm>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

namespace {

using namespace scrollingbenchmark;

constexpr int CharWidthPx = 7;
constexpr int CharHeightPx = 14;
constexpr int NarrowWidthPx = 640;
constexpr int WideWidthPx = 800;

class GeneratedText final : public ScrolledText {
public:
    LinesCount lineCount() const override
    {
        return LinesCount( LogLineCount + appended );
    }
    QString lineText( LineNumber position ) const override
    {
        return generatedLogLine( position.get() );
    }
    ScrollingViewport viewport() const override
    {
        return viewport_;
    }

    uint64_t appended = 0;
    ScrollingViewport viewport_{ .charWidthPx = CharWidthPx,
                                 .charHeightPx = CharHeightPx,
                                 .widthPx = WideWidthPx,
                                 .heightPx = 600,
                                 .largestDisplayLineNumber = LogLineCount };
};

// What the text view does around the scrolling rules, with a scrollbar that
// behaves as Qt's does.
class ScrolledView {
public:
    ScrolledView()
    {
        scrolling.viewportChanged();
        updateScrollBars();
    }

    void setScrollBarValue( int value )
    {
        value = std::clamp( value, 0, scrollBarMaximum );
        if ( value != scrollBarValue ) {
            scrollBarValue = value;
            scrolling.scrollBarMoved( value, 0 );
        }
    }

    void apply( const ScrollAnswer& answer )
    {
        if ( answer.scrolled ) {
            setScrollBarValue( answer.scrollBarValue );
        }
    }

    void updateScrollBars()
    {
        const auto ranges = scrolling.updateScrollBarRanges( LineLength( LongestLogLine ) );
        scrollBarMaximum = ranges.verticalMaximum;
        setScrollBarValue( scrollBarValue );
        apply( scrolling.keepAboveBottom() );
    }

    GeneratedText text;
    TextViewScrolling scrolling{ text, true };
    int scrollBarValue = 0;
    int scrollBarMaximum = 0;
};

constexpr int Notch = 120;
constexpr int Middle = static_cast<int>( LogLineCount / 2 );

} // namespace

TEST_CASE( "text view scrolling benchmarks", "[textviewscrolling-benchmark]" )
{
    ScrolledView view;
    view.setScrollBarValue( Middle );

    BENCHMARK( "wheel: 20 notches down and 20 up, wrapped" )
    {
        for ( int notch = 0; notch < 20; ++notch ) {
            view.apply( view.scrolling.turnWheel(
                WheelTurn{ .angleDeltaY = -Notch, .linesPerNotch = 3 } ) );
        }
        for ( int notch = 0; notch < 20; ++notch ) {
            view.apply(
                view.scrolling.turnWheel( WheelTurn{ .angleDeltaY = Notch, .linesPerNotch = 3 } ) );
        }
        return view.scrolling.position();
    };

    BENCHMARK( "keys: 5 pages down and 5 up, wrapped" )
    {
        for ( int page = 0; page < 5; ++page ) {
            view.apply( view.scrolling.stepPage( true ) );
        }
        for ( int page = 0; page < 5; ++page ) {
            view.apply( view.scrolling.stepPage( false ) );
        }
        return view.scrolling.position();
    };

    BENCHMARK( "scrollbar: dragged over 200 values" )
    {
        for ( int value = 0; value < 200; ++value ) {
            view.setScrollBarValue( Middle + ( value % 2 == 0 ? value : -value ) );
        }
        return view.scrolling.position();
    };

    BENCHMARK( "scrollbar: to its maximum and back" )
    {
        view.setScrollBarValue( view.scrollBarMaximum );
        view.setScrollBarValue( Middle );
        return view.scrolling.position();
    };

    BENCHMARK( "data changed: a Log Line appended" )
    {
        ++view.text.appended;
        if ( view.scrolling.dataChanged() ) {
            view.setScrollBarValue( 0 );
        }
        view.updateScrollBars();
        view.apply( view.scrolling.jumpToBottomIfFollowing() );
        return view.scrollBarMaximum;
    };

    view.text.appended = 0;
    view.updateScrollBars();
    view.setScrollBarValue( Middle );
    view.apply( view.scrolling.stepVisualLines( 2 ) );

    BENCHMARK( "width changed: narrower and wider again" )
    {
        for ( const int width : { NarrowWidthPx, WideWidthPx } ) {
            view.text.viewport_.widthPx = width;
            const bool wasAtBottom = view.scrolling.viewportChanged();
            view.updateScrollBars();
            view.apply( view.scrolling.jumpToBottomIfFollowing( wasAtBottom ) );
        }
        return view.scrolling.position();
    };
}

int main( int argc, char* argv[] )
{
    QCoreApplication app( argc, argv );

    return Catch::Session().run( argc, argv );
}
