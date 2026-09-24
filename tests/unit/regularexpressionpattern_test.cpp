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

#include <catch2/catch_test_macros.hpp>

#include <unordered_map>

#include "regularexpressionpattern.h"

namespace {

// Mirrors the shape of LogFilteredData's search-results cache key: every
// pattern hashes into the same bucket, so a lookup can only tell two
// patterns apart via operator==. This pins down that a cache keyed on
// RegularExpressionPattern cannot conflate two patterns that differ only in
// inverse, boolean, plain-text or prefilter mode.
struct AlwaysCollidingHash {
    std::size_t operator()( const RegularExpressionPattern& ) const
    {
        return 0;
    }
};

RegularExpressionPattern makePattern( const QString& text = "error", bool caseSensitive = true,
                                      bool inverse = false, bool boolean = false,
                                      bool plainText = false, bool prefilter = false )
{
    RegularExpressionPattern pattern( text, caseSensitive, inverse, boolean, plainText );
    pattern.isPrefilter = prefilter;
    return pattern;
}

} // namespace

SCENARIO( "RegularExpressionPattern equality compares every field against the other pattern",
          "[regex][pattern]" )
{
    GIVEN( "two patterns identical in every field" )
    {
        const auto a = makePattern();
        const auto b = makePattern();

        THEN( "they are equal" )
        {
            REQUIRE( a == b );
        }
    }

    GIVEN( "two patterns differing only in text" )
    {
        THEN( "they are not equal" )
        {
            REQUIRE_FALSE( makePattern( "error" ) == makePattern( "warning" ) );
        }
    }

    GIVEN( "two patterns differing only in case sensitivity" )
    {
        THEN( "they are not equal" )
        {
            REQUIRE_FALSE( makePattern( "error", true ) == makePattern( "error", false ) );
        }
    }

    GIVEN( "two patterns differing only in inverse (exclude) mode" )
    {
        THEN( "they are not equal" )
        {
            REQUIRE_FALSE( makePattern( "error", true, false )
                           == makePattern( "error", true, true ) );
        }
    }

    GIVEN( "two patterns differing only in boolean mode" )
    {
        THEN( "they are not equal" )
        {
            REQUIRE_FALSE( makePattern( "error", true, false, false )
                           == makePattern( "error", true, false, true ) );
        }
    }

    GIVEN( "two patterns differing only in plain-text mode" )
    {
        THEN( "they are not equal" )
        {
            REQUIRE_FALSE( makePattern( "error", true, false, false, false )
                           == makePattern( "error", true, false, false, true ) );
        }
    }

    GIVEN( "two patterns differing only in the prefilter flag" )
    {
        THEN( "they are not equal" )
        {
            REQUIRE_FALSE( makePattern( "error", true, false, false, false, false )
                           == makePattern( "error", true, false, false, false, true ) );
        }
    }
}

SCENARIO( "A cache keyed on RegularExpressionPattern does not conflate two modes",
          "[regex][pattern][cache]" )
{
    GIVEN( "a cache whose key is a RegularExpressionPattern, all colliding into one bucket" )
    {
        std::unordered_map<RegularExpressionPattern, int, AlwaysCollidingHash> cache;

        WHEN( "a plain search and its inverse, otherwise identical, are both cached" )
        {
            cache[ makePattern( "error", true, false ) ] = 1;
            cache[ makePattern( "error", true, true ) ] = 2;

            THEN( "each keeps its own entry" )
            {
                REQUIRE( cache.size() == 2 );
                REQUIRE( cache.at( makePattern( "error", true, false ) ) == 1 );
                REQUIRE( cache.at( makePattern( "error", true, true ) ) == 2 );
            }
        }

        WHEN( "a regex search and a boolean search, otherwise identical, are both cached" )
        {
            cache[ makePattern( "error", true, false, false ) ] = 1;
            cache[ makePattern( "error", true, false, true ) ] = 2;

            THEN( "each keeps its own entry" )
            {
                REQUIRE( cache.size() == 2 );
            }
        }

        WHEN( "a regular search and a prefilter search, otherwise identical, are both cached" )
        {
            cache[ makePattern( "error", true, false, false, false, false ) ] = 1;
            cache[ makePattern( "error", true, false, false, false, true ) ] = 2;

            THEN( "each keeps its own entry" )
            {
                REQUIRE( cache.size() == 2 );
            }
        }
    }
}

SCENARIO( "Only a pattern that refers back to a group gives up the capture-free fast path",
          "[regex][pattern][backref]" )
{
    GIVEN( "patterns that refer back to a group" )
    {
        THEN( "each of them is recognised" )
        {
            REQUIRE( usesBackreference( "(ERROR) \\1" ) );
            REQUIRE( usesBackreference( "(a)(b)\\2" ) );
            REQUIRE( usesBackreference( "(?<level>ERROR) \\k<level>" ) );
            REQUIRE( usesBackreference( "(?<level>ERROR) \\k'level'" ) );
            REQUIRE( usesBackreference( "(?P<level>ERROR) (?P=level)" ) );
            REQUIRE( usesBackreference( "(ERROR) \\g{1}" ) );
            REQUIRE( usesBackreference( "(ERROR) \\g-1" ) );
        }
    }

    GIVEN( "patterns in which a digit only looks like a backreference" )
    {
        THEN( "none of them is taken for one" )
        {
            REQUIRE_FALSE( usesBackreference( "ERROR" ) );
            REQUIRE_FALSE( usesBackreference( "path\\\\1" ) );
            REQUIRE_FALSE( usesBackreference( "\\d+ \\w+ \\s" ) );
            REQUIRE_FALSE( usesBackreference( "\\x41\\0" ) );
            REQUIRE_FALSE( usesBackreference( "[\\1-\\7]" ) );
            REQUIRE_FALSE( usesBackreference( "\\Q(a) \\1\\E" ) );
        }
    }

    GIVEN( "a pattern with a backreference" )
    {
        const RegularExpressionPattern pattern( "(ERROR) \\1" );

        THEN( "it is compiled with capturing groups, so it is valid" )
        {
            const auto regexp = static_cast<QRegularExpression>( pattern );
            REQUIRE( regexp.isValid() );
            REQUIRE_FALSE(
                regexp.patternOptions().testFlag( QRegularExpression::DontCaptureOption ) );
        }
    }

    GIVEN( "a pattern without a backreference" )
    {
        const RegularExpressionPattern pattern( "(ERROR|WARN) happened" );

        THEN( "it keeps the capture-free fast path" )
        {
            const auto regexp = static_cast<QRegularExpression>( pattern );
            REQUIRE( regexp.isValid() );
            REQUIRE( regexp.patternOptions().testFlag( QRegularExpression::DontCaptureOption ) );
        }
    }

    GIVEN( "a plain-text pattern that spells out a backslash and a digit" )
    {
        const RegularExpressionPattern pattern( "(a) \\1", true, false, false, true );

        THEN( "the escaped form decides, so it keeps the capture-free fast path" )
        {
            const auto regexp = static_cast<QRegularExpression>( pattern );
            REQUIRE( regexp.isValid() );
            REQUIRE( regexp.patternOptions().testFlag( QRegularExpression::DontCaptureOption ) );
            REQUIRE( regexp.match( "(a) \\1" ).hasMatch() );
        }
    }
}
