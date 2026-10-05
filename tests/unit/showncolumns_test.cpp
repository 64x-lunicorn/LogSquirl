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

// The columns of a Log Line as the view shows it with Value Names (#740),
// without a view: a Portion in the display columns of the raw Log Line, with
// tabs before, inside and after a Named Value.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QList>
#include <QString>

#include "showncolumns.h"
#include "valuenamer.h"
#include "valuenames_fixture.h"

using logsquirl::valuenames::GroupTable;
using logsquirl::valuenames::NameRow;
using logsquirl::valuenames::NameTable;
using logsquirl::valuenames::NamingGroup;
using logsquirl::valuenames::NamingRule;
using logsquirl::valuenames::ValueNamer;

namespace {

// The example of the ticket (#647), and "v=a<tab>b" naming its raw value
// "a<tab>b", which holds a tab.
ValueNamer exampleNamer()
{
    auto tabbed = NamingGroup::createNewGroup( QStringLiteral( "Tabbed" ) );
    NamingRule rule;
    rule.name = QStringLiteral( "Tabbed" );
    rule.pattern = QStringLiteral( "v=(a\tb)" );
    rule.groupTables = { GroupTable{ QStringLiteral( "1" ), QStringLiteral( "Tabbed" ) } };
    tabbed.setRules( { rule } );
    tabbed.setTables( { NameTable{ QStringLiteral( "Tabbed" ),
                                   { NameRow{ QStringLiteral( "a\tb" ), QStringLiteral( "AB" ) } } } } );
    return ValueNamer{ QList<NamingGroup>{ valuenamesfixture::exampleGroup(), tabbed } };
}

struct Covering {
    const char* what;
    QString line;
    int start;
    int end;
    int coveredStart;
    int coveredEnd;
};

// "<tab>BAP << ECU 0x15 0x14<tab>end": the tab is columns 0-7, "0x15" 19-22,
// "0x14" 24-27, the second tab 28-31 and "end" 32-34.
const QString EcuLine = QStringLiteral( "\tBAP << ECU 0x15 0x14\tend" );
// "v=a<tab>b end": the Named Value "a<tab>b" is columns 2-8, its tab 3-7.
const QString TabbedLine = QStringLiteral( "v=a\tb end" );
// "x id=7": the Named Value "7" is column 5, the last.
const QString IdLine = QStringLiteral( "x id=7" );

} // namespace

TEST_CASE( "A Portion covers every Named Value it takes part of", "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();
    REQUIRE_FALSE( namer.isEmpty() );

    const auto row = GENERATE( values<Covering>( {
        { "inside a Named Value after a tab", EcuLine, 20, 21, 19, 22 },
        { "exactly a Named Value", EcuLine, 19, 22, 19, 22 },
        { "from the first column of a Named Value", EcuLine, 19, 20, 19, 22 },
        { "on the last column of a Named Value", EcuLine, 22, 22, 19, 22 },
        { "from before a Named Value into it", EcuLine, 15, 20, 15, 22 },
        { "across two Named Values", EcuLine, 21, 25, 19, 27 },
        { "from a Named Value to the text after the tab after it", EcuLine, 25, 34, 24, 34 },
        { "beside a Named Value, before it", EcuLine, 18, 18, 18, 18 },
        { "beside a Named Value, between two", EcuLine, 23, 23, 23, 23 },
        { "after the tab after a Named Value", EcuLine, 32, 34, 32, 34 },
        { "over the tab before a Named Value", EcuLine, 0, 7, 0, 7 },
        { "from the tab before a Named Value into it", EcuLine, 0, 20, 0, 22 },
        { "inside the tab of a Named Value's raw text", TabbedLine, 4, 5, 2, 8 },
        { "after the tab of a Named Value's raw text", TabbedLine, 8, 8, 2, 8 },
        { "from before a Named Value with a tab into it", TabbedLine, 0, 2, 0, 8 },
        { "after a Named Value with a tab", TabbedLine, 10, 12, 10, 12 },
        { "from a Named Value past the end of the Log Line", EcuLine, 25, 50, 24, 34 },
        { "from text past the end of the Log Line", EcuLine, 33, 50, 33, 34 },
        { "from a Named Value at the end past the end of the Log Line", IdLine, 5, 20, 5, 5 },
        { "from before a Named Value at the end past the end", IdLine, 4, 20, 4, 5 },
    } ) );

    CAPTURE( row.what, row.line, row.start, row.end );
    const ShownColumns columns{ &namer, [ & ] { return row.line; } };
    const auto covered
        = columns.covering( Portion{ 3_lnum, LineColumn( row.start ), LineColumn( row.end ) } );

    CHECK( covered.line() == 3_lnum );
    CHECK( covered.startColumn() == LineColumn( row.coveredStart ) );
    CHECK( covered.endColumn() == LineColumn( row.coveredEnd ) );
}

TEST_CASE( "A Portion past the end of a Log Line covers no Named Value",
           "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();

    const auto row = GENERATE( values<Covering>( {
        { "just past a Named Value at the end", IdLine, 6, 6, 6, 6 },
        { "further past a Named Value at the end", IdLine, 6, 9, 6, 9 },
        { "far past the end of a Log Line with Named Values", EcuLine, 40, 60, 40, 60 },
    } ) );

    CAPTURE( row.what, row.line, row.start, row.end );
    const ShownColumns columns{ &namer, [ & ] { return row.line; } };
    const auto covered
        = columns.covering( Portion{ 0_lnum, LineColumn( row.start ), LineColumn( row.end ) } );

    CHECK( covered.startColumn() == LineColumn( row.coveredStart ) );
    CHECK( covered.endColumn() == LineColumn( row.coveredEnd ) );
}

TEST_CASE( "A Portion on a Log Line without Named Values stays as it is",
           "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();
    const ShownColumns columns{ &namer, [] { return QStringLiteral( "\tnothing named\there" ); } };

    const auto [ start, end ] = GENERATE( table<int, int>( {
        { 0, 7 },
        { 3, 10 },
        { 9, 40 },
        { 60, 70 },
    } ) );
    const auto covered
        = columns.covering( Portion{ 1_lnum, LineColumn( start ), LineColumn( end ) } );

    CHECK( covered.startColumn() == LineColumn( start ) );
    CHECK( covered.endColumn() == LineColumn( end ) );
}

TEST_CASE( "An invalid Portion stays as it is", "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();
    const ShownColumns columns{ &namer, [] { return EcuLine; } };

    CHECK_FALSE( columns.covering( Portion{} ).isValid() );
    const auto reversed = columns.covering( Portion{ 0_lnum, 21_lcol, 20_lcol } );
    CHECK( reversed.startColumn() == 21_lcol );
    CHECK( reversed.endColumn() == 20_lcol );
}

TEST_CASE( "Without a namer the columns read no Log Line and change no Portion",
           "[showncolumns][valuenames]" )
{
    int reads = 0;
    const auto readLine = [ &reads ] {
        ++reads;
        return EcuLine;
    };

    SECTION( "built without a namer" )
    {
        const ShownColumns columns{ nullptr, readLine };

        const auto [ start, end ] = GENERATE( table<int, int>( {
            { 20, 21 },
            { 3, 25 },
            { 25, 50 },
            { 60, 70 },
        } ) );
        const auto covered
            = columns.covering( Portion{ 2_lnum, LineColumn( start ), LineColumn( end ) } );

        CHECK( covered.line() == 2_lnum );
        CHECK( covered.startColumn() == LineColumn( start ) );
        CHECK( covered.endColumn() == LineColumn( end ) );
    }

    SECTION( "the identity" )
    {
        const ShownColumns columns;
        const auto covered = columns.covering( Portion{ 2_lnum, 20_lcol, 21_lcol } );

        CHECK( covered.startColumn() == 20_lcol );
        CHECK( covered.endColumn() == 21_lcol );
    }

    CHECK( reads == 0 );
}

TEST_CASE( "With a namer the columns read the Log Line once", "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();
    int reads = 0;
    const ShownColumns columns{ &namer, [ &reads ] {
                                   ++reads;
                                   return EcuLine;
                               } };

    columns.covering( Portion{ 0_lnum, 20_lcol, 21_lcol } );
    columns.covering( Portion{ 0_lnum, 25_lcol, 26_lcol } );

    CHECK( reads == 1 );
}
