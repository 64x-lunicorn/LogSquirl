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

bool isFormulaStart( QChar character )
{
    return character == QLatin1Char( '=' ) || character == QLatin1Char( '+' )
           || character == QLatin1Char( '-' ) || character == QLatin1Char( '@' );
}

// Whether a spreadsheet would read the field as a formula: it starts with a
// formula character, or with a tab or carriage return and then one (#831).
bool startsLikeFormula( const QString& field )
{
    if ( field.isEmpty() ) {
        return false;
    }
    const auto first = field.front();
    if ( isFormulaStart( first ) ) {
        return true;
    }
    return ( first == QChar::Tabulation || first == QChar::CarriageReturn ) && field.size() > 1
           && isFormulaStart( field[ 1 ] );
}

} // namespace

QString csvLine( const QStringList& fields, QChar separator )
{
    QString line;
    for ( qsizetype index = 0; index < fields.size(); ++index ) {
        if ( index > 0 ) {
            line += separator;
        }
        // The apostrophe keeps a formula text in a spreadsheet (#831). It goes
        // before the quoting so a quoted field keeps it inside the quotes.
        const auto field = startsLikeFormula( fields[ index ] )
                               ? QLatin1Char( '\'' ) + fields[ index ]
                               : fields[ index ];
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
