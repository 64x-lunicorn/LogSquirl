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

#include "logfieldextractor.h"

#include "jsonlogline.h"

LogFieldExtractor::LogFieldExtractor( const LogFormatDefinition& format )
    : format_( format )
{
    if ( format_.kind() == LogFormatKind::Json ) {
        // Every column is read, and the special fields even when they are not one
        jsonFields_ = columnNames();
        for ( const auto& special :
              { format_.timestampField(), format_.levelField(), format_.bodyField(),
                format_.threadIdField(), format_.opidField() } ) {
            if ( !special.isEmpty() && !jsonFields_.contains( special ) ) {
                jsonFields_ << special;
            }
        }
        return;
    }

    // Pre-compile all regex patterns from the format definition
    const auto& patterns = format_.regexPatterns();
    compiledPatterns_.reserve( patterns.size() );
    for ( const auto& patternStr : patterns ) {
        QRegularExpression re( patternStr );
        if ( !re.isValid() ) {
            continue;
        }

        CompiledPattern compiled;
        const auto groupNames = re.namedCaptureGroups();
        for ( int i = 1; i < groupNames.size(); ++i ) {
            const auto& name = groupNames[ i ];
            if ( name.isEmpty() ) {
                continue;
            }
            const auto isShared = groupNames.count( name ) > 1;
            compiled.namedGroups.append( { name, isShared ? -1 : i } );
        }
        compiled.regex = std::move( re );
        compiledPatterns_.append( std::move( compiled ) );
    }
}

QStringList LogFieldExtractor::columnNames() const
{
    QStringList columns;

    const auto& tsField = format_.timestampField();
    const auto& lvlField = format_.levelField();
    const auto& bodyField = format_.bodyField();
    const auto& threadField = format_.threadIdField();
    const auto& opidField = format_.opidField();
    const auto& valueDefs = format_.valueDefinitions();
    const auto& fieldOrder = format_.valueFieldOrder();

    if ( !fieldOrder.isEmpty() ) {
        // fieldOrder contains ALL capture group names in regex order.
        // Include each field if it is a known special field or a non-hidden value definition.
        for ( const auto& fieldName : fieldOrder ) {
            // Skip hidden value fields
            auto valIt = valueDefs.find( fieldName );
            if ( valIt != valueDefs.end() && valIt.value().hidden ) {
                continue;
            }

            // Accept special fields and value definitions
            const bool isSpecial
                = ( fieldName == tsField || fieldName == lvlField || fieldName == bodyField
                    || fieldName == threadField || fieldName == opidField );
            const bool isValueDef = ( valIt != valueDefs.end() );

            if ( isSpecial || isValueDef ) {
                columns << fieldName;
            }
        }

        // Append thread/opid if defined but not present in the regex capture groups
        if ( !threadField.isEmpty() && !columns.contains( threadField ) ) {
            // Insert before body if body is last
            const auto bodyIdx = columns.indexOf( bodyField );
            if ( bodyIdx >= 0 ) {
                columns.insert( bodyIdx, threadField );
            }
            else {
                columns << threadField;
            }
        }
        if ( !opidField.isEmpty() && !columns.contains( opidField ) ) {
            const auto bodyIdx = columns.indexOf( bodyField );
            if ( bodyIdx >= 0 ) {
                columns.insert( bodyIdx, opidField );
            }
            else {
                columns << opidField;
            }
        }
    }
    else {
        // Fallback: timestamp, level first, then sorted value fields, then body last
        if ( !tsField.isEmpty() ) {
            columns << tsField;
        }
        if ( !lvlField.isEmpty() ) {
            columns << lvlField;
        }

        QStringList valueFieldNames;
        for ( auto it = valueDefs.begin(); it != valueDefs.end(); ++it ) {
            const auto& fieldName = it.key();
            if ( fieldName != tsField && fieldName != lvlField && fieldName != bodyField
                 && !it.value().hidden ) {
                valueFieldNames << fieldName;
            }
        }
        valueFieldNames.sort();
        columns << valueFieldNames;

        if ( !threadField.isEmpty() && !columns.contains( threadField ) ) {
            columns << threadField;
        }
        if ( !opidField.isEmpty() && !columns.contains( opidField ) ) {
            columns << opidField;
        }
        if ( !bodyField.isEmpty() ) {
            columns << bodyField;
        }
    }

    return columns;
}

ExtractedFields LogFieldExtractor::extractFields( const QString& line ) const
{
    switch ( format_.kind() ) {
    case LogFormatKind::Json:
        return extractJsonFields( line );
    case LogFormatKind::Regex:
        break;
    }
    return extractRegexFields( line );
}

ExtractedFields LogFieldExtractor::extractJsonFields( const QString& line ) const
{
    // Valid even when the line is no JSON object: it is a Row with empty fields
    ExtractedFields result;
    result.setValid( true );

    const auto object = JsonLogLine::parse( line );
    if ( !object ) {
        return result;
    }

    for ( const auto& field : jsonFields_ ) {
        const auto value = JsonLogLine::valueAt( *object, field );
        if ( field == format_.timestampField() && value.isDouble() ) {
            result.setValue(
                field, JsonLogLine::epochCellText( value.toDouble(), format_.timestampDivisor() ) );
        }
        else {
            result.setValue( field, JsonLogLine::cellText( value ) );
        }
    }
    return result;
}

ExtractedFields LogFieldExtractor::extractRegexFields( const QString& line ) const
{
    ExtractedFields result;

    for ( const auto& pattern : compiledPatterns_ ) {
        auto match = pattern.regex.match( line );
        if ( match.hasMatch() ) {
            result.setValid( true );

            for ( const auto& [ name, index ] : pattern.namedGroups ) {
                result.setValue( name,
                                 index >= 0 ? match.captured( index ) : match.captured( name ) );
            }
            return result;
        }
    }

    // No pattern matched
    return result;
}
