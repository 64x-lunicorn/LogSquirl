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
// - The font is not the host's. The test loads its own font from
//   data/painting/logsquirl-painting-test.ttf (see make_test_font.py there):
//   fixed-width, 8 x 16 px, with every glyph edge on a pixel boundary, so a
//   glyph covers each pixel fully or not at all. Each platform draws it in
//   the rendering mode that keeps it that way (see paintingTestFont()), and
//   so each platform's rasteriser produces the same pixels.
//   Nothing needs to be installed on the host. If the platform cannot load
//   that font, or does not honour its metrics, the test fails and says so:
//   without its font it would verify nothing.
// - The palette, the frame, the scroll bars and the viewport size are set
//   explicitly, so no platform style leaks in.
// - The settings painting reads -- main search highlighting and its colors,
//   the QuickFind color, the active Highlighter Sets -- are set for the
//   duration of the test and restored afterwards.
//
// To accept a deliberate change to painting, run the test with
// LOGSQUIRL_UPDATE_PAINTING_GOLDENS set: it rewrites the golden images in
// the source tree instead of comparing, and the diff shows the new images
// for review. On a mismatch the image that was actually painted is written
// to the temporary directory, and the failure names the file.

#include <catch2/catch.hpp>

#include <optional>

#include <QCoreApplication>
#include <QDir>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QImage>
#include <QMouseEvent>
#include <QPalette>

#include "abstractlogdata.h"
#include "abstractlogview.h"
#include "configuration.h"
#include "fake_log_data.h"
#include "highlighterset.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"

namespace {

const QString PaintingTestDataDir = QStringLiteral( LOGSQUIRL_PAINTING_TEST_DATA_DIR );

constexpr int FontPixelSize = 16;
constexpr int ExpectedCharWidth = 8;
constexpr int ExpectedCharHeight = 16;

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
    };
    return lines;
}

QStringList paintedTexts()
{
    QStringList texts;
    for ( const auto& line : paintedLines() ) {
        texts << line.text;
    }
    return texts;
}

class PaintingLogView : public AbstractLogView {
public:
    PaintingLogView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern,
                     bool textWrap )
        : AbstractLogView( logData, quickFindPattern, textWrap )
    {
    }

protected:
    AbstractLogData::LineType lineType( LineNumber lineNumber ) const override
    {
        const auto& lines = paintedLines();
        return lineNumber.get() < lines.size() ? lines[ lineNumber.get() ].type
                                               : AbstractLogData::LineType{};
    }
};

// The settings painting reads, pinned to fixed values for as long as this
// object lives and restored when it goes: nothing a developer has configured
// reaches the images, and nothing set here leaks into the tests that run
// next.
class PinnedPaintingSettings {
public:
    PinnedPaintingSettings()
        : mainSearchHighlight_( Configuration::get().mainSearchHighlight() )
        , variateMainSearchHighlight_( Configuration::get().variateMainSearchHighlight() )
        , mainSearchBackColor_( Configuration::get().mainSearchBackColor() )
        , qfBackColor_( Configuration::get().qfBackColor() )
        , activeHighlighterSets_( HighlighterSetCollection::get().activeSetIds() )
    {
        auto& config = Configuration::get();
        config.setEnableMainSearchHighlight( true );
        config.setVariateMainSearchHighlight( false );
        config.setMainSearchBackColor( QColor{ 255, 200, 0 } );
        config.setQfBackColor( QColor{ Qt::yellow } );
        HighlighterSetCollection::get().deactivateAll();
    }

    ~PinnedPaintingSettings()
    {
        auto& config = Configuration::get();
        config.setEnableMainSearchHighlight( mainSearchHighlight_ );
        config.setVariateMainSearchHighlight( variateMainSearchHighlight_ );
        config.setMainSearchBackColor( mainSearchBackColor_ );
        config.setQfBackColor( qfBackColor_ );
        for ( const auto& setId : activeHighlighterSets_ ) {
            HighlighterSetCollection::get().activateSet( setId );
        }
    }

    PinnedPaintingSettings( const PinnedPaintingSettings& ) = delete;
    PinnedPaintingSettings& operator=( const PinnedPaintingSettings& ) = delete;

private:
    bool mainSearchHighlight_;
    bool variateMainSearchHighlight_;
    QColor mainSearchBackColor_;
    QColor qfBackColor_;
    QStringList activeHighlighterSets_;
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

// The painting test's own font, or nothing when the platform cannot load it.
std::optional<QFont> paintingTestFont()
{
    static const int fontId = QFontDatabase::addApplicationFont(
        PaintingTestDataDir + QStringLiteral( "/logsquirl-painting-test.ttf" ) );
    if ( fontId < 0 ) {
        return std::nullopt;
    }

    const auto families = QFontDatabase::applicationFontFamilies( fontId );
    if ( families.isEmpty() ) {
        return std::nullopt;
    }

    QFont font( families.first() );
    font.setPixelSize( FontPixelSize );
    // Each platform's text rasteriser has one mode that draws these
    // pixel-aligned glyphs exactly, and it is not the same mode everywhere.
#ifdef Q_OS_MACOS
    // CoreText smooths antialiased glyphs even where their edges sit exactly
    // on pixel boundaries, so macOS draws them unantialiased.
    font.setStyleStrategy( QFont::NoAntialias );
#else
    // Elsewhere an unantialiased glyph is a one-bit bitmap, which Qt copies
    // without the pen's transparency -- the dimmed Context Lines would not be
    // dimmed. Antialiased, a pixel-aligned glyph still covers every pixel
    // fully or not at all; subpixel rendering would color its edges.
    font.setStyleStrategy( QFont::NoSubpixelAntialias );
#endif
    font.setHintingPreference( QFont::PreferNoHinting );
    return font;
}

struct PaintingConfiguration {
    bool textWrap = false;
    bool lineNumbersVisible = false;
};

QImage paintLogView( const QFont& font, PaintingConfiguration configuration )
{
    const FakeLogData logData{ paintedTexts() };
    const QuickFindPattern quickFindPattern;

    PaintingLogView view( &logData, &quickFindPattern, configuration.textWrap );
    view.setFrameShape( QFrame::NoFrame );
    view.setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view.setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    view.setPalette( fixedPalette() );
    view.resize( ViewWidth, ViewHeight );
    view.show();
    QCoreApplication::processEvents();

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
    REQUIRE( view.viewport()->size() == QSize( ViewWidth, ViewHeight ) );

    return view.viewport()->grab().toImage().convertToFormat( QImage::Format_ARGB32 );
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

    const auto font = paintingTestFont();
    if ( !font.has_value() ) {
        FAIL( "The painting test could not load its own font from "
              << PaintingTestDataDir.toStdString()
              << "; without it there is nothing portable to compare against." );
    }

    const QFontMetrics metrics( *font );
    const auto charWidth = metrics.horizontalAdvance( QLatin1Char( 'm' ) );
    if ( charWidth != ExpectedCharWidth || metrics.height() != ExpectedCharHeight ) {
        FAIL( "The test font must measure " << ExpectedCharWidth << "x" << ExpectedCharHeight
                                            << " px, this platform measures it " << charWidth << "x"
                                            << metrics.height() );
    }

    const auto painted = paintLogView( *font, configuration );
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
    }
}

namespace {

// A FakeLogData that counts how often the Log Lines of a viewport are fetched.
class CountingLogData : public FakeLogData {
public:
    using FakeLogData::FakeLogData;

    mutable int linesFetched = 0;

protected:
    logsquirl::vector<QString> doGetLines( LineNumber first, LinesCount count ) const override
    {
        ++linesFetched;
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
                view.forceRefresh();
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
