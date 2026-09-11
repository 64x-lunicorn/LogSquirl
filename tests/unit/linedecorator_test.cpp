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

#include "linedecorator.h"

namespace {

using LineTypeFlags = AbstractLogData::LineTypeFlags;

HighlighterSet setWithHighlighter( const QString& pattern, bool highlightOnlyMatch,
                                   const QColor& foreColor, const QColor& backColor )
{
    auto set = HighlighterSet::createNewSet( "test" );
    set.addHighlighter( Highlighter{ pattern, false, highlightOnlyMatch, foreColor, backColor } );
    return set;
}

LineDecorator::Context emptyContext()
{
    return LineDecorator::Context{ HighlighterSet{},   std::nullopt,         {},
                                   QuickFindMatcher{}, QColor{ Qt::yellow }, SearchLimits{} };
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

    GIVEN( "a decorator with Search Limits restricted to lines 5-10" )
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

        WHEN( "asked about a line after the limits" )
        {
            const auto verdict = decorator.verdictFor( LogLine{ 11_lnum, "an ERROR occurred" },
                                                       LineTypeFlags::Plain );

            THEN( "it falls outside the Search Limits" )
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

            THEN( "there are no spans" )
            {
                REQUIRE( decoration.spans().empty() );
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
                REQUIRE( decoration.spans().size() == 1 );
                const auto& span = decoration.spans().front();
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
                REQUIRE( decoration.spans().size() == 1 );
                const auto& span = decoration.spans().front();
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
                REQUIRE( decoration.spans().size() == 1 );
                const auto& span = decoration.spans().front();
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
                REQUIRE( decoration.spans().empty() );
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
                REQUIRE( decoration.spans().size() == 1 );
                const auto& span = decoration.spans().front();
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
                REQUIRE( decoration.spans().empty() );
            }
        }
    }
}
