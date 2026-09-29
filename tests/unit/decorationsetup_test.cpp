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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>

#include "decorationsetup.h"

// The Decoration Setup is the one module that builds the Line Decorator's
// Context for either Presentation. Everything below drives it from a
// Decoration Policy literal: no settings store, no widget, no singleton --
// which is the point of the Policy travelling as a value.

namespace {

using LineTypeFlags = AbstractLogData::LineTypeFlags;

const QColor MainSearchBack{ 255, 200, 0 };
const QColor QuickFindBack{ Qt::yellow };

DecorationPolicy colorfulPolicy()
{
    return DecorationPolicy{ .mainSearchHighlight = true,
                             .variateMainSearchHighlight = false,
                             .mainSearchBackColor = MainSearchBack,
                             .quickFindBackColor = QuickFindBack };
}

} // namespace

SCENARIO( "A Decoration Setup builds the Line Decorator's Context from a Decoration Policy",
          "[decorationsetup]" )
{
    GIVEN( "a Policy and a main search pattern, both built from literals" )
    {
        DecorationSetup setup;
        setup.setPolicy( colorfulPolicy() );
        setup.setSearchPattern( RegularExpressionPattern{ QStringLiteral( "ERROR" ) } );

        WHEN( "the Context is built" )
        {
            const auto context = setup.context( HighlighterSet{}, SearchLimits{}, LinePalette{},
                                                LineStatusDisplay::InGutter );

            THEN( "it carries a main-search Highlighter colored as the Policy says" )
            {
                REQUIRE( context.mainSearch.has_value() );
                REQUIRE( context.mainSearch->pattern() == QStringLiteral( "ERROR" ) );
                REQUIRE( context.mainSearch->backColor() == MainSearchBack );
                REQUIRE( context.mainSearch->highlightOnlyMatch() );
                REQUIRE_FALSE( context.mainSearch->variateColors() );
            }

            THEN( "the QuickFind color is the Policy's" )
            {
                REQUIRE( context.quickFindColor == QuickFindBack );
            }
        }

        WHEN( "the Line Decorator is given that Context" )
        {
            const LineDecorator decorator{ setup.context(
                HighlighterSet{}, SearchLimits{}, LinePalette{}, LineStatusDisplay::InGutter ) };
            const QString line = QStringLiteral( "an ERROR occurred" );
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, line }, LineTypeFlags::Plain );

            THEN( "what the Search matched is colored in the Policy's color" )
            {
                const auto spans = decorator.decorate( line, verdict ).spans();
                REQUIRE( spans.size() == 3 );
                REQUIRE( spans[ 1 ].startColumn() == 3_lcol );
                REQUIRE( spans[ 1 ].size() == 5_length );
                REQUIRE( spans[ 1 ].backColor() == MainSearchBack );
            }
        }
    }
}

SCENARIO( "A Decoration Policy that colors no main search builds no main-search Highlighter",
          "[decorationsetup]" )
{
    GIVEN( "a Policy with main search highlighting switched off" )
    {
        auto policy = colorfulPolicy();
        policy.mainSearchHighlight = false;

        DecorationSetup setup;
        setup.setPolicy( policy );
        setup.setSearchPattern( RegularExpressionPattern{ QStringLiteral( "ERROR" ) } );

        THEN( "the Context carries none" )
        {
            REQUIRE_FALSE( setup
                               .context( HighlighterSet{}, SearchLimits{}, LinePalette{},
                                         LineStatusDisplay::InGutter )
                               .mainSearch );
        }
    }

    GIVEN( "a Policy that does color the main search" )
    {
        DecorationSetup setup;
        setup.setPolicy( colorfulPolicy() );

        WHEN( "the pattern selects Log Lines without pointing at text within them" )
        {
            THEN( "an empty pattern builds no Highlighter" )
            {
                setup.setSearchPattern( RegularExpressionPattern{ QString{} } );
                REQUIRE_FALSE( setup
                                   .context( HighlighterSet{}, SearchLimits{}, LinePalette{},
                                             LineStatusDisplay::InGutter )
                                   .mainSearch );
            }

            THEN( "a boolean pattern builds none" )
            {
                RegularExpressionPattern pattern{ QStringLiteral( "a and b" ) };
                pattern.isBoolean = true;
                setup.setSearchPattern( pattern );
                REQUIRE_FALSE( setup
                                   .context( HighlighterSet{}, SearchLimits{}, LinePalette{},
                                             LineStatusDisplay::InGutter )
                                   .mainSearch );
            }

            THEN( "an excluding pattern builds none" )
            {
                RegularExpressionPattern pattern{ QStringLiteral( "noise" ) };
                pattern.isExclude = true;
                setup.setSearchPattern( pattern );
                REQUIRE_FALSE( setup
                                   .context( HighlighterSet{}, SearchLimits{}, LinePalette{},
                                             LineStatusDisplay::InGutter )
                                   .mainSearch );
            }
        }
    }
}

SCENARIO( "A Decoration Setup turns the Color Labels it is handed into Highlighters",
          "[decorationsetup]" )
{
    GIVEN( "words in two slots and a color for each" )
    {
        DecorationSetup setup;
        setup.setPolicy( colorfulPolicy() );
        setup.setColorLabels(
            { QStringList{ "warn", "retry" }, QStringList{ "info" } },
            { HighlightColor{ Qt::black, Qt::yellow }, HighlightColor{ Qt::white, Qt::blue } } );

        THEN( "there is one Highlighter per word, in its slot's color" )
        {
            const auto context = setup.context( HighlighterSet{}, SearchLimits{}, LinePalette{},
                                                LineStatusDisplay::InGutter );
            REQUIRE( context.colorLabels.size() == 3 );
            REQUIRE( context.colorLabels[ 0 ].pattern() == QStringLiteral( "warn" ) );
            REQUIRE( context.colorLabels[ 0 ].backColor() == QColor{ Qt::yellow } );
            REQUIRE( context.colorLabels[ 2 ].pattern() == QStringLiteral( "info" ) );
            REQUIRE( context.colorLabels[ 2 ].backColor() == QColor{ Qt::blue } );
        }
    }

    GIVEN( "more word slots than colors" )
    {
        DecorationSetup setup;
        setup.setPolicy( colorfulPolicy() );
        setup.setColorLabels( { QStringList{ "warn" }, QStringList{ "info" } },
                              { HighlightColor{ Qt::black, Qt::yellow } } );

        THEN( "only the slots a color exists for are colored" )
        {
            const auto context = setup.context( HighlighterSet{}, SearchLimits{}, LinePalette{},
                                                LineStatusDisplay::InGutter );
            REQUIRE( context.colorLabels.size() == 1 );
            REQUIRE( context.colorLabels[ 0 ].pattern() == QStringLiteral( "warn" ) );
        }
    }
}

SCENARIO( "A Decoration Setup passes on the Highlighter Set and Search Limits it is given",
          "[decorationsetup]" )
{
    GIVEN( "a setup and an active Highlighter Set" )
    {
        DecorationSetup setup;
        setup.setPolicy( colorfulPolicy() );

        auto highlighterSet = HighlighterSet::createNewSet( "test" );
        highlighterSet.addHighlighter(
            Highlighter{ "ERROR", false, false, QColor{ Qt::white }, QColor{ Qt::red } } );

        WHEN( "a Context is built with Search Limits that exclude a Log Line" )
        {
            const auto context = setup.context( highlighterSet, SearchLimits{ 10_lnum, 20_lnum },
                                                LinePalette{}, LineStatusDisplay::InGutter );
            const LineDecorator decorator{ context };

            THEN( "a line outside them is judged so, and one inside is not" )
            {
                REQUIRE( decorator.verdictFor( LogLine{ 5_lnum, "an ERROR" }, LineTypeFlags::Plain )
                             .isOutsideSearchLimits() );
                REQUIRE_FALSE(
                    decorator.verdictFor( LogLine{ 15_lnum, "an ERROR" }, LineTypeFlags::Plain )
                        .isOutsideSearchLimits() );
                REQUIRE(
                    decorator.verdictFor( LogLine{ 20_lnum, "an ERROR" }, LineTypeFlags::Plain )
                        .isOutsideSearchLimits() );
            }

            THEN( "the palette and where Match and Mark show are the ones given" )
            {
                const LinePalette palette{ Qt::black, Qt::white, Qt::gray,  Qt::white,
                                           Qt::blue,  Qt::red,   Qt::green, Qt::magenta };
                const auto tableContext = setup.context( highlighterSet, SearchLimits{}, palette,
                                                         LineStatusDisplay::AsBackground );
                REQUIRE( tableContext.palette.selection == QColor{ Qt::blue } );
                REQUIRE( tableContext.lineStatus == LineStatusDisplay::AsBackground );
            }

            THEN( "the whole-line Highlighter of the set given is the one that decides" )
            {
                const auto verdict
                    = decorator.verdictFor( LogLine{ 15_lnum, "an ERROR" }, LineTypeFlags::Plain );
                REQUIRE( verdict.wholeLineHighlight().has_value() );
                REQUIRE( verdict.wholeLineHighlight()->backColor == QColor{ Qt::red } );
            }
        }
    }
}

SCENARIO( "The Mark and Match colors of the gutter and the overview are defined once",
          "[decorationsetup]" )
{
    THEN( "they are the colors the gutter and the overview have always painted" )
    {
        REQUIRE( LineStatusColors::match() == QColor{ Qt::red } );
        REQUIRE( LineStatusColors::mark() == QColor{ "dodgerblue" } );
        REQUIRE( LineStatusColors::markedMatch() == QColor{ "violet" } );
    }

    THEN( "a Mark that is also a Match is told apart from either" )
    {
        REQUIRE( LineStatusColors::markedMatch() != LineStatusColors::match() );
        REQUIRE( LineStatusColors::markedMatch() != LineStatusColors::mark() );
    }
}

namespace {

// Contrast as the Decoration Setup reckons it: WCAG 2.
double contrastRatio( const QColor& a, const QColor& b )
{
    const auto luminance = []( const QColor& color ) {
        const auto linear = []( float value ) {
            const double channel = static_cast<double>( value );
            return channel <= 0.04045 ? channel / 12.92
                                      : std::pow( ( channel + 0.055 ) / 1.055, 2.4 );
        };
        return 0.2126 * linear( color.redF() ) + 0.7152 * linear( color.greenF() )
               + 0.0722 * linear( color.blueF() );
    };
    const auto first = luminance( a );
    const auto second = luminance( b );
    return ( std::max( first, second ) + 0.05 ) / ( std::min( first, second ) + 0.05 );
}

// A Theme's 16 basic colors, told apart from any other color here.
std::array<QColor, 16> testAnsiColors()
{
    std::array<QColor, 16> colors;
    for ( std::size_t index = 0; index < colors.size(); ++index ) {
        colors[ index ] = QColor( 100 + static_cast<int>( index ), 50, 150 );
    }
    return colors;
}

DecorationSetup ansiSetup( bool showAnsiColors )
{
    DecorationSetup setup;
    auto policy = colorfulPolicy();
    policy.showAnsiColors = showAnsiColors;
    setup.setPolicy( policy );
    setup.setAnsiColors( testAnsiColors() );
    return setup;
}

LinePalette paletteOn( const QColor& text, const QColor& base )
{
    LinePalette palette;
    palette.text = text;
    palette.base = base;
    return palette;
}

AnsiColorSpan foregroundSpan( AnsiColor color )
{
    return AnsiColorSpan{ 0, 3, color, AnsiColor{} };
}

} // namespace

SCENARIO( "A Decoration Setup resolves ANSI colors against the Theme", "[decorationsetup][ansi]" )
{
    // Mid-gray on mid-gray would fail the contrast rule for nearly anything;
    // black text on white leaves the test colors alone.
    const auto palette = paletteOn( QColor( Qt::black ), QColor( Qt::white ) );

    GIVEN( "a Decoration Policy that shows ANSI colors, and a Theme's basic colors" )
    {
        const auto setup = ansiSetup( true );

        THEN( "a basic color is the Theme's" )
        {
            const auto colors
                = setup.ansiColorsFor( { foregroundSpan( AnsiColor::indexed( 4 ) ) }, palette );
            REQUIRE( colors.size() == 1 );
            REQUIRE( colors[ 0 ].startColumn() == 0_lcol );
            REQUIRE( colors[ 0 ].size() == 3_length );
            REQUIRE( colors[ 0 ].foreColor() == testAnsiColors()[ 4 ] );
            REQUIRE_FALSE( colors[ 0 ].backColor().isValid() );
        }

        THEN( "an indexed color past 15 is xterm's, from its cube or its gray ramp" )
        {
            const auto colors = setup.ansiColorsFor(
                { AnsiColorSpan{ 0, 1, AnsiColor{}, AnsiColor::indexed( 196 ) },
                  AnsiColorSpan{ 1, 1, AnsiColor{}, AnsiColor::indexed( 67 ) },
                  AnsiColorSpan{ 2, 1, AnsiColor{}, AnsiColor::indexed( 244 ) } },
                palette );
            REQUIRE( colors.size() == 3 );
            REQUIRE( colors[ 0 ].backColor() == QColor( 255, 0, 0 ) );
            REQUIRE( colors[ 1 ].backColor() == QColor( 95, 135, 175 ) );
            REQUIRE( colors[ 2 ].backColor() == QColor( 128, 128, 128 ) );
        }

        THEN( "a truecolor is as given" )
        {
            const auto colors = setup.ansiColorsFor(
                { AnsiColorSpan{ 0, 1, AnsiColor{}, AnsiColor::rgb( 1, 2, 3 ) } }, palette );
            REQUIRE( colors[ 0 ].backColor() == QColor( 1, 2, 3 ) );
        }
    }

    GIVEN( "a Decoration Policy that does not show ANSI colors" )
    {
        const auto setup = ansiSetup( false );

        THEN( "no color is resolved" )
        {
            REQUIRE( setup.ansiColorsFor( { foregroundSpan( AnsiColor::indexed( 1 ) ) }, palette )
                         .empty() );
        }
    }
}

SCENARIO( "A foreground ANSI color too faint to read is moved toward the Theme's text color",
          "[decorationsetup][ansi]" )
{
    DecorationSetup setup;
    auto policy = colorfulPolicy();
    policy.showAnsiColors = true;
    setup.setPolicy( policy );

    GIVEN( "a dark Theme and a dark foreground" )
    {
        // Smyck's background and text; its own black.
        const auto palette = paletteOn( QColor( "#F7F7F7" ), QColor( "#1B1B1B" ) );
        setup.setAnsiColors( { QColor( "#000000" ) } );

        THEN( "the foreground reaches 3:1 against the background, and no more than it needs" )
        {
            const auto colors
                = setup.ansiColorsFor( { foregroundSpan( AnsiColor::indexed( 0 ) ) }, palette );
            const auto foreground = colors[ 0 ].foreColor();
            REQUIRE( contrastRatio( foreground, palette.base ) >= 3.0 );
            REQUIRE( foreground != palette.text );
        }
    }

    GIVEN( "a light Theme and a light foreground" )
    {
        const auto palette = paletteOn( QColor( Qt::black ), QColor( Qt::white ) );

        THEN( "the foreground reaches 3:1 against the background" )
        {
            const auto colors = setup.ansiColorsFor(
                { foregroundSpan( AnsiColor::rgb( 255, 255, 0 ) ) }, palette );
            REQUIRE( contrastRatio( colors[ 0 ].foreColor(), palette.base ) >= 3.0 );
            REQUIRE( colors[ 0 ].foreColor() != QColor( 255, 255, 0 ) );
        }
    }

    GIVEN( "a foreground on an ANSI background it is hard to read on" )
    {
        const auto palette = paletteOn( QColor( Qt::white ), QColor( Qt::black ) );

        THEN( "the contrast is taken against the ANSI background" )
        {
            const auto colors
                = setup.ansiColorsFor( { AnsiColorSpan{ 0, 3, AnsiColor::rgb( 250, 250, 250 ),
                                                        AnsiColor::rgb( 255, 255, 255 ) } },
                                       palette );
            REQUIRE( colors[ 0 ].backColor() == QColor( 255, 255, 255 ) );
            // No blend toward the Theme's white text reaches 3:1 on white:
            // at worst the foreground becomes that text color.
            REQUIRE( colors[ 0 ].foreColor() == QColor( Qt::white ) );
        }
    }

    GIVEN( "a foreground that reads well" )
    {
        const auto palette = paletteOn( QColor( Qt::black ), QColor( Qt::white ) );

        THEN( "it is left as it is" )
        {
            const auto colors
                = setup.ansiColorsFor( { foregroundSpan( AnsiColor::rgb( 0, 0, 180 ) ) }, palette );
            REQUIRE( colors[ 0 ].foreColor() == QColor( 0, 0, 180 ) );
        }
    }
}
