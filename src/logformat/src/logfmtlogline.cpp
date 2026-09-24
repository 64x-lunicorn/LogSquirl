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

#include "logfmtlogline.h"

namespace LogfmtLogLine {

std::optional<QHash<QString, QString>> parse( const QString& line )
{
    QHash<QString, QString> pairs;
    const auto size = line.size();
    qsizetype pos = 0;

    const auto skipSpace = [ & ]() {
        while ( pos < size && line[ pos ].isSpace() ) {
            ++pos;
        }
    };

    skipSpace();
    if ( pos >= size || line[ pos ] == QLatin1Char( '{' ) ) {
        return std::nullopt;
    }

    while ( pos < size ) {
        const auto keyStart = pos;
        while ( pos < size && !line[ pos ].isSpace() && line[ pos ] != QLatin1Char( '=' ) ) {
            if ( line[ pos ] == QLatin1Char( '"' ) ) {
                return std::nullopt;
            }
            ++pos;
        }
        if ( pos == keyStart ) {
            return std::nullopt; // "=value": a pair without a key
        }
        const auto key = line.mid( keyStart, pos - keyStart );

        QString value;
        if ( pos < size && line[ pos ] == QLatin1Char( '=' ) ) {
            ++pos;
            if ( pos < size && line[ pos ] == QLatin1Char( '"' ) ) {
                ++pos;
                bool closed = false;
                while ( pos < size ) {
                    const auto character = line[ pos++ ];
                    if ( character == QLatin1Char( '"' ) ) {
                        closed = true;
                        break;
                    }
                    if ( character == QLatin1Char( '\\' ) && pos < size
                         && ( line[ pos ] == QLatin1Char( '"' )
                              || line[ pos ] == QLatin1Char( '\\' ) ) ) {
                        value += line[ pos++ ];
                    }
                    else {
                        value += character;
                    }
                }
                if ( !closed || ( pos < size && !line[ pos ].isSpace() ) ) {
                    return std::nullopt;
                }
            }
            else {
                const auto valueStart = pos;
                while ( pos < size && !line[ pos ].isSpace() ) {
                    ++pos;
                }
                value = line.mid( valueStart, pos - valueStart );
            }
        }
        pairs.insert( key, value );
        skipSpace();
    }
    return pairs;
}

} // namespace LogfmtLogLine
