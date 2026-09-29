/*
 * Copyright (C) 2009, 2010, 2012, 2017 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements the FilteredView concrete class.
// Most of the actual drawing and event management is done in AbstractLogView
// Only behaviour specific to the filtered (bottom) view is implemented here.

#include <cassert>
#include <utility>

#include "filteredview.h"

#include "logformatdefinition.h"

FilteredView::FilteredView( LogFilteredData* newLogData,
                            const QuickFindPattern* const quickFindPattern, bool initialTextWrap,
                            QWidget* parent )
    : AbstractLogView( newLogData, std::make_unique<FilteredViewLines>( newLogData ),
                       quickFindPattern, initialTextWrap, parent )
{
    // Kept for what is visible in the view
    logFilteredData_ = newLogData;
}

void FilteredView::setVisibility( Visibility visi )
{
    assert( logFilteredData_ );

    logFilteredData_->setVisibility( visi );

    updateData();
}

FilteredView::Visibility FilteredView::visibility() const
{
    assert( logFilteredData_ );

    return logFilteredData_->visibility();
}

void FilteredView::setPresentationPolicy( const PresentationPolicy& policy )
{
    AbstractLogView::setPresentationPolicy( policy );
    setLineNumbersVisible( policy.filteredLineNumbersVisible );
}

void FilteredView::setRecognizedFormat( std::function<RecognizedFormat()> recognizedFormat )
{
    recognizedFormat_ = std::move( recognizedFormat );
}

std::function<void()> FilteredView::exportAsCsvAction()
{
    if ( !recognizedFormat_ || !recognizedFormat_().format ) {
        return {};
    }
    return [ this ]() { exportAsCsv(); };
}

std::vector<CsvColumn>
FilteredView::csvColumns( const LogFormatDefinition* format,
                          logsquirl::vector<AbstractLogData::LineType> types ) const
{
    std::vector<CsvColumn> columns{ CsvColumn::lineNumber( tr( "Line" ) ),
                                    CsvColumn::lineTypes( tr( "Type" ), std::move( types ) ) };
    if ( format != nullptr ) {
        const auto tableColumns = CsvColumn::ofTable( *format );
        columns.insert( columns.end(), tableColumns.begin(), tableColumns.end() );
    }
    return columns;
}

CsvExportDialog::Setup FilteredView::csvExportSetup() const
{
    const auto recognized = recognizedFormat_ ? recognizedFormat_() : RecognizedFormat{};

    CsvExportDialog::Setup setup;
    setup.allRowsText = tr( "All shown lines" );
    setup.selectedRowsText = tr( "Selected lines" );
    setup.hasSelection = !selectedLogLines().empty();
    setup.rowOptions.push_back(
        { tr( "Include Context Lines" ),
          visibility().testFlag( LogFilteredData::VisibilityFlags::Context ), false } );
    setup.columns = csvDialogColumns( csvColumns( recognized.format.get(), {} ), 2 );
    if ( !recognized.logFilePath.isEmpty() ) {
        setup.proposedFileName = recognized.logFilePath + QStringLiteral( ".csv" );
    }
    return setup;
}

void FilteredView::exportAsCsv()
{
    if ( const auto choices = CsvExportDialog::ask( this, csvExportSetup() ) ) {
        exportCsvTo( *choices );
    }
}

void FilteredView::exportCsvTo( const CsvExportDialog::Choices& choices )
{
    const auto recognized = recognizedFormat_ ? recognizedFormat_() : RecognizedFormat{};
    if ( !recognized.format || choices.fileName.isEmpty() || choices.columns.empty() ) {
        return;
    }

    using Flags = AbstractLogData::LineTypeFlags;
    const bool withContextLines = visibility().testFlag( LogFilteredData::VisibilityFlags::Context )
                                  && !choices.rowOptions.empty() && choices.rowOptions.front();

    // The Log Lines and their types are copied here, on the UI thread: the
    // export reads the text of the Log File off it, and never the
    // LogFilteredData, which the UI thread goes on changing.
    CsvExport csvExport;
    logsquirl::vector<AbstractLogData::LineType> types;
    const auto take = [ & ]( LineNumber logLine ) {
        const auto type = logFilteredData_->lineTypeByLine( logLine );
        const bool matchOrMark = type.testFlag( Flags::Match ) || type.testFlag( Flags::Mark );
        if ( matchOrMark || ( withContextLines && type.testFlag( Flags::Context ) ) ) {
            csvExport.logLines.push_back( logLine );
            types.push_back( type );
        }
    };
    if ( choices.selectedRowsOnly ) {
        const auto& shown = lineMapping();
        for ( const auto logLine : selectedLogLines() ) {
            if ( shown.shows( logLine ) ) {
                take( logLine );
            }
        }
    }
    else {
        const auto displayed = logFilteredData_->copyDisplayedLines();
        csvExport.logLines.reserve( displayed.cardinality() );
        types.reserve( displayed.cardinality() );
        for ( const auto logLine : displayed ) {
            take( LineNumber( logLine ) );
        }
    }
    if ( csvExport.logLines.empty() ) {
        return;
    }

    applyCsvChoices( csvExport, choices,
                     csvColumns( recognized.format.get(), std::move( types ) ) );
    csvExport.format = recognized.format;
    csvExport.modificationDate = recognized.modificationDate;
    csvExport.logData = &lineMapping().logFile();

    exportCsvWithProgress( this, choices.fileName, std::move( csvExport ) );
}
