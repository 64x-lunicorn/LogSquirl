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

#include <functional>
#include <memory>
#include <optional>

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include "linetypes.h"
#include "logfieldextractor.h"
#include "logformatdefinition.h"
#include "timestampreader.h"

// What each cell of a Table View Row shows, computed from the text of Log
// Lines alone: the fields of the Log Format, and after the timestamp field the
// time elapsed since the previous Log Line of the Log File that has a
// Timestamp. The one place the cells of a Row are computed: the Table View's
// model and every export compute them here, so they cannot drift apart.
//
// It keeps no state beyond the Log Format: every method is const and reads
// only its arguments, so it may be used off the UI thread. The Log Format
// must outlive it.
class TableRowCells {
public:
    // How many Log Lines before a Row are looked at for the previous
    // Timestamp; beyond that the elapsed time is left empty.
    static constexpr uint64_t ElapsedLookBack = 100;

    // Timestamps without a year take the year of modificationDate, the date
    // the Log File was last written (ADR 0010); without one, the current year.
    explicit TableRowCells( const LogFormatDefinition& format, const QDate& modificationDate = {} );

    // The elapsed time as shown: "+0.004s", "+12.3s", "+5m02s", "+1h05m",
    // "+2d03h", with a "-" for a negative one (Log Lines out of order).
    static QString formatElapsed( qint64 milliseconds );

    // The names of the Log Format's fields, in the order of their columns.
    const QStringList& fieldNames() const
    {
        return fieldNames_;
    }
    // The header of each column, in the order the Table View shows them.
    QStringList columnNames() const;
    // The header of one column: "Δt" for the elapsed one, else its field's name.
    QString columnName( int column ) const;
    int columnCount() const
    {
        return static_cast<int>( fieldNames_.size() ) + ( elapsedColumn_ >= 0 ? 1 : 0 );
    }
    // The column with the elapsed time, -1 when there is none.
    int elapsedColumn() const
    {
        return elapsedColumn_;
    }
    // The column showing a field of the Log Format.
    int columnOfField( int field ) const
    {
        return elapsedColumn_ >= 0 && field >= elapsedColumn_ ? field + 1 : field;
    }
    // The field of the Log Format behind a column that is not the elapsed one.
    int fieldOfColumn( int column ) const
    {
        return elapsedColumn_ >= 0 && column > elapsedColumn_ ? column - 1 : column;
    }

    const LogFieldExtractor& extractor() const
    {
        return extractor_;
    }

    // The Timestamp of a Log Line, none for one without or when the Log
    // Format has no timestamp field.
    std::optional<QDateTime> timestampOf( const QString& line ) const;

    // The Timestamp of a Log Line of the Log File, by its number.
    using TimestampOfLogLine = std::function<std::optional<QDateTime>( LineNumber )>;

    // What a Row shows, and the Timestamp of its Log Line.
    struct Row {
        // One per column, as the Table View shows it.
        QStringList cells;
        std::optional<QDateTime> timestamp;
    };
    // The Row showing Log Line logLine, whose text is line. Its elapsed time
    // is measured from the nearest earlier Log Line of the Log File within
    // ElapsedLookBack whose Timestamp earlierTimestamp gives; it is empty for
    // a Log Line without a Timestamp, and when no Log Line before it has one.
    // A Log Line the Log Format does not match shows whole in the last field.
    Row rowOf( LineNumber logLine, const QString& line,
               const TimestampOfLogLine& earlierTimestamp ) const;

private:
    LogFieldExtractor extractor_;
    LogFormatKind kind_;
    QStringList fieldNames_;
    // Reads Timestamps; none when the Log Format has no timestamp field.
    std::unique_ptr<const TimestampReader> reader_;
    // The column with the elapsed time, -1 when there is none.
    int elapsedColumn_ = -1;
    // The field with the Timestamp, -1 when there is none.
    int timestampField_ = -1;
};
