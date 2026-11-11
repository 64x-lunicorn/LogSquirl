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
            // Not EsimState=\t9: the word alone, a text value (#957).
            REQUIRE( valueTextAt( line, line.indexOf( u'S' ) )
                     == rangeOf( line, QStringLiteral( "EsimState" ) ) );
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
        THEN( "it holds no number" )
        {
            const auto line = QStringLiteral( "EsimState=on" );
            REQUIRE_FALSE( valueIn( line, rangeOf( line, line ) ).has_value() );
        }

        THEN( "a word of hex letters without a digit is no number" )
        {
            const auto line = QStringLiteral( "state=beef" );
            REQUIRE_FALSE( valueIn( line, rangeOf( line, line ) ).has_value() );
        }

        THEN( "nothing is proposed for an empty range, one past the Log Line, or blanks" )
        {
            const auto line = QStringLiteral( "EsimState=on  x" );
            REQUIRE_FALSE( proposeNaming( line, LineRange{ 0, 0 } ).has_value() );
            REQUIRE_FALSE( proposeNaming( line, LineRange{ 5, 100 } ).has_value() );
            REQUIRE_FALSE( proposeNaming( line, LineRange{ -1, 2 } ).has_value() );
            REQUIRE_FALSE(
                proposeNaming( line, rangeOf( line, QStringLiteral( "  " ) ) ).has_value() );
        }
    }
}

SCENARIO( "Name this value proposes a text value: a word, an id or a selection",
          "[valuenames][valueproposal]" )
{
    // What a click on the text, nothing selected, proposes.
    const auto clickedOn = []( const QString& line, const QString& text ) {
        const auto at = valueTextAt( line, line.indexOf( text ) );
        REQUIRE( at.has_value() );
        const auto proposal = proposeNaming( line, *at );
        REQUIRE( proposal.has_value() );
        return *proposal;
    };

    GIVEN( "a word" )
    {
        const auto line = QStringLiteral( "12:00:01 level=info ready" );
        const auto proposal = clickedOn( line, QStringLiteral( "info" ) );

        THEN( "the word is the value, captured up to the blank after it" )
        {
            REQUIRE( proposal.value == QStringLiteral( "info" ) );
            REQUIRE( proposal.valueRange == rangeOf( line, QStringLiteral( "info" ) ) );
            REQUIRE( proposal.pattern == QStringLiteral( "level=([^ ]+)" ) );
            REQUIRE( proposal.tableName == QStringLiteral( "level" ) );
            REQUIRE( proposal.displayTemplate == QStringLiteral( "{name} ({value})" ) );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "info" ) );
            REQUIRE( capturedBy( proposal, QStringLiteral( "12:00:02 level=warning ready" ) )
                     == QStringLiteral( "warning" ) );
        }

        THEN( "the key names exactly that value" )
        {
            REQUIRE( proposal.key == QStringLiteral( "info" ) );
            REQUIRE( keyMatches( proposal, QStringLiteral( "info" ) ) );
            REQUIRE_FALSE( keyMatches( proposal, QStringLiteral( "information" ) ) );
            REQUIRE_FALSE( keyMatches( proposal, QStringLiteral( "xinfo" ) ) );
        }
    }

    GIVEN( "a UUID" )
    {
        const auto uuid = QStringLiteral( "84f1fc54-cd08-51c3-af5b-3687900e5e6a" );
        const auto line = QStringLiteral( "request " ) + uuid + QStringLiteral( " done" );
        const auto uuidPattern = QStringLiteral( "([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
                                                 "[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})" );

        THEN( "a click on any part of it takes all of it, captured by its shape" )
        {
            for ( const auto& part : { QStringLiteral( "84f1" ), QStringLiteral( "cd08" ),
                                       QStringLiteral( "af5b" ), QStringLiteral( "5e6a" ) } ) {
                REQUIRE( valueTextAt( line, line.indexOf( part ) ) == rangeOf( line, uuid ) );
                const auto proposal = clickedOn( line, part );
                REQUIRE( proposal.value == uuid );
                REQUIRE( proposal.pattern == QStringLiteral( "request " ) + uuidPattern );
                REQUIRE( proposal.tableName == QStringLiteral( "request" ) );
                REQUIRE( proposal.key == uuid );
                REQUIRE( capturedBy( proposal, line ) == uuid );
            }
        }

        THEN( "a selection of it, or of a part of it, proposes the same" )
        {
            for ( const auto& selected : { uuid, QStringLiteral( "51c3" ) } ) {
                const auto proposal = proposalFor( line, selected );
                REQUIRE( proposal.value == uuid );
                REQUIRE( proposal.pattern == QStringLiteral( "request " ) + uuidPattern );
            }
        }

        THEN( "a selection with the text before it keeps that text" )
        {
            const auto proposal = proposalFor( line, QStringLiteral( "request " ) + uuid );
            REQUIRE( proposal.value == uuid );
            REQUIRE( proposal.pattern == QStringLiteral( "request " ) + uuidPattern );
        }
    }

    GIVEN( "a word with _ or -" )
    {
        THEN( "it is one value" )
        {
            const auto line = QStringLiteral( "field user_id missing" );
            const auto proposal = clickedOn( line, QStringLiteral( "id " ) );
            REQUIRE( proposal.value == QStringLiteral( "user_id" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "field ([^ ]+)" ) );
            REQUIRE( proposal.tableName == QStringLiteral( "field" ) );
            REQUIRE( proposal.key == QStringLiteral( "user_id" ) );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "user_id" ) );

            const auto dashLine = QStringLiteral( "mode read-only set" );
            const auto dashed = clickedOn( dashLine, QStringLiteral( "only" ) );
            REQUIRE( dashed.value == QStringLiteral( "read-only" ) );
            REQUIRE( capturedBy( dashed, dashLine ) == QStringLiteral( "read-only" ) );
        }
    }

    GIVEN( "a selection with a blank" )
    {
        const auto line = QStringLiteral( "login user bob done" );
        const auto proposal = proposalFor( line, QStringLiteral( "user bob" ) );

        THEN( "the selection is the value as it is, up to the word after it" )
        {
            REQUIRE( proposal.value == QStringLiteral( "user bob" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "login (.+?) done" ) );
            REQUIRE( proposal.tableName == QStringLiteral( "login" ) );
            REQUIRE( proposal.key == QStringLiteral( "user bob" ) );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "user bob" ) );
            REQUIRE( capturedBy( proposal, QStringLiteral( "login alice smith done" ) )
                     == QStringLiteral( "alice smith" ) );
        }
    }

    GIVEN( "a selection of text without a number" )
    {
        const auto line = QStringLiteral( "EsimState=on" );
        const auto proposal = proposalFor( line, line );

        THEN( "it is the value as it is, with regex metacharacters of the key escaped" )
        {
            REQUIRE( proposal.value == line );
            REQUIRE( proposal.pattern == QStringLiteral( "^(.+)" ) );
            REQUIRE( keyMatches( proposal, line ) );

            const auto dotted = QStringLiteral( "at a.b now" );
            const auto dot = proposalFor( dotted, QStringLiteral( "a.b" ) );
            REQUIRE( dot.key == QStringLiteral( "a\\.b" ) );
            REQUIRE( keyMatches( dot, QStringLiteral( "a.b" ) ) );
            REQUIRE_FALSE( keyMatches( dot, QStringLiteral( "axb" ) ) );
        }
    }

    GIVEN( "a value at the end of the Log Line" )
    {
        const auto line = QStringLiteral( "status: ready" );
        const auto proposal = clickedOn( line, QStringLiteral( "ready" ) );

        THEN( "the rest of the Log Line is captured" )
        {
            REQUIRE( proposal.value == QStringLiteral( "ready" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "status: (.+)" ) );
            REQUIRE( proposal.tableName == QStringLiteral( "status" ) );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "ready" ) );
        }
    }

    GIVEN( "a value followed by , or )" )
    {
        THEN( "it is captured up to that separator" )
        {
            const auto commaLine = QStringLiteral( "user=bob, id=7" );
            const auto comma = clickedOn( commaLine, QStringLiteral( "bob" ) );
            REQUIRE( comma.pattern == QStringLiteral( "user=([^,]+)" ) );
            REQUIRE( comma.tableName == QStringLiteral( "user" ) );
            REQUIRE( capturedBy( comma, commaLine ) == QStringLiteral( "bob" ) );
            REQUIRE( capturedBy( comma, QStringLiteral( "user=alice smith, id=8" ) )
                     == QStringLiteral( "alice smith" ) );

            const auto parenLine = QStringLiteral( "login(bob) ok" );
            const auto paren = clickedOn( parenLine, QStringLiteral( "bob" ) );
            REQUIRE( paren.pattern == QStringLiteral( "login\\(([^)]+)" ) );
            REQUIRE( paren.tableName == QStringLiteral( "login" ) );
            REQUIRE( capturedBy( paren, parenLine ) == QStringLiteral( "bob" ) );
        }

        THEN( "a separator a character class gives a meaning to is escaped there" )
        {
            const auto line = QStringLiteral( "tag=x]y" );
            const auto proposal = proposalFor( line, QStringLiteral( "x" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "tag=([^\\]]+)" ) );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "x" ) );
        }
    }

    GIVEN( "a selection that ends within a word" )
    {
        const auto line = QStringLiteral( "mode information x" );
        const auto proposal = proposalFor( line, QStringLiteral( "inf" ) );

        THEN( "the rest of that word follows the capture group" )
        {
            REQUIRE( proposal.value == QStringLiteral( "inf" ) );
            REQUIRE( proposal.pattern == QStringLiteral( "mode (.+?)ormation" ) );
            REQUIRE( capturedBy( proposal, line ) == QStringLiteral( "inf" ) );
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

    THEN( "on a word without a value, it is the word, a text value" )
    {
        const auto at = valueTextAt( line, line.indexOf( QStringLiteral( "ERROR" ) ) + 2 );
        REQUIRE( at == rangeOf( line, QStringLiteral( "ERROR" ) ) );
    }

    THEN( "on a blank, or on a separator outside a value, there is none" )
    {
        const auto colon = QStringLiteral( "state: on" );
        REQUIRE_FALSE( valueTextAt( colon, colon.indexOf( u':' ) ).has_value() );
        REQUIRE_FALSE( valueTextAt( line, 1 ).has_value() );
        REQUIRE_FALSE( valueTextAt( line, -1 ).has_value() );
        REQUIRE_FALSE( valueTextAt( line, line.size() ).has_value() );
    }
}
