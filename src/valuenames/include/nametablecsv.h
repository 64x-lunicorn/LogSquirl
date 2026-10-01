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

#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>

#include "naminggroup.h"

// The rows of a Name Table read from and written to CSV (#647). Rows copied
// from a spreadsheet and pasted are tab-separated CSV and read the same way.
namespace logsquirl::valuenames {

// One record of a CSV text, with the line it starts on (1-based): a quoted
// field may span several lines.
struct CsvRecord {
    int line = 0;
    QStringList fields;

    bool operator==( const CsvRecord& ) const = default;
};

// The separator of a CSV text: ',', ';' or a tab, whichever separates the
// fields of the most of its first records, read as csvRecords() reads them;
// on a tie the first of tab, ';' and ','. A comma when none does.
QChar detectCsvSeparator( const QString& text );

// The records of a CSV text (RFC 4180 quoting, any line end). Only a quote
// that starts a field opens a quoted field. A byte order mark at the start,
// empty lines and lines starting with '#' are skipped.
QList<CsvRecord> csvRecords( const QString& text, QChar separator );

struct CsvImportOptions {
    // The columns, 0-based, the key and the name are taken from.
    int keyColumn = 0;
    int nameColumn = 1;
    // Whether the first record is a header, not a row.
    bool hasHeader = false;
    // Whether keys differing only in case are different keys.
    bool caseSensitive = false;
};

struct CsvImportWarning {
    enum class Kind {
        // The key was already read on firstLine: the first one wins.
        DuplicateKey,
        // The record has no key or no name column, or an empty key: it is
        // skipped.
        MissingColumn,
        // The name is empty. The row is read all the same.
        EmptyName,
        // The name holds a line break or control character, which is shown as
        // a space. The row is read all the same.
        ControlCharacterInName,
    };

    Kind kind;
    // The line the record starts on (1-based).
    int line = 0;
    QString key;
    int firstLine = 0;

    bool operator==( const CsvImportWarning& ) const = default;
};

struct CsvImport {
    QChar separator;
    QList<NameRow> rows;
    QList<CsvImportWarning> warnings;
};

// The rows of a CSV text, its separator detected.
CsvImport importCsv( const QString& text, const CsvImportOptions& options = {} );

// The rows as CSV, one record per row, key first, each line ended by '\n',
// after header when that is not empty. A field is quoted when it would not
// read back as it is: with any of the three separators, a quote, a line
// end, blanks at either end, or a key that is empty or starts with '#'.
QString exportCsv( const QList<NameRow>& rows, QChar separator = QLatin1Char( ',' ),
                   const QStringList& header = {} );

} // namespace logsquirl::valuenames
