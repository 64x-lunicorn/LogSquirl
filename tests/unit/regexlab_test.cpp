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

// What the Regex Lab shows of a pattern on sample lines (#659): the lines the
// Search's matcher matches, where they match, their capture groups, a pattern
// error with its position, and the bounds an evaluation keeps to.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <atomic>
#include <chrono>
#include <string>

#include "regexlab.h"
#include "regularexpression.h"

namespace {

const std::atomic<bool> NotCancelled{ false };

RegularExpressionPattern pattern( const QString& text, bool matchCase, bool inverse, bool logical,
                                  bool plainText )
{
    return RegularExpressionPattern( text, matchCase, inverse, logical, plainText );
}

RegularExpressionPattern regexp( const QString& text )
{
    return pattern( text, true, false, false, false );
}

regexlab::Result evaluate( const RegularExpressionPattern& searched,
                           const logsquirl::vector<QString>& sample,
                           RegexpEngine engine = RegexpEngine::Vectorscan,
                           const regexlab::Bounds& bounds = {} )
{
    return regexlab::evaluate( searched, engine, sample, bounds, NotCancelled );
}

const logsquirl::vector<QString> Sample{
    QStringLiteral( "2026-09-30 10:00:01 INFO  service started" ),
    QStringLiteral( "2026-09-30 10:00:02 ERROR connection refused: db-1" ),
    QStringLiteral( "2026-09-30 10:00:03 WARN  retrying in 5 s" ),
    QStringLiteral( "2026-09-30 10:00:08 error connection refused: db-2" ),
    QStringLiteral( "2026-09-30 10:00:09 INFO  connected to db-1 after \"retry\"" ),
    QString(),
    QStringLiteral( "Grüße aus München: ERROR" ),
};

} // namespace

SCENARIO( "The Regex Lab counts exactly the lines the Search's matcher matches", "[regexlab]" )
{
    const auto engine = GENERATE( RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression );
    const auto searched
        = GENERATE( pattern( "error", true, false, false, false ),
                    pattern( "error", false, false, false, false ),
                    pattern( "db-[0-9]", true, false, false, false ),
                    pattern( "db-[0-9]", true, false, false, true ),
                    pattern( "INFO", true, true, false, false ),
                    pattern( "\"ERROR\" or \"WARN\"", true, false, true, false ),
                    pattern( "\"connection\" and not(\"db-2\")", false, false, true, true ),
                    pattern( "\"retry\" and \"db\"", false, true, true, false ),
                    pattern( "M.nchen", true, false, false, false ),
                    pattern( "^$", true, false, false, false ) );

    GIVEN( "the pattern '" + searched.pattern.toStdString() + "'" )
    {
        const auto result = evaluate( searched, Sample, engine );

        THEN( "each line's verdict is the Search matcher's" )
        {
            REQUIRE_FALSE( result.error.has_value() );
            REQUIRE( result.lines.size() == Sample.size() );

            const RegularExpression expression( searched, engine );
            REQUIRE( expression.isValid() );
            const auto matcher = expression.createMatcher();

            std::size_t matching = 0;
            for ( std::size_t i = 0; i < Sample.size(); ++i ) {
                const auto utf8 = Sample[ i ].toUtf8();
                const auto expected = matcher->hasMatch(
                    std::string_view( utf8.constData(), static_cast<std::size_t>( utf8.size() ) ) );
                INFO( "line " << i );
                REQUIRE( result.lines[ i ].isMatch == expected );
                matching += expected ? 1 : 0;
            }
            REQUIRE( result.matchingLines == matching );
            REQUIRE( result.stop == regexlab::Stop::None );
        }
    }
}

SCENARIO( "The Regex Lab shows where a pattern matches a line", "[regexlab]" )
{
    GIVEN( "a pattern that matches twice in a line" )
    {
        const auto result = evaluate( regexp( "err(or)?" ), { "an error, an err and ERROR" } );

        THEN( "both places are marked, the case-sensitive way" )
        {
            REQUIRE( result.lines.size() == 1 );
            const auto& matches = result.lines[ 0 ].matches;
            REQUIRE( matches.size() == 2 );
            CHECK( matches[ 0 ].start == 3 );
            CHECK( matches[ 0 ].length == 5 );
            CHECK( matches[ 1 ].start == 13 );
            CHECK( matches[ 1 ].length == 3 );
        }
    }

    GIVEN( "plain text with characters a regular expression would read otherwise" )
    {
        const auto result = evaluate( pattern( "a.b", false, false, false, true ), { "axb A.B" } );

        THEN( "only the literal text is marked" )
        {
            REQUIRE( result.lines[ 0 ].isMatch );
            REQUIRE( result.lines[ 0 ].matches.size() == 1 );
            CHECK( result.lines[ 0 ].matches[ 0 ].start == 4 );
        }
    }

    GIVEN( "a logical combination" )
    {
        const auto result = evaluate(
            pattern( "\"refused\" and \"db-[12]\"", true, false, true, false ), { Sample[ 1 ] } );

        THEN( "each sub-pattern's matches are marked with the sub-pattern" )
        {
            REQUIRE( result.lines[ 0 ].isMatch );
            const auto& matches = result.lines[ 0 ].matches;
            REQUIRE( matches.size() == 2 );
            CHECK( matches[ 0 ].subPattern == 0 );
            CHECK( Sample[ 1 ].mid( matches[ 0 ].start, matches[ 0 ].length ) == "refused" );
            CHECK( matches[ 1 ].subPattern == 1 );
            CHECK( Sample[ 1 ].mid( matches[ 1 ].start, matches[ 1 ].length ) == "db-1" );
        }
    }
}

SCENARIO( "The Regex Lab lists the capture groups of a line, numbered and named", "[regexlab]" )
{
    const auto engine = GENERATE( RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression );
    const auto result
        = evaluate( regexp( R"((?<level>ERROR|WARN)\s+(\w+)( x)?)" ), { Sample[ 1 ] }, engine );

    REQUIRE( result.lines.size() == 1 );
    const auto& groups = result.lines[ 0 ].groups;
    REQUIRE( groups.size() == 4 );

    CHECK( groups[ 0 ].number == 0 );
    CHECK( groups[ 0 ].text == "ERROR connection" );
    CHECK( groups[ 0 ].start == 20 );

    CHECK( groups[ 1 ].number == 1 );
    CHECK( groups[ 1 ].name == "level" );
    CHECK( groups[ 1 ].text == "ERROR" );

    CHECK( groups[ 2 ].number == 2 );
    CHECK( groups[ 2 ].name.isEmpty() );
    CHECK( groups[ 2 ].text == "connection" );

    // A group that took no part in the match is listed as such.
    CHECK( groups[ 3 ].number == 3 );
    CHECK( groups[ 3 ].start == -1 );
}

SCENARIO( "The Regex Lab says what is wrong with a pattern and where", "[regexlab]" )
{
    const auto engine = GENERATE( RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression );

    GIVEN( "a regular expression with a group left open" )
    {
        const auto searched = regexp( "ab(cd" );
        const auto result = evaluate( searched, Sample, engine );

        THEN( "the Search's message is shown with a position in the pattern" )
        {
            REQUIRE( result.error.has_value() );
            CHECK( result.error->message == RegularExpression( searched, engine ).errorString() );
            CHECK( result.error->position >= 2 );
            CHECK( result.error->position <= searched.pattern.size() );
            CHECK( result.lines.empty() );
            CHECK( result.matchingLines == 0 );
        }
    }

    GIVEN( "a quantifier with its bounds out of order" )
    {
        const auto result = evaluate( regexp( "abc{3,1}" ), Sample, engine );

        THEN( "the position is the quantifier's" )
        {
            REQUIRE( result.error.has_value() );
            CHECK( result.error->position >= 3 );
            CHECK( result.error->position <= 8 );
        }
    }

    GIVEN( "a logical combination whose second sub-pattern is invalid" )
    {
        const QString text = "\"db\" and \"re[fused\"";
        const auto result = evaluate( pattern( text, true, false, true, false ), Sample, engine );

        THEN( "the position is in the second sub-pattern" )
        {
            REQUIRE( result.error.has_value() );
            CHECK( result.error->position > text.indexOf( "re[" ) );
            CHECK( result.error->position <= text.size() );
        }
    }

    GIVEN( "a logical combination with a quote left open" )
    {
        const QString text = "\"db\" and \"refused";
        const auto result = evaluate( pattern( text, true, false, true, false ), Sample, engine );

        THEN( "the position is that quote's" )
        {
            REQUIRE( result.error.has_value() );
            CHECK( result.error->position == text.lastIndexOf( '"' ) );
        }
    }

    GIVEN( "a logical combination with a word not in quotes" )
    {
        const QString text = "\"db\" and refused";
        const auto result = evaluate( pattern( text, true, false, true, false ), Sample, engine );

        THEN( "the position is that word's" )
        {
            REQUIRE( result.error.has_value() );
            CHECK( result.error->position == text.indexOf( "refused" ) );
        }
    }

    GIVEN( "a logical combination with a parenthesis left open" )
    {
        const QString text = "(\"db\" and \"refused\"";
        const auto result = evaluate( pattern( text, true, false, true, false ), Sample, engine );

        THEN( "the position is that parenthesis'" )
        {
            REQUIRE( result.error.has_value() );
            CHECK( result.error->position == 0 );
        }
    }
}

SCENARIO( "A Regex Lab evaluation keeps to its bounds and says where it stopped", "[regexlab]" )
{
    GIVEN( "a line longer than the length evaluated" )
    {
        regexlab::Bounds bounds;
        bounds.maxLineLength = 10;
        const auto result = evaluate( regexp( "error" ), { "0123456789 error", "error" },
                                      RegexpEngine::Vectorscan, bounds );

        THEN( "only its start is evaluated, and the line is reported cut" )
        {
            REQUIRE( result.lines.size() == 2 );
            CHECK( result.lines[ 0 ].isCut );
            CHECK_FALSE( result.lines[ 0 ].isMatch );
            CHECK_FALSE( result.lines[ 1 ].isCut );
            CHECK( result.lines[ 1 ].isMatch );
        }
    }

    GIVEN( "more sample lines than are evaluated" )
    {
        regexlab::Bounds bounds;
        bounds.maxLines = 3;
        const auto result = evaluate( regexp( "a" ), Sample, RegexpEngine::Vectorscan, bounds );

        THEN( "only the first lines are evaluated" )
        {
            CHECK( result.sampleLines == 3 );
            CHECK( result.lines.size() == 3 );
            CHECK( result.stop == regexlab::Stop::None );
        }
    }

    GIVEN( "no time left" )
    {
        regexlab::Bounds bounds;
        bounds.timeLimit = std::chrono::milliseconds{ 0 };
        bounds.slowThreshold = std::chrono::milliseconds{ 0 };
        const auto result = evaluate( regexp( "a" ), Sample, RegexpEngine::Vectorscan, bounds );

        THEN( "the evaluation stops after a line, and says so" )
        {
            CHECK( result.stop == regexlab::Stop::TimeLimit );
            CHECK( result.lines.size() == 1 );
            CHECK( result.sampleLines == Sample.size() );
            CHECK( result.isSlow );
        }
    }

    GIVEN( "an evaluation let go of" )
    {
        const std::atomic<bool> cancelled{ true };
        const auto result
            = regexlab::evaluate( regexp( "a" ), RegexpEngine::Vectorscan, Sample, {}, cancelled );

        THEN( "it evaluates nothing more" )
        {
            CHECK( result.stop == regexlab::Stop::Cancelled );
            CHECK( result.lines.empty() );
        }
    }

    GIVEN( "a pattern that backtracks catastrophically on a long line" )
    {
        const auto line = QString( 5000, QChar( 'a' ) ) + QStringLiteral( "!" );
        const logsquirl::vector<QString> sample( 20, line );
        regexlab::Bounds bounds;
        bounds.timeLimit = std::chrono::milliseconds{ 100 };
        const auto started = std::chrono::steady_clock::now();
        const auto result
            = evaluate( regexp( "(a|aa)+$" ), sample, RegexpEngine::QRegularExpression, bounds );
        const auto took = std::chrono::steady_clock::now() - started;

        THEN( "the evaluation stops soon after its time limit, and says so" )
        {
            CHECK_FALSE( result.error.has_value() );
            CHECK( result.stop == regexlab::Stop::TimeLimit );
            CHECK( result.lines.size() < sample.size() );
            CHECK( result.isSlow );
            CHECK( took < std::chrono::seconds{ 5 } );
        }
    }
}
