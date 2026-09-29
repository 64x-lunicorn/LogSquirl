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

#include "csvexport.h"

#include <QHash>
#include <QStringList>

#include "csv.h"
#include "logformatdefinition.h"
#include "tablerowcells.h"
#include "textencoding.h"

CsvColumn CsvColumn::ofTable( int tableColumn, const QString& name )
{
    CsvColumn column;
    column.name = name;
    column.tableColumn = tableColumn;
    return column;
}

CsvColumn CsvColumn::lineNumber( const QString& name )
{
    CsvColumn column;
    column.name = name;
    column.value
        = []( size_t, LineNumber logLine ) { return QString::number( logLine.get() + 1 ); };
    return column;
}

CsvColumn CsvColumn::lineTypes( const QString& name,
                                logsquirl::vector<AbstractLogData::LineType> types )
{
    CsvColumn column;
    column.name = name;
    column.value = [ types = std::make_shared<const logsquirl::vector<AbstractLogData::LineType>>(
                         std::move( types ) ) ]( size_t position, LineNumber ) {
        return position < types->size() ? typeName( ( *types )[ position ] ) : QString{};
    };
    return column;
}

std::vector<CsvColumn> CsvColumn::ofTable( const LogFormatDefinition& format )
{
    const TableRowCells cells( format );
    std::vector<CsvColumn> columns;
    columns.reserve( static_cast<size_t>( cells.columnCount() ) );
    for ( int column = 0; column < cells.columnCount(); ++column ) {
        columns.push_back( ofTable( column, cells.columnName( column ) ) );
    }
    return columns;
}

QString CsvColumn::typeName( AbstractLogData::LineType type )
{
    using Flags = AbstractLogData::LineTypeFlags;
    const bool match = type.testFlag( Flags::Match );
    const bool mark = type.testFlag( Flags::Mark );
    if ( match && mark ) {
        return QStringLiteral( "Match+Mark" );
    }
    if ( match ) {
        return QStringLiteral( "Match" );
    }
    if ( mark ) {
        return QStringLiteral( "Mark" );
    }
    if ( type.testFlag( Flags::Context ) ) {
        return QStringLiteral( "Context" );
    }
    return {};
}

std::vector<CsvExportDialog::Column> csvDialogColumns( const std::vector<CsvColumn>& offered,
                                                       size_t unchecked )
{
    std::vector<CsvExportDialog::Column> listed;
    listed.reserve( offered.size() );
    for ( size_t index = 0; index < offered.size(); ++index ) {
        listed.push_back( { offered[ index ].name, index >= unchecked } );
    }
    return listed;
}

void applyCsvChoices( CsvExport& csvExport, const CsvExportDialog::Choices& choices,
                      const std::vector<CsvColumn>& offered )
{
    csvExport.columns.clear();
    for ( const auto index : choices.columns ) {
        if ( index < offered.size() ) {
            csvExport.columns.push_back( offered[ index ] );
        }
    }
    csvExport.separator = choices.separator;
    csvExport.header = choices.header;
}

LineNumber csvLineCount( const CsvExport& csvExport )
{
    return LineNumber( csvExport.logLines.size() + ( csvExport.header ? 1u : 0u ) );
}

namespace {

// What the reader of an export keeps between the chunks it reads: the Log
// Format's cells, and the Timestamps it read last, so the elapsed time of a
// Log Line finds the one before it without reading it again.
struct ExportReading {
    explicit ExportReading( CsvExport exported )
        : csvExport( std::move( exported ) )
        , cells( *csvExport.format, csvExport.modificationDate )
    {
    }

    std::optional<QDateTime> timestampOf( LineNumber logLine )
    {
        const auto known = timestamps.constFind( logLine.get() );
        if ( known != timestamps.constEnd() ) {
            return known.value();
        }
        auto timestamp = cells.timestampOf( csvExport.logData->getLineString( logLine ) );
        remember( logLine, timestamp );
        return timestamp;
    }

    void remember( LineNumber logLine, const std::optional<QDateTime>& timestamp )
    {
        if ( timestamps.size() >= TimestampCapacity ) {
            timestamps.clear();
        }
        timestamps.insert( logLine.get(), timestamp );
    }

    QStringList fieldsAt( size_t position )
    {
        const auto logLine = csvExport.logLines[ position ];
        const auto line = csvExport.logData->getLineString( logLine );

        auto row = cells.rowOf( logLine, line,
                                [ this ]( LineNumber earlier ) { return timestampOf( earlier ); } );
        if ( cells.elapsedColumn() >= 0 ) {
            // The Log Line after this one finds it without reading it again.
            remember( logLine, row.timestamp );
        }

        QStringList exported;
        exported.reserve( static_cast<qsizetype>( csvExport.columns.size() ) );
        for ( const auto& column : csvExport.columns ) {
            if ( column.tableColumn >= 0 ) {
                exported.append( row.cells.value( column.tableColumn ) );
            }
            else if ( column.value ) {
                exported.append( column.value( position, logLine ) );
            }
            else {
                exported.append( QString{} );
            }
        }
        return exported;
    }

    QStringList header() const
    {
        QStringList names;
        for ( const auto& column : csvExport.columns ) {
            names.append( column.name );
        }
        return names;
    }

    static constexpr qsizetype TimestampCapacity = 16384;

    CsvExport csvExport;
    TableRowCells cells;
    QHash<uint64_t, std::optional<QDateTime>> timestamps;
};

} // namespace

DisplayedLinesReader csvLinesReader( CsvExport csvExport )
{
    auto reading = std::make_shared<ExportReading>( std::move( csvExport ) );
    return [ reading ]( LineNumber first, LinesCount count ) {
        const auto& exported = reading->csvExport;
        const auto separator = exported.separator;
        const uint64_t headerLines = exported.header ? 1u : 0u;

        logsquirl::vector<QString> lines;
        lines.reserve( static_cast<size_t>( count.get() ) );
        for ( auto position = first.get(); position < first.get() + count.get(); ++position ) {
            if ( position < headerLines ) {
                lines.push_back( csvLine( reading->header(), separator ) );
            }
            else {
                lines.push_back(
                    csvLine( reading->fieldsAt( static_cast<size_t>( position - headerLines ) ),
                             separator ) );
            }
        }
        return lines;
    };
}

void exportCsvWithProgress( QWidget* parent, const QString& filename, CsvExport csvExport )
{
    if ( !csvExport.format || !csvExport.logData ) {
        return;
    }
    const auto end = csvLineCount( csvExport );
    saveLinesWithProgress( parent, filename, csvLinesReader( std::move( csvExport ) ), 0_lnum, end,
                           TextEncoding::forName( "utf-8" ), LineEnds::CrLf );
}
