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

#include <catch2/catch.hpp>

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
            const auto context = setup.context( HighlighterSet{}, SearchLimits{} );

            THEN( "it carries a main-search Highlighter coloured as the Policy says" )
            {
                REQUIRE( context.mainSearch.has_value() );
                REQUIRE( context.mainSearch->pattern() == QStringLiteral( "ERROR" ) );
                REQUIRE( context.mainSearch->backColor() == MainSearchBack );
                REQUIRE( context.mainSearch->highlightOnlyMatch() );
                REQUIRE_FALSE( context.mainSearch->variateColors() );
            }

            THEN( "the QuickFind colour is the Policy's" )
            {
                REQUIRE( context.quickFindColor == QuickFindBack );
            }
        }

        WHEN( "the Line Decorator is given that Context" )
        {
            const LineDecorator decorator{ setup.context( HighlighterSet{}, SearchLimits{} ) };
            const QString line = QStringLiteral( "an ERROR occurred" );
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, line }, LineTypeFlags::Plain );

            THEN( "what the Search matched is coloured in the Policy's colour" )
            {
                const auto spans = decorator.decorate( line, verdict ).spans();
                REQUIRE( spans.size() == 1 );
                REQUIRE( spans.front().startColumn() == 3_lcol );
                REQUIRE( spans.front().size() == 5_length );
                REQUIRE( spans.front().backColor() == MainSearchBack );
            }
        }
    }
}

SCENARIO( "A Decoration Policy that colours no main search builds no main-search Highlighter",
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
            REQUIRE_FALSE( setup.context( HighlighterSet{}, SearchLimits{} ).mainSearch );
        }
    }

    GIVEN( "a Policy that does colour the main search" )
    {
        DecorationSetup setup;
        setup.setPolicy( colorfulPolicy() );

        WHEN( "the pattern selects Log Lines without pointing at text within them" )
        {
            THEN( "an empty pattern builds no Highlighter" )
            {
                setup.setSearchPattern( RegularExpressionPattern{ QString{} } );
                REQUIRE_FALSE( setup.context( HighlighterSet{}, SearchLimits{} ).mainSearch );
            }

            THEN( "a boolean pattern builds none" )
            {
                RegularExpressionPattern pattern{ QStringLiteral( "a and b" ) };
                pattern.isBoolean = true;
                setup.setSearchPattern( pattern );
                REQUIRE_FALSE( setup.context( HighlighterSet{}, SearchLimits{} ).mainSearch );
            }

            THEN( "an excluding pattern builds none" )
            {
                RegularExpressionPattern pattern{ QStringLiteral( "noise" ) };
                pattern.isExclude = true;
                setup.setSearchPattern( pattern );
                REQUIRE_FALSE( setup.context( HighlighterSet{}, SearchLimits{} ).mainSearch );
            }
        }
    }
}

SCENARIO( "A Decoration Setup turns the Color Labels it is handed into Highlighters",
          "[decorationsetup]" )
{
    GIVEN( "words in two slots and a colour for each" )
    {
        DecorationSetup setup;
        setup.setPolicy( colorfulPolicy() );
        setup.setColorLabels(
            { QStringList{ "warn", "retry" }, QStringList{ "info" } },
            { HighlightColor{ Qt::black, Qt::yellow }, HighlightColor{ Qt::white, Qt::blue } } );

        THEN( "there is one Highlighter per word, in its slot's colour" )
        {
            const auto context = setup.context( HighlighterSet{}, SearchLimits{} );
            REQUIRE( context.colorLabels.size() == 3 );
            REQUIRE( context.colorLabels[ 0 ].pattern() == QStringLiteral( "warn" ) );
            REQUIRE( context.colorLabels[ 0 ].backColor() == QColor{ Qt::yellow } );
            REQUIRE( context.colorLabels[ 2 ].pattern() == QStringLiteral( "info" ) );
            REQUIRE( context.colorLabels[ 2 ].backColor() == QColor{ Qt::blue } );
        }
    }

    GIVEN( "more word slots than colours" )
    {
        DecorationSetup setup;
        setup.setPolicy( colorfulPolicy() );
        setup.setColorLabels( { QStringList{ "warn" }, QStringList{ "info" } },
                              { HighlightColor{ Qt::black, Qt::yellow } } );

        THEN( "only the slots a colour exists for are coloured" )
        {
            const auto context = setup.context( HighlighterSet{}, SearchLimits{} );
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
            const auto context = setup.context( highlighterSet, SearchLimits{ 10_lnum, 20_lnum } );
            const LineDecorator decorator{ context };

            THEN( "a line outside them is judged so, and one inside is not" )
            {
                REQUIRE( decorator.verdictFor( LogLine{ 5_lnum, "an ERROR" }, LineTypeFlags::Plain )
                             .isOutsideSearchLimits() );
                REQUIRE_FALSE(
                    decorator.verdictFor( LogLine{ 15_lnum, "an ERROR" }, LineTypeFlags::Plain )
                        .isOutsideSearchLimits() );
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

SCENARIO( "The Mark and Match colours are defined once, for every Presentation",
          "[decorationsetup]" )
{
    THEN( "they are the colours both Presentations have always painted" )
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
