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

// The rule engine of Value Names (#647): which values of a Log Line the
// Naming Rules name, what is shown in their place, and the warnings the edit
// dialog shows.

#include "naminggroup.h"
#include "valuenamer.h"

#include <QSettings>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace logsquirl::valuenames;

namespace {

NameTable table( const QString& name, const QList<NameRow>& rows, bool caseSensitive = false )
{
    return NameTable{ name, rows, caseSensitive };
}

NamingRule rule( const QString& name, const QString& pattern, const QList<GroupTable>& groupTables,
                 const QString& displayTemplate = NamingRule::defaultTemplate() )
{
    NamingRule namingRule;
    namingRule.name = name;
    namingRule.pattern = pattern;
    namingRule.groupTables = groupTables;
    namingRule.displayTemplate = displayTemplate;
    return namingRule;
}

NamingGroup group( const QString& name, const QList<NamingRule>& rules,
                   const QList<NameTable>& tables )
{
    auto namingGroup = NamingGroup::createNewGroup( name );
    namingGroup.setRules( rules );
    namingGroup.setTables( tables );
    return namingGroup;
}

// The ticket's example: group 1 is the ECU, group 2 the function.
NamingGroup bapGroup()
{
    return group( "BAP",
                  { rule( "BAP ECU", "BAP << ECU (0x[0-9A-F]{2}) (0x[0-9A-F]{2})",
                          { { "1", "ECU" }, { "2", "Function" } } ) },
                  { table( "ECU", { { "0x0*15", "Beispiel" } } ),
                    table( "Function", { { "0x14", "Sample" } } ) } );
}

QString shown( const ValueNamer& namer, const QString& line )
{
    return shownLine( line, namer.namedValues( line ) );
}

} // namespace

TEST_CASE( "A Value Namer without rules names nothing", "[valuenames]" )
{
    CHECK( ValueNamer{}.isEmpty() );
    CHECK( ValueNamer{ QList<NamingGroup>{} }.isEmpty() );
    CHECK( ValueNamer{}.namedValues( "BAP << ECU 0x15 0x14" ).isEmpty() );

    SECTION( "nor with its rules or groups unchecked" )
    {
        auto bap = bapGroup();
        bap.setEnabled( false );
        CHECK( ValueNamer{ { bap } }.isEmpty() );

        auto rules = bapGroup().rules();
        rules[ 0 ].enabled = false;
        auto uncheckedRule = bapGroup();
        uncheckedRule.setRules( rules );
        CHECK( ValueNamer{ { uncheckedRule } }.isEmpty() );
    }

    SECTION( "nor with a rule whose regex does not compile" )
    {
        CHECK( ValueNamer{ { group( "G", { rule( "R", "(0x", { { "1", "T" } } ) },
                                    { table( "T", { { "0x1", "one" } } ) } ) } }
                   .isEmpty() );
    }

    SECTION( "nor with a rule that gives no group a table" )
    {
        CHECK( ValueNamer{ { group( "G", { rule( "R", "(0x..)", {} ) }, {} ) } }.isEmpty() );
    }

    CHECK_FALSE( ValueNamer{ { bapGroup() } }.isEmpty() );
}

TEST_CASE( "A Naming Rule names only the values in its context", "[valuenames]" )
{
    const ValueNamer namer{ { bapGroup() } };

    CHECK( shown( namer, "BAP << ECU 0x15 0x14 sonstiges" )
           == "BAP << ECU Beispiel(0x15) Sample(0x14) sonstiges" );
    CHECK( shown( namer, "regenbogen 0x15" ) == "regenbogen 0x15" );
    CHECK( namer.namedValues( "regenbogen 0x15" ).isEmpty() );

    SECTION( "the same value outside the rule's match stays unchanged" )
    {
        CHECK( shown( namer, "0x15 BAP << ECU 0x15 0x14 0x15" )
               == "0x15 BAP << ECU Beispiel(0x15) Sample(0x14) 0x15" );
    }

    SECTION( "the Named Values give the raw range and what the tooltip tells" )
    {
        const auto values = namer.namedValues( "BAP << ECU 0x15 0x14 sonstiges" );
        REQUIRE( values.size() == 2 );
        CHECK(
            values[ 0 ]
            == NamedValue{ 11, 4, "Beispiel(0x15)", "0x15", "Beispiel", "ECU", "BAP ECU", "BAP" } );
        CHECK( values[ 1 ]
               == NamedValue{ 16, 4, "Sample(0x14)", "0x14", "Sample", "Function", "BAP ECU",
                              "BAP" } );
    }
}

TEST_CASE( "A Naming Rule names every match in a Log Line", "[valuenames]" )
{
    const ValueNamer namer{ { group( "G", { rule( "R", "id=(\\d+)", { { "1", "Ids" } } ) },
                                     { table( "Ids", { { "1", "one" }, { "2", "two" } } ) } ) } };

    CHECK( shown( namer, "id=1 id=2 id=3 id=1" ) == "id=one(1) id=two(2) id=3 id=one(1)" );
}

TEST_CASE( "Named, unnamed and non-capturing groups", "[valuenames]" )
{
    const auto tables
        = QList<NameTable>{ table( "A", { { "a", "Alpha" } } ), table( "B", { { "b", "Beta" } } ) };

    SECTION( "a named group is given its table by name" )
    {
        const ValueNamer namer{ { group(
            "G", { rule( "R", "(?<first>\\w) (?<second>\\w)", { { "second", "B" } } ) },
            tables ) } };
        CHECK( shown( namer, "b b" ) == "b Beta(b)" );
    }

    SECTION( "a named group can also be given its table by number" )
    {
        const ValueNamer namer{ { group(
            "G", { rule( "R", "(?<first>\\w) (?<second>\\w)", { { "1", "A" } } ) }, tables ) } };
        CHECK( shown( namer, "a a" ) == "Alpha(a) a" );
    }

    SECTION( "a non-capturing group does not count" )
    {
        const ValueNamer namer{ { group(
            "G", { rule( "R", "(?:x|y)=(\\w) (\\w)", { { "1", "A" }, { "2", "B" } } ) },
            tables ) } };
        CHECK( shown( namer, "x=a b" ) == "x=Alpha(a) Beta(b)" );
    }
}

TEST_CASE( "A Naming Rule without groups looks up the whole match", "[valuenames]" )
{
    const ValueNamer namer{ { group( "G", { rule( "R", "0x[0-9A-F]{2}", { { "0", "T" } } ) },
                                     { table( "T", { { "0x15", "Beispiel" } } ) } ) } };

    CHECK( shown( namer, "a 0x15 b 0x16" ) == "a Beispiel(0x15) b 0x16" );
}

TEST_CASE( "A capture group without a Name Table stays unchanged", "[valuenames]" )
{
    const ValueNamer namer{ { group( "G", { rule( "R", "(\\w+):(\\w+)", { { "2", "T" } } ) },
                                     { table( "T", { { "x", "Ex" } } ) } ) } };

    CHECK( shown( namer, "x:x" ) == "x:Ex(x)" );

    SECTION( "as does one whose table the group does not have" )
    {
        const ValueNamer unknown{ { group(
            "G", { rule( "R", "(\\w+):(\\w+)", { { "1", "Missing" }, { "2", "T" } } ) },
            { table( "T", { { "x", "Ex" } } ) } ) } };
        CHECK( shown( unknown, "x:x" ) == "x:Ex(x)" );
    }
}

TEST_CASE( "A key matches the whole captured value, the first row wins", "[valuenames]" )
{
    const auto namerWith = []( const NameTable& nameTable ) {
        return ValueNamer{ { group( "G", { rule( "R", "v=(\\w+)", { { "1", "T" } } ) },
                                    { nameTable } ) } };
    };

    SECTION( "a key is anchored to the whole value" )
    {
        const auto namer = namerWith( table( "T", { { "0x1", "one" } } ) );
        CHECK( shown( namer, "v=0x15" ) == "v=0x15" );
        CHECK( shown( namer, "v=x0x1" ) == "v=x0x1" );
        CHECK( shown( namer, "v=0x1" ) == "v=one(0x1)" );
    }

    SECTION( "a key regex with alternatives is anchored as a whole" )
    {
        const auto namer = namerWith( table( "T", { { "a|b", "ab" } } ) );
        CHECK( shown( namer, "v=ax" ) == "v=ax" );
        CHECK( shown( namer, "v=b" ) == "v=ab(b)" );
    }

    SECTION( "the first matching row wins" )
    {
        const auto namer = namerWith( table( "T", { { "0x1.", "any" }, { "0x15", "exact" } } ) );
        CHECK( shown( namer, "v=0x15" ) == "v=any(0x15)" );
    }

    SECTION( "the first of two equal literal keys wins" )
    {
        const auto namer = namerWith( table( "T", { { "k", "first" }, { "k", "second" } } ) );
        CHECK( shown( namer, "v=k" ) == "v=first(k)" );
    }

    // A table of literal keys only is looked up by hash, one with a regex
    // key by its rows: both behave the same.
    SECTION( "keys ignore case by default" )
    {
        CHECK( shown( namerWith( table( "T", { { "0xab", "literal" } } ) ), "v=0xAB" )
               == "v=literal(0xAB)" );
        CHECK( shown( namerWith( table( "T", { { "0xc[d]", "regex" } } ) ), "v=0XCD" )
               == "v=regex(0XCD)" );
    }

    SECTION( "a case-sensitive table matches case" )
    {
        const auto literal
            = namerWith( table( "T", { { "0xab", "literal" } }, /*caseSensitive*/ true ) );
        CHECK( shown( literal, "v=0xAB" ) == "v=0xAB" );
        CHECK( shown( literal, "v=0xab" ) == "v=literal(0xab)" );

        const auto regex
            = namerWith( table( "T", { { "0xc[d]", "regex" } }, /*caseSensitive*/ true ) );
        CHECK( shown( regex, "v=0XCD" ) == "v=0XCD" );
        CHECK( shown( regex, "v=0xcd" ) == "v=regex(0xcd)" );
    }

    SECTION( "a literal key with a character a regex would read otherwise" )
    {
        const auto namer = namerWith( table( "T", { { "a.c", "dot" }, { "a-c", "dash" } } ) );
        CHECK( shown( namer, "v=abc" ) == "v=dot(abc)" );
        CHECK( shown( namer, "v=a_c" ) == "v=dot(a_c)" );
    }

    SECTION( "a key that does not compile is left out" )
    {
        const auto namer = namerWith( table( "T", { { "(", "broken" }, { "\\w+", "word" } } ) );
        CHECK( shown( namer, "v=x" ) == "v=word(x)" );
    }
}

TEST_CASE( "A name uses the groups of its key, a template places name and value", "[valuenames]" )
{
    SECTION( "{1} in a name is the key's first group" )
    {
        const ValueNamer namer{ { group( "G", { rule( "R", "door (0x\\w+)", { { "1", "T" } } ) },
                                         { table( "T", { { "0x2([0-9A-F])", "Tür{1}" } } ) } ) } };
        CHECK( shown( namer, "door 0x23" ) == "door Tür3(0x23)" );
    }

    SECTION( "a missing group stays literal" )
    {
        CHECK( nameWithKeyGroups( "a{1}b{2}c{0}{x}", { "whole", "one" } ) == "aoneb{2}c{0}{x}" );
        CHECK( nameWithKeyGroups( "{12}", { "w" } ) == "{12}" );
        CHECK( nameWithKeyGroups( "{1}", { "w", QString() } ) == "" );
    }

    SECTION( "the template places {name} and {value}" )
    {
        const ValueNamer namer{ { group(
            "G", { rule( "R", "v=(\\w+)", { { "1", "T" } }, "{value} is {name}, {name}!" ) },
            { table( "T", { { "x", "Ex" } } ) } ) } };
        CHECK( shown( namer, "v=x" ) == "v=x is Ex, Ex!" );
    }

    SECTION( "a name holding a placeholder is not filled in again" )
    {
        CHECK( fillTemplate( "{name}({value})", "{value}", "0x1" ) == "{value}(0x1)" );
        CHECK( fillTemplate( "{nam}{name", "N", "V" ) == "{nam}{name" );
    }
}

TEST_CASE( "An unknown value stays unchanged", "[valuenames]" )
{
    const ValueNamer namer{ { bapGroup() } };

    CHECK( shown( namer, "BAP << ECU 0x99 0x98" ) == "BAP << ECU 0x99 0x98" );
    CHECK( namer.namedValues( "BAP << ECU 0x99 0x98" ).isEmpty() );
    CHECK( shown( namer, "BAP << ECU 0x99 0x14" ) == "BAP << ECU 0x99 Sample(0x14)" );
}

TEST_CASE( "On overlap the earlier rule wins, across rules and groups", "[valuenames]" )
{
    const auto numbers = table( "N", { { "\\d+", "num" } } );
    const auto words = table( "W", { { "\\w+", "word" } } );

    SECTION( "within a group, the earlier rule" )
    {
        const ValueNamer namer{ { group( "G",
                                         { rule( "Numbers", "(\\d+)", { { "1", "N" } } ),
                                           rule( "Words", "(\\w+)", { { "1", "W" } } ) },
                                         { numbers, words } ) } };
        CHECK( shown( namer, "12 ab" ) == "num(12) word(ab)" );
    }

    SECTION( "a later rule skips a range that overlaps a taken one, even in part" )
    {
        const ValueNamer namer{ { group( "G",
                                         { rule( "Short", "a(bc)", { { "1", "W" } } ),
                                           rule( "Long", "(cd)", { { "1", "W" } } ) },
                                         { words } ) } };
        CHECK( shown( namer, "abcd" ) == "aword(bc)d" );
    }

    SECTION( "across groups, the earlier group" )
    {
        const auto first
            = group( "First", { rule( "Words", "(\\w+)", { { "1", "W" } } ) }, { words } );
        const auto second
            = group( "Second", { rule( "Numbers", "(\\d+)", { { "1", "N" } } ) }, { numbers } );

        CHECK( shown( ValueNamer{ { first, second } }, "12" ) == "word(12)" );
        CHECK( shown( ValueNamer{ { second, first } }, "12" ) == "num(12)" );
        CHECK( ValueNamer{ { second, first } }.namedValues( "12" ).front().group == "Second" );
    }

    SECTION( "an earlier rule that finds no name leaves the range to a later one" )
    {
        const ValueNamer namer{ { group( "G",
                                         { rule( "Numbers", "(\\w+)", { { "1", "N" } } ),
                                           rule( "Words", "(\\w+)", { { "1", "W" } } ) },
                                         { numbers, words } ) } };
        CHECK( shown( namer, "12 ab" ) == "num(12) word(ab)" );
    }

    SECTION( "the Named Values are ordered by start" )
    {
        const ValueNamer namer{ { group( "G",
                                         { rule( "Words", "([a-z]+)", { { "1", "W" } } ),
                                           rule( "Numbers", "(\\d+)", { { "1", "N" } } ) },
                                         { numbers, words } ) } };
        const auto values = namer.namedValues( "1 a 2 b" );
        REQUIRE( values.size() == 4 );
        CHECK( values[ 0 ].start == 0 );
        CHECK( values[ 1 ].start == 2 );
        CHECK( values[ 2 ].start == 4 );
        CHECK( values[ 3 ].start == 6 );
    }
}

TEST_CASE( "Every Naming Rule sees the raw text only", "[valuenames]" )
{
    // The first rule's output would give the second something to match.
    const ValueNamer namer{ { group(
        "G",
        { rule( "Codes", "code=(\\d+)", { { "1", "Codes" } } ),
          rule( "Words", "(ERROR)", { { "1", "Words" } } ) },
        { table( "Codes", { { "1", "ERROR" } } ), table( "Words", { { "error", "bad" } } ) } ) } };

    CHECK( shown( namer, "code=1" ) == "code=ERROR(1)" );
    CHECK( shown( namer, "code=1 ERROR" ) == "code=ERROR(1) bad(ERROR)" );
}

TEST_CASE( "A Log Line too long to match gets no names", "[valuenames]" )
{
    const ValueNamer namer{ { bapGroup() } };
    auto line = QString( "BAP << ECU 0x15 0x14 " );
    line += QString( ValueNamer::MaxLineLength, QLatin1Char( 'x' ) );

    CHECK( namer.namedValues( line ).isEmpty() );
}

TEST_CASE( "The edit dialog's warnings", "[valuenames]" )
{
    SECTION( "a group without problems has none" )
    {
        CHECK( validate( bapGroup() ).isEmpty() );
    }

    SECTION( "an invalid rule regex" )
    {
        const auto problems = validate( group( "G", { rule( "R", "(0x", {} ) }, {} ) );
        REQUIRE( problems.size() == 1 );
        CHECK( problems[ 0 ].kind == Problem::Kind::InvalidRuleRegex );
        CHECK( problems[ 0 ].rule == "R" );
        CHECK_FALSE( problems[ 0 ].detail.isEmpty() );
    }

    SECTION( "an invalid key regex, with its row" )
    {
        const auto problems
            = validate( group( "G", {}, { table( "T", { { "ok", "a" }, { "[", "b" } } ) } ) );
        REQUIRE( problems.size() == 1 );
        CHECK( problems[ 0 ].kind == Problem::Kind::InvalidKeyRegex );
        CHECK( problems[ 0 ].table == "T" );
        CHECK( problems[ 0 ].row == 1 );
    }

    SECTION( "a duplicate key, with its row and the first one's" )
    {
        const auto problems = validate(
            group( "G", {}, { table( "T", { { "a", "1" }, { "b", "2" }, { "A", "3" } } ) } ) );
        REQUIRE( problems.size() == 1 );
        CHECK( problems[ 0 ] == Problem{ Problem::Kind::DuplicateKey, {}, "T", 2, 0, "A" } );

        CHECK( validate( group( "G", {},
                                { table( "T", { { "a", "1" }, { "A", "3" } },
                                         /*caseSensitive*/ true ) } ) )
                   .isEmpty() );
    }

    SECTION( "{n} without a matching group in the key" )
    {
        const auto problems = validate(
            group( "G", {}, { table( "T", { { "0x2([0-9])", "Tür{1}{2}" }, { "x", "{1}" } } ) } ) );
        REQUIRE( problems.size() == 2 );
        CHECK( problems[ 0 ] == Problem{ Problem::Kind::MissingKeyGroup, {}, "T", 0, -1, "{2}" } );
        CHECK( problems[ 1 ] == Problem{ Problem::Kind::MissingKeyGroup, {}, "T", 1, -1, "{1}" } );
    }

    SECTION( "a table the group does not have, a capture group the regex does not have" )
    {
        const auto problems
            = validate( group( "G",
                               { rule( "R", "(?<ecu>\\w+) (\\w+)",
                                       { { "ecu", "Nope" }, { "3", "T" }, { "x", "T" } } ) },
                               { table( "T", {} ) } ) );
        REQUIRE( problems.size() == 3 );
        CHECK( problems[ 0 ] == Problem{ Problem::Kind::UnknownTable, "R", {}, -1, -1, "Nope" } );
        CHECK( problems[ 1 ]
               == Problem{ Problem::Kind::UnknownCaptureGroup, "R", {}, -1, -1, "3" } );
        CHECK( problems[ 2 ]
               == Problem{ Problem::Kind::UnknownCaptureGroup, "R", {}, -1, -1, "x" } );
    }

    SECTION( "group 0 is the whole match of a rule without groups" )
    {
        CHECK( validate(
                   group( "G", { rule( "R", "\\w+", { { "0", "T" } } ) }, { table( "T", {} ) } ) )
                   .isEmpty() );
    }
}

TEST_CASE( "A Naming Group round-trips through the settings", "[valuenames]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto file = dir.filePath( "group.conf" );

    auto original = bapGroup();
    auto rules = original.rules();
    rules.append( rule( "Whole", "0x[0-9A-F]+", { { "0", "ECU" } }, "{name}" ) );
    original.setRules( rules );
    auto tables = original.tables();
    tables.append( table( "Empty", {}, /*caseSensitive*/ true ) );
    tables.append( table( "Quoted", { { "a,\"b\"", "x=y; z" }, { "", "" } } ) );
    original.setTables( tables );

    {
        QSettings settings( file, QSettings::IniFormat );
        original.saveToStorage( settings );
    }

    QSettings settings( file, QSettings::IniFormat );
    NamingGroup read;
    read.retrieveFromStorage( settings );

    CHECK( read == original );
    CHECK( read.id() == original.id() );
    CHECK_FALSE( read.id().isEmpty() );

    SECTION( "the checks are not part of it" )
    {
        auto unchecked = original;
        unchecked.setEnabled( false );
        auto uncheckedRules = unchecked.rules();
        uncheckedRules[ 0 ].enabled = false;
        unchecked.setRules( uncheckedRules );

        const auto other = dir.filePath( "unchecked.conf" );
        {
            QSettings out( other, QSettings::IniFormat );
            unchecked.saveToStorage( out );
        }
        QSettings in( other, QSettings::IniFormat );
        NamingGroup readBack;
        readBack.retrieveFromStorage( in );
        CHECK( readBack.isEnabled() );
        CHECK( readBack.rules()[ 0 ].enabled );
    }

    SECTION( "a file without a Naming Group gives an empty one" )
    {
        QSettings empty( dir.filePath( "empty.conf" ), QSettings::IniFormat );
        NamingGroup none = original;
        none.retrieveFromStorage( empty );
        CHECK( none.rules().isEmpty() );
        CHECK( none.tables().isEmpty() );
    }
}

TEST_CASE( "A key group that took no part in the match gives an empty text", "[valuenames]" )
{
    const ValueNamer namer{ { group( "G", { rule( "R", "v=(\\w+)", { { "1", "T" } } ) },
                                     { table( "T", { { "(a)|(b)", "A{1}B{2}" } } ) } ) } };

    CHECK( shown( namer, "v=a" ) == "v=AaB(a)" );
    CHECK( shown( namer, "v=b" ) == "v=ABb(b)" );
}

TEST_CASE( "Rule and key regexes read Unicode", "[valuenames]" )
{
    const ValueNamer namer{ { group(
        "G", { rule( "R", "door=(\\w+)", { { "1", "T" } } ) },
        { table( "T", { { "t\\wr", "door" }, { "\\w+", "word" } } ) } ) } };

    CHECK( shown( namer, "door=Tür" ) == "door=door(Tür)" );
    CHECK( shown( namer, "door=Größe" ) == "door=word(Größe)" );
}

TEST_CASE( "Line breaks and control characters never reach the shown text", "[valuenames]" )
{
    const ValueNamer namer{ { group(
        "G", { rule( "R", "v=(\\w+)", { { "1", "T" } }, "{name}\t({value})\n" ) },
        { table( "T", { { "x", "Line\nBreak\r\x01" }, { "y\\w*", "Para\u2029graph" } } ) } ) } };

    CHECK( shown( namer, "v=x" ) == "v=Line Break   (x) " );
    CHECK( shown( namer, "v=y" ) == "v=Para graph (y) " );
    CHECK( namer.namedValues( "v=x" ).front().name == "Line Break  " );
}

TEST_CASE( "No name is taken from a table without a name", "[valuenames]" )
{
    const ValueNamer namer{ { group( "G", { rule( "R", "v=(\\w+)", { { "1", "" } } ) },
                                     { table( "", { { "x", "Ex" } } ) } ) } };

    CHECK( namer.isEmpty() );
    CHECK( shown( namer, "v=x" ) == "v=x" );
}

TEST_CASE( "A Value Namer whose tables name nothing is empty", "[valuenames]" )
{
    CHECK( ValueNamer{
        { group( "G", { rule( "R", "v=(\\w+)", { { "1", "T" } } ) }, { table( "T", {} ) } ) } }
               .isEmpty() );
    CHECK( ValueNamer{ { group( "G", { rule( "R", "v=(\\w+)", { { "1", "T" } } ) },
                                { table( "T", { { "(", "broken" }, { "[", "too" } } ) } ) } }
               .isEmpty() );
}

TEST_CASE( "More of the edit dialog's warnings", "[valuenames]" )
{
    SECTION( "regex keys are duplicates only when written the same" )
    {
        CHECK( validate(
                   group( "G", {}, { table( "T", { { "\\d", "digit" }, { "\\D", "other" } } ) } ) )
                   .isEmpty() );
        const auto problems
            = validate( group( "G", {}, { table( "T", { { "\\d", "a" }, { "\\d", "b" } } ) } ) );
        REQUIRE( problems.size() == 1 );
        CHECK( problems[ 0 ].kind == Problem::Kind::DuplicateKey );
    }

    SECTION( "a capture group given two tables" )
    {
        const auto problems = validate(
            group( "G", { rule( "R", "(?<ecu>\\w+)", { { "1", "T" }, { "ecu", "T" } } ) },
                   { table( "T", {} ) } ) );
        REQUIRE( problems.size() == 1 );
        CHECK( problems[ 0 ]
               == Problem{ Problem::Kind::DuplicateCaptureGroup, "R", {}, -1, -1, "ecu" } );
    }

    SECTION( "two rules of the same name" )
    {
        const auto problems = validate( group(
            "G", { rule( "R", "a", {} ), rule( "S", "b", {} ), rule( "R", "c", {} ) }, {} ) );
        REQUIRE( problems.size() == 1 );
        CHECK( problems[ 0 ] == Problem{ Problem::Kind::DuplicateRuleName, "R", {}, -1, -1, "R" } );
    }

    SECTION( "line breaks and control characters in a name or a template" )
    {
        const auto problems
            = validate( group( "G", { rule( "R", "a", {}, "{name}\n" ) },
                               { table( "T", { { "k", "ok" }, { "l", "two\nlines" } } ) } ) );
        REQUIRE( problems.size() == 2 );
        CHECK(
            problems[ 0 ]
            == Problem{ Problem::Kind::ControlCharacterInTemplate, "R", {}, -1, -1, "{name}\n" } );
        CHECK( problems[ 1 ]
               == Problem{ Problem::Kind::ControlCharacterInName, {}, "T", 1, -1, "two\nlines" } );
    }
}

TEST_CASE( "Naming Groups compare with and without their checks", "[valuenames]" )
{
    const auto original = bapGroup();
    auto unchecked = original;
    auto rules = unchecked.rules();
    rules[ 0 ].enabled = false;
    unchecked.setRules( rules );
    unchecked.setEnabled( false );

    CHECK_FALSE( unchecked == original );
    CHECK( unchecked.sameAs( original ) );
    CHECK( original.withId( "other" ).sameAs( original ) );

    auto renamed = original;
    renamed.setName( "Other" );
    CHECK_FALSE( renamed.sameAs( original ) );
}
