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

#include <optional>
#include <tuple>

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
    tabbed.setTables(
        { NameTable{ QStringLiteral( "Tabbed" ),
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

TEST_CASE( "A Portion inside a tab beside a Named Value keeps its columns",
           "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();

    const auto row = GENERATE( values<Covering>( {
        { "from inside the tab before a Named Value to text before it", EcuLine, 3, 10, 3, 10 },
        { "from inside the tab before a Named Value into it", EcuLine, 3, 20, 3, 22 },
        { "from a whole Named Value into the tab after it", EcuLine, 24, 29, 24, 29 },
        { "from inside a Named Value into the tab after it", EcuLine, 26, 29, 24, 29 },
        { "inside the tab after a Named Value", EcuLine, 29, 30, 29, 30 },
        { "from inside the tab after a Named Value to the end", EcuLine, 30, 50, 30, 34 },
        { "inside the tab of a Named Value's raw text", TabbedLine, 4, 5, 2, 8 },
    } ) );

    CAPTURE( row.what, row.line, row.start, row.end );
    const ShownColumns columns{ &namer, [ & ] { return row.line; } };
    const auto covered
        = columns.covering( Portion{ 0_lnum, LineColumn( row.start ), LineColumn( row.end ) } );

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

namespace {

struct TextShown {
    const char* what;
    QString line;
    int start;
    int end;
    QString text;
};

} // namespace

// The text shown of EcuLine is "<tab>BAP << ECU Beispiel(0x15) Sample(0x14)<tab>end":
// its second tab is two columns wide, not four. That of TabbedLine is
// "v=AB(a<tab>b) end", whose tab is two columns wide, not five.
TEST_CASE( "Copy as Shown of a Portion is the text shown for it", "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();

    const auto row = GENERATE( values<TextShown>( {
        { "inside a Named Value after a tab", EcuLine, 20, 21, QStringLiteral( "Beispiel(0x15)" ) },
        { "exactly a Named Value", EcuLine, 19, 22, QStringLiteral( "Beispiel(0x15)" ) },
        { "ending inside a Named Value", EcuLine, 8, 20,
          QStringLiteral( "BAP << ECU Beispiel(0x15)" ) },
        { "starting inside a Named Value", EcuLine, 21, 23, QStringLiteral( "Beispiel(0x15) " ) },
        { "across two Named Values", EcuLine, 21, 25,
          QStringLiteral( "Beispiel(0x15) Sample(0x14)" ) },
        { "beside a Named Value", EcuLine, 18, 18, QStringLiteral( " " ) },
        { "over the tab before a Named Value", EcuLine, 0, 7, QStringLiteral( "        " ) },
        { "from the tab before a Named Value into it", EcuLine, 0, 20,
          QStringLiteral( "        BAP << ECU Beispiel(0x15)" ) },
        { "from a Named Value over the tab after it", EcuLine, 25, 34,
          QStringLiteral( "Sample(0x14)  end" ) },
        { "after the tab after a Named Value", EcuLine, 32, 34, QStringLiteral( "end" ) },
        { "from a Named Value past the end of the Log Line", EcuLine, 25, 50,
          QStringLiteral( "Sample(0x14)  end" ) },
        { "inside the tab of a Named Value's raw text", TabbedLine, 4, 5,
          QStringLiteral( "AB(a  b)" ) },
        { "from before a Named Value with a tab into it", TabbedLine, 0, 2,
          QStringLiteral( "v=AB(a  b)" ) },
        { "after a Named Value with a tab", TabbedLine, 10, 12, QStringLiteral( "end" ) },
        { "from a Named Value at the end past the end of the Log Line", IdLine, 5, 20,
          QStringLiteral( "seven(7)" ) },
        { "from before a Named Value at the end past the end", IdLine, 4, 20,
          QStringLiteral( "=seven(7)" ) },
        { "just past a Named Value at the end", IdLine, 6, 6, QString{} },
        { "far past the end of a Log Line", EcuLine, 40, 60, QString{} },
    } ) );

    CAPTURE( row.what, row.line, row.start, row.end );
    const ShownColumns columns{ &namer, [ & ] { return row.line; } };

    CHECK( columns.textShown( Portion{ 3_lnum, LineColumn( row.start ), LineColumn( row.end ) } )
           == row.text );
}

// An end outside every Named Value keeps its place inside its tab: the same
// offset from the start of the tab in the text shown, cut to the width the
// tab has there (#748).
TEST_CASE( "Copy as Shown of a Portion inside a tab copies the part of it selected",
           "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();

    const auto row = GENERATE( values<TextShown>( {
        { "from inside the tab before a Named Value to text before it", EcuLine, 3, 10,
          QStringLiteral( "     BAP" ) },
        { "from inside the tab before a Named Value into it", EcuLine, 3, 20,
          QStringLiteral( "     BAP << ECU Beispiel(0x15)" ) },
        { "inside the tab before a Named Value", EcuLine, 2, 4, QStringLiteral( "   " ) },
        { "from a Named Value to inside the narrower tab after it", EcuLine, 24, 28,
          QStringLiteral( "Sample(0x14) " ) },
        { "from inside a Named Value to inside the narrower tab after it", EcuLine, 26, 29,
          QStringLiteral( "Sample(0x14)  " ) },
        { "on the first column of the narrower tab after a Named Value", EcuLine, 28, 28,
          QStringLiteral( " " ) },
        { "past the width the tab after a Named Value is shown with", EcuLine, 30, 31,
          QStringLiteral( " " ) },
        { "from inside the narrower tab after a Named Value to the end", EcuLine, 29, 50,
          QStringLiteral( " end" ) },
    } ) );

    CAPTURE( row.what, row.line, row.start, row.end );
    const ShownColumns columns{ &namer, [ & ] { return row.line; } };

    CHECK( columns.textShown( Portion{ 3_lnum, LineColumn( row.start ), LineColumn( row.end ) } )
           == row.text );
}

TEST_CASE( "Copy as Shown of a Portion on a Log Line without Named Values is what Copy gives",
           "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();
    const auto line = QStringLiteral( "\tnothing named\there" );
    const ShownColumns columns{ &namer, [ & ] { return line; } };
    const ShownColumns identity{ nullptr, [ & ] { return line; } };

    const auto row = GENERATE( values<TextShown>( {
        { "the tab", {}, 0, 7, QStringLiteral( "        " ) },
        { "a word", {}, 8, 14, QStringLiteral( "nothing" ) },
        { "from inside the first tab", {}, 3, 10, QStringLiteral( "     not" ) },
        { "inside the first tab", {}, 2, 4, QStringLiteral( "   " ) },
        { "into the second tab", {}, 18, 22, QStringLiteral( "med  " ) },
        { "from inside the second tab past the end", {}, 22, 40, QStringLiteral( "  here" ) },
        { "past the end", {}, 30, 40, QString{} },
    } ) );

    CAPTURE( row.what, row.start, row.end );
    const Portion portion{ 1_lnum, LineColumn( row.start ), LineColumn( row.end ) };
    CHECK( columns.textShown( portion ) == row.text );
    CHECK( columns.textShown( portion ) == identity.textShown( portion ) );
}

TEST_CASE( "Without a namer Copy as Shown of a Portion is its raw text, as Copy gives it",
           "[showncolumns][valuenames]" )
{
    int reads = 0;
    const ShownColumns columns{ nullptr, [ &reads ] {
                                   ++reads;
                                   return EcuLine;
                               } };

    const auto row = GENERATE( values<TextShown>( {
        { "inside a value", {}, 20, 21, QStringLiteral( "x1" ) },
        { "inside the tab before a value", {}, 3, 10, QStringLiteral( "     BAP" ) },
        { "inside the tab after a value", {}, 29, 30, QStringLiteral( "  " ) },
        { "past the end of the Log Line", {}, 30, 50, QStringLiteral( "  end" ) },
        { "wholly past the end", {}, 40, 60, QString{} },
    } ) );

    CAPTURE( row.what );
    CHECK( columns.textShown( Portion{ 1_lnum, LineColumn( row.start ), LineColumn( row.end ) } )
           == row.text );
    CHECK( reads == 1 );
}

TEST_CASE( "The identity has no text shown", "[showncolumns][valuenames]" )
{
    CHECK( ShownColumns{}.textShown( Portion{ 0_lnum, 0_lcol, 5_lcol } ).isEmpty() );
}

TEST_CASE( "Copy as Shown reads and names the Log Line once", "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();
    int reads = 0;
    const ShownColumns columns{ &namer, [ &reads ] {
                                   ++reads;
                                   return EcuLine;
                               } };

    CHECK( columns.textShown( Portion{ 0_lnum, 20_lcol, 21_lcol } )
           == QStringLiteral( "Beispiel(0x15)" ) );
    CHECK( columns.textShown( Portion{ 0_lnum, 25_lcol, 26_lcol } )
           == QStringLiteral( "Sample(0x14)" ) );

    CHECK( reads == 1 );
}

TEST_CASE( "Copy as Shown of a whole Log Line is its text shown, its tabs as they are",
           "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();
    int reads = 0;
    const auto read = [ &reads ]( const QString& line ) {
        return [ &reads, line ] {
            ++reads;
            return line;
        };
    };

    SECTION( "a Log Line with Named Values" )
    {
        CHECK( ShownColumns{ &namer, read( EcuLine ) }.textShown()
               == QStringLiteral( "\tBAP << ECU Beispiel(0x15) Sample(0x14)\tend" ) );
        CHECK( ShownColumns{ &namer, read( TabbedLine ) }.textShown()
               == QStringLiteral( "v=AB(a\tb) end" ) );
        CHECK( reads == 2 );
    }

    SECTION( "a Log Line without Named Values is what Copy gives" )
    {
        const auto line = QStringLiteral( "\tno value\there" );
        CHECK( ShownColumns{ &namer, read( line ) }.textShown() == line );
        CHECK( reads == 1 );
    }

    SECTION( "without a namer, the Log Line read, once" )
    {
        CHECK( ShownColumns{ nullptr, read( EcuLine ) }.textShown() == EcuLine );
        CHECK( reads == 1 );
    }

    SECTION( "the identity, which reads none, gives an empty text" )
    {
        CHECK( ShownColumns{}.textShown().isEmpty() );
    }
}

namespace {

// A Portion, comparable, as its line and columns; one of -1 for none.
std::tuple<uint64_t, int64_t, int64_t> columnsOf( const std::optional<Portion>& portion )
{
    if ( !portion.has_value() ) {
        return { 0, -1, -1 };
    }
    return { portion->line().get(), portion->startColumn().get(), portion->endColumn().get() };
}

} // namespace

TEST_CASE( "A Log Line named already gives what it gives read and named",
           "[showncolumns][valuenames]" )
{
    using logsquirl::valuenames::Snap;
    const auto namer = exampleNamer();
    // What the Viewport holds for a Log Line: its raw text, and the Log Line
    // as shown, empty where nothing is named (#745).
    const auto line = GENERATE( EcuLine, TabbedLine, IdLine, QStringLiteral( "\tnothing named" ) );
    const auto namedValues = namer.namedValues( line );
    const auto shown = namedValues.isEmpty()
                           ? logsquirl::valuenames::ShownLine{}
                           : logsquirl::valuenames::ShownLine{ line, namedValues };
    CAPTURE( line );

    const ShownColumns named{ line, shown };
    const ShownColumns read{ &namer, [ & ] { return line; } };

    for ( int start = 0; start < 40; ++start ) {
        for ( const auto end : { start, start + 1, start + 6, 50 } ) {
            CAPTURE( start, end );
            const Portion portion{ 2_lnum, LineColumn( start ), LineColumn( end ) };
            CHECK( columnsOf( named.covering( portion ) )
                   == columnsOf( read.covering( portion ) ) );
            CHECK( named.textShown( portion ) == read.textShown( portion ) );
        }
        const FilePosition position{ 2_lnum, LineColumn( start ) };
        CHECK( columnsOf( named.namedValueAt( position ) )
               == columnsOf( read.namedValueAt( position ) ) );
        for ( const auto snap : { Snap::ToStart, Snap::ToEnd } ) {
            CHECK( named.shownColumn( LineColumn( start ), snap )
                   == read.shownColumn( LineColumn( start ), snap ) );
            CHECK( named.rawColumn( LineColumn( start ), snap )
                   == read.rawColumn( LineColumn( start ), snap ) );
        }
        const auto namedValue = named.namedValueShownAt( LineColumn( start ) );
        const auto readValue = read.namedValueShownAt( LineColumn( start ) );
        CHECK( namedValue.has_value() == readValue.has_value() );
        if ( namedValue.has_value() && readValue.has_value() ) {
            CHECK( namedValue->start == readValue->start );
            CHECK( namedValue->name == readValue->name );
        }
    }
}

namespace {

struct ValueAt {
    const char* what;
    QString line;
    int column;
    // The raw display columns of the Named Value; -1 for none.
    int valueStart;
    int valueEnd;
};

} // namespace

TEST_CASE( "A double-click on a Named Value takes all of it", "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();

    const auto row = GENERATE( values<ValueAt>( {
        { "inside a Named Value after a tab", EcuLine, 20, 19, 22 },
        { "on the first column of a Named Value", EcuLine, 19, 19, 22 },
        { "on the last column of a Named Value", EcuLine, 22, 19, 22 },
        { "on the last column of a Named Value before a tab", EcuLine, 27, 24, 27 },
        { "inside the tab of a Named Value's raw text", TabbedLine, 4, 2, 8 },
        { "after the tab of a Named Value's raw text", TabbedLine, 8, 2, 8 },
        { "on a Named Value at the end of the Log Line", IdLine, 5, 5, 5 },
        { "beside a Named Value, before it", EcuLine, 18, -1, -1 },
        { "beside a Named Value, between two", EcuLine, 23, -1, -1 },
        { "inside the tab before a Named Value", EcuLine, 3, -1, -1 },
        { "on the first column of the tab after a Named Value", EcuLine, 28, -1, -1 },
        { "inside the tab after a Named Value", EcuLine, 30, -1, -1 },
        { "after a Named Value with a tab", TabbedLine, 9, -1, -1 },
        { "just past a Named Value at the end", IdLine, 6, -1, -1 },
        { "far past the end of the Log Line", EcuLine, 50, -1, -1 },
    } ) );

    CAPTURE( row.what, row.line, row.column );
    const ShownColumns columns{ &namer, [ & ] { return row.line; } };
    const auto value = columns.namedValueAt( FilePosition{ 4_lnum, LineColumn( row.column ) } );

    if ( row.valueStart < 0 ) {
        CHECK_FALSE( value.has_value() );
    }
    else {
        REQUIRE( value.has_value() );
        CHECK( value->line() == 4_lnum );
        CHECK( value->startColumn() == LineColumn( row.valueStart ) );
        CHECK( value->endColumn() == LineColumn( row.valueEnd ) );
    }
}

TEST_CASE( "No Named Value is found on a Log Line without one or without a namer",
           "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();
    const auto column = GENERATE( 0, 3, 9, 20, 60 );
    CAPTURE( column );

    SECTION( "a Log Line without Named Values" )
    {
        const ShownColumns columns{ &namer,
                                    [] { return QStringLiteral( "\tnothing named\there" ); } };
        CHECK_FALSE( columns.namedValueAt( FilePosition{ 0_lnum, LineColumn( column ) } ) );
    }

    SECTION( "without a namer" )
    {
        int reads = 0;
        const ShownColumns columns{ nullptr, [ &reads ] {
                                       ++reads;
                                       return EcuLine;
                                   } };
        CHECK_FALSE( columns.namedValueAt( FilePosition{ 0_lnum, LineColumn( column ) } ) );
        CHECK_FALSE( ShownColumns{}.namedValueAt( FilePosition{ 0_lnum, LineColumn( column ) } ) );
        CHECK( reads == 0 );
    }
}

namespace {

struct ShownColumn {
    const char* what;
    QString line;
    int rawColumn;
    int toStart;
    int toEnd;
};

} // namespace

// The text shown of EcuLine is "<tab>BAP << ECU Beispiel(0x15) Sample(0x14)<tab>end":
// "Beispiel(0x15)" is columns 19-32, "Sample(0x14)" 34-45, the tab after it
// 46-47 and "end" 48-50. That of TabbedLine is "v=AB(a<tab>b) end": the Named
// Value is columns 2-9 and "end" 11-13. That of IdLine is "x id=seven(7)".
TEST_CASE( "A raw display column is a display column of the text shown",
           "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();

    const auto row = GENERATE( values<ShownColumn>( {
        { "on the tab before a Named Value", EcuLine, 0, 0, 0 },
        { "inside the tab before a Named Value", EcuLine, 3, 3, 3 },
        { "beside a Named Value, before it", EcuLine, 18, 18, 18 },
        { "on the first column of a Named Value", EcuLine, 19, 19, 32 },
        { "inside a Named Value", EcuLine, 20, 19, 32 },
        { "on the last column of a Named Value", EcuLine, 22, 19, 32 },
        { "beside a Named Value, between two", EcuLine, 23, 33, 33 },
        { "on the last column of a Named Value before a tab", EcuLine, 27, 34, 45 },
        { "on the first column of the narrower tab after a Named Value", EcuLine, 28, 46, 46 },
        { "inside the narrower tab after a Named Value", EcuLine, 29, 47, 47 },
        { "past the width the tab after a Named Value is shown with", EcuLine, 31, 47, 47 },
        { "after the tab after a Named Value", EcuLine, 32, 48, 48 },
        { "on the last column of the Log Line", EcuLine, 34, 50, 50 },
        { "just past the end of the Log Line", EcuLine, 35, 51, 51 },
        { "far past the end of the Log Line", EcuLine, 40, 56, 56 },
        { "before a Named Value with a tab", TabbedLine, 0, 0, 0 },
        { "inside the tab of a Named Value's raw text", TabbedLine, 4, 2, 9 },
        { "after the tab of a Named Value's raw text", TabbedLine, 8, 2, 9 },
        { "after a Named Value with a tab", TabbedLine, 10, 11, 11 },
        { "on a Named Value at the end of the Log Line", IdLine, 5, 5, 12 },
        { "just past a Named Value at the end", IdLine, 6, 13, 13 },
        { "further past a Named Value at the end", IdLine, 9, 16, 16 },
    } ) );

    CAPTURE( row.what, row.line, row.rawColumn );
    const ShownColumns columns{ &namer, [ & ] { return row.line; } };
    using logsquirl::valuenames::Snap;

    CHECK( columns.shownColumn( LineColumn( row.rawColumn ), Snap::ToStart )
           == LineColumn( row.toStart ) );
    CHECK( columns.shownColumn( LineColumn( row.rawColumn ), Snap::ToEnd )
           == LineColumn( row.toEnd ) );
}

TEST_CASE( "A raw display column on a Log Line without Named Values or without a namer is the "
           "column shown",
           "[showncolumns][valuenames]" )
{
    using logsquirl::valuenames::Snap;
    const auto namer = exampleNamer();
    const auto column = GENERATE( 0, 3, 9, 20, 60 );
    const auto snap = GENERATE( Snap::ToStart, Snap::ToEnd );
    CAPTURE( column );

    SECTION( "a Log Line without Named Values" )
    {
        const ShownColumns columns{ &namer,
                                    [] { return QStringLiteral( "\tnothing named\there" ); } };
        CHECK( columns.shownColumn( LineColumn( column ), snap ) == LineColumn( column ) );
    }

    SECTION( "without a namer" )
    {
        int reads = 0;
        const ShownColumns columns{ nullptr, [ &reads ] {
                                       ++reads;
                                       return EcuLine;
                                   } };
        CHECK( columns.shownColumn( LineColumn( column ), snap ) == LineColumn( column ) );
        CHECK( ShownColumns{}.shownColumn( LineColumn( column ), snap ) == LineColumn( column ) );
        CHECK( reads == 0 );
    }
}

namespace {

struct RawColumn {
    const char* what;
    QString line;
    int shownColumn;
    int toStart;
    int toEnd;
};

// "x id=7<tab>z": the Named Value "7" is column 5 and the tab 6-7. Shown as
// "x id=seven(7)<tab>z", the value is columns 5-12 and the tab 13-15: wider
// than the raw one.
const QString IdTabLine = QStringLiteral( "x id=7\tz" );

} // namespace

// What a click on the text shown lands on (#743), the reverse of the table
// above: EcuLine is shown with its second tab two columns wide instead of four,
// IdTabLine with its tab three wide instead of two.
TEST_CASE( "A display column of the text shown is a raw display column",
           "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();

    const auto row = GENERATE( values<RawColumn>( {
        { "on the tab before a Named Value", EcuLine, 0, 0, 0 },
        { "inside the tab before a Named Value", EcuLine, 3, 3, 3 },
        { "beside a Named Value, before it", EcuLine, 18, 18, 18 },
        { "on the first column of a Named Value", EcuLine, 19, 19, 22 },
        { "inside a Named Value", EcuLine, 25, 19, 22 },
        { "on the last column of a Named Value", EcuLine, 32, 19, 22 },
        { "beside a Named Value, between two", EcuLine, 33, 23, 23 },
        { "on the last column of a Named Value before a tab", EcuLine, 45, 24, 27 },
        { "on the first column of the narrower tab after a Named Value", EcuLine, 46, 28, 28 },
        { "inside the narrower tab after a Named Value", EcuLine, 47, 29, 29 },
        { "after the tab after a Named Value", EcuLine, 48, 32, 32 },
        { "on the last column of the Log Line", EcuLine, 50, 34, 34 },
        { "just past the end of the Log Line", EcuLine, 51, 35, 35 },
        { "far past the end of the Log Line", EcuLine, 56, 40, 40 },
        { "on the first column of the wider tab after a Named Value", IdTabLine, 13, 6, 6 },
        { "inside the wider tab after a Named Value", IdTabLine, 14, 7, 7 },
        { "past the width the tab after a Named Value has raw", IdTabLine, 15, 7, 7 },
        { "after the wider tab", IdTabLine, 16, 8, 8 },
        { "just past the end of a Log Line ending after a tab", IdTabLine, 17, 9, 9 },
        { "before a Named Value with a tab", TabbedLine, 1, 1, 1 },
        { "on the tab of a Named Value's text shown", TabbedLine, 7, 2, 8 },
        { "after a Named Value with a tab", TabbedLine, 10, 9, 9 },
        { "on a Named Value at the end of the Log Line", IdLine, 9, 5, 5 },
        { "just past a Named Value at the end", IdLine, 13, 6, 6 },
        { "further past a Named Value at the end", IdLine, 16, 9, 9 },
    } ) );

    CAPTURE( row.what, row.line, row.shownColumn );
    const ShownColumns columns{ &namer, [ & ] { return row.line; } };
    using logsquirl::valuenames::Snap;

    CHECK( columns.rawColumn( LineColumn( row.shownColumn ), Snap::ToStart )
           == LineColumn( row.toStart ) );
    CHECK( columns.rawColumn( LineColumn( row.shownColumn ), Snap::ToEnd )
           == LineColumn( row.toEnd ) );
}

namespace {

struct RoundTrip {
    const char* what;
    QString line;
    std::vector<int> columns;
};

} // namespace

// A column outside every Named Value comes back where it was, unless the
// character it is in -- a tab -- is narrower on the other side.
TEST_CASE( "A display column outside every Named Value goes there and back unchanged",
           "[showncolumns][valuenames]" )
{
    using logsquirl::valuenames::Snap;
    const auto namer = exampleNamer();
    const auto snap = GENERATE( Snap::ToStart, Snap::ToEnd );

    SECTION( "raw to shown and back" )
    {
        const auto row = GENERATE( values<RoundTrip>( {
            { "before and between Named Values", EcuLine, { 0, 3, 7, 8, 18, 23 } },
            { "in the part of the narrower tab shown, and after", EcuLine, { 28, 29, 32, 34 } },
            { "past the end", EcuLine, { 35, 40 } },
            { "in the wider tab, and after", IdTabLine, { 6, 7, 8, 9, 12 } },
            { "around a Named Value with a tab", TabbedLine, { 0, 1, 9, 10, 12, 13 } },
        } ) );
        const ShownColumns columns{ &namer, [ & ] { return row.line; } };
        for ( const auto column : row.columns ) {
            CAPTURE( row.what, column );
            CHECK( columns.rawColumn( columns.shownColumn( LineColumn( column ), snap ), snap )
                   == LineColumn( column ) );
        }
    }

    SECTION( "shown to raw and back" )
    {
        const auto row = GENERATE( values<RoundTrip>( {
            { "before and between Named Values", EcuLine, { 0, 3, 7, 8, 18, 33 } },
            { "in the narrower tab, and after", EcuLine, { 46, 47, 48, 50, 51, 56 } },
            { "in the part of the wider tab raw, and after", IdTabLine, { 13, 14, 16, 17 } },
            { "around a Named Value with a tab", TabbedLine, { 0, 1, 10, 11, 14 } },
        } ) );
        const ShownColumns columns{ &namer, [ & ] { return row.line; } };
        for ( const auto column : row.columns ) {
            CAPTURE( row.what, column );
            CHECK( columns.shownColumn( columns.rawColumn( LineColumn( column ), snap ), snap )
                   == LineColumn( column ) );
        }
    }
}

TEST_CASE( "A display column shown on a Log Line without Named Values or without a namer is the "
           "raw column",
           "[showncolumns][valuenames]" )
{
    using logsquirl::valuenames::Snap;
    const auto namer = exampleNamer();
    const auto column = GENERATE( 0, 3, 9, 20, 60 );
    const auto snap = GENERATE( Snap::ToStart, Snap::ToEnd );
    CAPTURE( column );

    SECTION( "a Log Line without Named Values" )
    {
        const ShownColumns columns{ &namer,
                                    [] { return QStringLiteral( "\tnothing named\there" ); } };
        CHECK( columns.rawColumn( LineColumn( column ), snap ) == LineColumn( column ) );
    }

    SECTION( "without a namer" )
    {
        int reads = 0;
        const ShownColumns columns{ nullptr, [ &reads ] {
                                       ++reads;
                                       return EcuLine;
                                   } };
        CHECK( columns.rawColumn( LineColumn( column ), snap ) == LineColumn( column ) );
        CHECK( ShownColumns{}.rawColumn( LineColumn( column ), snap ) == LineColumn( column ) );
        CHECK( reads == 0 );
    }
}

namespace {

struct ValueShownAt {
    const char* what;
    QString line;
    int shownColumn;
    // The raw value and its name; none for none.
    const char* value;
    const char* name;
};

} // namespace

// What the tooltip over the text shown tells of (#743).
TEST_CASE( "The Named Value at a display column of the text shown", "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();

    const auto row = GENERATE( values<ValueShownAt>( {
        { "on the first column of a Named Value", EcuLine, 19, "0x15", "Beispiel" },
        { "on the last column of a Named Value", EcuLine, 32, "0x15", "Beispiel" },
        { "on a Named Value after another", EcuLine, 40, "0x14", "Sample" },
        { "on the tab of a Named Value's text shown", TabbedLine, 7, "a\tb", "AB" },
        { "on a Named Value at the end of the Log Line", IdLine, 12, "7", "seven" },
        { "inside the tab before a Named Value", EcuLine, 3, nullptr, nullptr },
        { "beside a Named Value, before it", EcuLine, 18, nullptr, nullptr },
        { "beside a Named Value, between two", EcuLine, 33, nullptr, nullptr },
        { "inside the tab after a Named Value", EcuLine, 47, nullptr, nullptr },
        { "inside the wider tab after a Named Value", IdTabLine, 15, nullptr, nullptr },
        { "just past a Named Value at the end", IdLine, 13, nullptr, nullptr },
        { "far past the end of the Log Line", EcuLine, 60, nullptr, nullptr },
    } ) );

    CAPTURE( row.what, row.line, row.shownColumn );
    const ShownColumns columns{ &namer, [ & ] { return row.line; } };
    const auto value = columns.namedValueShownAt( LineColumn( row.shownColumn ) );

    if ( row.value == nullptr ) {
        CHECK_FALSE( value.has_value() );
    }
    else {
        REQUIRE( value.has_value() );
        CHECK( value->value == QString::fromUtf8( row.value ) );
        CHECK( value->name == QString::fromUtf8( row.name ) );
    }
}

TEST_CASE( "No Named Value is shown on a Log Line without one or without a namer",
           "[showncolumns][valuenames]" )
{
    const auto namer = exampleNamer();
    const auto column = GENERATE( 0, 3, 9, 20, 60 );
    CAPTURE( column );

    SECTION( "a Log Line without Named Values" )
    {
        const ShownColumns columns{ &namer,
                                    [] { return QStringLiteral( "\tnothing named\there" ); } };
        CHECK_FALSE( columns.namedValueShownAt( LineColumn( column ) ) );
    }

    SECTION( "without a namer" )
    {
        int reads = 0;
        const ShownColumns columns{ nullptr, [ &reads ] {
                                       ++reads;
                                       return EcuLine;
                                   } };
        CHECK_FALSE( columns.namedValueShownAt( LineColumn( column ) ) );
        CHECK_FALSE( ShownColumns{}.namedValueShownAt( LineColumn( column ) ) );
        CHECK( reads == 0 );
    }
}
