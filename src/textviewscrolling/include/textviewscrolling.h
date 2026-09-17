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

#include "linetypes.h"
#include "settingspolicies.h"
#include "viewportlayout.h"
#include "viewtools.h"
#include "wrappedstring.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// How a text view scrolls: its Scroll Position, follow with its elastic hook,
// the bottom of the Log File, keeping the reading position across a re-wrap
// and the wheel steps not yet scrolled. It knows no widget (#246).
//
// It is given the lines the view shows and its Viewport through ScrolledText,
// and the Presentation Policy. It answers a wheel turn, a key step, the
// scrollbar moved to a value, the data changed and the Viewport changed with a
// ScrollAnswer: the new Scroll Position, the value the vertical scrollbar is to
// show, and whether follow is left or engaged. The widget turns Qt events into
// these calls and applies the answers. Selection is not part of it.
//
// The vertical scrollbar counts whole Log Lines (docs/adr/0001). No step here
// does work proportional to the Log File: a move wraps only the lines it passes
// over, and the bottom is found wrapping backwards no more lines than the
// Viewport has rows.

// The Viewport of a text view, as the view measures it now.
struct ScrollingViewport {
    int charWidthPx = 1;
    int charHeightPx = 1;
    int widthPx = 0;
    int heightPx = 0;
    bool lineNumbersVisible = false;
    // The largest line number drawn; it fixes the width of the line numbers.
    LineNumber::UnderlyingType largestDisplayLineNumber = 0;
};

// What scrolling reads of the text view it scrolls. Every line is named by its
// position in the view: a Log Line in the main view, a place among the
// Displayed Lines in the Filtered View.
class ScrolledText {
public:
    virtual ~ScrolledText() = default;

    // How many lines the view shows.
    virtual LinesCount lineCount() const = 0;
    // The text of the line at position, as the Log File holds it.
    virtual QString lineText( LineNumber position ) const = 0;
    // The texts of the count lines from first on, as lineText() gives them.
    // Scrolling asks for lines it passes over together through this; a view
    // whose lines are cheaper to read together than one by one overrides it.
    virtual logsquirl::vector<QString> lineTexts( LineNumber first, LinesCount count ) const;
    // The Viewport as it is now.
    virtual ScrollingViewport viewport() const = 0;

protected:
    ScrolledText() = default;
    ScrolledText( const ScrolledText& ) = default;
    ScrolledText& operator=( const ScrolledText& ) = default;
};

// Whether a step leaves follow or engages it. The view says so to whoever
// holds the follow toggle; that one hands the new state back through
// TextViewScrolling::followSet().
enum class FollowChange { None, Leave, Engage };

// How the lines a text view shows changed.
enum class LinesChange {
    // In any way: lines may have been changed, inserted or removed anywhere.
    Any,
    // Lines were only added at the end. The lines there were before are as
    // they were, but for the last of them, which may have grown longer.
    Appended,
};

// What the view does after a step.
struct ScrollAnswer {
    // Whether the step set the Scroll Position, even to where it already was.
    // The view then moves the vertical scrollbar to scrollBarValue, or, when it
    // is there already, takes what follows any move (a repaint, the overview).
    bool scrolled = false;
    ScrollPosition position;
    int scrollBarValue = 0;
    FollowChange followChange = FollowChange::None;
    // Whether the view is redrawn even where the Scroll Position stayed.
    bool redraw = false;
    // A wheel turn only: mostly sideways, the scroll area scrolls it horizontally.
    bool scrollHorizontally = false;
};

// Where a wheel turn is in a gesture on a trackpad.
enum class WheelPhase { None, Begin, Update, End, Momentum };

// A turn of the wheel, as plain numbers.
struct WheelTurn {
    int angleDeltaX = 0;
    int angleDeltaY = 0;
    int pixelDeltaX = 0;
    int pixelDeltaY = 0;
    // The fast scroll modifier is held.
    bool fastScrollHeld = false;
    // The modifier that makes a notch scroll a page is held.
    bool pageHeld = false;
    WheelPhase phase = WheelPhase::None;
    // Visual Lines a notch scrolls, as the platform is set.
    int linesPerNotch = 3;

    // How far the turn pulls, in pixels: the pixel delta where the device
    // gives one, the angle delta otherwise. 0 moves nothing.
    int pixels() const;
};

// The ranges the scrollbars are to have.
struct ScrollBarRanges {
    int verticalMaximum = 0;
    int horizontalMaximum = 0;
    int horizontalPageStep = 0;
};

// The text of a Log Line as a text view draws it: tabs expanded, and split into
// Visual Lines columns wide, or one without text wrapping. Always at least one
// Visual Line.
WrappedString wrapLogLine( QString text, LineLength columns, bool textWrap );

class TextViewScrolling {
public:
    // How far the elastic hook is pulled before follow engages.
    static constexpr int HookThreshold = 300;

    // Lets a test read what scrolling counted.
    template <class T>
    struct access_by;

    // text outlives this. textWrap is the state the view starts in.
    TextViewScrolling( const ScrolledText& text, bool textWrap );

    TextViewScrolling( const TextViewScrolling& ) = delete;
    TextViewScrolling( TextViewScrolling&& ) = delete;
    TextViewScrolling& operator=( const TextViewScrolling& ) = delete;
    TextViewScrolling& operator=( TextViewScrolling&& ) = delete;
    ~TextViewScrolling() = default;

    // --- what it holds -------------------------------------------------

    // The Scroll Position: the Log Line at the top of the Viewport and which of
    // its Visual Lines is shown first.
    ScrollPosition position() const
    {
        return position_;
    }
    // First display column at the left edge (text wrapping off only).
    LineColumn firstColumn() const
    {
        return firstColumn_;
    }
    // Whether long Log Lines are wrapped into several Visual Lines.
    bool textWrap() const
    {
        return textWrap_;
    }
    // Whether follow is engaged: the view stays at the bottom of the Log File.
    bool follows() const
    {
        return follow_;
    }
    // Whether pulling past the bottom may engage follow (see allowFollow()).
    bool followAllowed() const
    {
        return elasticHook_.isHookAllowed();
    }
    const PresentationPolicy& presentationPolicy() const
    {
        return presentationPolicy_;
    }
    // Its signals say when the pull-to-follow bar is to be redrawn, and when
    // pulling it engaged or left follow.
    const ElasticHook& elasticHook() const
    {
        return elasticHook_;
    }

    // --- what it answers -----------------------------------------------

    // The layout of the Viewport without its Visual Lines, drawing offset
    // included: margins, visible counts, scroll ranges and pull-to-follow.
    ViewportLayout geometry() const;
    // The follow state the pull-to-follow geometry is placed from.
    PullToFollowState pullToFollowState() const;
    // Where the last Visual Line of the Log File sits on the Viewport's last
    // row. Wrapped backwards from the end, no more lines than the Viewport has
    // rows, when something it depends on changed.
    const LogFileBottom& logFileBottom() const;
    // The bottom Scroll Position: where follow and the vertical scrollbar's
    // maximum put the view, and scrolling goes no further down.
    ScrollPosition bottomScrollPosition() const
    {
        return logFileBottom().scrollPosition;
    }
    // Visual Lines a page moves.
    std::int64_t visualLinesPerPage() const;
    // The Visual Line of the line holding position, at the current width.
    ScrollPosition visualLineOf( FilePosition position ) const;
    // The text of a line as drawn at the current text wrapping.
    WrappedString wrap( QString text, LineLength columns ) const;

    // --- what it is handed ---------------------------------------------

    // What this view scrolls under. Nothing is derived from it and kept.
    void setPresentationPolicy( const PresentationPolicy& policy );
    // Whether pulling past the bottom may engage follow.
    void allowFollow( bool allow );

    // --- moves ---------------------------------------------------------

    // Moves to position, brought into the Log File and no further than the
    // bottom. Moving between Visual Lines of one line leaves the scrollbar.
    ScrollAnswer scrollTo( ScrollPosition position );
    // A step by a key or selection autoscroll, visualLines down (up when
    // negative). As the scrollbar's own steps do, a step up leaves follow.
    ScrollAnswer stepVisualLines( std::int64_t visualLines );
    // Page Down, or Page Up.
    ScrollAnswer stepPage( bool down );
    // A turn of the wheel that pulls (WheelTurn::pixels() is not 0). At the
    // bottom Scroll Position it pulls the elastic hook instead of scrolling,
    // when the Presentation Policy lets scrolling engage follow.
    ScrollAnswer turnWheel( const WheelTurn& turn );
    ScrollAnswer jumpToBottom();
    // A move that does, when follow is on or orAtBottom: the view goes to the
    // bottom Scroll Position. Otherwise it does not scroll.
    ScrollAnswer jumpToBottomIfFollowing( bool orAtBottom = false );
    // Brings the view back to the bottom Scroll Position when it stands below
    // it, as when the Viewport grew.
    ScrollAnswer keepAboveBottom();

    // --- follow --------------------------------------------------------

    // Follow was turned on or off. On, the view goes to the bottom.
    ScrollAnswer followSet( bool checked );
    // Asks to leave follow: a move away from the bottom by the user.
    ScrollAnswer leaveFollow();
    // Asks to engage follow, from the bottom Scroll Position.
    ScrollAnswer engageFollow();

    // --- the scrollbars ------------------------------------------------

    // The vertical scrollbar moved to value, the horizontal one by dx.
    //
    // A value the view moved the scrollbar to itself is it only catching up
    // with the line of the Scroll Position, which then stands, Visual Line and
    // all. The scrollbar's maximum is the bottom Scroll Position. Any other
    // value lands on the first Visual Line of the line it maps to.
    void scrollBarMoved( int value, int dx );
    // One of the vertical scrollbar's own actions was triggered: steppedUp for
    // a step or page up, which leaves follow. Moved to the maximum it is
    // already at, the scrollbar changes no value; the view still lands at the
    // bottom Scroll Position, also from partway up the last line.
    ScrollAnswer scrollBarActionTriggered( bool steppedUp, int sliderPosition, int value );
    // The vertical scrollbar's thumb was released.
    ScrollAnswer scrollBarReleased( int sliderPosition, int value );
    // The ranges for the scrollbars, for lines up to maxLineLength long. The
    // lines at the end may have changed, so the bottom is wrapped again. The
    // view sets them, then asks keepAboveBottom().
    ScrollBarRanges updateScrollBarRanges( LineLength maxLineLength );

    // --- changes -------------------------------------------------------

    // The lines the view shows changed, as change says. Returns whether the
    // Scroll Position was past them and went back to the top: the view then
    // moves both scrollbars to 0. Before the view updates its scrollbars.
    //
    // Told that lines were only appended, the bottom of the Log File is found
    // from the lines added and the Visual Lines counted for the old bottom,
    // rather than by wrapping the end of the Log File again.
    bool dataChanged( LinesChange change = LinesChange::Any );
    // The text of the lines may have changed although their number did not,
    // as under another Encoding: nothing counted for them is kept.
    void linesReread();
    // The Viewport changed size, font or line numbers, which re-wraps it: the
    // Scroll Position keeps its line, and its Visual Line becomes the one
    // holding the character that was first on the top row. Returns whether the
    // view stood at the bottom before, to keep it there
    // (jumpToBottomIfFollowing()) once the scrollbars are updated.
    bool viewportChanged();
    // Text wrapping turned on or off. The line at the top stays, from its
    // first Visual Line. The view updates its scrollbars after.
    void setTextWrap( bool textWrap );

private:
    // Everything the bottom of the Log File depends on but its lines. A change
    // to those comes through updateScrollBarRanges(), which wraps the bottom
    // again.
    struct LogFileBottomKey {
        LinesCount totalLines{ 0 };
        int viewportWidth = -1;
        int viewportHeight = -1;
        int charWidth = -1;
        int charHeight = -1;
        bool textWrap = false;
        bool lineNumbersVisible = false;

        bool operator==( const LogFileBottomKey& ) const = default;
    };

    // The layout input without the drawing offset, which the pull-to-follow
    // geometry derives from the rest.
    ViewportLayoutInput layoutInput() const;

    ScrollAnswer answer( bool scrolled ) const;
    static void merge( ScrollAnswer& into, const ScrollAnswer& step );

    ScrollAnswer scrollByVisualLines( std::int64_t visualLines );
    // How many Visual Lines a wheel turn scrolls down (up when negative).
    std::int64_t wheelVisualLines( const WheelTurn& turn );
    ScrollAnswer landAtBottomOnMaximum( int sliderPosition, int value );

    std::size_t visualLineCount( LineNumber line, LineLength columns ) const;
    // position, with a Visual Line past the end of its line brought back to
    // that line's last.
    ScrollPosition withinLogLine( ScrollPosition position ) const;
    void rewrap();
    // Aligns the last Visual Line on the last row at the bottom Scroll
    // Position, the first on the top row otherwise.
    void updateAtBottom();

    int lineToScrollBar( LineNumber line ) const;
    LineNumber scrollBarToLine( int value ) const;
    double scrollBarMultiplicator() const;

    const ScrolledText& text_;
    PresentationPolicy presentationPolicy_;

    // Only scrolling moves it.
    ScrollPosition position_;
    // The text columns position_'s Visual Line was counted at, so that a
    // re-wrap finds the same character again.
    LineLength positionColumns_{ 0 };
    // The view stands at the bottom Scroll Position, and draws the last Visual
    // Line of the Log File on the Viewport's last row. Scrolling updates it,
    // so lines added below a view that is not following leave it as it is.
    bool atBottom_ = false;
    // The fraction of a Visual Line the wheel has turned but not yet scrolled.
    double wheelVisualLinesPending_ = 0;
    bool textWrap_;
    LineColumn firstColumn_{ 0 };
    // The maximum the vertical scrollbar was last given.
    int scrollBarMaximum_ = 0;

    bool follow_ = false;
    ElasticHook elasticHook_{ HookThreshold };

    mutable std::optional<LogFileBottom> logFileBottom_;
    mutable LogFileBottomKey logFileBottomKey_;

    // The Visual Lines of the lines from the bottom Scroll Position's line to
    // the end, as last counted, at the columns they were counted at.
    struct BottomLines {
        LineLength columns{ 0 };
        LineNumber first{ 0 };
        std::vector<std::size_t> visualLineCounts;

        LineNumber end() const
        {
            return first + LinesCount( visualLineCounts.size() );
        }
    };
    mutable std::optional<BottomLines> bottomLines_;
    // Lines were only appended since bottomLines_ was counted, and the
    // scrollbars were not updated since.
    bool linesOnlyAppended_ = false;
};
