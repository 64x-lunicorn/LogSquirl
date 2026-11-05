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

// What Chart this value... proposes from a number selected in a Log Line
// (#898): whether the selection is a number, and the chart series' pattern,
// capturing the number in Log Lines like the clicked one, and its name.

#include "chartvalueproposal.h"

#include <QRegularExpression>

#include <catch2/catch_test_macros.hpp>

using namespace logsquirl::chart;
using logsquirl::valuenames::LineRange;

namespace {

// The range of the occurrence of text in line, from.
LineRange rangeOf( const QString& line, const QString& text, qsizetype from = 0 )
{
    const auto start = line.indexOf( text, from );
    REQUIRE( start >= 0 );
    return LineRange{ start, text.size() };
}

ChartValueProposal proposalFor( const QString& line, LineRange selection )
{
    const auto proposal = proposeChartValue( line, selection );
    REQUIRE( proposal.has_value() );
    // A chart series of one capture group, the value's.
    const QRegularExpression regex( proposal->pattern );
    REQUIRE( regex.isValid() );
    REQUIRE( regex.captureCount() == 1 );
    return *proposal;
}

// The range the pattern's capture group has in the line, as the chart reads
// it: in its first match. None when the pattern does not match.
std::optional<LineRange> capturedRange( const ChartValueProposal& proposal, const QString& line )
{
    const auto match = QRegularExpression( proposal.pattern ).match( line );
    if ( !match.hasMatch() ) {
        return std::nullopt;
    }
    return LineRange{ match.capturedStart( 1 ), match.capturedLength( 1 ) };
}

QString captured( const ChartValueProposal& proposal, const QString& line )
{
    return QRegularExpression( proposal.pattern ).match( line ).captured( 1 );
}

bool isNumber( const QString& line, LineRange selection )
{
    return numberSelected( line, selection ).has_value();
}

} // namespace

SCENARIO( "Chart this value... is for a selection that is a number", "[chart][chartvalue]" )
{
    const auto line = QStringLiteral( "GET /api took 125 ms, load 3.25 temp -5 id 0x1F hash 1f2e "
                                      "size 12ms" );

    THEN( "a whole number, one with a fraction, and a negative one are numbers" )
    {
        CHECK( isNumber( line, rangeOf( line, QStringLiteral( "125" ) ) ) );
        CHECK( isNumber( line, rangeOf( line, QStringLiteral( "3.25" ) ) ) );
        CHECK( isNumber( line, rangeOf( line, QStringLiteral( "-5" ) ) ) );
        // The digits of 5 alone: the minus before it is the number's.
        CHECK( isNumber( line, LineRange{ line.indexOf( QStringLiteral( "-5" ) ) + 1, 1 } ) );
        // The 12 of 12ms.
        CHECK( isNumber( line, rangeOf( line, QStringLiteral( "12" ),
                                        line.indexOf( QStringLiteral( "12ms" ) ) ) ) );
    }

    THEN( "blanks around the number do not count" )
    {
        CHECK( isNumber( line, rangeOf( line, QStringLiteral( " 125 " ) ) ) );
    }

    THEN( "a number with more text, part of a number, a word and a hexadecimal value are not" )
    {
        CHECK_FALSE( isNumber( line, rangeOf( line, QStringLiteral( "took 125" ) ) ) );
        CHECK_FALSE( isNumber( line, rangeOf( line, QStringLiteral( "125 ms" ) ) ) );
        CHECK_FALSE( isNumber( line, rangeOf( line, QStringLiteral( "25" ) ) ) );
        CHECK_FALSE( isNumber( line, rangeOf( line, QStringLiteral( "12ms" ) ) ) );
        CHECK_FALSE( isNumber( line, rangeOf( line, QStringLiteral( "took" ) ) ) );
        CHECK_FALSE( isNumber( line, rangeOf( line, QStringLiteral( "0x1F" ) ) ) );
        CHECK_FALSE( isNumber( line, rangeOf( line, QStringLiteral( "1f2e" ) ) ) );
        CHECK_FALSE( isNumber( line, LineRange{ 0, 0 } ) );
        CHECK_FALSE( isNumber( line, LineRange{ line.size(), 3 } ) );
    }

    THEN( "no proposal is made for what is not a number" )
    {
        CHECK_FALSE( proposeChartValue( line, rangeOf( line, QStringLiteral( "took" ) ) ) );
        CHECK_FALSE( proposeChartValue( line, rangeOf( line, QStringLiteral( "0x1F" ) ) ) );
    }
}

SCENARIO( "Chart this value... proposes a pattern capturing the number, and a series name from "
          "the text before it",
          "[chart][chartvalue]" )
{
    GIVEN( "the number 125 selected after took" )
    {
        const auto line = QStringLiteral( "12:00:01 GET /api took 125 ms" );
        const auto selection = rangeOf( line, QStringLiteral( "125" ) );
        const auto proposal = proposalFor( line, selection );

        THEN( "the pattern captures exactly that number in the clicked Log Line" )
        {
            REQUIRE( capturedRange( proposal, line ) == selection );
        }

        THEN( "it captures the number of Log Lines like it, whole, with a fraction, negative" )
        {
            CHECK( captured( proposal, QStringLiteral( "12:00:02 GET /api took 980 ms" ) )
                   == QStringLiteral( "980" ) );
            CHECK( captured( proposal, QStringLiteral( "12:00:03 GET /x took 1.5 ms" ) )
                   == QStringLiteral( "1.5" ) );
            CHECK( captured( proposal, QStringLiteral( "took -2 ms" ) ) == QStringLiteral( "-2" ) );
        }

        THEN( "it does not match Log Lines without the text before the number" )
        {
            CHECK_FALSE( capturedRange( proposal, QStringLiteral( "12:00:04 GET /api 125 ms" ) ) );
        }

        THEN( "the series is named after the word before the number" )
        {
            REQUIRE( proposal.seriesName == QStringLiteral( "took" ) );
        }
    }

    GIVEN( "a number after a key and a separator with regex metacharacters" )
    {
        const auto line = QStringLiteral( "q[0].latency=3.25ms (ok)" );
        const auto selection = rangeOf( line, QStringLiteral( "3.25" ) );
        const auto proposal = proposalFor( line, selection );

        THEN( "the pattern captures it, and the series is named after the key" )
        {
            REQUIRE( capturedRange( proposal, line ) == selection );
            REQUIRE( proposal.seriesName == QStringLiteral( "latency" ) );
        }
    }

    GIVEN( "a negative number selected" )
    {
        const auto line = QStringLiteral( "sensor temp -5 C" );
        const auto selection = rangeOf( line, QStringLiteral( "-5" ) );
        const auto proposal = proposalFor( line, selection );

        THEN( "the pattern captures it with its sign" )
        {
            REQUIRE( capturedRange( proposal, line ) == selection );
            REQUIRE( proposal.seriesName == QStringLiteral( "temp" ) );
        }
    }

    GIVEN( "the second of two numbers after the same text" )
    {
        const auto line = QStringLiteral( "a=1 b=7 a=2" );
        const auto selection = rangeOf( line, QStringLiteral( "2" ) );
        const auto proposal = proposalFor( line, selection );

        THEN( "the pattern captures the selected one in the clicked Log Line, not the first" )
        {
            REQUIRE( capturedRange( proposal, line ) == selection );
            REQUIRE( proposal.seriesName == QStringLiteral( "a" ) );
        }

        THEN( "it captures the value of a Log Line like it whose earlier numbers differ" )
        {
            CHECK( captured( proposal, QStringLiteral( "a=1 b=8 a=3" ) ) == QStringLiteral( "3" ) );
            CHECK( captured( proposal, QStringLiteral( "a=5 b=-9 a=4.5" ) )
                   == QStringLiteral( "4.5" ) );
        }
    }

    GIVEN( "a number after another one in a Log Line led by a timestamp" )
    {
        const auto line = QStringLiteral( "12:00:01 3 4" );
        const auto selection = LineRange{ line.lastIndexOf( u'4' ), 1 };
        const auto proposal = proposalFor( line, selection );

        THEN( "the pattern captures the selected one in the clicked Log Line" )
        {
            REQUIRE( capturedRange( proposal, line ) == selection );
        }

        THEN( "it captures the value of a Log Line like it at another time, with another number "
              "before it" )
        {
            CHECK( captured( proposal, QStringLiteral( "12:00:02 5 6" ) )
                   == QStringLiteral( "6" ) );
            CHECK( captured( proposal, QStringLiteral( "13:59:59 10 -2.5" ) )
                   == QStringLiteral( "-2.5" ) );
        }
    }

    GIVEN( "a number right after a timestamp" )
    {
        const auto line = QStringLiteral( "12:00:01 42" );
        const auto selection = rangeOf( line, QStringLiteral( "42" ) );
        const auto proposal = proposalFor( line, selection );

        THEN( "the pattern captures it, and the value of a Log Line like it at another time" )
        {
            REQUIRE( capturedRange( proposal, line ) == selection );
            CHECK( captured( proposal, QStringLiteral( "12:00:02 7" ) ) == QStringLiteral( "7" ) );
        }
    }

    GIVEN( "a word with digits in it in the text the pattern takes before the number" )
    {
        const auto line = QStringLiteral( "a=1 v2 a=2" );
        const auto selection = LineRange{ line.lastIndexOf( u'2' ), 1 };
        const auto proposal = proposalFor( line, selection );

        THEN( "the word is kept as it is: a Log Line with another word there does not match" )
        {
            REQUIRE( capturedRange( proposal, line ) == selection );
            CHECK( captured( proposal, QStringLiteral( "a=0 v2 a=8" ) ) == QStringLiteral( "8" ) );
            CHECK_FALSE( capturedRange( proposal, QStringLiteral( "a=0 v3 a=8" ) ) );
        }
    }

    GIVEN( "a number in a version string" )
    {
        const auto line = QStringLiteral( "v 1.5.6" );

        THEN( "a part of it is no number" )
        {
            CHECK_FALSE( isNumber( line, rangeOf( line, QStringLiteral( "1.5" ) ) ) );
            CHECK_FALSE( isNumber( line, rangeOf( line, QStringLiteral( "5.6" ) ) ) );
        }
    }

    GIVEN( "a number the Log Line starts with" )
    {
        const auto line = QStringLiteral( "42 requests queued" );
        const auto selection = rangeOf( line, QStringLiteral( "42" ) );
        const auto proposal = proposalFor( line, selection );

        THEN( "the pattern captures it at the start, and there is no name to propose" )
        {
            REQUIRE( capturedRange( proposal, line ) == selection );
            CHECK( captured( proposal, QStringLiteral( "7 requests queued" ) )
                   == QStringLiteral( "7" ) );
            REQUIRE( proposal.seriesName.isEmpty() );
        }
    }

    GIVEN( "a number with blanks selected around it" )
    {
        const auto line = QStringLiteral( "took 125 ms" );
        const auto proposal = proposalFor( line, rangeOf( line, QStringLiteral( " 125 " ) ) );

        THEN( "the pattern captures the number alone" )
        {
            REQUIRE( capturedRange( proposal, line ) == rangeOf( line, QStringLiteral( "125" ) ) );
        }
    }
}
