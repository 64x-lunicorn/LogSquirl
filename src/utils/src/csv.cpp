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

#include "csv.h"

namespace {

bool needsQuotes( const QString& field, QChar separator )
{
    for ( const auto character : field ) {
        if ( character == separator || character == QLatin1Char( '"' )
             || character == QChar::CarriageReturn || character == QChar::LineFeed ) {
            return true;
        }
    }
    return false;
}

} // namespace

QString csvLine( const QStringList& fields, QChar separator )
{
    QString line;
    for ( qsizetype index = 0; index < fields.size(); ++index ) {
        if ( index > 0 ) {
            line += separator;
        }
        const auto& field = fields[ index ];
        if ( needsQuotes( field, separator ) ) {
            line += QLatin1Char( '"' );
            line += QString( field ).replace( QLatin1Char( '"' ), QStringLiteral( "\"\"" ) );
            line += QLatin1Char( '"' );
        }
        else {
            line += field;
        }
    }
    return line;
}
