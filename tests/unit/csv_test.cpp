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

// A CSV test of RFC 4180 quoting (#572).

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "csv.h"

SCENARIO( "a CSV line quotes the fields that need it, as RFC 4180 says", "[csv]" )
{
    const auto separator = GENERATE( QChar( ',' ), QChar( ';' ), QChar( '\t' ) );
    const QString sep( separator );

    GIVEN( "fields that need no quotes" )
    {
        const QStringList fields = { "plain", "two words", "12:00:00.123" };

        THEN( "they are written as they are, separated by the separator" )
        {
            REQUIRE( csvLine( fields, separator )
                     == "plain" + sep + "two words" + sep + "12:00:00.123" );
        }
    }

    GIVEN( "a field holding the separator" )
    {
        const QStringList fields = { "a" + sep + "b", "c" };

        THEN( "that field is quoted" )
        {
            REQUIRE( csvLine( fields, separator ) == "\"a" + sep + "b\"" + sep + "c" );
        }
    }

    GIVEN( "a field holding a quote" )
    {
        const QStringList fields = { R"(say "hi")", "c" };

        THEN( "that field is quoted and its quote doubled" )
        {
            REQUIRE( csvLine( fields, separator ) == R"("say ""hi""")" + sep + "c" );
        }
    }

    GIVEN( "fields holding a carriage return and a line feed" )
    {
        const QStringList fields = { "one\rtwo", "three\nfour", "five\r\nsix" };

        THEN( "those fields are quoted, their line ends kept" )
        {
            REQUIRE( csvLine( fields, separator )
                     == "\"one\rtwo\"" + sep + "\"three\nfour\"" + sep + "\"five\r\nsix\"" );
        }
    }

    GIVEN( "empty fields" )
    {
        const QStringList fields = { "", "b", "" };

        THEN( "they are written empty, not quoted" )
        {
            REQUIRE( csvLine( fields, separator ) == sep + "b" + sep );
        }
    }
}

SCENARIO( "a CSV field is quoted only for its own separator", "[csv]" )
{
    GIVEN( "a field holding a comma" )
    {
        const QStringList fields = { "a,b" };

        THEN( "it is quoted with the comma as separator, and not with the semicolon or tab" )
        {
            REQUIRE( csvLine( fields, ',' ) == "\"a,b\"" );
            REQUIRE( csvLine( fields, ';' ) == "a,b" );
            REQUIRE( csvLine( fields, '\t' ) == "a,b" );
        }
    }
}

// A field a spreadsheet would read as a formula is written as text (#831).
SCENARIO( "a CSV field starting like a formula is prefixed with an apostrophe", "[csv]" )
{
    GIVEN( "a field starting with =, +, - or @" )
    {
        const auto start
            = GENERATE( QString( "=" ), QString( "+" ), QString( "-" ), QString( "@" ) );
        const QStringList fields = { start + "cmd|' /C calc'!A0", "c" };

        THEN( "an apostrophe is written before it" )
        {
            REQUIRE( csvLine( fields, ';' ) == "'" + start + "cmd|' /C calc'!A0;c" );
        }
    }

    GIVEN( "a field starting with a tab or a carriage return and then a formula character" )
    {
        const auto start = GENERATE( QString( "\t=" ), QString( "\r=" ), QString( "\t+" ),
                                     QString( "\r-" ), QString( "\t@" ) );
        const QStringList fields = { start + "1+1" };

        THEN( "an apostrophe is written before it, and a carriage return is still quoted" )
        {
            const QString expected = "'" + start + "1+1";
            REQUIRE(
                csvLine( fields, ',' )
                == ( start.front() == QChar::CarriageReturn ? "\"" + expected + "\"" : expected ) );
        }
    }

    GIVEN( "a field starting with = and holding the separator" )
    {
        const QStringList fields = { "=SUM(A1,A2)", "c" };

        THEN( "it is quoted, the apostrophe inside the quotes" )
        {
            REQUIRE( csvLine( fields, ',' ) == R"x("'=SUM(A1,A2)",c)x" );
        }
    }

    GIVEN( "a field starting with = and holding a quote" )
    {
        const QStringList fields = { R"x(=HYPERLINK("http://x"))x" };

        THEN( "it is quoted, the apostrophe inside the quotes, its quotes doubled" )
        {
            REQUIRE( csvLine( fields, ',' ) == R"x("'=HYPERLINK(""http://x"")")x" );
        }
    }

    GIVEN( "a negative number" )
    {
        const QStringList fields = { "-5" };

        THEN( "it is prefixed as well: the apostrophe keeps it text" )
        {
            REQUIRE( csvLine( fields, ',' ) == "'-5" );
        }
    }

    GIVEN( "fields starting with a digit, a letter, a blank, a tab before a letter, or empty" )
    {
        const QStringList fields = { "5", "x=1", " =1", "\tx", "" };

        THEN( "they are written as they are" )
        {
            REQUIRE( csvLine( fields, ',' ) == "5,x=1, =1,\tx," );
        }
    }

    GIVEN( "a field that is a lone tab" )
    {
        const QStringList fields = { "\t" };

        THEN( "it is written as it is" )
        {
            REQUIRE( csvLine( fields, ',' ) == "\t" );
        }
    }
}
