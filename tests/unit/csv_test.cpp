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
