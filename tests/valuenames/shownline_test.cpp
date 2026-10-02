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

#include "shownline.h"

#include <catch2/catch_test_macros.hpp>

using namespace logsquirl::valuenames;

namespace {

NamedValue named( qsizetype start, qsizetype length, const QString& shown )
{
    NamedValue value;
    value.start = start;
    value.length = length;
    value.shown = shown;
    return value;
}

// "ECU 0x15 0x14 end": 0x15 shown as "Beispiel(0x15)", 0x14 as "S".
ShownLine exampleLine()
{
    return ShownLine{ QStringLiteral( "ECU 0x15 0x14 end" ),
                      { named( 4, 4, QStringLiteral( "Beispiel(0x15)" ) ),
                        named( 9, 4, QStringLiteral( "S" ) ) } };
}

} // namespace

TEST_CASE( "A Log Line without Named Values is shown as it is", "[valuenames][shownline]" )
{
    const ShownLine line{ QStringLiteral( "a\tb" ), {} };

    CHECK_FALSE( line.hasNamedValues() );
    CHECK( line.text() == QStringLiteral( "a\tb" ) );
    for ( qsizetype column = 0; column <= 3; ++column ) {
        CHECK( line.toShown( column, Snap::ToStart ) == column );
        CHECK( line.toRaw( column, Snap::ToEnd ) == column );
    }
    CHECK( line.valueAtRaw( 1 ) == -1 );
    CHECK( line.valueAtShown( 1 ) == -1 );
}

TEST_CASE( "A Log Line is shown with its Named Values in place of their raw text",
           "[valuenames][shownline]" )
{
    const auto line = exampleLine();

    CHECK( line.hasNamedValues() );
    CHECK( line.text() == QStringLiteral( "ECU Beispiel(0x15) S end" ) );
    CHECK( line.shownStart( 0 ) == 4 );
    CHECK( line.shownEnd( 0 ) == 18 );
    CHECK( line.shownStart( 1 ) == 19 );
    CHECK( line.shownEnd( 1 ) == 20 );
}

TEST_CASE( "A raw column maps to the column shown", "[valuenames][shownline]" )
{
    const auto line = exampleLine();

    SECTION( "before, between and after the Named Values it moves with them" )
    {
        CHECK( line.toShown( 0, Snap::ToStart ) == 0 );
        CHECK( line.toShown( 4, Snap::ToEnd ) == 4 );
        CHECK( line.toShown( 8, Snap::ToStart ) == 18 );
        CHECK( line.toShown( 9, Snap::ToStart ) == 19 );
        CHECK( line.toShown( 13, Snap::ToStart ) == 20 );
        CHECK( line.toShown( 17, Snap::ToEnd ) == 24 );
    }

    SECTION( "inside a Named Value it goes to the start or the end of what is shown" )
    {
        CHECK( line.toShown( 5, Snap::ToStart ) == 4 );
        CHECK( line.toShown( 5, Snap::ToEnd ) == 18 );
        CHECK( line.toShown( 12, Snap::ToStart ) == 19 );
        CHECK( line.toShown( 12, Snap::ToEnd ) == 20 );
    }
}

TEST_CASE( "A column shown maps back to the raw column", "[valuenames][shownline]" )
{
    const auto line = exampleLine();

    SECTION( "outside the Named Values" )
    {
        CHECK( line.toRaw( 0, Snap::ToStart ) == 0 );
        CHECK( line.toRaw( 4, Snap::ToStart ) == 4 );
        CHECK( line.toRaw( 18, Snap::ToEnd ) == 8 );
        CHECK( line.toRaw( 19, Snap::ToStart ) == 9 );
        CHECK( line.toRaw( 20, Snap::ToStart ) == 13 );
        CHECK( line.toRaw( 24, Snap::ToStart ) == 17 );
    }

    SECTION( "inside what a Named Value shows it goes to the start or end of its raw text" )
    {
        CHECK( line.toRaw( 10, Snap::ToStart ) == 4 );
        CHECK( line.toRaw( 10, Snap::ToEnd ) == 8 );
    }
}

TEST_CASE( "The Named Value a character belongs to", "[valuenames][shownline]" )
{
    const auto line = exampleLine();

    CHECK( line.valueAtRaw( 3 ) == -1 );
    CHECK( line.valueAtRaw( 4 ) == 0 );
    CHECK( line.valueAtRaw( 7 ) == 0 );
    CHECK( line.valueAtRaw( 8 ) == -1 );
    CHECK( line.valueAtRaw( 9 ) == 1 );
    CHECK( line.valueAtRaw( 13 ) == -1 );

    CHECK( line.valueAtShown( 3 ) == -1 );
    CHECK( line.valueAtShown( 4 ) == 0 );
    CHECK( line.valueAtShown( 17 ) == 0 );
    CHECK( line.valueAtShown( 18 ) == -1 );
    CHECK( line.valueAtShown( 19 ) == 1 );
    CHECK( line.valueAtShown( 20 ) == -1 );
}

TEST_CASE( "A raw range touching part of a Named Value covers all of it",
           "[valuenames][shownline]" )
{
    const auto line = exampleLine();

    // "x1" of "0x15" up to the blank before "0x14".
    CHECK( line.wholeRawRange( 5, 7 ) == std::pair<qsizetype, qsizetype>{ 4, 8 } );
    // From the blank after 0x15 into 0x14.
    CHECK( line.wholeRawRange( 8, 10 ) == std::pair<qsizetype, qsizetype>{ 8, 13 } );
    // Next to a Named Value, not in it.
    CHECK( line.wholeRawRange( 0, 4 ) == std::pair<qsizetype, qsizetype>{ 0, 4 } );
    CHECK( line.wholeRawRange( 13, 17 ) == std::pair<qsizetype, qsizetype>{ 13, 17 } );
}

TEST_CASE( "Adjacent Named Values and one that shows nothing", "[valuenames][shownline]" )
{
    const ShownLine line{ QStringLiteral( "ab" ),
                          { named( 0, 1, QStringLiteral( "XY" ) ), named( 1, 1, QString{} ) } };

    CHECK( line.text() == QStringLiteral( "XY" ) );
    CHECK( line.toShown( 1, Snap::ToStart ) == 2 );
    CHECK( line.toShown( 2, Snap::ToStart ) == 2 );
    CHECK( line.toRaw( 2, Snap::ToEnd ) == 2 );
    CHECK( line.toRaw( 1, Snap::ToStart ) == 0 );
    CHECK( line.toRaw( 1, Snap::ToEnd ) == 1 );
    CHECK( line.valueAtShown( 1 ) == 0 );
}
