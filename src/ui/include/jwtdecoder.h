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

#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTimeZone>

namespace logsquirl {
namespace jwt {

// Decode a Base64URL-encoded segment (RFC 7515) to a byte array.
// JWT uses '-' instead of '+', '_' instead of '/', and omits trailing '=' padding.
inline QByteArray decodeBase64Url( const QByteArray& input )
{
    QByteArray base64 = input;
    base64.replace( '-', '+' );
    base64.replace( '_', '/' );

    const int pad = static_cast<int>( ( 4 - base64.size() % 4 ) % 4 );
    base64.append( pad, '=' );

    return QByteArray::fromBase64( base64 );
}

// Format a JSON object with human-readable timestamps for known JWT epoch fields.
// Annotates iat, exp, nbf, and auth_time values with their UTC date/time.
inline QString formatJwtJson( const QByteArray& jsonBytes )
{
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson( jsonBytes, &parseError );
    if ( doc.isNull() || !doc.isObject() ) {
        return QString::fromUtf8( jsonBytes );
    }

    const auto indented = doc.toJson( QJsonDocument::Indented );
    auto result = QString::fromUtf8( indented );

    static const QStringList epochFields = { "iat", "exp", "nbf", "auth_time" };
    const auto obj = doc.object();

    for ( const auto& field : epochFields ) {
        if ( !obj.contains( field ) ) {
            continue;
        }
        const auto val = obj.value( field );
        if ( !val.isDouble() ) {
            continue;
        }

        const auto epoch = static_cast<qint64>( val.toDouble() );
        const auto dt = QDateTime::fromSecsSinceEpoch( epoch, QTimeZone::utc() );
        const auto readable = dt.toString( "yyyy-MM-ddThh:mm:ssZ" );

        // Annotate the numeric value in the formatted output with the readable date
        const auto pattern
            = QString( "\"%1\": %2" ).arg( field ).arg( epoch );
        const auto annotated
            = QString( "\"%1\": %2  // %3" ).arg( field ).arg( epoch ).arg( readable );
        result.replace( pattern, annotated );
    }

    return result;
}

// Decode a complete JWT token string into a formatted human-readable representation.
// Returns an empty string if the input is not a valid JWT structure.
inline QString decodeToken( const QString& token )
{
    const auto trimmed = token.trimmed();
    const auto parts = trimmed.split( '.' );
    if ( parts.size() < 2 || parts.size() > 3 ) {
        return QString{};
    }

    const auto headerBytes = decodeBase64Url( parts[ 0 ].toUtf8() );
    const auto payloadBytes = decodeBase64Url( parts[ 1 ].toUtf8() );

    QString result;
    result += QString::fromUtf8( "═══ JWT Header ═══\n" );
    result += formatJwtJson( headerBytes );
    result += '\n';
    result += QString::fromUtf8( "\n═══ JWT Payload ═══\n" );
    result += formatJwtJson( payloadBytes );
    result += '\n';

    if ( parts.size() == 3 && !parts[ 2 ].isEmpty() ) {
        const auto sigBytes = decodeBase64Url( parts[ 2 ].toUtf8() );
        result += QString::fromUtf8( "\n═══ Signature ═══\n" );
        result += QString::fromLatin1( sigBytes.toHex() );
        result += '\n';
    }

    return result;
}

} // namespace jwt
} // namespace logsquirl
