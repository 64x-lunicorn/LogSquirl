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

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include <QChar>
#include <QDate>
#include <QString>

#include "abstractlogdata.h"
#include "containers.h"
#include "csvexportdialog.h"
#include "linessaver.h"
#include "linetypes.h"

class LogFormatDefinition;
class QWidget;

// One column of a CSV export: a column of the Table View, or a column of its
// own whose values a function gives.
struct CsvColumn {
    // Written in the header row.
    QString name;
    // The column of the Table View the values are taken from; -1 for a column
    // of its own.
    int tableColumn = -1;
    // The value of a column of its own for the Log Line at a position of the
    // export (0 is the first Log Line exported). It is called off the UI
    // thread, so it may read only what it holds, never live state of a view.
    std::function<QString( size_t position, LineNumber logLine )> value;

    // The column tableColumn of the Table View, headed name.
    static CsvColumn ofTable( int tableColumn, const QString& name );
    // The 1-based number of each Log Line, as Copy with line numbers writes it.
    static CsvColumn lineNumber( const QString& name );
    // Whether the Log Line at each position is a Match, a Mark, both or a
    // Context Line, as typeName() writes it; types holds one per position,
    // copied on the UI thread.
    static CsvColumn lineTypes( const QString& name,
                                logsquirl::vector<AbstractLogData::LineType> types );
    // Every column of the Table View for format, in its order, headed as the
    // Table View heads them.
    static std::vector<CsvColumn> ofTable( const LogFormatDefinition& format );

    // "Match", "Mark", "Match+Mark" or "Context", never translated, so a
    // spreadsheet can filter on it; empty for a plain Log Line.
    static QString typeName( AbstractLogData::LineType type );
};

// What a CSV export writes: one CSV line for each of logLines, in their order,
// with the cells the Table View shows for the Log Line, after a header row of
// the column names if header is set. Every value is the Table View's, computed
// by TableRowCells from the text of the Log File.
struct CsvExport {
    // A copy of the Log Format, so the export does not depend on the one the
    // Log File's view holds.
    std::shared_ptr<const LogFormatDefinition> format;
    // The date the Log File was last written, which gives Timestamps without a
    // year theirs, as the Table View's (ADR 0010).
    QDate modificationDate;
    // Read off the UI thread, as Save to file reads it.
    const AbstractLogData* logData = nullptr;
    // A copy, taken on the UI thread, of the Log Lines to export.
    logsquirl::vector<LineNumber> logLines;
    std::vector<CsvColumn> columns;
    QChar separator = QLatin1Char( ',' );
    bool header = true;
};

// The columns of the dialog Export as CSV for the columns a view offers, in
// their order: the first unchecked ones unchecked, every other one checked.
std::vector<CsvExportDialog::Column> csvDialogColumns( const std::vector<CsvColumn>& offered,
                                                       size_t unchecked );

// Takes what the user chose in the dialog Export as CSV: the separator, the
// header row, and the columns checked out of offered, which the dialog listed
// in the same order.
void applyCsvChoices( CsvExport& csvExport, const CsvExportDialog::Choices& choices,
                      const std::vector<CsvColumn>& offered );

// How many lines the export writes: the header row and one per Log Line.
LineNumber csvLineCount( const CsvExport& csvExport );

// Reads the CSV lines of an export, without line ends: position 0 is the
// header row when there is one, then one line per Log Line. Positions are
// read in increasing order by one caller at a time, as saveDisplayedLines()
// reads them, off the UI thread.
DisplayedLinesReader csvLinesReader( CsvExport csvExport );

// Writes an export to filename behind a progress dialog over parent, in UTF-8
// with a Byte Order Mark and CR LF line ends, as saveLinesWithProgress()
// does: cancelled or failed, filename is left as it was.
void exportCsvWithProgress( QWidget* parent, const QString& filename, CsvExport csvExport );
