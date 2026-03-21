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

#include "jwtdecoder.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace logsquirl::jwt;

SCENARIO( "Base64URL decoding handles JWT-specific encoding", "[jwtdecoder]" )
{
    GIVEN( "A standard Base64URL string with no padding" )
    {
        // "Hello" in Base64URL is "SGVsbG8" (no padding)
        const QByteArray input = "SGVsbG8";

        WHEN( "It is decoded" )
        {
            const auto result = decodeBase64Url( input );

            THEN( "The original bytes are recovered" )
            {
                REQUIRE( result == "Hello" );
            }
        }
    }

    GIVEN( "A Base64URL string using URL-safe characters" )
    {
        // Standard base64 "n/+w" becomes "n_-w" in Base64URL
        const QByteArray standard = "n/+w";
        const QByteArray urlSafe = "n_-w";

        WHEN( "The URL-safe variant is decoded" )
        {
            const auto fromUrlSafe = decodeBase64Url( urlSafe );
            const auto fromStandard = QByteArray::fromBase64( standard );

            THEN( "Both produce the same result" )
            {
                REQUIRE( fromUrlSafe == fromStandard );
            }
        }
    }

    GIVEN( "An empty input" )
    {
        const QByteArray input = "";

        WHEN( "It is decoded" )
        {
            const auto result = decodeBase64Url( input );

            THEN( "The result is empty" )
            {
                REQUIRE( result.isEmpty() );
            }
        }
    }
}

SCENARIO( "JWT JSON formatting annotates epoch timestamps", "[jwtdecoder]" )
{
    GIVEN( "A JSON payload with iat and exp fields" )
    {
        QJsonObject obj;
        obj[ "sub" ] = "1234567890";
        obj[ "name" ] = "John Doe";
        obj[ "iat" ] = 1516239022;
        obj[ "exp" ] = 1516242622;

        const auto jsonBytes = QJsonDocument( obj ).toJson( QJsonDocument::Compact );

        WHEN( "It is formatted" )
        {
            const auto result = formatJwtJson( jsonBytes );

            THEN( "The output contains the iat timestamp annotation" )
            {
                REQUIRE( result.contains( "\"iat\": 1516239022  // 2018-01-18T01:30:22Z" ) );
            }

            THEN( "The output contains the exp timestamp annotation" )
            {
                REQUIRE( result.contains( "\"exp\": 1516242622  // 2018-01-18T02:30:22Z" ) );
            }

            THEN( "Non-timestamp fields are not annotated" )
            {
                REQUIRE_FALSE( result.contains( "\"sub\": \"1234567890\"  //" ) );
            }
        }
    }

    GIVEN( "A JSON payload with nbf and auth_time fields" )
    {
        QJsonObject obj;
        obj[ "nbf" ] = 0;
        obj[ "auth_time" ] = 1609459200;

        const auto jsonBytes = QJsonDocument( obj ).toJson( QJsonDocument::Compact );

        WHEN( "It is formatted" )
        {
            const auto result = formatJwtJson( jsonBytes );

            THEN( "nbf epoch 0 is annotated as 1970-01-01" )
            {
                REQUIRE( result.contains( "\"nbf\": 0  // 1970-01-01T00:00:00Z" ) );
            }

            THEN( "auth_time is annotated correctly" )
            {
                REQUIRE( result.contains( "\"auth_time\": 1609459200  // 2021-01-01T00:00:00Z" ) );
            }
        }
    }

    GIVEN( "Invalid JSON bytes" )
    {
        const QByteArray garbage = "not json at all";

        WHEN( "It is formatted" )
        {
            const auto result = formatJwtJson( garbage );

            THEN( "The raw input is returned unchanged" )
            {
                REQUIRE( result == "not json at all" );
            }
        }
    }
}

SCENARIO( "Full JWT token decoding", "[jwtdecoder]" )
{
    GIVEN( "A valid 3-part JWT token" )
    {
        // Header: {"alg":"HS256","typ":"JWT"}
        // Payload: {"sub":"1234567890","name":"John Doe","iat":1516239022}
        // Signature: (dummy bytes)
        const QString token
            = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
              "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
              "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c";

        WHEN( "It is decoded" )
        {
            const auto result = decodeToken( token );

            THEN( "The result is not empty" )
            {
                REQUIRE_FALSE( result.isEmpty() );
            }

            THEN( "It contains the header section" )
            {
                REQUIRE( result.contains( QString::fromUtf8( "═══ JWT Header ═══" ) ) );
            }

            THEN( "It contains the payload section" )
            {
                REQUIRE( result.contains( QString::fromUtf8( "═══ JWT Payload ═══" ) ) );
            }

            THEN( "It contains the signature section" )
            {
                REQUIRE( result.contains( QString::fromUtf8( "═══ Signature ═══" ) ) );
            }

            THEN( "The header shows the algorithm" )
            {
                REQUIRE( result.contains( "\"alg\": \"HS256\"" ) );
            }

            THEN( "The payload shows the subject" )
            {
                REQUIRE( result.contains( "\"sub\": \"1234567890\"" ) );
            }

            THEN( "The iat timestamp is annotated with a readable date" )
            {
                REQUIRE( result.contains( "\"iat\": 1516239022  // 2018-01-18T01:30:22Z" ) );
            }
        }
    }

    GIVEN( "A JWT with only header and payload (no signature)" )
    {
        // Header: {"alg":"none"}
        // Payload: {"sub":"test"}
        const QString token = "eyJhbGciOiJub25lIn0.eyJzdWIiOiJ0ZXN0In0.";

        WHEN( "It is decoded" )
        {
            const auto result = decodeToken( token );

            THEN( "The result contains header and payload" )
            {
                REQUIRE( result.contains( QString::fromUtf8( "═══ JWT Header ═══" ) ) );
                REQUIRE( result.contains( QString::fromUtf8( "═══ JWT Payload ═══" ) ) );
            }

            THEN( "The signature section is not present for empty signature" )
            {
                REQUIRE_FALSE( result.contains( QString::fromUtf8( "═══ Signature ═══" ) ) );
            }
        }
    }

    GIVEN( "A token with leading and trailing whitespace" )
    {
        const QString token
            = "  eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
              "eyJzdWIiOiIxMjM0NTY3ODkwIn0."
              "dozjgNryP4J3jVmNHl0w5N_XgL0n3I9PlFUP0THsR8U  \n";

        WHEN( "It is decoded" )
        {
            const auto result = decodeToken( token );

            THEN( "Whitespace is trimmed and decoding succeeds" )
            {
                REQUIRE_FALSE( result.isEmpty() );
                REQUIRE( result.contains( "\"alg\": \"HS256\"" ) );
            }
        }
    }

    GIVEN( "An invalid string with wrong number of parts" )
    {
        WHEN( "A string with only one part is decoded" )
        {
            const auto result = decodeToken( "justonepart" );

            THEN( "The result is empty" )
            {
                REQUIRE( result.isEmpty() );
            }
        }

        WHEN( "A string with four parts is decoded" )
        {
            const auto result = decodeToken( "a.b.c.d" );

            THEN( "The result is empty" )
            {
                REQUIRE( result.isEmpty() );
            }
        }
    }

    GIVEN( "An empty string" )
    {
        WHEN( "It is decoded" )
        {
            const auto result = decodeToken( "" );

            THEN( "The result is empty" )
            {
                REQUIRE( result.isEmpty() );
            }
        }
    }
}
