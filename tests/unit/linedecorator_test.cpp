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

#include <atomic>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "crc32.h"
#include "linedecorator.h"

namespace {

using LineTypeFlags = AbstractLogData::LineTypeFlags;

// A palette whose every color is told apart from the others and from the
// colors the sources below use.
const LinePalette TestPalette{ QColor{ 10, 10, 10 }, QColor{ 250, 250, 250 },
                               QColor{ 128, 128, 128 }, QColor{ 240, 240, 200 },
                               QColor{ 30, 60, 200 } };

// Whether the spans cover [0, length) in order, without a gap or an overlap.
bool coversWithoutGaps( const Decoration& decoration, int length )
{
    int covered = 0;
    for ( const auto& span : decoration.spans() ) {
        if ( span.startColumn().get() != covered || span.size() <= 0_length ) {
            return false;
        }
        covered += static_cast<int>( span.size().get() );
    }
    return covered == length;
}

HighlighterSet setWithHighlighter( const QString& pattern, bool highlightOnlyMatch,
                                   const QColor& foreColor, const QColor& backColor )
{
    auto set = HighlighterSet::createNewSet( "test" );
    set.addHighlighter( Highlighter{ pattern, false, highlightOnlyMatch, foreColor, backColor } );
    return set;
}

LineDecorator::Context emptyContext()
{
    return LineDecorator::Context{
        HighlighterSet{},     std::nullopt,   {},          QuickFindMatcher{},
        QColor{ Qt::yellow }, SearchLimits{}, TestPalette, LineStatusDisplay::InGutter
    };
}

} // namespace

SCENARIO( "LineDecorator::verdictFor decides the facts about a whole Log Line", "[linedecorator]" )
{
    GIVEN( "a decorator with no highlighter set and default search limits" )
    {
        LineDecorator decorator{ emptyContext() };

        WHEN( "asked about a plain line" )
        {
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, "hello world" }, LineTypeFlags::Plain );

            THEN( "nothing stands out" )
            {
                REQUIRE_FALSE( verdict.wholeLineHighlight().has_value() );
                REQUIRE_FALSE( verdict.isMatch() );
                REQUIRE_FALSE( verdict.isMark() );
                REQUIRE_FALSE( verdict.isContextLine() );
                REQUIRE_FALSE( verdict.isOutsideSearchLimits() );
            }
        }

        WHEN( "asked about a Match line" )
        {
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, "hello" }, LineTypeFlags::Match );
            THEN( "the verdict says so" )
            {
                REQUIRE( verdict.isMatch() );
            }
        }

        WHEN( "asked about a Mark line" )
        {
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, "hello" }, LineTypeFlags::Mark );
            THEN( "the verdict says so" )
            {
                REQUIRE( verdict.isMark() );
            }
        }

        WHEN( "asked about a Context line" )
        {
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, "hello" }, LineTypeFlags::Context );
            THEN( "the verdict says so" )
            {
                REQUIRE( verdict.isContextLine() );
            }
        }
    }

    GIVEN( "a decorator with a whole-line Highlighter for lines containing ERROR" )
    {
        auto context = emptyContext();
        context.highlighterSet
            = setWithHighlighter( "ERROR", false, QColor{ Qt::white }, QColor{ Qt::red } );
        LineDecorator decorator{ std::move( context ) };

        WHEN( "the line matches the highlighter" )
        {
            const auto verdict = decorator.verdictFor( LogLine{ 0_lnum, "an ERROR occurred" },
                                                       LineTypeFlags::Plain );

            THEN( "the whole-line highlight is reported with the highlighter's colors" )
            {
                REQUIRE( verdict.wholeLineHighlight().has_value() );
                REQUIRE( verdict.wholeLineHighlight()->foreColor == QColor{ Qt::white } );
                REQUIRE( verdict.wholeLineHighlight()->backColor == QColor{ Qt::red } );
            }
        }

        WHEN( "the line does not match the highlighter" )
        {
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, "all is well" }, LineTypeFlags::Plain );

            THEN( "there is no whole-line highlight" )
            {
                REQUIRE_FALSE( verdict.wholeLineHighlight().has_value() );
            }
        }
    }

    GIVEN( "a decorator with a word-only Highlighter for ERROR" )
    {
        auto context = emptyContext();
        context.highlighterSet
            = setWithHighlighter( "ERROR", true, QColor{ Qt::white }, QColor{ Qt::red } );
        LineDecorator decorator{ std::move( context ) };

        WHEN( "the line matches the highlighter's word" )
        {
            const auto verdict = decorator.verdictFor( LogLine{ 0_lnum, "an ERROR occurred" },
                                                       LineTypeFlags::Plain );

            THEN( "it is not a whole-line highlight, but the matched word is reported" )
            {
                REQUIRE_FALSE( verdict.wholeLineHighlight().has_value() );
                REQUIRE( verdict.highlighterSpans().size() == 1 );
                const auto& match = verdict.highlighterSpans().front();
                REQUIRE( match.startColumn() == 3_lcol );
                REQUIRE( match.size() == LineLength{ 5 } );
                REQUIRE( match.foreColor() == QColor{ Qt::white } );
                REQUIRE( match.backColor() == QColor{ Qt::red } );
            }
        }
    }

    GIVEN( "a decorator with Search Limits from line 5 up to, not including, line 10" )
    {
        auto context = emptyContext();
        context.highlighterSet
            = setWithHighlighter( "ERROR", false, QColor{ Qt::white }, QColor{ Qt::red } );
        context.searchLimits = SearchLimits{ 5_lnum, 10_lnum };
        LineDecorator decorator{ std::move( context ) };

        WHEN( "asked about a line before the limits" )
        {
            const auto verdict = decorator.verdictFor( LogLine{ 4_lnum, "an ERROR occurred" },
                                                       LineTypeFlags::Plain );

            THEN( "it falls outside the Search Limits and highlighting is suppressed" )
            {
                REQUIRE( verdict.isOutsideSearchLimits() );
                REQUIRE_FALSE( verdict.wholeLineHighlight().has_value() );
            }
        }

        WHEN( "asked about a line inside the limits" )
        {
            const auto verdict = decorator.verdictFor( LogLine{ 7_lnum, "an ERROR occurred" },
                                                       LineTypeFlags::Plain );

            THEN( "it is inside the Search Limits and highlighting still applies" )
            {
                REQUIRE_FALSE( verdict.isOutsideSearchLimits() );
                REQUIRE( verdict.wholeLineHighlight().has_value() );
            }
        }

        WHEN( "asked about the first and the last line inside the limits" )
        {
            const auto first = decorator.verdictFor( LogLine{ 5_lnum, "an ERROR occurred" },
                                                     LineTypeFlags::Plain );
            const auto last = decorator.verdictFor( LogLine{ 9_lnum, "an ERROR occurred" },
                                                    LineTypeFlags::Plain );

            THEN( "both are inside the Search Limits" )
            {
                REQUIRE_FALSE( first.isOutsideSearchLimits() );
                REQUIRE_FALSE( last.isOutsideSearchLimits() );
            }
        }

        WHEN( "asked about the line the limits end at" )
        {
            const auto verdict = decorator.verdictFor( LogLine{ 10_lnum, "an ERROR occurred" },
                                                       LineTypeFlags::Plain );

            THEN( "it falls outside the Search Limits: their end is not searched" )
            {
                REQUIRE( verdict.isOutsideSearchLimits() );
            }
        }
    }
}

SCENARIO( "LineDecorator::decorate turns text and a Line Verdict into a Decoration",
          "[linedecorator]" )
{
    GIVEN( "a decorator with no context at all" )
    {
        LineDecorator decorator{ emptyContext() };

        WHEN( "decorating plain text with a plain verdict" )
        {
            const auto decoration = decorator.decorate( "hello world", LineVerdict{} );

            THEN( "one span covers the text in the line's own colors" )
            {
                REQUIRE( decoration.spans().size() == 1 );
                const auto& span = decoration.spans().front();
                REQUIRE( span.startColumn() == 0_lcol );
                REQUIRE( span.size() == LineLength{ 11 } );
                REQUIRE( span.foreColor() == TestPalette.text );
                REQUIRE( span.backColor() == TestPalette.base );
                REQUIRE( decoration.lineColors().backColor == TestPalette.base );
            }
        }
    }

    GIVEN( "a decorator whose context provides all five color sources" )
    {
        auto context = emptyContext();
        context.mainSearch
            = Highlighter{ "wor", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } };
        context.colorLabels.push_back(
            Highlighter{ "wor", false, true, QColor{ Qt::black }, QColor{ Qt::green } } );
        {
            QRegularExpression qfRegex{ "wor" };
            context.quickFind = QuickFindMatcher{ true, qfRegex };
        }
        context.quickFindColor = QColor{ Qt::cyan };
        LineDecorator decorator{ std::move( context ) };

        const QString text = "hello world";
        // Mirrors what verdictFor() produces for a whole-line Highlighter
        // match: the whole-line colour, plus the single full-line span
        // HighlighterSet::matchLine returns for it.
        const LineVerdict wholeLineVerdict{
            HighlightColor{ QColor{ Qt::white }, QColor{ Qt::red } },
            LineTypeFlags::Plain,
            false,
            { HighlightedMatch{ 0_lcol, LineLength{ text.size() }, QColor{ Qt::white },
                                QColor{ Qt::red } } }
        };

        WHEN( "only a whole-line highlight applies" )
        {
            LineDecorator soloDecorator{ emptyContext() };
            const auto decoration = soloDecorator.decorate( text, wholeLineVerdict );

            THEN( "one span covers the entire run with the highlighter's colors" )
            {
                REQUIRE( decoration.spans().size() == 1 );
                const auto& span = decoration.spans().front();
                REQUIRE( span.startColumn() == 0_lcol );
                REQUIRE( span.size() == LineLength{ text.size() } );
                REQUIRE( span.foreColor() == QColor{ Qt::white } );
                REQUIRE( span.backColor() == QColor{ Qt::red } );
            }
        }

        WHEN( "the whole-line highlight, main search, Color Labels and QuickFind all overlap the "
              "same word" )
        {
            const auto decoration = decorator.decorate( text, wholeLineVerdict );

            THEN( "QuickFind wins that overlap, since it is highest precedence among them" )
            {
                bool foundQuickFindSpan = false;
                for ( const auto& span : decoration.spans() ) {
                    if ( span.startColumn() == 6_lcol && span.backColor() == QColor{ Qt::cyan } ) {
                        foundQuickFindSpan = true;
                    }
                    // No span still carries the main search or Color Label color
                    // where QuickFind matched.
                    if ( span.startColumn() == 6_lcol ) {
                        REQUIRE( span.backColor() != QColor{ Qt::yellow } );
                        REQUIRE( span.backColor() != QColor{ Qt::green } );
                    }
                }
                REQUIRE( foundQuickFindSpan );

                AND_THEN( "the untouched prefix keeps the whole-line highlighter's color" )
                {
                    const auto& first = decoration.spans().front();
                    REQUIRE( first.startColumn() == 0_lcol );
                    REQUIRE( first.backColor() == QColor{ Qt::red } );
                }
            }
        }

        WHEN( "Color Labels overlap the main search but QuickFind is inactive" )
        {
            auto localContext = emptyContext();
            localContext.mainSearch
                = Highlighter{ "wor", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } };
            localContext.colorLabels.push_back(
                Highlighter{ "wor", false, true, QColor{ Qt::black }, QColor{ Qt::green } } );
            LineDecorator localDecorator{ std::move( localContext ) };

            const auto decoration = localDecorator.decorate( text, LineVerdict{} );

            THEN( "Color Labels win over main search on the overlap" )
            {
                bool found = false;
                for ( const auto& span : decoration.spans() ) {
                    if ( span.startColumn() == 6_lcol ) {
                        REQUIRE( span.backColor() == QColor{ Qt::green } );
                        found = true;
                    }
                }
                REQUIRE( found );
            }
        }

        WHEN( "main search overlaps a whole-line Highlighter, with no Color Labels or QuickFind" )
        {
            auto localContext = emptyContext();
            localContext.mainSearch
                = Highlighter{ "wor", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } };
            LineDecorator localDecorator{ std::move( localContext ) };

            const auto decoration = localDecorator.decorate( text, wholeLineVerdict );

            THEN( "main search wins over the whole-line Highlighter on the overlap" )
            {
                bool found = false;
                for ( const auto& span : decoration.spans() ) {
                    if ( span.startColumn() == 6_lcol ) {
                        REQUIRE( span.backColor() == QColor{ Qt::yellow } );
                        found = true;
                    }
                }
                REQUIRE( found );

                AND_THEN( "the untouched prefix keeps the whole-line Highlighter's color" )
                {
                    const auto& first = decoration.spans().front();
                    REQUIRE( first.startColumn() == 0_lcol );
                    REQUIRE( first.backColor() == QColor{ Qt::red } );
                }
            }
        }

        WHEN( "QuickFind overlaps a Color Label, with no whole-line Highlighter or main search" )
        {
            auto localContext = emptyContext();
            localContext.colorLabels.push_back(
                Highlighter{ "wor", false, true, QColor{ Qt::black }, QColor{ Qt::green } } );
            QRegularExpression qfRegex{ "wor" };
            localContext.quickFind = QuickFindMatcher{ true, qfRegex };
            localContext.quickFindColor = QColor{ Qt::cyan };
            LineDecorator localDecorator{ std::move( localContext ) };

            const auto decoration = localDecorator.decorate( text, LineVerdict{} );

            THEN( "QuickFind wins over the Color Label on the overlap" )
            {
                REQUIRE( decoration.spans().size() == 3 );
                const auto& span = decoration.spans()[ 1 ];
                REQUIRE( span.startColumn() == 6_lcol );
                REQUIRE( span.backColor() == QColor{ Qt::cyan } );
            }
        }

        WHEN( "a selection overlaps every other source" )
        {
            const HighlightedMatch selection{ 6_lcol, LineLength{ 5 }, QColor{ Qt::white },
                                              QColor{ Qt::blue } };
            const auto decoration = decorator.decorate( text, wholeLineVerdict, selection );

            THEN( "the selection wins the whole overlapped region" )
            {
                bool found = false;
                for ( const auto& span : decoration.spans() ) {
                    if ( span.startColumn() == 6_lcol ) {
                        REQUIRE( span.size() == LineLength{ 5 } );
                        REQUIRE( span.backColor() == QColor{ Qt::blue } );
                        found = true;
                    }
                }
                REQUIRE( found );
            }
        }
    }

    GIVEN( "a Line Verdict that falls outside the Search Limits" )
    {
        auto context = emptyContext();
        context.mainSearch
            = Highlighter{ "wor", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } };
        context.colorLabels.push_back(
            Highlighter{ "wor", false, true, QColor{ Qt::black }, QColor{ Qt::green } } );
        {
            QRegularExpression qfRegex{ "wor" };
            context.quickFind = QuickFindMatcher{ true, qfRegex };
        }
        context.quickFindColor = QColor{ Qt::cyan };
        LineDecorator decorator{ std::move( context ) };

        const LineVerdict outsideVerdict{ HighlightColor{ QColor{ Qt::white }, QColor{ Qt::red } },
                                          LineTypeFlags::Plain, true };

        WHEN( "decorating text under that verdict" )
        {
            const auto decoration = decorator.decorate( "hello world", outsideVerdict );

            THEN( "the whole-line highlight, main search and Color Labels are suppressed, "
                  "but QuickFind still shows" )
            {
                REQUIRE( decoration.spans().size() == 3 );
                const auto& subdued = decoration.spans().front();
                REQUIRE( subdued.foreColor() == TestPalette.subduedText );
                REQUIRE( subdued.backColor() == TestPalette.base );
                const auto& span = decoration.spans()[ 1 ];
                REQUIRE( span.startColumn() == 6_lcol );
                REQUIRE( span.backColor() == QColor{ Qt::cyan } );
            }
        }
    }

    GIVEN( "a decorator with a word-only Highlighter for ERROR" )
    {
        auto context = emptyContext();
        context.highlighterSet
            = setWithHighlighter( "ERROR", true, QColor{ Qt::white }, QColor{ Qt::red } );
        LineDecorator decorator{ std::move( context ) };

        const QString text = "an ERROR occurred";
        const auto verdict = decorator.verdictFor( LogLine{ 0_lnum, text }, LineTypeFlags::Plain );

        WHEN( "decorating the matched line" )
        {
            const auto decoration = decorator.decorate( text, verdict );

            THEN( "the matched word carries the Highlighter's colors" )
            {
                REQUIRE( decoration.spans().size() == 3 );
                const auto& span = decoration.spans()[ 1 ];
                REQUIRE( span.startColumn() == 3_lcol );
                REQUIRE( span.size() == LineLength{ 5 } );
                REQUIRE( span.foreColor() == QColor{ Qt::white } );
                REQUIRE( span.backColor() == QColor{ Qt::red } );
            }
        }

        WHEN( "a Search Limit puts the line outside the limits" )
        {
            auto outsideContext = emptyContext();
            outsideContext.highlighterSet
                = setWithHighlighter( "ERROR", true, QColor{ Qt::white }, QColor{ Qt::red } );
            outsideContext.searchLimits = SearchLimits{ 5_lnum, 10_lnum };
            LineDecorator outsideDecorator{ std::move( outsideContext ) };
            const auto outsideVerdict
                = outsideDecorator.verdictFor( LogLine{ 0_lnum, text }, LineTypeFlags::Plain );

            const auto decoration = outsideDecorator.decorate( text, outsideVerdict );

            THEN( "the word highlight is suppressed" )
            {
                REQUIRE( decoration.spans().size() == 1 );
                REQUIRE( decoration.spans().front().foreColor() == TestPalette.subduedText );
            }
        }
    }

    GIVEN( "a Highlighter Set with a word-only rule added before a whole-line rule" )
    {
        // HighlighterSet::matchLine walks the set in reverse, so the
        // whole-line rule (added second, matched first) sets matchType to
        // LineMatch, and the word-only rule (added first, matched last)
        // still layers its own span on top -- the two are not mutually
        // exclusive the way a single verdict's wholeLineHighlight() is.
        auto set = HighlighterSet::createNewSet( "test" );
        set.addHighlighter(
            Highlighter{ "CRITICAL", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } } );
        set.addHighlighter(
            Highlighter{ "ERROR", false, false, QColor{ Qt::white }, QColor{ Qt::red } } );

        auto context = emptyContext();
        context.highlighterSet = set;
        LineDecorator decorator{ std::move( context ) };

        const QString text = "an ERROR occurred: CRITICAL failure";
        const auto verdict = decorator.verdictFor( LogLine{ 0_lnum, text }, LineTypeFlags::Plain );

        WHEN( "decorating the matched line" )
        {
            const auto decoration = decorator.decorate( text, verdict );

            THEN( "the CRITICAL word still stands out within the whole-line highlight" )
            {
                bool foundCritical = false;
                for ( const auto& span : decoration.spans() ) {
                    if ( span.backColor() == QColor{ Qt::yellow } ) {
                        foundCritical = true;
                        REQUIRE( span.startColumn() == LineColumn{ text.indexOf( "CRITICAL" ) } );
                    }
                }
                REQUIRE( foundCritical );

                AND_THEN( "the rest of the line keeps the whole-line Highlighter's color" )
                {
                    const auto& first = decoration.spans().front();
                    REQUIRE( first.startColumn() == 0_lcol );
                    REQUIRE( first.backColor() == QColor{ Qt::red } );
                }
            }
        }
    }
}

// Issue #80's coordinate-space unification: QuickFind is now matched
// against the raw Log Line (via LineDecorator's Context, as of #80),
// instead of the tab-expanded display line. This is a deliberate,
// acknowledged behavior change for patterns that match whitespace or tab
// characters -- these two scenarios pin down exactly what changed, using
// "a\tb" (one raw tab between two letters, expanding to "a" followed by
// 7 spaces up to the next tab stop, then "b").
SCENARIO( "QuickFind is matched against the raw line, not the tab-expanded line",
          "[linedecorator][quickfind-raw-space]" )
{
    const QString rawLine = "a\tb";

    GIVEN( "a QuickFind pattern for a literal tab character" )
    {
        auto context = emptyContext();
        QRegularExpression qfRegex{ "\\t" };
        context.quickFind = QuickFindMatcher{ true, qfRegex };
        LineDecorator decorator{ std::move( context ) };

        WHEN( "decorating the raw line" )
        {
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, rawLine }, LineTypeFlags::Plain );
            const auto decoration = decorator.decorate( rawLine, verdict );

            THEN( "the tab character itself is found -- before #80, matching against the "
                  "expanded line (all spaces) never found a tab at all" )
            {
                REQUIRE( decoration.spans().size() == 3 );
                const auto& span = decoration.spans()[ 1 ];
                REQUIRE( span.startColumn() == 1_lcol );
                REQUIRE( span.size() == LineLength{ 1 } );
            }
        }
    }

    GIVEN( "a QuickFind pattern for the run of spaces the tab used to expand to" )
    {
        auto context = emptyContext();
        QRegularExpression qfRegex{ " {7}" }; // "a\tb" expanded to "a" + 7 spaces + "b"
        context.quickFind = QuickFindMatcher{ true, qfRegex };
        LineDecorator decorator{ std::move( context ) };

        WHEN( "decorating the raw line" )
        {
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, rawLine }, LineTypeFlags::Plain );
            const auto decoration = decorator.decorate( rawLine, verdict );

            THEN( "there is no match -- before #80, matching against the expanded line found "
                  "one, even though the file contains no run of spaces at all, only a tab" )
            {
                REQUIRE( decoration.spans().size() == 1 );
                REQUIRE( decoration.spans().front().backColor() == TestPalette.base );
            }
        }
    }
}

// A line's own colors are decided in one place, from its Line Verdict (#241).
SCENARIO( "LineDecorator::lineColorsFor decides a line's own colors from its Line Verdict",
          "[linedecorator][linecolors]" )
{
    GIVEN( "a decorator for a Presentation that shows Match and Mark as a background" )
    {
        auto context = emptyContext();
        context.lineStatus = LineStatusDisplay::AsBackground;
        const LineDecorator decorator{ std::move( context ) };

        THEN( "a Plain line has the palette's text and base colors" )
        {
            const auto colors = decorator.lineColorsFor(
                LineVerdict{ std::nullopt, LineTypeFlags::Plain, false } );
            REQUIRE( colors.foreColor == TestPalette.text );
            REQUIRE( colors.backColor == TestPalette.base );
        }

        THEN( "a Match, a Mark and a Mark that is a Match each have their own background" )
        {
            REQUIRE(
                decorator.lineColorsFor( LineVerdict{ std::nullopt, LineTypeFlags::Match, false } )
                    .backColor
                == LineStatusColors::match() );
            REQUIRE(
                decorator.lineColorsFor( LineVerdict{ std::nullopt, LineTypeFlags::Mark, false } )
                    .backColor
                == LineStatusColors::mark() );
            REQUIRE( decorator
                         .lineColorsFor( LineVerdict{
                             std::nullopt, LineTypeFlags::Mark | LineTypeFlags::Match, false } )
                         .backColor
                     == LineStatusColors::markedMatch() );
        }

        THEN( "marking a line changes its background" )
        {
            const auto before = decorator.lineColorsFor(
                LineVerdict{ std::nullopt, LineTypeFlags::Plain, false } );
            const auto after = decorator.lineColorsFor(
                LineVerdict{ std::nullopt, LineTypeFlags::Mark, false } );
            REQUIRE( before.backColor != after.backColor );
        }

        THEN( "a Context Line's text is dimmed and its background left alone" )
        {
            const auto colors = decorator.lineColorsFor(
                LineVerdict{ std::nullopt, LineTypeFlags::Context, false } );
            REQUIRE( colors.foreColor.alpha() == 128 );
            REQUIRE( colors.backColor == TestPalette.base );
        }

        THEN( "a whole-line Highlighter wins over the Match background" )
        {
            const auto colors = decorator.lineColorsFor(
                LineVerdict{ HighlightColor{ QColor{ Qt::white }, QColor{ Qt::green } },
                             LineTypeFlags::Match, false } );
            REQUIRE( colors.foreColor == QColor{ Qt::white } );
            REQUIRE( colors.backColor == QColor{ Qt::green } );
        }

        THEN( "a line outside the Search Limits is subdued and shows no Match background" )
        {
            const auto colors = decorator.lineColorsFor( LineVerdict{
                std::nullopt, LineTypeFlags::Match, /* isOutsideSearchLimits = */ true } );
            REQUIRE( colors.foreColor == TestPalette.subduedText );
            REQUIRE( colors.backColor == TestPalette.base );
        }

        THEN( "a line selected as a whole has the selection colors, whatever else it is" )
        {
            const auto colors = decorator.lineColorsFor(
                LineVerdict{ HighlightColor{ QColor{ Qt::white }, QColor{ Qt::green } },
                             LineTypeFlags::Match,
                             true,
                             {},
                             /* isSelectedAsWhole = */ true } );
            REQUIRE( colors.foreColor == TestPalette.selectedText );
            REQUIRE( colors.backColor == TestPalette.selection );
        }
    }

    GIVEN( "a decorator for a Presentation that shows Match and Mark in a gutter" )
    {
        const LineDecorator decorator{ emptyContext() };

        THEN( "a Marked Match keeps the palette's base" )
        {
            const auto colors = decorator.lineColorsFor(
                LineVerdict{ std::nullopt, LineTypeFlags::Mark | LineTypeFlags::Match, false } );
            REQUIRE( colors.backColor == TestPalette.base );
        }
    }
}

SCENARIO( "A line selected as a whole shows the selection colors with only its QuickFind matches",
          "[linedecorator][selectedaswhole]" )
{
    GIVEN( "a whole-line Highlighter, a main search and a QuickFind pattern that all match" )
    {
        auto context = emptyContext();
        context.highlighterSet
            = setWithHighlighter( "hello", false, QColor{ Qt::white }, QColor{ Qt::red } );
        context.mainSearch
            = Highlighter{ "hello", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } };
        QRegularExpression qfRegex{ "wor" };
        context.quickFind = QuickFindMatcher{ true, qfRegex };
        context.quickFindColor = QColor{ Qt::cyan };
        const LineDecorator decorator{ std::move( context ) };

        const QString text = "hello world";

        WHEN( "the line is selected as a whole" )
        {
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, text }, LineTypeFlags::Plain, true );
            const auto decoration = decorator.decorate( text, verdict );

            THEN( "no Highlighter is matched against it" )
            {
                REQUIRE( verdict.isSelectedAsWhole() );
                REQUIRE_FALSE( verdict.isOutsideSearchLimits() );
                REQUIRE_FALSE( verdict.wholeLineHighlight().has_value() );
            }

            THEN( "the text is in the selection colors, with the QuickFind match on top" )
            {
                REQUIRE( coversWithoutGaps( decoration, static_cast<int>( text.size() ) ) );
                REQUIRE( decoration.spans().size() == 3 );
                REQUIRE( decoration.spans()[ 0 ].backColor() == TestPalette.selection );
                REQUIRE( decoration.spans()[ 0 ].foreColor() == TestPalette.selectedText );
                REQUIRE( decoration.spans()[ 1 ].startColumn() == 6_lcol );
                REQUIRE( decoration.spans()[ 1 ].size() == LineLength{ 3 } );
                REQUIRE( decoration.spans()[ 1 ].backColor() == QColor{ Qt::cyan } );
                REQUIRE( decoration.spans()[ 2 ].backColor() == TestPalette.selection );
                REQUIRE( decoration.lineColors().backColor == TestPalette.selection );
            }
        }
    }
}

// The Text View decorates the raw Log Line: it moves its selection from
// display columns to raw columns first, and the finished Decoration to display
// columns once afterwards (#241).
SCENARIO( "A partial selection over text containing tabs goes through the Line Decorator",
          "[linedecorator][tabs]" )
{
    // "ab\tcd" expands to "ab" + 6 spaces + "cd": display columns 0-1 are
    // "ab", 2-7 the tab, 8-9 "cd".
    const QString rawText = "ab\tcd";
    const LineLength displayLength{ 10 };
    const QColor selectedText{ Qt::white };
    const QColor selection{ Qt::blue };

    GIVEN( "a decorator with a QuickFind pattern for cd" )
    {
        auto context = emptyContext();
        QRegularExpression qfRegex{ "cd" };
        context.quickFind = QuickFindMatcher{ true, qfRegex };
        context.quickFindColor = QColor{ Qt::cyan };
        const LineDecorator decorator{ std::move( context ) };
        const auto verdict
            = decorator.verdictFor( LogLine{ 0_lnum, rawText }, LineTypeFlags::Plain );

        WHEN( "display columns 1 to 4 are selected -- the b and part of the tab" )
        {
            const auto rawSelection = inRawColumns(
                rawText, HighlightedMatch{ 1_lcol, LineLength{ 4 }, selectedText, selection } );

            THEN( "the selection covers the b and the whole tab in raw columns" )
            {
                REQUIRE( rawSelection.startColumn() == 1_lcol );
                REQUIRE( rawSelection.size() == LineLength{ 2 } );
            }

            AND_WHEN( "the text is decorated and moved to display columns" )
            {
                const auto decoration = decorator.decorate( rawText, verdict, rawSelection )
                                            .inDisplayColumns( rawText, displayLength );

                THEN( "the Decoration covers the displayed text without gaps" )
                {
                    REQUIRE( coversWithoutGaps( decoration, 10 ) );
                }

                THEN( "the a is in the line's colors, the b and the expanded tab are selected, "
                      "and the QuickFind match lands on the displayed cd" )
                {
                    const auto& spans = decoration.spans();
                    REQUIRE( spans.size() == 3 );
                    REQUIRE( spans[ 0 ].startColumn() == 0_lcol );
                    REQUIRE( spans[ 0 ].backColor() == TestPalette.base );
                    REQUIRE( spans[ 1 ].startColumn() == 1_lcol );
                    REQUIRE( spans[ 1 ].size() == LineLength{ 7 } );
                    REQUIRE( spans[ 1 ].backColor() == selection );
                    REQUIRE( spans[ 2 ].startColumn() == 8_lcol );
                    REQUIRE( spans[ 2 ].size() == LineLength{ 2 } );
                    REQUIRE( spans[ 2 ].backColor() == QColor{ Qt::cyan } );
                }
            }
        }
    }
}

SCENARIO( "A Decoration covers the whole text without gaps", "[linedecorator][coverage]" )
{
    GIVEN( "a decorator with a word-only Highlighter, a main search, a Color Label and QuickFind" )
    {
        auto context = emptyContext();
        context.highlighterSet
            = setWithHighlighter( "ERROR", true, QColor{ Qt::white }, QColor{ Qt::red } );
        context.mainSearch
            = Highlighter{ "disk", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } };
        context.colorLabels.push_back(
            Highlighter{ "full", false, true, QColor{ Qt::black }, QColor{ Qt::green } } );
        QRegularExpression qfRegex{ "is" };
        context.quickFind = QuickFindMatcher{ true, qfRegex };
        context.quickFindColor = QColor{ Qt::cyan };
        const LineDecorator decorator{ std::move( context ) };

        const QString text = "ERROR: disk is full, ERROR again";

        WHEN( "decorating the text" )
        {
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, text }, LineTypeFlags::Plain );
            const auto decoration = decorator.decorate( text, verdict );

            THEN( "the spans cover it in order, without a gap or an overlap" )
            {
                REQUIRE( coversWithoutGaps( decoration, static_cast<int>( text.size() ) ) );
            }

            THEN( "the text between the sources carries the line's own colors" )
            {
                REQUIRE( decoration.spans()[ 1 ].startColumn() == 5_lcol );
                REQUIRE( decoration.spans()[ 1 ].foreColor() == TestPalette.text );
                REQUIRE( decoration.spans()[ 1 ].backColor() == TestPalette.base );
            }
        }

        WHEN( "a selection reaches past the end of the text" )
        {
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, text }, LineTypeFlags::Plain );
            const auto decoration
                = decorator.decorate( text, verdict,
                                      HighlightedMatch{ 25_lcol, LineLength{ 40 },
                                                        QColor{ Qt::white }, QColor{ Qt::blue } } );

            THEN( "the Decoration still ends where the text ends" )
            {
                REQUIRE( coversWithoutGaps( decoration, static_cast<int>( text.size() ) ) );
                REQUIRE( decoration.spans().back().backColor() == QColor{ Qt::blue } );
            }
        }

        WHEN( "decorating empty text" )
        {
            const auto decoration = decorator.decorate( "", LineVerdict{} );

            THEN( "there is no span, but the line still has its own colors" )
            {
                REQUIRE( decoration.spans().empty() );
                REQUIRE( decoration.lineColors().backColor == TestPalette.base );
            }
        }
    }
}

// Issue #293: matching a Log Line against the Highlighter Set reuses what it
// scanned the previous line with instead of setting it up again for every
// line. These scenarios pin down that nothing one line leaves behind shows
// in the next one, whichever Highlighter Set and thread it comes from.
namespace {

// The start column and length of every Highlighter span, in order.
logsquirl::vector<std::pair<int, int>> spanColumns( const LineVerdict& verdict )
{
    logsquirl::vector<std::pair<int, int>> columns;
    for ( const auto& span : verdict.highlighterSpans() ) {
        columns.emplace_back( static_cast<int>( span.startColumn().get() ),
                              static_cast<int>( span.size().get() ) );
    }
    return columns;
}

LineDecorator decoratorWith( HighlighterSet set )
{
    auto context = emptyContext();
    context.highlighterSet = std::move( set );
    return LineDecorator{ std::move( context ) };
}

HighlighterSet errorAndWarnSet()
{
    auto set = HighlighterSet::createNewSet( "errors and warnings" );
    set.addHighlighter(
        Highlighter{ "ERROR", false, true, QColor{ Qt::white }, QColor{ Qt::red } } );
    set.addHighlighter(
        Highlighter{ "WARN", false, true, QColor{ Qt::black }, QColor{ Qt::yellow } } );
    return set;
}

using Columns = logsquirl::vector<std::pair<int, int>>;

} // namespace

SCENARIO( "Each Log Line is matched against the Highlighter Set on its own",
          "[linedecorator][highlighter-reuse]" )
{
    GIVEN( "a decorator with word-only Highlighters for ERROR and WARN" )
    {
        const auto decorator = decoratorWith( errorAndWarnSet() );

        WHEN( "a very long line is followed by short ones" )
        {
            const QString longLine = QString( 5000, QChar{ 'x' } ) + "WARN";
            const auto longVerdict
                = decorator.verdictFor( LogLine{ 0_lnum, longLine }, LineTypeFlags::Plain );
            const auto noMatchVerdict
                = decorator.verdictFor( LogLine{ 1_lnum, "xx" }, LineTypeFlags::Plain );
            const auto shortVerdict
                = decorator.verdictFor( LogLine{ 2_lnum, "an ERROR" }, LineTypeFlags::Plain );

            THEN( "every line gets only its own matches" )
            {
                REQUIRE( spanColumns( longVerdict ) == Columns{ { 5000, 4 } } );
                REQUIRE( spanColumns( noMatchVerdict ).empty() );
                REQUIRE( spanColumns( shortVerdict ) == Columns{ { 3, 5 } } );
            }
        }

        WHEN( "a line with multi-byte characters is followed by a plain one" )
        {
            const auto wideVerdict = decorator.verdictFor(
                LogLine{ 0_lnum, QStringLiteral( "Größe → ERROR" ) }, LineTypeFlags::Plain );
            const auto plainVerdict
                = decorator.verdictFor( LogLine{ 1_lnum, "WARN ok" }, LineTypeFlags::Plain );

            THEN( "the columns are the lines' own characters" )
            {
                REQUIRE( spanColumns( wideVerdict ) == Columns{ { 8, 5 } } );
                REQUIRE( spanColumns( plainVerdict ) == Columns{ { 0, 4 } } );
            }
        }
    }

    GIVEN( "two decorators with different Highlighter Sets on one thread" )
    {
        const auto errorsAndWarnings = decoratorWith( errorAndWarnSet() );
        const auto infoOnly = decoratorWith(
            setWithHighlighter( "INFO", true, QColor{ Qt::white }, QColor{ Qt::green } ) );

        WHEN( "they match lines in turn" )
        {
            const QString text = "INFO then ERROR";
            Columns fromErrors;
            Columns fromInfo;
            for ( int i = 0; i < 3; ++i ) {
                fromErrors = spanColumns(
                    errorsAndWarnings.verdictFor( LogLine{ 0_lnum, text }, LineTypeFlags::Plain ) );
                fromInfo = spanColumns(
                    infoOnly.verdictFor( LogLine{ 0_lnum, text }, LineTypeFlags::Plain ) );
            }

            THEN( "each matches with its own Highlighter Set" )
            {
                REQUIRE( fromErrors == Columns{ { 10, 5 } } );
                REQUIRE( fromInfo == Columns{ { 0, 4 } } );
            }
        }
    }

    GIVEN( "one compiled Highlighter Set shared by decorators on several threads" )
    {
        auto set = errorAndWarnSet();
        set.compile();

        WHEN( "every thread matches many lines at once" )
        {
            constexpr int ThreadCount = 4;
            constexpr int LinesPerThread = 500;
            std::atomic<int> wrongVerdicts{ 0 };

            std::vector<std::thread> threads;
            for ( int t = 0; t < ThreadCount; ++t ) {
                threads.emplace_back( [ set, t, &wrongVerdicts ] {
                    const auto decorator = decoratorWith( set );
                    for ( int i = 0; i < LinesPerThread; ++i ) {
                        // Each thread and line puts the match somewhere else.
                        const int padding = ( t * 7 + i ) % 40;
                        const QString text
                            = QString( padding, QChar{ ' ' } ) + ( i % 2 == 0 ? "ERROR" : "WARN" );
                        const auto columns = spanColumns(
                            decorator.verdictFor( LogLine{ 0_lnum, text }, LineTypeFlags::Plain ) );
                        const Columns expected{ { padding, i % 2 == 0 ? 5 : 4 } };
                        if ( columns != expected ) {
                            ++wrongVerdicts;
                        }
                    }
                } );
            }
            for ( auto& thread : threads ) {
                thread.join();
            }

            THEN( "every line gets exactly its own match" )
            {
                REQUIRE( wrongVerdicts.load() == 0 );
            }
        }
    }
}

SCENARIO( "A word-only Highlighter that varies its colors keeps them per matched text",
          "[linedecorator][highlighter-reuse]" )
{
    GIVEN( "a Highlighter with a capture group and color variation" )
    {
        Highlighter highlighter{ "user=(\\w+)", false, true, QColor{ 200, 100, 50 },
                                 QColor{ 20, 40, 160 } };
        highlighter.setVariateColors( true );
        highlighter.setColorVariance( 30 );
        auto set = HighlighterSet::createNewSet( "users" );
        set.addHighlighter( highlighter );
        const auto decorator = decoratorWith( set );

        WHEN( "a line names two users" )
        {
            const QString text = "user=alice user=bob";
            const auto verdict
                = decorator.verdictFor( LogLine{ 0_lnum, text }, LineTypeFlags::Plain );

            THEN( "each captured name gets the colors its text varies them to" )
            {
                // The colors color variation gave these names before #293: a
                // darkening factor drawn from a generator seeded with the CRC32
                // of the text. The distribution is the standard library's own,
                // so the values are computed here, not written down.
                const auto varied = []( const QString& name ) {
                    std::uniform_int_distribution<int> distribution( 100 - 30, 100 + 30 );
                    std::minstd_rand0 generator( Crc32::calculate( name.toUtf8() ) );
                    const auto factor = distribution( generator );
                    return std::make_pair( QColor{ 200, 100, 50 }.darker( factor ).name(),
                                           QColor{ 20, 40, 160 }.darker( factor ).name() );
                };
                REQUIRE( spanColumns( verdict ) == Columns{ { 5, 5 }, { 16, 3 } } );
                const auto& spans = verdict.highlighterSpans();
                REQUIRE(
                    std::make_pair( spans[ 0 ].foreColor().name(), spans[ 0 ].backColor().name() )
                    == varied( "alice" ) );
                REQUIRE(
                    std::make_pair( spans[ 1 ].foreColor().name(), spans[ 1 ].backColor().name() )
                    == varied( "bob" ) );
                REQUIRE( varied( "alice" ) != varied( "bob" ) );
            }
        }
    }
}

SCENARIO( "A Highlighter matches with the pattern it has now",
          "[linedecorator][highlighter-reuse]" )
{
    GIVEN( "a word-only Highlighter for ERROR that has matched a line" )
    {
        Highlighter highlighter{ "ERROR", false, true, QColor{ Qt::white }, QColor{ Qt::red } };
        logsquirl::vector<HighlightedMatch> matches;
        REQUIRE( highlighter.matchLine( "an ERROR", matches ) );

        WHEN( "a copy of it is changed to WARN, case insensitive and literal" )
        {
            auto changed = highlighter;
            changed.setPattern( "W.RN" );
            changed.setIgnoreCase( true );
            changed.setUseRegex( false );

            THEN( "the copy matches its new pattern and the original its old one" )
            {
                REQUIRE_FALSE( changed.matchLine( "an ERROR, a warn", matches ) );
                REQUIRE( changed.matchLine( "an ERROR, a w.rn", matches ) );
                REQUIRE( matches.size() == 1 );
                REQUIRE( matches.front().startColumn().get() == 12 );

                REQUIRE( highlighter.matchLine( "an ERROR, a w.rn", matches ) );
                REQUIRE( matches.size() == 1 );
                REQUIRE( matches.front().startColumn().get() == 3 );
            }
        }
    }
}
