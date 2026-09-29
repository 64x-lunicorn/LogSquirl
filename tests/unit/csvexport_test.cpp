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

// A CSV export writes what the Table View shows, in UTF-8 with a Byte Order
// Mark and CR LF line ends, off the UI thread, and replaces its file only when
// every line was written (#572).

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFile>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QTimer>

#include "csv.h"
#include "csvexport.h"
#include "fake_log_data.h"
#include "logformatdefinition.h"
#include "logformattablemodel.h"

namespace {

// Timestamp, level and message, with an elapsed-time column after the
// timestamp.
LogFormatDefinition makeFormat()
{
    LogFormatDefinition format;
    format.setName( "csvexport_test" );
    format.setTitle( "CSV export test" );
    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<timestamp>\d{2}:\d{2}:\d{2}\.\d{3}) (?<level>[A-Z]+) (?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setTimestampField( "timestamp" );
    format.setTimestampFormats( { "%H:%M:%S.%L" } );
    format.setLevelField( "level" );
    format.setBodyField( "body" );
    return format;
}

const QStringList SampleLines = {
    "10:00:00.000 INFO start",    "10:00:00.004 WARN disk \"sda\" at 91%, still writing",
    "    at Foo.bar(Foo.java:1)", "10:00:12.304 ERROR failed; retrying",
    "10:05:14.304 INFO done",
};

// Every CSV line of an export, read as saveDisplayedLines() reads them.
QStringList exportedLines( const CsvExport& csvExport )
{
    const auto end = csvLineCount( csvExport );
    const auto readLines = csvLinesReader( csvExport );
    QStringList lines;
    // In chunks, as the save reads them.
    for ( uint64_t first = 0; first < end.get(); first += 2 ) {
        const auto count = std::min<uint64_t>( 2, end.get() - first );
        for ( const auto& line : readLines( LineNumber( first ), LinesCount( count ) ) ) {
            lines << line;
        }
    }
    return lines;
}

CsvExport exportOf( const LogFormatDefinition& format, const AbstractLogData& logData,
                    const QStringList& lines )
{
    CsvExport csvExport;
    csvExport.format = std::make_shared<const LogFormatDefinition>( format );
    csvExport.logData = &logData;
    for ( int line = 0; line < lines.size(); ++line ) {
        csvExport.logLines.push_back( LineNumber( static_cast<uint64_t>( line ) ) );
    }
    return csvExport;
}

QByteArray contentOf( const QString& fileName )
{
    QFile file{ fileName };
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
}

} // namespace

SCENARIO( "a CSV export writes exactly what the Table View shows", "[csvexport]" )
{
    const auto format = makeFormat();
    FakeLogData logData( SampleLines );
    LogFormatTableModel model( format, &logData );
    model.setLineCount( static_cast<int>( SampleLines.size() ) );
    REQUIRE( model.columnCount() == 4 );
    REQUIRE( model.isElapsedColumn( 1 ) );

    GIVEN( "an export of every column of every Row, with a header row" )
    {
        auto csvExport = exportOf( format, logData, SampleLines );
        for ( int column = 0; column < model.columnCount(); ++column ) {
            csvExport.columns.push_back( CsvColumn::ofTable(
                column, model.headerData( column, Qt::Horizontal ).toString() ) );
        }
        const auto separator = QChar( ';' );
        csvExport.separator = separator;

        WHEN( "its lines are read" )
        {
            const auto lines = exportedLines( csvExport );

            THEN( "the header row names the table's columns, and each line holds the cells the "
                  "table shows for its Row, the elapsed time and a Log Line the Log Format does "
                  "not match included" )
            {
                REQUIRE( lines.size() == SampleLines.size() + 1 );
                REQUIRE( lines[ 0 ] == "timestamp;Δt;level;body" );

                for ( int row = 0; row < model.rowCount(); ++row ) {
                    QStringList shown;
                    for ( int column = 0; column < model.columnCount(); ++column ) {
                        shown << model.data( model.index( row, column ) ).toString();
                    }
                    INFO( "Row " << row );
                    REQUIRE( lines[ row + 1 ] == csvLine( shown, separator ) );
                }

                // What the comparison covers, spelled out.
                REQUIRE( lines[ 2 ]
                         == R"(10:00:00.004;+0.004s;WARN;"disk ""sda"" at 91%, still writing")" );
                REQUIRE( lines[ 3 ] == ";;;    at Foo.bar(Foo.java:1)" );
                REQUIRE( lines[ 4 ] == R"(10:00:12.304;+12.3s;ERROR;"failed; retrying")" );
            }
        }
    }

    GIVEN( "an export of some Log Lines, out of the Log File's order, with the Line column and "
           "without a header row" )
    {
        auto csvExport = exportOf( format, logData, {} );
        csvExport.logLines = { 4_lnum, 1_lnum };
        csvExport.columns = { CsvColumn::lineNumber( "Line" ), CsvColumn::ofTable( 1, "Δt" ),
                              CsvColumn::ofTable( 3, "body" ) };
        csvExport.header = false;

        WHEN( "its lines are read" )
        {
            const auto lines = exportedLines( csvExport );

            THEN( "each Log Line is written in the order given, its 1-based number first, and "
                  "its elapsed time measured against the Log File" )
            {
                REQUIRE( lines
                         == QStringList{ "5,+5m02s,done",
                                         R"(2,+0.004s,"disk ""sda"" at 91%, still writing")" } );
            }
        }
    }

    GIVEN( "an export with a column of its own" )
    {
        auto csvExport = exportOf( format, logData, {} );
        csvExport.logLines = { 3_lnum, 0_lnum };
        const QStringList kinds = { "Match", "Context" };
        CsvColumn kind;
        kind.name = "Type";
        kind.value = [ kinds ]( size_t position, LineNumber ) {
            return kinds[ static_cast<qsizetype>( position ) ];
        };
        csvExport.columns = { kind, CsvColumn::ofTable( 2, "level" ) };

        THEN( "its values are those it gives for each position" )
        {
            REQUIRE( exportedLines( csvExport )
                     == QStringList{ "Type,level", "Match,ERROR", "Context,INFO" } );
        }
    }
}

SCENARIO( "a CSV file is UTF-8 with a Byte Order Mark and CR LF line ends", "[csvexport]" )
{
    const auto format = makeFormat();
    FakeLogData logData( { "10:00:00.000 INFO Grüße", "10:00:01.000 INFO two\nlines" } );
    auto csvExport = exportOf( format, logData, { "", "" } );
    csvExport.columns = { CsvColumn::ofTable( 3, "body" ) };

    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto fileName = dir.filePath( "export.csv" );

    WHEN( "the export is written" )
    {
        exportCsvWithProgress( nullptr, fileName, csvExport );

        THEN( "the file starts with the Byte Order Mark and ends every line with CR LF; a line "
              "end inside a field stays as it is, quoted" )
        {
            REQUIRE( contentOf( fileName )
                     == QByteArray( "\xEF\xBB\xBF" ) + "body\r\n" + QString( "Grüße" ).toUtf8()
                            + "\r\n\"10:00:01.000 INFO two\nlines\"\r\n" );
        }
    }
}

SCENARIO( "a CSV export offers the Table View's columns and takes the dialog's choices",
          "[csvexport]" )
{
    const auto format = makeFormat();
    FakeLogData logData( SampleLines );
    LogFormatTableModel model( format, &logData );

    GIVEN( "the columns of the Table View for a Log Format" )
    {
        const auto columns = CsvColumn::ofTable( format );

        THEN( "they are the table's columns, in its order and headed as it heads them" )
        {
            REQUIRE( columns.size() == static_cast<size_t>( model.columnCount() ) );
            for ( size_t column = 0; column < columns.size(); ++column ) {
                const auto tableColumn = static_cast<int>( column );
                REQUIRE( columns[ column ].tableColumn == tableColumn );
                REQUIRE( columns[ column ].name
                         == model.headerData( tableColumn, Qt::Horizontal ).toString() );
            }
        }
    }

    GIVEN( "a Line column of its own before them" )
    {
        std::vector<CsvColumn> offered{ CsvColumn::lineNumber( "Line" ) };
        const auto tableColumns = CsvColumn::ofTable( format );
        offered.insert( offered.end(), tableColumns.begin(), tableColumns.end() );

        THEN( "the dialog lists them all, the Line column unchecked" )
        {
            const auto listed = csvDialogColumns( offered, 1 );
            QStringList names;
            std::vector<bool> checked;
            for ( const auto& column : listed ) {
                names << column.name;
                checked.push_back( column.checked );
            }
            REQUIRE( names == QStringList{ "Line", "timestamp", "Δt", "level", "body" } );
            REQUIRE( checked == std::vector<bool>{ false, true, true, true, true } );
        }

        WHEN( "the dialog's choices are taken" )
        {
            CsvExportDialog::Choices choices;
            choices.columns = { 0, 2, 4, 99 };
            choices.separator = QLatin1Char( ';' );
            choices.header = false;

            auto csvExport = exportOf( format, logData, {} );
            csvExport.logLines = { 1_lnum };
            applyCsvChoices( csvExport, choices, offered );

            THEN( "the export writes the checked columns with the chosen separator, and an index "
                  "past the columns offered is ignored" )
            {
                REQUIRE( exportedLines( csvExport )
                         == QStringList{ R"(2;+0.004s;"disk ""sda"" at 91%, still writing")" } );
            }
        }
    }
}

SCENARIO( "a CSV export's Type column names what each Log Line is", "[csvexport]" )
{
    using LineType = AbstractLogData::LineType;
    using Flags = AbstractLogData::LineTypeFlags;

    THEN( "each kind has its English name, never translated" )
    {
        REQUIRE( CsvColumn::typeName( Flags::Match ) == "Match" );
        REQUIRE( CsvColumn::typeName( Flags::Mark ) == "Mark" );
        REQUIRE( CsvColumn::typeName( LineType{ Flags::Match } | Flags::Mark ) == "Match+Mark" );
        REQUIRE( CsvColumn::typeName( Flags::Context ) == "Context" );
        REQUIRE( CsvColumn::typeName( Flags::Plain ).isEmpty() );
    }

    GIVEN( "an export with a Type column" )
    {
        const auto format = makeFormat();
        FakeLogData logData( SampleLines );
        auto csvExport = exportOf( format, logData, {} );
        csvExport.logLines = { 0_lnum, 1_lnum, 3_lnum };
        csvExport.columns
            = { CsvColumn::lineTypes( "Type", { Flags::Context, Flags::Match,
                                                LineType{ Flags::Match } | Flags::Mark } ),
                CsvColumn::ofTable( 2, "level" ) };

        THEN( "each line holds the type copied for its position" )
        {
            REQUIRE(
                exportedLines( csvExport )
                == QStringList{ "Type,level", "Context,INFO", "Match,WARN", "Match+Mark,ERROR" } );
        }
    }
}

namespace {

// A Log File of many Log Lines, made up as they are read.
class GeneratedLogData : public FakeLogData {
public:
    explicit GeneratedLogData( uint64_t lines )
        : lines_( lines )
    {
    }

protected:
    QString doGetLineString( LineNumber line ) const override
    {
        const auto milliseconds = static_cast<int>( line.get() % 1000 );
        return QStringLiteral( "10:00:00.%1 INFO line %2" )
            .arg( milliseconds, 3, 10, QLatin1Char( '0' ) )
            .arg( line.get() );
    }
    LinesCount doGetNbLine() const override
    {
        return LinesCount( lines_ );
    }

private:
    uint64_t lines_;
};

constexpr uint64_t ManyLines = 1'000'000;

CsvExport manyLinesExport( const LogFormatDefinition& format, const AbstractLogData& logData )
{
    CsvExport csvExport;
    csvExport.format = std::make_shared<const LogFormatDefinition>( format );
    csvExport.logData = &logData;
    csvExport.logLines.reserve( ManyLines );
    for ( uint64_t line = 0; line < ManyLines; ++line ) {
        csvExport.logLines.push_back( LineNumber( line ) );
    }
    csvExport.columns = { CsvColumn::lineNumber( "Line" ), CsvColumn::ofTable( 0, "timestamp" ),
                          CsvColumn::ofTable( 1, "Δt" ), CsvColumn::ofTable( 3, "body" ) };
    return csvExport;
}

// Cancels the progress dialog of an export once it has shown progress, as
// the user would; returns whether it did.
class Canceller {
public:
    Canceller()
    {
        QObject::connect( &poll_, &QTimer::timeout, &poll_, [ this ]() {
            auto* dialog = qobject_cast<QProgressDialog*>( QApplication::activeModalWidget() );
            if ( dialog != nullptr && dialog->value() > 0 ) {
                poll_.stop();
                cancelled_ = true;
                dialog->cancel();
            }
        } );
        poll_.start( 5 );
    }

    bool cancelled() const
    {
        return cancelled_;
    }

private:
    QTimer poll_;
    bool cancelled_ = false;
};

} // namespace

SCENARIO( "a CSV export of a million Rows can be cancelled and leaves no file behind",
          "[csvexport]" )
{
    const auto format = makeFormat();
    GeneratedLogData logData( ManyLines );

    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    GIVEN( "an existing file to export to" )
    {
        const auto fileName = dir.filePath( "existing.csv" );
        {
            QFile existing{ fileName };
            REQUIRE( existing.open( QIODevice::WriteOnly ) );
            existing.write( "kept" );
        }

        WHEN( "the export is cancelled while it runs" )
        {
            const Canceller canceller;
            exportCsvWithProgress( nullptr, fileName, manyLinesExport( format, logData ) );
            REQUIRE( canceller.cancelled() );

            THEN( "the file is left as it was" )
            {
                REQUIRE( contentOf( fileName ) == "kept" );
            }
        }
    }

    GIVEN( "no file to export to yet" )
    {
        const auto fileName = dir.filePath( "new.csv" );

        WHEN( "the export is cancelled while it runs" )
        {
            const Canceller canceller;
            exportCsvWithProgress( nullptr, fileName, manyLinesExport( format, logData ) );
            REQUIRE( canceller.cancelled() );

            THEN( "no file is created" )
            {
                REQUIRE( !QFile::exists( fileName ) );
            }
        }
    }
}

SCENARIO( "a CSV export of a million Rows writes them all", "[csvexport][.slow]" )
{
    const auto format = makeFormat();
    GeneratedLogData logData( ManyLines );
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto fileName = dir.filePath( "all.csv" );

    WHEN( "the export runs to its end" )
    {
        exportCsvWithProgress( nullptr, fileName, manyLinesExport( format, logData ) );

        THEN( "the file holds the header row and one line per Row" )
        {
            const auto content = contentOf( fileName );
            REQUIRE( content.count( "\r\n" ) == static_cast<qsizetype>( ManyLines + 1 ) );
            REQUIRE( content.endsWith( "1000000,10:00:00.999,+0.001s,line 999999\r\n" ) );
        }
    }
}
