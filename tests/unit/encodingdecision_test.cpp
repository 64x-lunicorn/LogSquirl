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

#include "encodingdecision.h"

#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "textencoding.h"

// The one rule that decides the Encoding a Log File is shown in, and whether
// showing it indexes the Log File again or only decodes its Log Lines again
// (#947), on its own: no Log File, no log data, no Qt application, just the
// Encodings by their MIBs.

namespace {

constexpr int Utf8 = TextEncoding::Utf8Mib;
constexpr int Latin1 = TextEncoding::Latin1Mib;
constexpr int Utf16LE = TextEncoding::Utf16LEMib;
constexpr int Utf16BE = TextEncoding::Utf16BEMib;

constexpr std::optional<int> None = std::nullopt;

} // namespace

SCENARIO( "A Log File nothing is chosen or detected for yet is shown in the locale's Encoding",
          "[encodingdecision]" )
{
    const auto locale = TextEncoding::forLocale()->mibEnum();

    GIVEN( "no Encoding chosen, none detected and an Index with none" )
    {
        WHEN( "it was never settled" )
        {
            const auto decision = decideEncoding( None, None, None, None, None );

            THEN( "it is shown in the locale's Encoding and its Log Lines decode again" )
            {
                REQUIRE( decision.shown == locale );
                REQUIRE( decision.change == EncodingChange::Redecode );
            }
        }

        WHEN( "it was settled in the locale's Encoding before" )
        {
            const auto decision = decideEncoding( locale, None, None, None, None );

            THEN( "nothing changes" )
            {
                REQUIRE( decision.shown == locale );
                REQUIRE( decision.change == EncodingChange::Unchanged );
            }
        }
    }
}

SCENARIO( "The Encoding chosen equal to the one detected keeps the Index", "[encodingdecision]" )
{
    GIVEN( "UTF-8 chosen for a Log File indexed in the UTF-8 it was detected in" )
    {
        WHEN( "it was never settled" )
        {
            const auto decision = decideEncoding( None, Utf8, Utf8, None, Utf8 );

            THEN( "it is shown in UTF-8 and its Log Lines decode again" )
            {
                REQUIRE( decision.shown == Utf8 );
                REQUIRE( decision.change == EncodingChange::Redecode );
            }
        }

        WHEN( "it was settled in UTF-8 before" )
        {
            const auto decision = decideEncoding( Utf8, Utf8, Utf8, None, Utf8 );

            THEN( "nothing changes" )
            {
                REQUIRE( decision.shown == Utf8 );
                REQUIRE( decision.change == EncodingChange::Unchanged );
            }
        }
    }
}

SCENARIO( "An Encoding chosen that splits the Log Lines differently indexes again in it",
          "[encodingdecision]" )
{
    GIVEN( "a Log File shown and indexed in the UTF-8 detected" )
    {
        WHEN( "UTF-16LE is chosen" )
        {
            const auto decision = decideEncoding( Utf8, Utf16LE, Utf8, None, Utf8 );

            THEN( "it is shown in UTF-16LE and indexed again forced to it" )
            {
                REQUIRE( decision.shown == Utf16LE );
                REQUIRE( decision.change == EncodingChange::ReindexForced );
            }
        }
    }

    GIVEN( "a Log File indexed forced to the UTF-16LE chosen" )
    {
        WHEN( "UTF-16BE is chosen" )
        {
            const auto decision = decideEncoding( Utf16LE, Utf16BE, Utf8, Utf16LE, Utf8 );

            THEN( "it is indexed again forced to UTF-16BE, its line feed sits elsewhere" )
            {
                REQUIRE( decision.shown == Utf16BE );
                REQUIRE( decision.change == EncodingChange::ReindexForced );
            }
        }
    }
}

SCENARIO( "An Encoding chosen that differs only in name decodes the Log Lines again",
          "[encodingdecision]" )
{
    GIVEN( "a Log File shown and indexed in the UTF-8 detected" )
    {
        WHEN( "ISO-8859-1 is chosen, whose line feed is the same byte" )
        {
            const auto decision = decideEncoding( Utf8, Latin1, Utf8, None, Utf8 );

            THEN( "it is shown in ISO-8859-1 and keeps its Index" )
            {
                REQUIRE( decision.shown == Latin1 );
                REQUIRE( decision.change == EncodingChange::Redecode );
            }
        }
    }
}

SCENARIO( "Back to Auto from an Encoding forced on the Index detects anew", "[encodingdecision]" )
{
    GIVEN( "a Log File indexed forced to the UTF-16LE chosen, UTF-8 detected" )
    {
        WHEN( "the Encoding is set back to Auto" )
        {
            const auto decision = decideEncoding( Utf16LE, None, Utf8, Utf16LE, Utf8 );

            THEN( "it is shown in the UTF-8 detected and indexed again detecting anew" )
            {
                REQUIRE( decision.shown == Utf8 );
                REQUIRE( decision.change == EncodingChange::ReindexDetecting );
            }
        }
    }

    GIVEN( "a Log File indexed forced to ISO-8859-1 chosen, UTF-8 detected" )
    {
        WHEN( "the Encoding is set back to Auto" )
        {
            const auto decision = decideEncoding( Latin1, None, Utf8, Latin1, Utf8 );

            THEN( "its Log Lines only decode again, they split the same" )
            {
                REQUIRE( decision.shown == Utf8 );
                REQUIRE( decision.change == EncodingChange::Redecode );
            }
        }
    }
}

SCENARIO( "A Log File that grew is shown in the Encoding detected in it now", "[encodingdecision]" )
{
    GIVEN( "a Log File shown in the ISO-8859-1 detected, nothing chosen" )
    {
        WHEN( "after it grew, UTF-8 is detected and the Index guessed it" )
        {
            const auto decision = decideEncoding( Latin1, None, Utf8, None, Utf8 );

            THEN( "it is shown in UTF-8 and its Log Lines decode again" )
            {
                REQUIRE( decision.shown == Utf8 );
                REQUIRE( decision.change == EncodingChange::Redecode );
            }
        }

        WHEN( "after it grew, the Encoding detected is the same" )
        {
            const auto decision = decideEncoding( Latin1, None, Latin1, None, Latin1 );

            THEN( "nothing changes: what was read in it stays" )
            {
                REQUIRE( decision.shown == Latin1 );
                REQUIRE( decision.change == EncodingChange::Unchanged );
            }
        }
    }

    GIVEN( "a Log File with UTF-8 chosen" )
    {
        WHEN( "after it grew, another Encoding is detected" )
        {
            const auto decision = decideEncoding( Utf8, Utf8, Latin1, Utf8, Latin1 );

            THEN( "the one chosen stays, nothing changes" )
            {
                REQUIRE( decision.shown == Utf8 );
                REQUIRE( decision.change == EncodingChange::Unchanged );
            }
        }
    }
}

SCENARIO( "The MIB of an Encoding", "[encodingdecision]" )
{
    REQUIRE( encodingMib( nullptr ) == std::nullopt );
    REQUIRE( encodingMib( TextEncoding::forMib( Utf8 ) ) == Utf8 );
}
