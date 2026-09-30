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

// A Name Table's rows read from CSV and written to it (#647), and rows
// pasted from a spreadsheet.

#include "nametablecsv.h"

#include <catch2/catch_test_macros.hpp>

using namespace logsquirl::valuenames;

namespace {

const QList<NameRow> ecuRows{ { "0x15", "Beispiel" }, { "0x14", "Sample" } };

} // namespace

TEST_CASE( "The separator of a CSV text is detected", "[valuenames][csv]" )
{
    CHECK( detectCsvSeparator( "0x15,Beispiel\n0x14,Sample\n" ) == QLatin1Char( ',' ) );
    CHECK( detectCsvSeparator( "0x15;Beispiel\n0x14;Sample\n" ) == QLatin1Char( ';' ) );
    CHECK( detectCsvSeparator( "0x15\tBeispiel\n0x14\tSample\n" ) == QLatin1Char( '\t' ) );

    SECTION( "outside quotes only" )
    {
        CHECK( detectCsvSeparator( "\"a,b,c\";x\n\"d,e\";y\n" ) == QLatin1Char( ';' ) );
    }

    SECTION( "the one that separates the most records" )
    {
        CHECK( detectCsvSeparator( "a;b\nc;d\ne,f;g\n" ) == QLatin1Char( ';' ) );
    }

    SECTION( "not from comment lines" )
    {
        CHECK( detectCsvSeparator( "# a;b;c;d\n# e;f\n0x15,x\n" ) == QLatin1Char( ',' ) );
    }

    SECTION( "a comma when nothing separates" )
    {
        CHECK( detectCsvSeparator( "single\ncolumn\n" ) == QLatin1Char( ',' ) );
        CHECK( detectCsvSeparator( "" ) == QLatin1Char( ',' ) );
    }
}

TEST_CASE( "The records of a CSV text", "[valuenames][csv]" )
{
    SECTION( "quoted fields as usual CSV" )
    {
        const auto records
            = csvRecords( "\"a,b\",\"say \"\"hi\"\"\",plain\n\"two\nlines\",x\r\nlast,y", ',' );
        REQUIRE( records.size() == 3 );
        CHECK( records[ 0 ] == CsvRecord{ 1, { "a,b", "say \"hi\"", "plain" } } );
        CHECK( records[ 1 ] == CsvRecord{ 2, { "two\nlines", "x" } } );
        CHECK( records[ 2 ] == CsvRecord{ 4, { "last", "y" } } );
    }

    SECTION( "blanks around a field are dropped, those inside quotes kept" )
    {
        const auto records = csvRecords( " 0x15 , Beispiel \n\" a \" ,b\n", ',' );
        REQUIRE( records.size() == 2 );
        CHECK( records[ 0 ].fields == QStringList{ "0x15", "Beispiel" } );
        CHECK( records[ 1 ].fields == QStringList{ " a ", "b" } );
    }

    SECTION( "empty fields are kept" )
    {
        const auto records = csvRecords( "a,,c,\n", ',' );
        REQUIRE( records.size() == 1 );
        CHECK( records[ 0 ].fields == QStringList{ "a", "", "c", "" } );
    }

    SECTION( "empty and comment lines are skipped" )
    {
        const auto records = csvRecords( "# comment\n\na,b\n#x,y\n\"#quoted\",z\n", ',' );
        REQUIRE( records.size() == 2 );
        CHECK( records[ 0 ] == CsvRecord{ 3, { "a", "b" } } );
        CHECK( records[ 1 ] == CsvRecord{ 5, { "#quoted", "z" } } );
    }
}

TEST_CASE( "A Name Table's rows are imported from CSV", "[valuenames][csv]" )
{
    SECTION( "with each separator" )
    {
        for ( const auto* text : { "0x15,Beispiel\n0x14,Sample\n", "0x15;Beispiel\n0x14;Sample\n",
                                   "0x15\tBeispiel\n0x14\tSample\n" } ) {
            const auto imported = importCsv( QString::fromUtf8( text ) );
            CHECK( imported.rows == ecuRows );
            CHECK( imported.warnings.isEmpty() );
        }
    }

    SECTION( "with a header row" )
    {
        CsvImportOptions options;
        options.hasHeader = true;
        const auto imported = importCsv( "key;name\n0x15;Beispiel\n0x14;Sample\n", options );
        CHECK( imported.rows == ecuRows );
        CHECK( imported.separator == QLatin1Char( ';' ) );

        SECTION( "after comment lines" )
        {
            CHECK( importCsv( "# ECUs\nkey;name\n0x15;Beispiel\n0x14;Sample\n", options ).rows
                   == ecuRows );
        }
    }

    SECTION( "without a header row, the first record is a row" )
    {
        CHECK( importCsv( "key,name\n0x15,Beispiel\n" ).rows
               == QList<NameRow>{ { "key", "name" }, { "0x15", "Beispiel" } } );
    }

    SECTION( "skipping comment lines" )
    {
        CHECK( importCsv( "# exported from the spec\n0x15,Beispiel\n# more\n0x14,Sample\n" ).rows
               == ecuRows );
    }

    SECTION( "from the columns chosen, of any number" )
    {
        CsvImportOptions options;
        options.keyColumn = 3;
        options.nameColumn = 1;
        const auto imported
            = importCsv( "id,Beispiel,comment,0x15,more\nid,Sample,\"a, b\",0x14,more\n", options );
        CHECK( imported.rows == ecuRows );
        CHECK( imported.warnings.isEmpty() );
    }

    SECTION( "a record without the chosen columns is skipped with a warning" )
    {
        const auto imported = importCsv( "0x15,Beispiel\nlonely\n,empty key\n0x14,Sample\n" );
        CHECK( imported.rows == ecuRows );
        CHECK(
            imported.warnings
            == QList<CsvImportWarning>{ { CsvImportWarning::Kind::MissingColumn, 2, "lonely", 0 },
                                        { CsvImportWarning::Kind::MissingColumn, 3, "", 0 } } );
    }

    SECTION( "on a duplicate key the first wins, with a warning naming the line" )
    {
        const auto imported = importCsv( "0x15,Beispiel\n0x14,Sample\n0x15,Other\n0X14,Upper\n" );
        CHECK( imported.rows == ecuRows );
        CHECK(
            imported.warnings
            == QList<CsvImportWarning>{ { CsvImportWarning::Kind::DuplicateKey, 3, "0x15", 1 },
                                        { CsvImportWarning::Kind::DuplicateKey, 4, "0X14", 2 } } );

        SECTION( "keys differing in case are different for a case-sensitive table" )
        {
            CsvImportOptions options;
            options.caseSensitive = true;
            const auto sensitive = importCsv( "0x14,Sample\n0X14,Upper\n", options );
            CHECK( sensitive.rows == QList<NameRow>{ { "0x14", "Sample" }, { "0X14", "Upper" } } );
            CHECK( sensitive.warnings.isEmpty() );
        }
    }

    SECTION( "rows pasted from a spreadsheet" )
    {
        const auto imported
            = importCsv( "0x15\tBeispiel\tcomment, with comma\r\n0x14\tSample\t\r\n" );
        CHECK( imported.separator == QLatin1Char( '\t' ) );
        CHECK( imported.rows == ecuRows );
    }
}

TEST_CASE( "A Name Table's rows are exported to CSV", "[valuenames][csv]" )
{
    const QList<NameRow> rows{ { "0x1[0-9]", "Door, left" }, { "say", "\"hi\"" } };
    const QList<NameRow> awkward{ { "#1", " padded " }, { " k", "" } };

    CHECK( exportCsv( rows ) == "0x1[0-9],\"Door, left\"\nsay,\"\"\"hi\"\"\"\n" );
    CHECK( exportCsv( rows, QLatin1Char( ';' ), { "key", "name" } )
           == "key;name\n0x1[0-9];Door, left\nsay;\"\"\"hi\"\"\"\n" );

    SECTION( "and read back as they were" )
    {
        for ( const auto separator :
              { QLatin1Char( ',' ), QLatin1Char( ';' ), QLatin1Char( '\t' ) } ) {
            CsvImportOptions options;
            options.hasHeader = true;
            const auto imported
                = importCsv( exportCsv( rows, separator, { "key", "name" } ), options );
            CHECK( imported.separator == separator );
            CHECK( imported.rows == rows );
            CHECK( importCsv( exportCsv( awkward, separator ) ).rows == awkward );
        }
    }
}
