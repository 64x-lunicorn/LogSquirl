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

// What the log view paints, verified pixel for pixel against golden images
// (#135). This is the safety net for changes to painting: anything that
// alters what is drawn -- a color, a margin, a glyph position, a bullet --
// turns it red.
//
// A pixel comparison is only as portable as its inputs, so every input
// painting reads is pinned here:
//
// - The font is not the host's. The test loads its own font (see
//   painting_test_font.h): fixed-width, 8 x 16 px, with every glyph edge on a
//   pixel boundary, so a glyph covers each pixel fully or not at all. Each
//   platform draws it in the rendering mode that keeps it that way, and so
//   each platform's rasteriser produces the same pixels.
//   Nothing needs to be installed on the host. If the platform cannot load
//   that font, or does not honour its metrics, the test fails and says so:
//   without its font it would verify nothing.
// - The palette, the frame, the scroll bars and the viewport size are set
//   explicitly, so no platform style leaks in. The margins -- the bullet zone
//   and the line numbers -- are drawn in the Tokens of the active Theme, which
//   is Light for the duration of the test.
// - The settings painting reads -- main search highlighting and its colors,
//   the QuickFind color, the active Highlighter Sets -- are set for the
//   duration of the test and restored afterwards. Whether scrolling may pull
//   the view into follow mode is not among them: the view is handed that in
//   its Presentation Policy, as the application hands it (#184).
//
// To accept a deliberate change to painting, run the test with
// LOGSQUIRL_UPDATE_PAINTING_GOLDENS set: it rewrites the golden images in
// the source tree instead of comparing, and the diff shows the new images
// for review. On a mismatch the image that was actually painted is written
// to the temporary directory, and the failure names the file.

#include <catch2/catch.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QFontInfo>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QScrollBar>
#include <QWheelEvent>

#include "abstractlogdata.h"
#include "abstractlogview.h"
#include "configuration.h"
#include "fake_log_data.h"
#include "highlighterset.h"
#include "painting_test_font.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "test_policies.h"
#include "theme.h"

namespace {

using paintingtestfont::PaintingTestDataDir;

constexpr int ViewWidth = 480;
constexpr int ViewHeight = 224;

using LineTypeFlags = AbstractLogData::LineTypeFlags;

// Log Lines with a Match, a Mark, a line that is both, Context Lines around
// them, and lines long enough to wrap at the width of the view.
struct PaintedLine {
    QString text;
    AbstractLogData::LineType type;
};

const std::vector<PaintedLine>& paintedLines()
{
    static const std::vector<PaintedLine> lines = {
        { "10:00:00 INFO  service started", LineTypeFlags::Plain },
        { "10:00:01 DEBUG opening connection pool", LineTypeFlags::Context },
        { "10:00:02 ERROR connection refused by upstream", LineTypeFlags::Match },
        { "10:00:03 DEBUG retrying in 5 s", LineTypeFlags::Context },
        { "10:00:04 WARN  a long line that keeps going past the right edge of the view, so "
          "that text wrapping has to split it into several Visual Lines",
          LineTypeFlags::Mark },
        { "10:00:05 ERROR marked and matched at once", LineTypeFlags::Mark | LineTypeFlags::Match },
        { "10:00:06 INFO  recovered", LineTypeFlags::Plain },
        { "10:00:07 ERROR another failure, this one also long enough to be wrapped onto a "
          "second Visual Line",
          LineTypeFlags::Match },
        { "10:00:08 INFO  done", LineTypeFlags::Plain },
        { "10:00:09 INFO  idle", LineTypeFlags::Plain },
        { "10:00:10 INFO  idle", LineTypeFlags::Plain },
        { "10:00:11 INFO  idle", LineTypeFlags::Plain },
        { "10:00:12 INFO  idle", LineTypeFlags::Plain },
        { "10:00:13 INFO  idle", LineTypeFlags::Plain },
        { "10:00:14 INFO  idle", LineTypeFlags::Plain },
        // Below the first screen: enough Log Lines that a Scroll Position on
        // Log Line 4 is in the middle of the Log File, not at its bottom.
        { "10:00:15 INFO  idle", LineTypeFlags::Plain },
        { "10:00:16 INFO  idle", LineTypeFlags::Plain },
        { "10:00:17 INFO  idle", LineTypeFlags::Plain },
        { "10:00:18 INFO  idle", LineTypeFlags::Plain },
        { "10:00:19 INFO  idle", LineTypeFlags::Plain },
        { "10:00:20 INFO  idle", LineTypeFlags::Plain },
        { "10:00:21 INFO  idle", LineTypeFlags::Plain },
        { "10:00:22 INFO  idle", LineTypeFlags::Plain },
        { "10:00:23 INFO  idle", LineTypeFlags::Plain },
        { "10:00:24 INFO  idle", LineTypeFlags::Plain },
        { "10:00:25 INFO  idle", LineTypeFlags::Plain },
        { "10:00:26 INFO  idle", LineTypeFlags::Plain },
        { "10:00:27 INFO  idle", LineTypeFlags::Plain },
        { "10:00:28 INFO  idle", LineTypeFlags::Plain },
        { "10:00:29 INFO  idle", LineTypeFlags::Plain },
        // The end of the Log File: Log Lines that wrap, so the bottom Scroll
        // Position is counted in Visual Lines.
        { "10:00:30 ERROR the last failure, long enough to be wrapped onto a second Visual Line "
          "at the bottom",
          LineTypeFlags::Match },
        { "10:00:31 INFO  shutting down: the last Log Line of the file, which wraps as well",
          LineTypeFlags::Plain },
    };
    return lines;
}

// The texts of the first count painted Log Lines, or of all of them.
QStringList paintedTexts( std::optional<size_t> count = std::nullopt )
{
    QStringList texts;
    for ( const auto& line : paintedLines() ) {
        if ( count.has_value() && static_cast<size_t>( texts.size() ) == *count ) {
            break;
        }
        texts << line.text;
    }
    return texts;
}

// Every Log Line at its own position, a Match or a Mark as paintedLines() says,
// unless a test has made it something else. It remembers which Log Lines the
// view asked about: a view decorating a Log Line asks what it is.
class PaintedLineTypes : public EveryLogLine {
public:
    using EveryLogLine::EveryLogLine;

    LineType lineType( LineNumber lineNumber ) const override
    {
        asked.push_back( lineNumber );
        if ( const auto changed = changedTypes.find( lineNumber.get() );
             changed != changedTypes.end() ) {
            return changed->second;
        }
        const auto& lines = paintedLines();
        return lineNumber.get() < lines.size() ? lines[ lineNumber.get() ].type
                                               : AbstractLogData::LineType{};
    }

    // How many Log Lines the Log File holds, unless it is the view's lines:
    // a Filtered View's Log File grows while its Displayed Lines stay.
    LinesCount logLineCount() const override
    {
        return logFileLineCount.value_or( EveryLogLine::logLineCount() );
    }

    mutable std::vector<LineNumber> asked;
    std::map<LineNumber::UnderlyingType, LineType> changedTypes;
    std::optional<LinesCount> logFileLineCount;
};

class PaintingLogView : public AbstractLogView {
public:
    PaintingLogView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern,
                     bool textWrap )
        : PaintingLogView( logData, std::make_unique<PaintedLineTypes>( logData ), quickFindPattern,
                           textWrap )
    {
    }

    // What the view was handed to tell what each Log Line is.
    PaintedLineTypes& lineTypes()
    {
        return *lineTypes_;
    }

private:
    PaintingLogView( const AbstractLogData* logData, std::unique_ptr<PaintedLineTypes> lineTypes,
                     const QuickFindPattern* quickFindPattern, bool textWrap )
        : AbstractLogView( logData, std::move( lineTypes ), quickFindPattern, textWrap )
    {
        lineTypes_ = dynamic_cast<PaintedLineTypes*>(
            const_cast<LineMapping*>( &AbstractLogView::lineMapping() ) );
    }

    PaintedLineTypes* lineTypes_ = nullptr;
};

// The Highlighter Sets a developer has configured, kept out of the images for
// as long as this object lives and restored when it goes; the Theme the
// margins are drawn in is Light for as long. The Highlighter Sets are the
// user's own coloring, read from the collection as the view paints, and no
// Settings Policy carries them -- which is why they are pinned here and the
// settings that color Log Lines are not: those reach the view as its
// Decoration Policy (see showForPainting()).
class PinnedPaintingSettings {
public:
    PinnedPaintingSettings()
        : activeHighlighterSets_( HighlighterSetCollection::get().activeSetIds() )
        , activeTheme_( Theme::active().name() )
    {
        HighlighterSetCollection::get().deactivateAll();
        // Applied only when another test left a different Theme active:
        // applying one also installs the application stylesheet.
        if ( activeTheme_ != Theme::LightKey ) {
            Theme::apply( Theme::LightKey );
        }
    }

    ~PinnedPaintingSettings()
    {
        for ( const auto& setId : activeHighlighterSets_ ) {
            HighlighterSetCollection::get().activateSet( setId );
        }
        if ( activeTheme_ != Theme::LightKey ) {
            Theme::apply( activeTheme_ );
        }
    }

    PinnedPaintingSettings( const PinnedPaintingSettings& ) = delete;
    PinnedPaintingSettings& operator=( const PinnedPaintingSettings& ) = delete;

private:
    QStringList activeHighlighterSets_;
    QString activeTheme_;
};

QPalette fixedPalette()
{
    QPalette palette;
    palette.setColor( QPalette::Window, QColor{ 240, 240, 240 } );
    palette.setColor( QPalette::Base, Qt::white );
    palette.setColor( QPalette::Text, Qt::black );
    palette.setColor( QPalette::Highlight, QColor{ 48, 140, 198 } );
    palette.setColor( QPalette::HighlightedText, Qt::white );
    palette.setColor( QPalette::Disabled, QPalette::Text, QColor{ 150, 150, 150 } );
    return palette;
}

struct PaintingConfiguration {
    bool textWrap = false;
    bool lineNumbersVisible = false;
    // Where the view is scrolled to before it is painted.
    ScrollPosition scrollPosition{};
    // Scrolled to the scrollbar's maximum instead: the bottom Scroll Position.
    bool atScrollbarMaximum = false;
    int viewHeight = ViewHeight;
    // How many of the painted Log Lines the Log File holds; all of them if unset.
    std::optional<size_t> logLineCount{};
    // How far the view is pulled past its bottom once it is scrolled, in
    // wheel pixels (see pullPastTheBottom()).
    int pullPx = 0;
};

// Shows the view the way every painting test paints it.
void showForPainting( PaintingLogView& view, const FakeLogData& logData, const QFont& font,
                      const PaintingConfiguration& configuration )
{
    view.setFrameShape( QFrame::NoFrame );
    view.setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view.setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view.setPalette( fixedPalette() );
    view.resize( ViewWidth, configuration.viewHeight );
    view.show();
    QCoreApplication::processEvents();

    // What the application hands a view it builds: the view reads no setting
    // of its own, so every input to these images is a literal here. These are
    // the colors the golden images were drawn with.
    view.setDecorationPolicy( DecorationPolicy{ .mainSearchHighlight = true,
                                                .variateMainSearchHighlight = false,
                                                .mainSearchBackColor = QColor{ 255, 200, 0 },
                                                .quickFindBackColor = QColor{ Qt::yellow } } );

    // Pulling the view past its bottom into follow mode is part of what these
    // images show, so the Policy allowing that is handed over here.
    auto presentationPolicy = testSettingsPolicies().presentation;
    presentationPolicy.allowFollowOnScroll = true;
    view.setPresentationPolicy( presentationPolicy );

    view.updateFont( font );
    view.setLineNumbersVisible( configuration.lineNumbersVisible );
    view.setSearchPattern( RegularExpressionPattern{ QStringLiteral( "ERROR" ) } );
    view.setSearchLimits( 0_lnum, LineNumber( logData.getNbLine().get() ) );
    view.updateData();

    // The view must actually be painting with the test font; a platform
    // that substituted another one would produce images of that font.
    INFO( "The view resolved the font to \"" << QFontInfo( view.font() ).family().toStdString()
                                             << "\"" );
    REQUIRE( QFontInfo( view.font() ).family() == font.family() );
    REQUIRE( view.viewport()->size() == QSize( ViewWidth, configuration.viewHeight ) );
}

// Pulls the view at its bottom further down by pixels, the way a trackpad
// does: through a wheel event, and nothing else. The scroll begins with the
// finger on the pad, which holds the elastic hook -- so the pull stays where
// it is while the view is painted, rather than springing back at whatever
// pace the host runs the hook's timer.
void pullPastTheBottom( AbstractLogView& view, int pixels )
{
    const auto before = view.scrollPosition();

    const QPointF inside{ ViewWidth / 2.0, 8.0 };
    QWheelEvent wheel( inside, view.viewport()->mapToGlobal( inside ), QPoint{ 0, -pixels },
                       QPoint{ 0, -pixels }, Qt::NoButton, Qt::NoModifier, Qt::ScrollBegin, false );
    QCoreApplication::sendEvent( view.viewport(), &wheel );

    // A pull moves the pull-to-follow bar and the text with it, never the
    // Scroll Position.
    REQUIRE( view.scrollPosition() == before );
}

QImage grabViewport( AbstractLogView& view )
{
    return view.viewport()->grab().toImage().convertToFormat( QImage::Format_ARGB32 );
}

QImage paintLogView( const QFont& font, PaintingConfiguration configuration )
{
    const FakeLogData logData{ paintedTexts( configuration.logLineCount ) };
    const QuickFindPattern quickFindPattern;

    PaintingLogView view( &logData, &quickFindPattern, configuration.textWrap );
    showForPainting( view, logData, font, configuration );

    if ( configuration.atScrollbarMaximum ) {
        view.verticalScrollBar()->setValue( view.verticalScrollBar()->maximum() );
    }
    else {
        // The scrollbar lands on the Log Line, a step of the arrow key moves one
        // Visual Line further.
        view.verticalScrollBar()->setValue(
            static_cast<int>( configuration.scrollPosition.lineNumber.get() ) );
        for ( size_t step = 0; step < configuration.scrollPosition.visualLineIndex; ++step ) {
            QKeyEvent down( QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier );
            QCoreApplication::sendEvent( &view, &down );
        }
        REQUIRE( view.scrollPosition() == configuration.scrollPosition );
    }

    if ( configuration.pullPx > 0 ) {
        pullPastTheBottom( view, configuration.pullPx );
    }

    return grabViewport( view );
}

std::optional<QString> firstDifference( const QImage& golden, const QImage& actual )
{
    if ( golden.size() != actual.size() ) {
        return QStringLiteral( "the golden image is %1x%2, the painted one %3x%4" )
            .arg( golden.width() )
            .arg( golden.height() )
            .arg( actual.width() )
            .arg( actual.height() );
    }

    int differingPixels = 0;
    std::optional<QPoint> first;
    for ( int y = 0; y < golden.height(); ++y ) {
        for ( int x = 0; x < golden.width(); ++x ) {
            if ( golden.pixel( x, y ) != actual.pixel( x, y ) ) {
                ++differingPixels;
                if ( !first ) {
                    first = QPoint( x, y );
                }
            }
        }
    }

    if ( !first ) {
        return std::nullopt;
    }

    return QStringLiteral( "%1 pixels differ, the first at (%2, %3): golden #%4, painted #%5" )
        .arg( differingPixels )
        .arg( first->x() )
        .arg( first->y() )
        .arg( golden.pixel( *first ), 8, 16, QLatin1Char( '0' ) )
        .arg( actual.pixel( *first ), 8, 16, QLatin1Char( '0' ) );
}

void requirePaintingMatchesGolden( PaintingConfiguration configuration, const QString& name )
{
    const PinnedPaintingSettings settings;
    const auto font = paintingtestfont::requirePaintingTestFont();

    const auto painted = paintLogView( font, configuration );
    const auto goldenPath
        = PaintingTestDataDir + QStringLiteral( "/" ) + name + QStringLiteral( ".png" );

    if ( qEnvironmentVariableIsSet( "LOGSQUIRL_UPDATE_PAINTING_GOLDENS" ) ) {
        REQUIRE( painted.save( goldenPath ) );
        WARN( "Updated golden image " << goldenPath.toStdString() );
        return;
    }

    const QImage golden = QImage( goldenPath ).convertToFormat( QImage::Format_ARGB32 );
    INFO( "Golden image " << goldenPath.toStdString() );
    REQUIRE_FALSE( golden.isNull() );

    const auto difference = firstDifference( golden, painted );
    if ( difference ) {
        const auto paintedPath = QDir::temp().filePath( QStringLiteral( "logsquirl-painting-" )
                                                        + name + QStringLiteral( "-actual.png" ) );
        painted.save( paintedPath );
        FAIL( "Painting differs from the golden image: " << difference->toStdString()
                                                         << ". The painted image is at "
                                                         << paintedPath.toStdString() );
    }
}

} // namespace

SCENARIO( "The log view paints exactly what it painted before", "[logviewpainting]" )
{
    GIVEN( "Log Lines with Marks, Matches and Context Lines in a fixed-width font" )
    {
        WHEN( "text wrapping is off and line numbers are hidden" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden( { .textWrap = false, .lineNumbersVisible = false },
                                              QStringLiteral( "unwrapped" ) );
            }
        }

        WHEN( "text wrapping is off and line numbers are shown" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden( { .textWrap = false, .lineNumbersVisible = true },
                                              QStringLiteral( "unwrapped-line-numbers" ) );
            }
        }

        WHEN( "text wrapping is on and line numbers are hidden" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden( { .textWrap = true, .lineNumbersVisible = false },
                                              QStringLiteral( "wrapped" ) );
            }
        }

        WHEN( "text wrapping is on and line numbers are shown" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden( { .textWrap = true, .lineNumbersVisible = true },
                                              QStringLiteral( "wrapped-line-numbers" ) );
            }
        }

        // Log Line 4 wraps into three Visual Lines; the view shows it from its
        // second. Its bullet and line number sit beside its first Visual Line,
        // above the Viewport, so the top row has neither.
        WHEN( "the Scroll Position is partway through a wrapped Log Line and line numbers are "
              "hidden" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden( { .textWrap = true,
                                                .lineNumbersVisible = false,
                                                .scrollPosition = ScrollPosition{ 4_lnum, 1 } },
                                              QStringLiteral( "wrapped-partway" ) );
            }
        }

        WHEN( "the Scroll Position is partway through a wrapped Log Line and line numbers are "
              "shown" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden( { .textWrap = true,
                                                .lineNumbersVisible = true,
                                                .scrollPosition = ScrollPosition{ 4_lnum, 1 } },
                                              QStringLiteral( "wrapped-partway-line-numbers" ) );
            }
        }

        // The last two Log Lines wrap into two Visual Lines each. 224 px is 14
        // rows of 16 px, and the last Visual Line of the Log File is on the
        // last of them.
        WHEN( "the view is at the scrollbar's maximum and the last Log Lines wrap" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden(
                    { .textWrap = true, .lineNumbersVisible = false, .atScrollbarMaximum = true },
                    QStringLiteral( "wrapped-bottom" ) );
            }
        }

        // 232 px is 14 rows and half a row. At the bottom the text is drawn 8 px
        // up, so the last Visual Line ends at the bottom of the Viewport and the
        // one on the top row is cut in half.
        WHEN( "the view is at the scrollbar's maximum with a partly visible row and line numbers "
              "shown" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden(
                    { .textWrap = true,
                      .lineNumbersVisible = true,
                      .atScrollbarMaximum = true,
                      .viewHeight = ViewHeight + 8 },
                    QStringLiteral( "wrapped-bottom-partial-row-line-numbers" ) );
            }
        }
    }
}

// Where the pull-to-follow bar is drawn, and the text with it (#149). The
// arithmetic is pinned in viewportlayout_test.cpp; these see it painted.
// Every pull is a wheel event at the bottom Scroll Position. The elastic
// hook turns 14 units of pull into a pixel of bar, and hooks at 300.
SCENARIO( "The log view paints the pull-to-follow bar where it places the text",
          "[logviewpainting][pulltofollow]" )
{
    GIVEN( "Log Lines with Marks, Matches and Context Lines in a fixed-width font" )
    {
        // The Viewport's 14 rows hold exactly the Visual Lines down to the
        // end of the Log File. A pull of 112 is 8 px: the text moves 8 px up,
        // and the bar fills the 8 px below its last Visual Line.
        WHEN( "the view at its bottom is pulled without hooking" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden(
                    { .textWrap = true, .atScrollbarMaximum = true, .pullPx = 112 },
                    QStringLiteral( "pull-elastic" ) );
            }
        }

        // 232 px: the last Log Line is aligned on the Viewport's last row,
        // with the top row cut in half. The pull moves the aligned text
        // another 8 px up, and the bar starts right below the last Visual Line.
        WHEN( "the view with its last Log Line aligned on a partly visible row is pulled without "
              "hooking" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden( { .textWrap = true,
                                                .lineNumbersVisible = true,
                                                .atScrollbarMaximum = true,
                                                .viewHeight = ViewHeight + 8,
                                                .pullPx = 112 },
                                              QStringLiteral( "pull-elastic-aligned" ) );
            }
        }

        // A pull of exactly 300 hooks and leaves no elastic length. The hooked
        // bar takes the half row the last Visual Line reached below the
        // Viewport plus 10 px: the text moves up 18 px, and the bar shows in
        // the Viewport's last 10 px, right below the last Visual Line.
        WHEN( "the view at its bottom is pulled until the elastic hooks" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden( { .textWrap = true,
                                                .lineNumbersVisible = true,
                                                .atScrollbarMaximum = true,
                                                .viewHeight = ViewHeight + 8,
                                                .pullPx = 300 },
                                              QStringLiteral( "pull-hooked" ) );
            }
        }

        // Five Log Lines, seven Visual Lines: fewer than the 14 rows. The
        // Log File stays at the top, moved up only by the 5 px left of a pull
        // of 370 once it hooked, and the hooked bar sits at the bottom of the
        // Viewport.
        WHEN( "a Log File shorter than a screenful is pulled until the elastic hooks" )
        {
            THEN( "the view matches its golden image" )
            {
                requirePaintingMatchesGolden( { .textWrap = true,
                                                .lineNumbersVisible = true,
                                                .logLineCount = 5,
                                                .pullPx = 370 },
                                              QStringLiteral( "pull-hooked-short-file" ) );
            }
        }
    }
}

// The Search Limits are half-open, from the first Log Line searched up to the
// Log Line after the last one, as the Presentations hold them. The Table
// View's delegate test subdues the same Log Lines for the same limits (#232).
namespace {

// Whether the view, unwrapped and scrolled to the top, draws the given Log
// Line subdued: its text in the palette's disabled text color. Only the
// text is looked at -- the separator beside the bullets is drawn in that
// color on every row.
bool isLogLineSubdued( const QImage& painted, LineNumber logLine )
{
    const QColor subdued = fixedPalette().color( QPalette::Disabled, QPalette::Text );
    // The image is in device pixels; the rows and columns are logical ones.
    const auto scale = painted.devicePixelRatio();
    const auto toDevice = [ scale ]( int logical ) { return static_cast<int>( logical * scale ); };
    const int top = static_cast<int>( logLine.get() ) * paintingtestfont::CharHeight;
    for ( int y = toDevice( top ); y < toDevice( top + paintingtestfont::CharHeight ); ++y ) {
        for ( int x = toDevice( 40 ); x < toDevice( 120 ); ++x ) {
            if ( painted.pixel( x, y ) == subdued.rgb() ) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

SCENARIO( "The log view subdues exactly the Log Lines outside the Search Limits",
          "[logviewpainting][searchlimits]" )
{
    const PinnedPaintingSettings settings;
    const auto font = paintingtestfont::requirePaintingTestFont();

    GIVEN( "Search Limits from Log Line 8 up to, not including, Log Line 12" )
    {
        const FakeLogData logData{ paintedTexts() };
        const QuickFindPattern quickFindPattern;
        PaintingLogView view( &logData, &quickFindPattern, false );
        showForPainting( view, logData, font, {} );

        view.setSearchLimits( 8_lnum, 12_lnum );
        const auto painted = grabViewport( view );

        THEN( "the Log Line before the first one searched is subdued" )
        {
            REQUIRE( isLogLineSubdued( painted, 7_lnum ) );
        }

        THEN( "the first Log Line searched is not subdued" )
        {
            REQUIRE_FALSE( isLogLineSubdued( painted, 8_lnum ) );
        }

        THEN( "the last Log Line searched is not subdued" )
        {
            REQUIRE_FALSE( isLogLineSubdued( painted, 11_lnum ) );
        }

        THEN( "the Log Line directly after the end is subdued" )
        {
            REQUIRE( isLogLineSubdued( painted, 12_lnum ) );
        }
    }
}

namespace {

// A FakeLogData that counts how often Log Lines are fetched.
class CountingLogData : public FakeLogData {
public:
    using FakeLogData::FakeLogData;

    // Every fetch of Log Lines.
    mutable int linesFetched = 0;
    // The fetches of the Log Lines at the top of the Log File, where the
    // views these tests count stand. Scrolling reads Log Lines of its own at
    // the end of the Log File, to find its bottom.
    mutable int topLinesFetched = 0;

protected:
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        ++linesFetched;
        if ( first == 0_lnum ) {
            ++topLinesFetched;
        }
        return FakeLogData::doGetLines( first, count );
    }
};

} // namespace

SCENARIO( "The log view expands and wraps a viewport once per change", "[logviewpainting]" )
{
    const PinnedPaintingSettings settings;

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a painted view with text wrapping " << ( textWrap ? "on" : "off" ) )
        {
            const CountingLogData logData{ paintedTexts() };
            const QuickFindPattern quickFindPattern;

            PaintingLogView view( &logData, &quickFindPattern, textWrap );
            view.setFrameShape( QFrame::NoFrame );
            view.resize( ViewWidth, ViewHeight );
            view.show();
            QCoreApplication::processEvents();
            view.updateData();
            view.viewport()->grab();

            WHEN( "the Log File changes, and the view is painted and hovered over" )
            {
                logData.linesFetched = 0;
                view.rereadLogLines();
                view.viewport()->grab();

                const QPoint overText{ ViewWidth / 2, ViewHeight / 2 };
                QMouseEvent hover( QEvent::MouseMove, overText,
                                   view.viewport()->mapToGlobal( overText ), Qt::NoButton,
                                   Qt::NoButton, Qt::NoModifier );
                QCoreApplication::sendEvent( view.viewport(), &hover );

                THEN( "painting and hit testing read the Log Lines of one expansion" )
                {
                    REQUIRE( logData.linesFetched == 1 );
                }
            }
        }
    }
}

// A view tells a change of Decoration from a change of text (#295). What only
// decorates the Log Lines in the Viewport -- QuickFind typing, the Search
// pattern and its progress, Color Labels, the Decoration Policy, the Search
// Limits, Marks -- is repainted from the text the view has already read;
// only what changes the text -- a reload, another Encoding, Log Lines
// appended -- reads the Log Lines again.
SCENARIO( "The log view repaints a changed Decoration without reading the Log Lines again",
          "[logviewpainting]" )
{
    const PinnedPaintingSettings settings;
    const auto font = paintingtestfont::requirePaintingTestFont();

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a view painted with text wrapping " << ( textWrap ? "on" : "off" ) )
        {
            CountingLogData logData{ paintedTexts() };
            QuickFindPattern quickFindPattern;
            PaintingLogView view( &logData, &quickFindPattern, textWrap );
            showForPainting( view, logData, font, { .textWrap = textWrap } );
            const auto before = grabViewport( view );
            logData.linesFetched = 0;
            logData.topLinesFetched = 0;

            WHEN( "a QuickFind pattern is typed character by character" )
            {
                std::optional<QImage> painted;
                for ( const auto* typed : { "i", "id", "idl", "idle" } ) {
                    quickFindPattern.changeSearchPattern( QString::fromLatin1( typed ),
                                                          /* useExtendedRegexp */ false );
                    painted = grabViewport( view );
                }

                THEN( "each keystroke is painted from the Log Lines already read" )
                {
                    REQUIRE( logData.linesFetched == 0 );
                    REQUIRE( painted != before );
                }
            }

            WHEN( "the Search pattern changes" )
            {
                view.setSearchPattern( RegularExpressionPattern{ QStringLiteral( "INFO" ) } );
                const auto painted = grabViewport( view );

                THEN( "it is painted from the Log Lines already read" )
                {
                    REQUIRE( logData.linesFetched == 0 );
                    REQUIRE( painted != before );
                }
            }

            WHEN( "the Color Labels change" )
            {
                auto colorLabels = std::vector<AbstractLogView::QuickHighlighters>( 9 );
                colorLabels[ 0 ] << QStringLiteral( "idle" );
                view.setQuickHighlighters( colorLabels );
                grabViewport( view );

                THEN( "they are painted from the Log Lines already read" )
                {
                    REQUIRE( logData.linesFetched == 0 );
                }
            }

            WHEN( "the Decoration Policy changes" )
            {
                view.setDecorationPolicy( DecorationPolicy{ .mainSearchHighlight = false } );
                grabViewport( view );

                THEN( "it is painted from the Log Lines already read" )
                {
                    REQUIRE( logData.linesFetched == 0 );
                }
            }

            WHEN( "the Search Limits change" )
            {
                view.setSearchLimits( 2_lnum, 6_lnum );
                grabViewport( view );

                THEN( "they are painted from the Log Lines already read" )
                {
                    REQUIRE( logData.linesFetched == 0 );
                }
            }

            WHEN( "Matches or Marks change, as a Search progresses or a Mark is set" )
            {
                view.updateDecorations();
                grabViewport( view );

                THEN( "they are painted from the Log Lines already read" )
                {
                    REQUIRE( logData.linesFetched == 0 );
                }
            }

            WHEN( "the Log File is reloaded" )
            {
                view.updateData();
                grabViewport( view );

                THEN( "the Log Lines in the Viewport are read again, once" )
                {
                    REQUIRE( logData.topLinesFetched == 1 );
                }
            }

            WHEN( "the Log Lines are to be read again, as after a change of Encoding" )
            {
                view.rereadLogLines();
                grabViewport( view );

                THEN( "the Log Lines in the Viewport are read again, once" )
                {
                    REQUIRE( logData.topLinesFetched == 1 );
                }
            }

            WHEN( "Log Lines are appended" )
            {
                logData.setLines( paintedTexts() << QStringLiteral( "10:00:32 INFO  appended" ) );
                view.updateData();
                grabViewport( view );

                THEN( "the Log Lines in the Viewport are read again, once" )
                {
                    REQUIRE( logData.topLinesFetched == 1 );
                }
            }
        }
    }
}

// Painting draws from the cached viewport content (#137), which a change to
// the text of a Log Line does not invalidate by itself: the Log File's line
// count stays the same. Every change to a Log File reaches the view through
// updateData() or rereadLogLines(), and after either the new text is painted.
SCENARIO( "The log view paints a Log Line's new text when its line count stays the same",
          "[logviewpainting]" )
{
    const PinnedPaintingSettings settings;
    const auto font = paintingtestfont::requirePaintingTestFont();

    auto changedTexts = paintedTexts();
    changedTexts[ 0 ] = QStringLiteral( "10:00:00 INFO  service stopped" );
    changedTexts[ 4 ] = changedTexts[ 4 ].toUpper();
    REQUIRE( changedTexts.size() == paintedTexts().size() );

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a view painted with text wrapping " << ( textWrap ? "on" : "off" ) )
        {
            const PaintingConfiguration configuration{ .textWrap = textWrap,
                                                       .lineNumbersVisible = true };

            FakeLogData logData{ paintedTexts() };
            const QuickFindPattern quickFindPattern;
            PaintingLogView view( &logData, &quickFindPattern, textWrap );
            showForPainting( view, logData, font, configuration );
            const auto before = grabViewport( view );

            // What a view shows when it was given the new text from the start.
            const FakeLogData changedLogData{ changedTexts };
            PaintingLogView freshView( &changedLogData, &quickFindPattern, textWrap );
            showForPainting( freshView, changedLogData, font, configuration );
            const auto expected = grabViewport( freshView );
            REQUIRE( expected != before );

            WHEN( "the text of Log Lines changes and the view is told through updateData()" )
            {
                logData.setLines( changedTexts );
                view.updateData();

                THEN( "it paints the new text" )
                {
                    REQUIRE( firstDifference( expected, grabViewport( view ) ) == std::nullopt );
                }
            }

            WHEN( "the text of Log Lines changes and the view is told through rereadLogLines()" )
            {
                logData.setLines( changedTexts );
                view.rereadLogLines();

                THEN( "it paints the new text" )
                {
                    REQUIRE( firstDifference( expected, grabViewport( view ) ) == std::nullopt );
                }
            }
        }
    }
}

// Scrolling repaints only what it exposes (#296): a small vertical scroll
// without text wrapping moves what the Viewport already painted and draws
// only the Visual Lines that came into view, and the Log Lines already read
// and decorated for the Viewport are kept, with or without text wrapping.
// Whatever it saves, the view must look exactly as if it had painted
// everything anew.
//
// ctest also runs these at a device pixel ratio of 2 (QT_SCALE_FACTOR=2),
// where moving what was painted is counted in device pixels.
namespace {

// Moves the view to position the way a user can reach it: the scrollbar to
// its Log Line, then the arrow key down to its Visual Line.
void scrollViewTo( AbstractLogView& view, ScrollPosition position )
{
    view.verticalScrollBar()->setValue( static_cast<int>( position.lineNumber.get() ) );
    for ( size_t step = 0; step < position.visualLineIndex; ++step ) {
        QKeyEvent down( QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier );
        QCoreApplication::sendEvent( &view, &down );
    }
    REQUIRE( view.scrollPosition() == position );
}

void pressKey( AbstractLogView& view, Qt::Key key )
{
    QKeyEvent press( QEvent::KeyPress, key, Qt::NoModifier );
    QCoreApplication::sendEvent( &view, &press );
}

void turnWheel( AbstractLogView& view, int angleDeltaY )
{
    const QPointF inside{ ViewWidth / 2.0, 8.0 };
    QWheelEvent wheel( inside, view.viewport()->mapToGlobal( inside ), QPoint{},
                       QPoint{ 0, angleDeltaY }, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                       false );
    QCoreApplication::sendEvent( view.viewport(), &wheel );
}

void clickLogLine( AbstractLogView& view, LineNumber logLine, Qt::KeyboardModifiers modifiers )
{
    const auto rect = view.viewportLayout().rectForLine( logLine );
    REQUIRE( rect.height > 0 );
    const QPointF onText{ ViewWidth / 2.0, rect.y + paintingtestfont::CharHeight / 2.0 };
    const auto global = view.viewport()->mapToGlobal( onText );
    QMouseEvent press( QEvent::MouseButtonPress, onText, global, Qt::LeftButton, Qt::LeftButton,
                       modifiers );
    QCoreApplication::sendEvent( view.viewport(), &press );
    QMouseEvent release( QEvent::MouseButtonRelease, onText, global, Qt::LeftButton, Qt::NoButton,
                         modifiers );
    QCoreApplication::sendEvent( view.viewport(), &release );
}

// Every source of color at once: Matches and Marks (paintedLines()), the
// Search pattern (showForPainting()), a QuickFind pattern, a Color Label and
// a selection of three Log Lines, made from the top of the Log File.
void decorateEverything( AbstractLogView& view, QuickFindPattern& quickFindPattern )
{
    quickFindPattern.changeSearchPattern( QStringLiteral( "retry|idle" ),
                                          /* useExtendedRegexp */ true );
    auto colorLabels = std::vector<AbstractLogView::QuickHighlighters>( 9 );
    colorLabels[ 1 ] << QStringLiteral( "INFO" );
    view.setQuickHighlighters( colorLabels );

    clickLogLine( view, 8_lnum, Qt::NoModifier );
    clickLogLine( view, 10_lnum, Qt::ShiftModifier );
    REQUIRE( view.scrollPosition() == ScrollPosition{} );
}

// What a view decorated like view paints at its Scroll Position when it
// paints everything anew. It has a QuickFind pattern of its own: typing into
// the one view shares would repaint that view from scratch as well.
QImage repaintedFromScratch( const AbstractLogView& view, const QFont& font, bool textWrap,
                             const FakeLogData& logData )
{
    QuickFindPattern quickFindPattern;
    PaintingLogView fresh( &logData, &quickFindPattern, textWrap );
    showForPainting( fresh, logData, font, { .textWrap = textWrap } );
    decorateEverything( fresh, quickFindPattern );
    scrollViewTo( fresh, view.scrollPosition() );
    // Nothing kept from before: the Log Lines read again, every row painted.
    fresh.rereadLogLines();
    return grabViewport( fresh );
}

void requireSameImage( const QImage& expected, const QImage& painted, const std::string& step )
{
    INFO( "after " << step << ", at a device pixel ratio of " << painted.devicePixelRatio() );
    const auto difference = firstDifference( expected, painted );
    if ( difference ) {
        const auto paintedPath
            = QDir::temp().filePath( QStringLiteral( "logsquirl-scrolled-actual.png" ) );
        const auto expectedPath
            = QDir::temp().filePath( QStringLiteral( "logsquirl-scrolled-expected.png" ) );
        painted.save( paintedPath );
        expected.save( expectedPath );
        FAIL( "Scrolling painted other pixels than a full repaint: "
              << difference->toStdString() << ". Painted " << paintedPath.toStdString()
              << ", expected " << expectedPath.toStdString() );
    }
}

} // namespace

SCENARIO( "A scrolled log view paints exactly what a full repaint paints",
          "[logviewpainting][scrollrepaint]" )
{
    const PinnedPaintingSettings settings;
    const auto font = paintingtestfont::requirePaintingTestFont();

    if ( qEnvironmentVariableIsSet( "LOGSQUIRL_EXPECT_DEVICE_PIXEL_RATIO" ) ) {
        // The run that is meant to paint at a higher device pixel ratio must
        // actually do so, or it verifies nothing new.
        const QWidget probe;
        REQUIRE( probe.devicePixelRatio()
                 == qEnvironmentVariable( "LOGSQUIRL_EXPECT_DEVICE_PIXEL_RATIO" ).toDouble() );
    }

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a view with Matches, Marks, a Search, QuickFind, a Color Label and a selection, "
               "text wrapping "
               << ( textWrap ? "on" : "off" ) )
        {
            const FakeLogData logData{ paintedTexts() };
            QuickFindPattern quickFindPattern;
            PaintingLogView view( &logData, &quickFindPattern, textWrap );
            showForPainting( view, logData, font, { .textWrap = textWrap } );
            decorateEverything( view, quickFindPattern );
            grabViewport( view );

            WHEN( "it is scrolled in small steps, each painted" )
            {
                const std::vector<std::pair<std::string, std::function<void()>>> steps = {
                    { "a key down", [ & ] { pressKey( view, Qt::Key_Down ); } },
                    { "another key down", [ & ] { pressKey( view, Qt::Key_Down ); } },
                    { "a notch of the wheel down", [ & ] { turnWheel( view, -120 ); } },
                    { "a key up", [ & ] { pressKey( view, Qt::Key_Up ); } },
                    { "two notches of the wheel down",
                      [ & ] {
                          turnWheel( view, -120 );
                          turnWheel( view, -120 );
                      } },
                    { "a key down past the selection", [ & ] { pressKey( view, Qt::Key_Down ); } },
                    { "a notch of the wheel up", [ & ] { turnWheel( view, 120 ); } },
                    { "the scrollbar two Log Lines down",
                      [ & ] {
                          view.verticalScrollBar()->setValue( view.verticalScrollBar()->value()
                                                              + 2 );
                      } },
                    { "a page down", [ & ] { pressKey( view, Qt::Key_PageDown ); } },
                    { "a key up at the bottom", [ & ] { pressKey( view, Qt::Key_Up ); } },
                };

                THEN( "after every step it paints what a full repaint paints" )
                {
                    auto before = view.scrollPosition();
                    for ( const auto& [ name, step ] : steps ) {
                        step();
                        const auto painted = grabViewport( view );
                        INFO( "Scroll Position " << view.scrollPosition().lineNumber.get() << ":"
                                                 << view.scrollPosition().visualLineIndex );
                        requireSameImage( repaintedFromScratch( view, font, textWrap, logData ),
                                          painted, name );
                        REQUIRE( view.scrollPosition() != before );
                        before = view.scrollPosition();
                    }
                }
            }

            WHEN( "a Log Line below the Viewport is selected, which scrolls it into view" )
            {
                // Log Lines 8 to 21 fill the Viewport's 14 whole rows; Log Line
                // 22 is on the row below them, out of sight. Selecting it
                // scrolls it up, as far as the bottom lets it go, without
                // the view being told of a new Decoration first.
                scrollViewTo( view, ScrollPosition{ 8_lnum, 0 } );
                grabViewport( view );
                view.selectAndDisplayLine( 22_lnum );
                REQUIRE( view.scrollPosition().lineNumber > 8_lnum );
                const auto painted = grabViewport( view );

                THEN( "the Log Lines whose selection changed are painted as a full repaint "
                      "paints them" )
                {
                    requireSameImage(
                        [ & ] {
                            QuickFindPattern freshQuickFindPattern;
                            PaintingLogView fresh( &logData, &freshQuickFindPattern, textWrap );
                            showForPainting( fresh, logData, font, { .textWrap = textWrap } );
                            decorateEverything( fresh, freshQuickFindPattern );
                            scrollViewTo( fresh, ScrollPosition{ 8_lnum, 0 } );
                            fresh.selectAndDisplayLine( 22_lnum );
                            REQUIRE( fresh.scrollPosition() == view.scrollPosition() );
                            fresh.rereadLogLines();
                            return grabViewport( fresh );
                        }(),
                        painted, "a selection that scrolled" );
                }
            }
        }
    }
}

SCENARIO( "A scroll of one Visual Line decorates only the Log Lines it exposes",
          "[logviewpainting][scrollrepaint]" )
{
    const PinnedPaintingSettings settings;
    const auto font = paintingtestfont::requirePaintingTestFont();

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a view painted from Log Line 8, text wrapping " << ( textWrap ? "on" : "off" ) )
        {
            // From Log Line 8 on every Log Line is one Visual Line, and the
            // Viewport's 15 rows hold Log Lines 8 to 22.
            const FakeLogData logData{ paintedTexts() };
            const QuickFindPattern quickFindPattern;
            PaintingLogView view( &logData, &quickFindPattern, textWrap );
            showForPainting( view, logData, font, { .textWrap = textWrap } );
            scrollViewTo( view, ScrollPosition{ 8_lnum, 0 } );
            grabViewport( view );
            view.lineTypes().asked.clear();

            WHEN( "it is scrolled one Visual Line down and painted" )
            {
                pressKey( view, Qt::Key_Down );
                grabViewport( view );

                THEN( "only the Log Line that came into view at the bottom is decorated" )
                {
                    REQUIRE( view.lineTypes().asked == std::vector<LineNumber>{ 23_lnum } );
                }
            }

            WHEN( "it is scrolled one Visual Line up and painted" )
            {
                pressKey( view, Qt::Key_Up );
                grabViewport( view );

                THEN( "only the Log Line that came into view at the top is decorated" )
                {
                    REQUIRE( view.lineTypes().asked == std::vector<LineNumber>{ 7_lnum } );
                }
            }
        }
    }
}

SCENARIO( "A scrolled log view repaints what changed about the Log Lines it kept",
          "[logviewpainting][scrollrepaint]" )
{
    const PinnedPaintingSettings settings;
    const auto font = paintingtestfont::requirePaintingTestFont();

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a view scrolled down a few Visual Lines, text wrapping "
               << ( textWrap ? "on" : "off" ) )
        {
            FakeLogData logData{ paintedTexts() };
            QuickFindPattern quickFindPattern;
            PaintingLogView view( &logData, &quickFindPattern, textWrap );
            showForPainting( view, logData, font, { .textWrap = textWrap } );
            grabViewport( view );
            for ( int step = 0; step < 3; ++step ) {
                pressKey( view, Qt::Key_Down );
                grabViewport( view );
            }

            WHEN( "a Log Line in view becomes a Mark and the view is told of a new Decoration" )
            {
                view.lineTypes().changedTypes[ 12 ] = LineTypeFlags::Mark;
                view.updateDecorations();
                pressKey( view, Qt::Key_Down );
                const auto painted = grabViewport( view );

                THEN( "it is painted as a Mark" )
                {
                    PaintingLogView fresh( &logData, &quickFindPattern, textWrap );
                    fresh.lineTypes().changedTypes[ 12 ] = LineTypeFlags::Mark;
                    showForPainting( fresh, logData, font, { .textWrap = textWrap } );
                    scrollViewTo( fresh, view.scrollPosition() );
                    fresh.rereadLogLines();
                    requireSameImage( grabViewport( fresh ), painted, "a new Mark" );
                }
            }

            WHEN( "a QuickFind pattern is typed" )
            {
                quickFindPattern.changeSearchPattern( QStringLiteral( "idle" ),
                                                      /* useExtendedRegexp */ false );
                pressKey( view, Qt::Key_Down );
                const auto painted = grabViewport( view );

                THEN( "its matches are painted" )
                {
                    PaintingLogView fresh( &logData, &quickFindPattern, textWrap );
                    showForPainting( fresh, logData, font, { .textWrap = textWrap } );
                    scrollViewTo( fresh, view.scrollPosition() );
                    fresh.rereadLogLines();
                    requireSameImage( grabViewport( fresh ), painted, "QuickFind typed" );
                }
            }

            WHEN( "the text of the Log Lines in view changes and the view reads them again" )
            {
                auto changedTexts = paintedTexts();
                for ( auto& text : changedTexts ) {
                    text.replace( QStringLiteral( "idle" ), QStringLiteral( "busy" ) );
                }
                logData.setLines( changedTexts );
                view.rereadLogLines();
                pressKey( view, Qt::Key_Down );
                const auto painted = grabViewport( view );

                THEN( "the new text is painted" )
                {
                    const FakeLogData changedLogData{ changedTexts };
                    PaintingLogView fresh( &changedLogData, &quickFindPattern, textWrap );
                    showForPainting( fresh, changedLogData, font, { .textWrap = textWrap } );
                    scrollViewTo( fresh, view.scrollPosition() );
                    fresh.rereadLogLines();
                    requireSameImage( grabViewport( fresh ), painted, "the text changed" );
                }
            }
        }
    }
}

SCENARIO( "A scrolled log view paints its line numbers as wide as a full repaint does",
          "[logviewpainting][scrollrepaint]" )
{
    const PinnedPaintingSettings settings;
    const auto font = paintingtestfont::requirePaintingTestFont();

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a view showing line numbers of two digits, text wrapping "
               << ( textWrap ? "on" : "off" ) )
        {
            const FakeLogData logData{ paintedTexts() };
            const QuickFindPattern quickFindPattern;
            PaintingLogView view( &logData, &quickFindPattern, textWrap );
            showForPainting( view, logData, font,
                             { .textWrap = textWrap, .lineNumbersVisible = true } );
            scrollViewTo( view, ScrollPosition{ 8_lnum, 0 } );
            grabViewport( view );

            WHEN( "its Log File grows to line numbers of three digits, the lines it shows "
                  "staying the same, and it is scrolled one Visual Line down" )
            {
                view.lineTypes().logFileLineCount = 100_lcount;
                pressKey( view, Qt::Key_Down );
                const auto painted = grabViewport( view );

                THEN( "it paints what a full repaint paints" )
                {
                    PaintingLogView fresh( &logData, &quickFindPattern, textWrap );
                    fresh.lineTypes().logFileLineCount = 100_lcount;
                    showForPainting( fresh, logData, font,
                                     { .textWrap = textWrap, .lineNumbersVisible = true } );
                    scrollViewTo( fresh, view.scrollPosition() );
                    fresh.rereadLogLines();
                    requireSameImage( grabViewport( fresh ), painted, "line numbers grown wider" );
                }
            }
        }
    }
}

// Told that Log Lines were only appended, scrolling keeps the Visual Lines it
// counted for the bottom of the Log File, but for the last Log Line, which may
// have grown (#296).
SCENARIO( "A log view at its bottom paints Log Lines appended as a new view does",
          "[logviewpainting][scrollrepaint]" )
{
    const PinnedPaintingSettings settings;
    const auto font = paintingtestfont::requirePaintingTestFont();

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a view at the bottom of its Log File, text wrapping "
               << ( textWrap ? "on" : "off" ) )
        {
            FakeLogData logData{ paintedTexts() };
            const QuickFindPattern quickFindPattern;
            PaintingLogView view( &logData, &quickFindPattern, textWrap );
            showForPainting( view, logData, font, { .textWrap = textWrap } );
            view.followSet( true );
            grabViewport( view );

            WHEN( "its last Log Line grows, Log Lines are appended and it is told only that" )
            {
                auto grown = paintedTexts();
                grown.last() += QStringLiteral( " and grows longer, long enough for another row" );
                grown << QStringLiteral( "10:00:32 INFO  appended" )
                      << QStringLiteral( "10:00:33 ERROR appended, and long enough to be wrapped "
                                         "onto a second Visual Line" );
                logData.setLines( grown );
                view.updateData( LinesChange::Appended );
                // As a finished load hands every view the whole Log File.
                view.setSearchLimits( 0_lnum, LineNumber( logData.getNbLine().get() ) );
                const auto painted = grabViewport( view );

                THEN( "it paints the new bottom of the Log File as a view that read it all does" )
                {
                    const FakeLogData grownLogData{ grown };
                    PaintingLogView fresh( &grownLogData, &quickFindPattern, textWrap );
                    showForPainting( fresh, grownLogData, font, { .textWrap = textWrap } );
                    fresh.followSet( true );
                    REQUIRE( fresh.scrollPosition() == view.scrollPosition() );
                    requireSameImage( grabViewport( fresh ), painted, "Log Lines appended" );
                }
            }
        }
    }
}

namespace {

// QuickFind and a Color Label, whose backgrounds are as wide as the text
// measures; unlike decorateEverything(), no click that depends on the font.
void highlightWithoutSelecting( AbstractLogView& view, QuickFindPattern& quickFindPattern )
{
    quickFindPattern.changeSearchPattern( QStringLiteral( "retry|idle" ),
                                          /* useExtendedRegexp */ true );
    auto colorLabels = std::vector<AbstractLogView::QuickHighlighters>( 9 );
    colorLabels[ 1 ] << QStringLiteral( "INFO" );
    view.setQuickHighlighters( colorLabels );
}

} // namespace

SCENARIO( "A log view paints in a new font as a view that started with it does",
          "[logviewpainting]" )
{
    // The view keeps what it measured of a font between paints (#304); a
    // new font must be measured again.
    const PinnedPaintingSettings settings;
    const auto font = paintingtestfont::requirePaintingTestFont();
    auto largerFont = font;
    largerFont.setPixelSize( font.pixelSize() * 3 / 2 );

    for ( const bool textWrap : { false, true } ) {
        GIVEN( "a view with QuickFind and a Color Label painted in the test font, text wrapping "
               << ( textWrap ? "on" : "off" ) )
        {
            const FakeLogData logData{ paintedTexts() };
            QuickFindPattern quickFindPattern;
            PaintingLogView view( &logData, &quickFindPattern, textWrap );
            // Its very first paint already in the test font.
            view.updateFont( font );
            showForPainting( view, logData, font, { .textWrap = textWrap } );
            highlightWithoutSelecting( view, quickFindPattern );
            grabViewport( view );

            WHEN( "it is given a larger font" )
            {
                view.updateFont( largerFont );
                const auto painted = grabViewport( view );

                THEN( "it paints what a view shown in the larger font paints" )
                {
                    QuickFindPattern freshQuickFindPattern;
                    PaintingLogView fresh( &logData, &freshQuickFindPattern, textWrap );
                    showForPainting( fresh, logData, largerFont, { .textWrap = textWrap } );
                    highlightWithoutSelecting( fresh, freshQuickFindPattern );
                    requireSameImage( grabViewport( fresh ), painted, "a new font" );
                }
            }
        }
    }
}
