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

// What "Name this value..." proposes from the text the user points at in a
// Log Line (#883): the regex with the value as its capture group, the Name
// Table's name and the key the value is entered as.

#include "valueproposal.h"

#include <QRegularExpression>

#include <catch2/catch_test_macros.hpp>

using namespace logsquirl::valuenames;

namespace {

// The range of the first occurrence of text in line.
LineRange rangeOf( const QString& line, const QString& text )
{
    const auto start = line.indexOf( text );
    REQUIRE( start >= 0 );
    return LineRange{ start, text.size() };
}

NamingProposal proposalFor( const QString& line, const QString& selected )
{
    const auto proposal = proposeNaming( line, rangeOf( line, selected ) );
    REQUIRE( proposal.has_value() );
    return *proposal;
}

// What the proposed regex captures in the line, empty when it does not match.
QString capturedBy( const NamingProposal& proposal, const QString& line )
{
    const QRegularExpression regex( proposal.pattern );
    REQUIRE( regex.isValid() );
    REQUIRE( regex.captureCount() == 1 );
    const auto match = regex.match( line );
    return match.hasMatch() ? match.captured( 1 ) : QString{};
}

// Whether the key matches the whole value, as a Name Table's key does.
bool keyMatches( const NamingProposal& proposal, const QString& value )
{
    const QRegularExpression key( QRegularExpression::anchoredPattern( proposal.key ),
                                  QRegularExpression::CaseInsensitiveOption );
    REQUIRE( key.isValid() );
    return key.match( value ).hasMatch();
}

} // namespace

SCENARIO( "Name this value proposes a regex, a table and a template from the selection",
          "[valuenames][valueproposal]" )
{
    GIVEN( "the selection EsimState=9 of a Log Line" )
    {
        const auto line = QStringLiteral( "12:00:01 modem EsimState=9 done" );
        const auto proposal = proposalFor( line, QStringLiteral( "EsimState=9" ) );

        THEN( "the regex is the text before the value with the value as its capture group" )
        {
            REQUIRE( proposal.pattern == QStringLiteral( "EsimState=(\\d+)" ) );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "9" ) );
        }

        THEN( "the table is named after the text before the value" )
        {
            REQUIRE( proposal.tableName == QStringLiteral( "EsimState" ) );
        }

        THEN( "the value is the key, and the template shows the name and the value" )
        {
            REQUIRE( proposal.value == QStringLiteral( "9" ) );
            REQUIRE( proposal.valueRange == rangeOf( line, QStringLiteral( "9" ) ) );
            REQUIRE( proposal.key == QStringLiteral( "9" ) );
            REQUIRE( proposal.displayTemplate == QStringLiteral( "{name} ({value})" ) );
        }
    }

    GIVEN( "only the value selected" )
    {
        const auto line = QStringLiteral( "modem EsimState=9 done" );

        THEN( "the word before it in the Log Line and what separates them come before it" )
        {
            const auto proposal = proposalFor( line, QStringLiteral( "9" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "EsimState=(\\d+)" ) );
            REQUIRE( proposal.tableName == QStringLiteral( "EsimState" ) );
        }

        THEN( "a selection of the separator and the value takes the word before too" )
        {
            const auto proposal = proposalFor( line, QStringLiteral( "=9" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "EsimState=(\\d+)" ) );
            REQUIRE( proposal.tableName == QStringLiteral( "EsimState" ) );
        }

        THEN( "a blank between the word and the value is part of the regex" )
        {
            const auto blankLine = QStringLiteral( "state: 4 now" );
            const auto proposal = proposalFor( blankLine, QStringLiteral( "4" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "state: (\\d+)" ) );
            REQUIRE( proposal.tableName == QStringLiteral( "state" ) );
            REQUIRE( capturedBy( proposal, blankLine ) == QStringLiteral( "4" ) );
        }

        THEN( "of several values before it only the last word is taken" )
        {
            const auto proposal = proposalFor( QStringLiteral( "a=1,b=2" ), QStringLiteral( "2" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "b=(\\d+)" ) );
            REQUIRE( proposal.tableName == QStringLiteral( "b" ) );
        }
    }

    GIVEN( "a hexadecimal value" )
    {
        THEN( "one with 0x is captured with its prefix" )
        {
            const auto line = QStringLiteral( "ECU ecu=0x1F up" );
            const auto proposal = proposalFor( line, QStringLiteral( "ecu=0x1F" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "ecu=(0[xX][0-9A-Fa-f]+)" ) );
            REQUIRE( proposal.value == QStringLiteral( "0x1F" ) );
            REQUIRE( proposal.key == QStringLiteral( "0x1F" ) );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "0x1F" ) );
        }

        THEN( "one without, of digits and letters, is captured as hex digits" )
        {
            const auto line = QStringLiteral( "id=1f2e" );
            const auto proposal = proposalFor( line, QStringLiteral( "id=1f2e" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "id=([0-9A-Fa-f]+)" ) );
            REQUIRE( proposal.value == QStringLiteral( "1f2e" ) );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "1f2e" ) );
        }
    }

    GIVEN( "a negative value" )
    {
        const auto line = QStringLiteral( "delta=-5 ok" );

        THEN( "the minus sign is part of the value, selected or not" )
        {
            for ( const auto& selected : { QStringLiteral( "-5" ), QStringLiteral( "5" ),
                                           QStringLiteral( "delta=-5" ) } ) {
                const auto proposal = proposalFor( line, selected );
                REQUIRE( proposal.pattern == QStringLiteral( "delta=(-?\\d+)" ) );
                REQUIRE( proposal.value == QStringLiteral( "-5" ) );
                REQUIRE( proposal.tableName == QStringLiteral( "delta" ) );
                REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "-5" ) );
                REQUIRE( keyMatches( proposal, QStringLiteral( "-5" ) ) );
                REQUIRE_FALSE( keyMatches( proposal, QStringLiteral( "5" ) ) );
            }
        }

        THEN( "a dash after a word is no minus sign" )
        {
            const auto proposal = proposalFor( QStringLiteral( "id-5" ), QStringLiteral( "5" ) );
            REQUIRE( proposal.value == QStringLiteral( "5" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "id-(\\d+)" ) );
        }
    }

    GIVEN( "a value with a fraction" )
    {
        const auto line = QStringLiteral( "load=3.25" );
        const auto proposal = proposalFor( line, QStringLiteral( "load=3.25" ) );

        THEN( "the fraction is part of the value, and the key matches it alone" )
        {
            REQUIRE( proposal.value == QStringLiteral( "3.25" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "load=(\\d+\\.\\d+)" ) );
            REQUIRE( keyMatches( proposal, QStringLiteral( "3.25" ) ) );
            REQUIRE_FALSE( keyMatches( proposal, QStringLiteral( "3x25" ) ) );
        }
    }

    GIVEN( "a value with a unit after it" )
    {
        const auto line = QStringLiteral( "took 12ms" );

        THEN( "the digits are the value, and the unit follows the capture group" )
        {
            const auto proposal = proposalFor( line, QStringLiteral( "12ms" ) );
            REQUIRE( proposal.value == QStringLiteral( "12" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "took (\\d+)ms" ) );
            REQUIRE( proposal.tableName == QStringLiteral( "took" ) );
        }
    }

    GIVEN( "a value at the start of the Log Line" )
    {
        const auto line = QStringLiteral( "9 apples" );

        THEN( "the regex starts the Log Line, and the table has no name proposed" )
        {
            const auto proposal = proposalFor( line, QStringLiteral( "9" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "^(\\d+)" ) );
            REQUIRE( proposal.tableName.isEmpty() );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "9" ) );
        }

        THEN( "the selected text after the value follows the capture group" )
        {
            const auto proposal = proposalFor( line, QStringLiteral( "9 apples" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "^(\\d+) apples" ) );
        }
    }

    GIVEN( "a raw Log Line with tabs before the key and between the key and the value" )
    {
        const auto line = QStringLiteral( "a\tEsimState=\t9 done" );

        THEN( "the regex writes the tab as \\t and matches the raw Log Line, whatever is "
              "selected" )
        {
            for ( const auto& selected : { QStringLiteral( "9" ), QStringLiteral( "EsimState=\t9" ),
                                           QStringLiteral( "=\t9" ) } ) {
                const auto proposal = proposalFor( line, selected );
                REQUIRE( proposal.pattern == QStringLiteral( "EsimState=\\t(\\d+)" ) );
                REQUIRE( proposal.tableName == QStringLiteral( "EsimState" ) );
                REQUIRE( proposal.value == QStringLiteral( "9" ) );
                REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "9" ) );
            }
        }

        THEN( "a tab ends the text under the cursor, as a blank does" )
        {
            REQUIRE( valueTextAt( line, line.indexOf( u'9' ) )
                     == rangeOf( line, QStringLiteral( "9" ) ) );
            REQUIRE_FALSE( valueTextAt( line, line.indexOf( u'S' ) ).has_value() );
        }
    }

    GIVEN( "regex metacharacters in the text around the value" )
    {
        const auto line = QStringLiteral( "slot[0].state(x)=7 + 1" );
        const auto proposal = proposalFor( line, QStringLiteral( "slot[0].state(x)=7 +" ) );

        THEN( "they are escaped, so that the regex matches the Log Line" )
        {
            REQUIRE( proposal.pattern
                     == QStringLiteral( "slot\\[0\\]\\.state\\(x\\)=(\\d+) \\+" ) );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "7" ) );
            REQUIRE( proposal.tableName == QStringLiteral( "x" ) );
        }
    }

    GIVEN( "a selection without a number" )
    {
        THEN( "nothing is proposed" )
        {
            const auto line = QStringLiteral( "EsimState=on" );
            REQUIRE_FALSE( proposeNaming( line, rangeOf( line, line ) ).has_value() );
            REQUIRE_FALSE( valueIn( line, rangeOf( line, line ) ).has_value() );
            REQUIRE_FALSE( proposeNaming( line, LineRange{ 0, 0 } ).has_value() );
            REQUIRE_FALSE( proposeNaming( line, LineRange{ 5, 100 } ).has_value() );
        }

        THEN( "a word of hex letters without a digit is no value" )
        {
            const auto line = QStringLiteral( "state=beef" );
            REQUIRE_FALSE( valueIn( line, rangeOf( line, line ) ).has_value() );
        }
    }
}

SCENARIO( "Name this value takes the value under the cursor when nothing is selected",
          "[valuenames][valueproposal]" )
{
    const auto line = QStringLiteral( "x EsimState=9 ERROR" );

    THEN( "on the value, it is the word under the cursor" )
    {
        const auto at = valueTextAt( line, line.indexOf( u'9' ) );
        REQUIRE( at == rangeOf( line, QStringLiteral( "9" ) ) );
    }

    THEN( "on the word before the value, it is the text up to the next blank" )
    {
        const auto at = valueTextAt( line, line.indexOf( u'S' ) );
        REQUIRE( at == rangeOf( line, QStringLiteral( "EsimState=9" ) ) );
    }

    THEN( "on a word without a value, or on a blank, there is none" )
    {
        REQUIRE_FALSE( valueTextAt( line, line.indexOf( QStringLiteral( "ERROR" ) ) ).has_value() );
        REQUIRE_FALSE( valueTextAt( line, 1 ).has_value() );
        REQUIRE_FALSE( valueTextAt( line, -1 ).has_value() );
        REQUIRE_FALSE( valueTextAt( line, line.size() ).has_value() );
    }
}
