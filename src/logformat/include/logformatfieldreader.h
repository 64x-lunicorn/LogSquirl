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

#include <memory>
#include <optional>
#include <utility>

#include <QString>
#include <QStringList>

#include "fieldreader.h"
#include "logformatdefinition.h"
#include "tablerowcells.h"

// Reads the fields of a Log Line through a Log Format, for the Field Terms
// of a Search (#798): the regex library's FieldReader implemented over the
// Log Format. The fields are the Log Format's, named as the Table View's
// column headers name them, and their text is what the Table View's cells
// show: both are computed by TableRowCells, so they cannot drift apart.
//
// Holds the Log Format shared, since a Search's worker threads read through
// this object while the tab may let go of the Log Format; the Log Format is
// immutable, so sharing it is safe.
class LogFormatFieldReader : public FieldReader {
public:
    explicit LogFormatFieldReader( std::shared_ptr<const LogFormatDefinition> format );

    // The Log Format's name and a fingerprint of its patterns and fields: a
    // Log Format edited on disk and recognized anew reads differently.
    QString identity() const override;
    QStringList fieldNames() const override;
    std::optional<int> fieldIndex( const QString& name ) const override;
    bool readFields( const QString& line, QStringList& fields ) const override;
    std::optional<std::pair<qsizetype, qsizetype>> fieldSpan( const QString& line,
                                                              int field ) const override;

    const LogFormatDefinition& format() const
    {
        return *format_;
    }

private:
    std::shared_ptr<const LogFormatDefinition> format_;
    TableRowCells cells_;
    QString identity_;
};
