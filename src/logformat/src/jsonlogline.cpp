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

#include "jsonlogline.h"

#include <QJsonDocument>
#include <QLocale>
#include <QTimeZone>

#include <cmath>

namespace JsonLogLine {

std::optional<QJsonObject> parse( const QString& line )
{
    qsizetype start = 0;
    while ( start < line.size() && line[ start ].isSpace() ) {
        ++start;
    }
    if ( start >= line.size() || line[ start ] != QLatin1Char( '{' ) ) {
        return std::nullopt;
    }

    const auto document = QJsonDocument::fromJson( line.toUtf8() );
    if ( !document.isObject() ) {
        return std::nullopt;
    }
    return document.object();
}

QJsonValue valueAt( const QJsonObject& object, const QString& path )
{
    const auto direct = object.value( path );
    if ( !direct.isUndefined() || !path.contains( QLatin1Char( '/' ) ) ) {
        return direct;
    }

    QJsonValue current( object );
    for ( const auto& part : path.split( QLatin1Char( '/' ) ) ) {
        if ( !current.isObject() ) {
            return QJsonValue::Undefined;
        }
        current = current.toObject().value( part );
    }
    return current;
}

QString cellText( const QJsonValue& value )
{
    switch ( value.type() ) {
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral( "true" ) : QStringLiteral( "false" );
    case QJsonValue::Double: {
        const auto number = value.toDouble();
        if ( std::floor( number ) == number && std::fabs( number ) < 9e15 ) {
            return QString::number( static_cast<qint64>( number ) );
        }
        return QString::number( number, 'g', QLocale::FloatingPointShortest );
    }
    default:
        return {};
    }
}

QDateTime fromEpoch( double value, double divisor )
{
    const auto safeDivisor = divisor > 0.0 ? divisor : 1.0;
    return QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>( std::llround( value * 1000.0 / safeDivisor ) ), QTimeZone::UTC );
}

QString epochCellText( double value, double divisor )
{
    return fromEpoch( value, divisor ).toString( QStringLiteral( "yyyy-MM-dd HH:mm:ss.zzz" ) );
}

} // namespace JsonLogLine
