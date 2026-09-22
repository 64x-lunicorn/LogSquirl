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

// The Search Line without a widget (#399): how adding a word to the Search,
// excluding one, replacing the Search or combining Predefined Filters edits
// its pattern in each reading of it, and what the line says about the Search
// that runs. The Crawler Widget's tests keep a few of these to check that it
// mirrors the line.

#include "regularexpression.h"
#include "searchline.h"

#include <QString>

#include <string>

#include <catch2/catch.hpp>

namespace {

// How the pattern is read: the regexp and logical combination buttons.
enum class Reading { Plain, Regexp, Boolean, BooleanRegexp };

std::string describe( Reading reading )
{
    switch ( reading ) {
    case Reading::Plain:
        return "read as a fixed string";
    case Reading::Regexp:
        return "read as a regexp";
    case Reading::Boolean:
        return "read as a logical combination of fixed strings";
    case Reading::BooleanRegexp:
        return "read as a logical combination of regexps";
    }
    return {};
}

QuickFindPolicy startingState( Reading reading, bool autoRun = false )
{
    QuickFindPolicy policy;
    policy.mainRegexpType = ( reading == Reading::Regexp || reading == Reading::BooleanRegexp )
                                ? SearchRegexpType::ExtendedRegexp
                                : SearchRegexpType::FixedString;
    policy.searchLogicalCombiningDefault
        = reading == Reading::Boolean || reading == Reading::BooleanRegexp;
    policy.autoRunSearchOnPatternChange = autoRun;
    return policy;
}

SearchLine lineWith( Reading reading, const QString& pattern )
{
    SearchLine line{ startingState( reading ) };
    line.setPattern( pattern );
    return line;
}

// A Search the Search Session tells about.
SearchSession::State session( SearchSession::Phase phase, int progress = 0,
                              LinesCount matches = 0_lcount )
{
    SearchSession::State state;
    state.phase = phase;
    state.progress = progress;
    state.matchCount = matches;
    return state;
}

// A line whose Search runs.
SearchLine runningLine()
{
    SearchLine line{ startingState( Reading::Plain ) };
    line.setPattern( "alpha" );
    line.requested( session( SearchSession::Phase::Running ) );
    return line;
}

// Whether the pattern and the buttons ask for a Search that compiles and
// matches text.
bool matches( const SearchLine& line, const std::string& text )
{
    const RegularExpression expression{ line.request(), RegexpEngine::QRegularExpression };
    UNSCOPED_INFO( "pattern: " << line.pattern().toStdString()
                               << ", error: " << expression.errorString().toStdString() );
    return expression.isValid() && expression.createMatcher()->hasMatch( text );
}

using Phase = SearchSession::Phase;
using AutoRefresh = SearchAutoRefresh::State;

} // namespace

SCENARIO( "The Search Line starts in the state the QuickFind Policy says", "[searchline]" )
{
    QuickFindPolicy policy;
    policy.mainRegexpType = SearchRegexpType::ExtendedRegexp;
    policy.searchIgnoreCaseDefault = false;
    policy.searchAutoRefreshDefault = true;
    policy.searchLogicalCombiningDefault = true;

    const SearchLine line{ policy };

    THEN( "its buttons are as the Policy says, inverse off, and no pattern" )
    {
        REQUIRE( line.flags()
                 == SearchLine::Flags{ .matchCase = true,
                                       .useRegexp = true,
                                       .inverse = false,
                                       .booleanCombination = true,
                                       .autoRefresh = true } );
        REQUIRE( line.pattern().isEmpty() );
    }

    THEN( "it shows the Search and Clear buttons and no text" )
    {
        REQUIRE( line.display() == SearchLine::Display{} );
    }

    WHEN( "the pattern is read as a fixed string or a wildcard, and case is ignored" )
    {
        for ( const auto type : { SearchRegexpType::FixedString, SearchRegexpType::Wildcard } ) {
            policy.mainRegexpType = type;
            policy.searchIgnoreCaseDefault = true;
            const SearchLine plain{ policy };
            REQUIRE_FALSE( plain.flags().useRegexp );
            REQUIRE_FALSE( plain.flags().matchCase );
        }
    }
}

SCENARIO( "The Search Line asks for the Search its pattern and buttons say", "[searchline]" )
{
    SearchLine line{ startingState( Reading::Plain ) };
    line.setPattern( "alpha" );
    line.setFlags( { .matchCase = true,
                     .useRegexp = false,
                     .inverse = true,
                     .booleanCombination = true,
                     .autoRefresh = false } );

    const auto request = line.request();

    REQUIRE( request.pattern == "alpha" );
    REQUIRE( request.isCaseSensitive );
    REQUIRE( request.isExclude );
    REQUIRE( request.isBoolean );
    REQUIRE( request.isPlainText );
}

SCENARIO( "A word edits the Search Line's pattern as the buttons read it", "[searchline]" )
{
    struct Row {
        Reading reading;
        QString pattern;
        QString added;
        QString excluded;
        QString replaced;
        QString filtersCombined;
    };

    // The word a.b, whose dot a regexp escapes; the Predefined Filters a.b,
    // a fixed string, and x|y, a regexp.
    const auto row = GENERATE( values<Row>( {
        { Reading::Plain, "alpha", "alphaa.b", R"("alpha" and not("a.b"))", "a.b", "a.bx|y" },
        { Reading::Regexp, "alpha", R"(alpha|a\.b)", R"("alpha" and not("a\.b"))", R"(a\.b)",
          R"(a\.b|x|y)" },
        { Reading::Boolean, R"("alpha")", R"("alpha" or "a.b")", R"("alpha" and not("a.b"))",
          R"("a.b")", R"("a.b" or "x|y")" },
        { Reading::BooleanRegexp, R"("alpha")", R"("alpha" or "a\.b")",
          R"("alpha" and not("a\.b"))", R"("a\.b")", R"("a\.b" or "x|y")" },
    } ) );

    GIVEN( "a Search for " << row.pattern.toStdString() << ", " << describe( row.reading ) )
    {
        auto line = lineWith( row.reading, row.pattern );
        const auto flagsBefore = line.flags();

        WHEN( "the word is added to it" )
        {
            line.add( "a.b" );

            THEN( "it is an alternative of the pattern" )
            {
                REQUIRE( line.pattern() == row.added );
                REQUIRE( line.flags() == flagsBefore );
            }
        }

        WHEN( "the word is excluded from it" )
        {
            line.exclude( "a.b" );

            THEN( "the pattern is a logical combination that excludes it" )
            {
                REQUIRE( line.pattern() == row.excluded );
                auto expected = flagsBefore;
                expected.booleanCombination = true;
                REQUIRE( line.flags() == expected );
            }
        }

        WHEN( "the Search is replaced with the word" )
        {
            line.replace( "a.b" );

            THEN( "the word is the pattern" )
            {
                REQUIRE( line.pattern() == row.replaced );
                REQUIRE( line.flags() == flagsBefore );
            }
        }

        WHEN( "Predefined Filters are combined" )
        {
            line.useFilters( { { "fixed", "a.b", false }, { "regexp", "x|y", true } } );

            THEN( "they are the pattern, as alternatives" )
            {
                REQUIRE( line.pattern() == row.filtersCombined );
                REQUIRE( line.flags() == flagsBefore );
            }
        }

        WHEN( "no Predefined Filter is combined" )
        {
            line.useFilters( {} );

            THEN( "the pattern is empty" )
            {
                REQUIRE( line.pattern().isEmpty() );
            }
        }
    }
}

// In the logical combination mode every sub-pattern is enclosed in quotes and
// a quote inside it is written \" (#398).
SCENARIO( "A word with quotes keeps the Search Line's pattern valid", "[searchline][search]" )
{
    const QString quotedWord = R"(say "hi")";

    for ( const auto reading : { Reading::Boolean, Reading::BooleanRegexp } ) {
        GIVEN( "a Search " << describe( reading ) )
        {
            WHEN( "the word is added to a Search for nothing" )
            {
                auto line = lineWith( reading, R"("nothing")" );
                line.add( quotedWord );

                THEN( "the Search matches the word with its quotes" )
                {
                    if ( reading == Reading::Boolean ) {
                        REQUIRE( line.pattern() == R"("nothing" or "say \"hi\"")" );
                    }
                    REQUIRE( matches( line, R"(alpha say "hi")" ) );
                    REQUIRE_FALSE( matches( line, "alpha say hi" ) );
                }
            }

            WHEN( "the word is excluded from a Search for alpha" )
            {
                auto line = lineWith( reading, R"("alpha")" );
                line.exclude( quotedWord );

                THEN( "the Search matches alpha without the word" )
                {
                    if ( reading == Reading::Boolean ) {
                        REQUIRE( line.pattern() == R"("alpha" and not("say \"hi\""))" );
                    }
                    REQUIRE( matches( line, "alpha say hi" ) );
                    REQUIRE_FALSE( matches( line, R"(alpha say "hi")" ) );
                }
            }

            WHEN( "the Search is replaced with the word" )
            {
                auto line = lineWith( reading, R"("alpha")" );
                line.replace( quotedWord );

                THEN( "the Search matches the word with its quotes" )
                {
                    REQUIRE( matches( line, R"(beta say "hi")" ) );
                    REQUIRE_FALSE( matches( line, "alpha say hi" ) );
                }
            }

            WHEN( "Predefined Filters for the word and for beta are combined" )
            {
                auto line = lineWith( reading, {} );
                line.useFilters( { { "word", quotedWord, false }, { "beta", "beta", false } } );

                THEN( "the Search matches either" )
                {
                    REQUIRE( matches( line, R"(alpha say "hi")" ) );
                    REQUIRE( matches( line, "beta say hi" ) );
                    REQUIRE_FALSE( matches( line, "alpha say hi" ) );
                }
            }
        }
    }

    for ( const auto reading : { Reading::Plain, Reading::Regexp } ) {
        GIVEN( "a Search for the word " << describe( reading ) )
        {
            auto line = lineWith( reading, quotedWord );

            WHEN( "beta is excluded from it" )
            {
                line.exclude( "beta" );

                THEN( "the Search is a logical combination that matches the word with its quotes "
                      "and without beta" )
                {
                    REQUIRE( line.flags().booleanCombination );
                    REQUIRE( line.pattern() == R"("say \"hi\"" and not("beta"))" );
                    REQUIRE( matches( line, R"(alpha say "hi")" ) );
                    REQUIRE_FALSE( matches( line, R"(beta say "hi")" ) );
                }
            }
        }
    }
}

// A backslash right before a quote of a sub-pattern is written \\, so that a
// word ending in a backslash does not escape its closing quote (#405).
SCENARIO( "A word with backslashes keeps the Search Line's pattern valid", "[searchline][search]" )
{
    struct Row {
        QString word;
        std::string lineWith;
        std::string lineWithout;
    };

    const auto row = GENERATE( values<Row>( {
        { R"(C:\temp\)", R"(open C:\temp\ now)", R"(open C:\temp now)" },
        { R"(a\\)", R"(x a\\ y)", R"(x a\ y)" },
        { R"(a\"b)", R"(x a\"b y)", R"(x a"b y)" },
        { R"(a\\"b)", R"(x a\\"b y)", R"(x a\"b y)" },
    } ) );

    for ( const auto reading : { Reading::Boolean, Reading::BooleanRegexp } ) {
        GIVEN( "the word " << row.word.toStdString() << " and a Search " << describe( reading ) )
        {
            WHEN( "the word is added to a Search for nothing" )
            {
                auto line = lineWith( reading, R"("nothing")" );
                line.add( row.word );

                THEN( "the Search matches the Log Lines with the word" )
                {
                    REQUIRE( matches( line, row.lineWith ) );
                    REQUIRE_FALSE( matches( line, row.lineWithout ) );
                }
            }

            WHEN( "the word is excluded from a Search for a space" )
            {
                auto line = lineWith( reading, R"(" ")" );
                line.exclude( row.word );

                THEN( "the Search matches the Log Lines without the word" )
                {
                    REQUIRE( matches( line, row.lineWithout ) );
                    REQUIRE_FALSE( matches( line, row.lineWith ) );
                }
            }

            WHEN( "the Search is replaced with the word" )
            {
                auto line = lineWith( reading, R"("nothing")" );
                line.replace( row.word );

                THEN( "the Search matches the Log Lines with the word" )
                {
                    REQUIRE( matches( line, row.lineWith ) );
                    REQUIRE_FALSE( matches( line, row.lineWithout ) );
                }
            }

            WHEN( "Predefined Filters for the word and for beta are combined" )
            {
                auto line = lineWith( reading, {} );
                line.useFilters( { { "word", row.word, false }, { "beta", "beta", false } } );

                THEN( "the Search matches either" )
                {
                    REQUIRE( matches( line, row.lineWith ) );
                    REQUIRE( matches( line, "beta" ) );
                    REQUIRE_FALSE( matches( line, row.lineWithout ) );
                }
            }
        }
    }

    GIVEN( "a regexp Predefined Filter ending in an escaped backslash, in a logical combination "
           "of regexps" )
    {
        auto line = lineWith( Reading::BooleanRegexp, {} );
        line.useFilters( { { "dir", R"(dir\\)", true }, { "beta", "beta", false } } );

        THEN( "the Search matches the Log Lines with dir and a backslash" )
        {
            REQUIRE( matches( line, R"(open dir\ now)" ) );
            REQUIRE_FALSE( matches( line, "open dir now" ) );
        }
    }
}

SCENARIO( "An edited pattern runs the Search at once only when auto-run is on", "[searchline]" )
{
    for ( const auto autoRun : { false, true } ) {
        GIVEN( "a Search Line with auto-run " << ( autoRun ? "on" : "off" ) )
        {
            SearchLine line{ startingState( Reading::Plain, autoRun ) };
            line.setPattern( "alpha" );

            THEN( "adding, excluding, replacing and combining Predefined Filters say so" )
            {
                REQUIRE( line.add( "beta" ) == autoRun );
                REQUIRE( line.exclude( "gamma" ) == autoRun );
                REQUIRE( line.replace( "delta" ) == autoRun );
                REQUIRE( line.useFilters( { { "f", "epsilon", false } } ) == autoRun );
            }

            WHEN( "a Policy arrives that turns auto-run the other way" )
            {
                auto policy = startingState( Reading::Regexp, !autoRun );
                policy.searchLogicalCombiningDefault = true;
                const auto flagsBefore = line.flags();
                line.setQuickFindPolicy( policy );

                THEN( "an edited pattern follows it, and the buttons stay as they were" )
                {
                    REQUIRE( line.add( "beta" ) == !autoRun );
                    REQUIRE( line.flags() == flagsBefore );
                }
            }
        }
    }
}

SCENARIO( "The Search Line shows a requested Search", "[searchline]" )
{
    SearchLine line{ startingState( Reading::Plain ) };
    line.setPattern( "alpha" );
    line.settled( AutoRefresh::Static, 2_lcount );

    WHEN( "the Search runs" )
    {
        line.requested( session( Phase::Running ) );

        THEN( "the Stop button shows and the text is hidden" )
        {
            const auto display = line.display();
            REQUIRE( display.buttons == SearchLine::Buttons::Stop );
            REQUIRE_FALSE( display.visible );
            REQUIRE_FALSE( display.isError );
            REQUIRE_FALSE( display.gauge );
        }
    }

    WHEN( "its pattern is in error" )
    {
        auto state = session( Phase::InvalidPattern );
        state.errorString = "missing )";
        line.requested( state );

        THEN( "the error in the expression shows as an error, and the Search button stays" )
        {
            const auto display = line.display();
            REQUIRE( display.text == "Error in expression: missing )" );
            REQUIRE( display.visible );
            REQUIRE( display.isError );
            REQUIRE( display.buttons == SearchLine::Buttons::Search );
        }

        AND_WHEN( "the next Search runs" )
        {
            line.requested( session( Phase::Running ) );

            THEN( "it is no longer an error" )
            {
                REQUIRE_FALSE( line.display().isError );
                REQUIRE_FALSE( line.display().visible );
            }
        }
    }
}

SCENARIO( "The Search Line shows a Search in progress", "[searchline]" )
{
    struct Row {
        int progress;
        LinesCount matches;
        QString text;
    };

    // Some languages translate the plural the same as the singular, so the
    // whole text is chosen, not only its noun.
    const auto row = GENERATE( values<Row>( {
        { 40, 1_lcount, "Search in progress (40 %)... 1 match found so far." },
        { 75, 2_lcount, "Search in progress (75 %)... 2 matches found so far." },
    } ) );

    auto line = runningLine();

    WHEN( "the Search is " << row.progress << " % through with " << row.matches.get()
                           << " Matches" )
    {
        line.progressed( session( Phase::Running, row.progress, row.matches ),
                         AutoRefresh::Static );

        THEN( "the text says so, over a gauge that far, and the Stop button stays" )
        {
            const auto display = line.display();
            REQUIRE( display.text == row.text );
            REQUIRE( display.gauge == row.progress );
            REQUIRE( display.visible );
            REQUIRE( display.buttons == SearchLine::Buttons::Stop );
        }
    }

    WHEN( "the Search has only started" )
    {
        const auto textBefore = line.display().text;
        line.progressed( session( Phase::Running, 0, row.matches ), AutoRefresh::Static );

        THEN( "neither text nor gauge flash for it" )
        {
            REQUIRE( line.display().text == textBefore );
            REQUIRE_FALSE( line.display().gauge );
            REQUIRE( line.display().buttons == SearchLine::Buttons::Stop );
        }
    }
}

SCENARIO( "The Search Line shows a Search done", "[searchline]" )
{
    auto line = runningLine();
    line.progressed( session( Phase::Running, 50, 1_lcount ), AutoRefresh::Static );

    GIVEN( "a completed Search" )
    {
        struct Row {
            AutoRefresh autoRefresh;
            LinesCount matches;
            QString text;
        };
        const auto row = GENERATE( values<Row>( {
            { AutoRefresh::Static, 1_lcount, "1 match found" },
            { AutoRefresh::Static, 3_lcount, "3 matches found" },
            { AutoRefresh::Autorefreshing, 3_lcount, "3 matches found" },
            { AutoRefresh::FileTruncated, 3_lcount, "File truncated on disk" },
            { AutoRefresh::TruncatedAutorefreshing, 3_lcount, "File truncated on disk" },
        } ) );

        line.progressed( session( Phase::Complete, 100, row.matches ), row.autoRefresh );

        THEN( "the text says " << row.text.toStdString()
                               << ", the gauge goes and the Search button is back" )
        {
            const auto display = line.display();
            REQUIRE( display.text == row.text );
            REQUIRE( display.visible );
            REQUIRE_FALSE( display.isError );
            REQUIRE_FALSE( display.gauge );
            REQUIRE( display.buttons == SearchLine::Buttons::Search );
            REQUIRE( display.offerIssueReport.isEmpty() );
        }
    }

    GIVEN( "a failed Search" )
    {
        auto state = session( Phase::Failed );
        state.errorString = "out of memory";
        line.progressed( state, AutoRefresh::Static );

        THEN( "it shows as an error, and the failure is offered to be reported" )
        {
            const auto display = line.display();
            REQUIRE( display.text == "Search failed" );
            REQUIRE( display.isError );
            REQUIRE( display.visible );
            REQUIRE_FALSE( display.gauge );
            REQUIRE( display.buttons == SearchLine::Buttons::Search );
            REQUIRE( display.offerIssueReport == "out of memory" );
        }

        AND_WHEN( "the next Search is requested" )
        {
            line.requested( session( Phase::Running ) );

            THEN( "the failure is not offered again" )
            {
                REQUIRE( line.display().offerIssueReport.isEmpty() );
            }
        }
    }

    for ( const auto phase : { Phase::Interrupted, Phase::InvalidPattern } ) {
        GIVEN( "a Search told " << ( phase == Phase::Interrupted ? "interrupted" : "invalid" ) )
        {
            const auto textBefore = line.display().text;
            line.progressed( session( phase ), AutoRefresh::Static );

            THEN( "the text whoever ended it set stays, the gauge goes and the Search button is "
                  "back" )
            {
                const auto display = line.display();
                REQUIRE( display.text == textBefore );
                REQUIRE( display.visible );
                REQUIRE_FALSE( display.gauge );
                REQUIRE( display.buttons == SearchLine::Buttons::Search );
            }
        }
    }

    GIVEN( "a Search Session gone idle" )
    {
        const auto displayBefore = line.display();
        line.progressed( session( Phase::Idle ), AutoRefresh::NoSearch );

        THEN( "nothing changes" )
        {
            REQUIRE( line.display() == displayBefore );
        }
    }
}

SCENARIO( "The Search Line shows a stopped Search", "[searchline]" )
{
    auto line = runningLine();
    line.progressed( session( Phase::Running, 50, 1_lcount ), AutoRefresh::Static );

    WHEN( "the user stops the Search" )
    {
        line.stopped( AutoRefresh::Static );

        THEN( "the gauge goes and the Search button is back" )
        {
            const auto display = line.display();
            REQUIRE_FALSE( display.gauge );
            REQUIRE( display.buttons == SearchLine::Buttons::Search );
            REQUIRE_FALSE( display.isError );
        }
    }

    WHEN( "the user stops the Search over a truncated Log File" )
    {
        line.stopped( AutoRefresh::FileTruncated );

        THEN( "the text says so" )
        {
            REQUIRE( line.display().text == "File truncated on disk" );
            REQUIRE( line.display().visible );
        }
    }
}

SCENARIO( "The Search Line says what is known of a Search that does not run", "[searchline]" )
{
    auto line = runningLine();
    line.progressed( session( Phase::Complete, 100, 2_lcount ), AutoRefresh::Static );

    WHEN( "the Log File is truncated under it" )
    {
        line.settled( AutoRefresh::FileTruncated, 2_lcount );

        THEN( "the text says so" )
        {
            REQUIRE( line.display().text == "File truncated on disk" );
            REQUIRE( line.display().visible );
            REQUIRE_FALSE( line.display().isError );
        }
    }

    WHEN( "there is no Search any more" )
    {
        line.settled( AutoRefresh::NoSearch, 2_lcount );

        THEN( "the line is hidden" )
        {
            REQUIRE( line.display().text.isEmpty() );
            REQUIRE_FALSE( line.display().visible );
        }
    }

    WHEN( "the Search is replaced by none" )
    {
        line.cleared();

        THEN( "the line is hidden" )
        {
            REQUIRE( line.display().text.isEmpty() );
            REQUIRE_FALSE( line.display().visible );
            REQUIRE( line.display().buttons == SearchLine::Buttons::Search );
        }
    }

    WHEN( "an error is followed by what is known" )
    {
        auto state = session( Phase::InvalidPattern );
        state.errorString = "missing )";
        line.requested( state );
        line.settled( AutoRefresh::Static, 1_lcount );

        THEN( "it is no longer an error" )
        {
            REQUIRE( line.display().text == "1 match found" );
            REQUIRE_FALSE( line.display().isError );
        }
    }
}
