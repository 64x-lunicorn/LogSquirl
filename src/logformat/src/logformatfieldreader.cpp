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

#include "logformatfieldreader.h"

#include <algorithm>

#include <QHash>

namespace {

// What tells one Log Format's reading from another's: its patterns (or the
// fields it reads by key) and the fields in column order.
QString fingerprintOf( const LogFormatDefinition& format, const QStringList& fieldNames )
{
    QStringList parts;
    const auto& patterns = format.regexPatterns();
    for ( auto it = patterns.cbegin(); it != patterns.cend(); ++it ) {
        parts << it.key() + QChar( '=' ) + it.value();
    }
    std::ranges::sort( parts );
    parts << fieldNames;
    parts << QString::number( static_cast<int>( format.kind() ) );
    return QString::number( qHash( parts.join( QChar( '\n' ) ) ), 16 );
}

} // namespace

LogFormatFieldReader::LogFormatFieldReader( std::shared_ptr<const LogFormatDefinition> format )
    : format_( std::move( format ) )
    , cells_( *format_ )
    , identity_( format_->name() + QChar( '#' ) + fingerprintOf( *format_, cells_.fieldNames() ) )
{
}

QString LogFormatFieldReader::identity() const
{
    return identity_;
}

QStringList LogFormatFieldReader::fieldNames() const
{
    return cells_.fieldNames();
}

std::optional<int> LogFormatFieldReader::fieldIndex( const QString& name ) const
{
    const auto index = cells_.fieldNames().indexOf( name );
    if ( index < 0 ) {
        return std::nullopt;
    }
    return static_cast<int>( index );
}

bool LogFormatFieldReader::readFields( const QString& line, QStringList& fields ) const
{
    // What TableRowCells::rowOf() puts in the cells: each field's value when
    // the Log Format matches the Log Line, no field at all when it does not.
    const auto extracted = cells_.extractor().extractFields( line );
    if ( !extracted.isValid() ) {
        return false;
    }
    const auto& names = cells_.fieldNames();
    fields.resize( names.size() );
    for ( qsizetype field = 0; field < names.size(); ++field ) {
        fields[ field ] = extracted.value( names[ field ] );
    }
    return true;
}

std::optional<std::pair<qsizetype, qsizetype>> LogFormatFieldReader::fieldSpan( const QString& line,
                                                                                int field ) const
{
    const auto& names = cells_.fieldNames();
    if ( field < 0 || field >= names.size() ) {
        return std::nullopt;
    }
    const auto spans = cells_.extractor().fieldSpans( line );
    if ( !spans ) {
        return std::nullopt;
    }
    const auto span = spans->constFind( names[ field ] );
    if ( span == spans->cend() ) {
        return std::nullopt;
    }
    return std::make_pair( static_cast<qsizetype>( span->first ),
                           static_cast<qsizetype>( span->second ) );
}
