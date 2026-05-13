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

LogFieldExtractor::LogFieldExtractor( const LogFormatDefinition& format, int cacheCapacity )
    : format_( format )
    , cacheCapacity_( cacheCapacity )
{
    // Pre-compile all regex patterns from the format definition
    const auto& patterns = format_.regexPatterns();
    compiledPatterns_.reserve( patterns.size() );
    for ( const auto& patternStr : patterns ) {
        QRegularExpression re( patternStr );
        if ( re.isValid() ) {
            compiledPatterns_.append( std::move( re ) );
        }
    }
}

ExtractedFields LogFieldExtractor::extractFields( const QString& line, int64_t lineNumber )
{
    // If lineNumber is valid, check the LRU cache
    if ( lineNumber >= 0 ) {
        auto cacheIt = cacheMap_.find( lineNumber );
        if ( cacheIt != cacheMap_.end() ) {
            // Move to front of LRU list (most recently used)
            cacheList_.splice( cacheList_.begin(), cacheList_, cacheIt->second );
            return cacheIt->second->second;
        }
    }

    // Not in cache — perform extraction
    auto result = doExtract( line );

    // Store in cache if lineNumber is valid
    if ( lineNumber >= 0 ) {
        // Evict oldest if cache is full
        if ( static_cast<int>( cacheMap_.size() ) >= cacheCapacity_ ) {
            auto oldest = cacheList_.back().first;
            cacheMap_.erase( oldest );
            cacheList_.pop_back();
        }

        // Insert at front
        cacheList_.emplace_front( lineNumber, result );
        cacheMap_[ lineNumber ] = cacheList_.begin();
    }

    return result;
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
            const bool isSpecial = ( fieldName == tsField || fieldName == lvlField
                                     || fieldName == bodyField || fieldName == threadField
                                     || fieldName == opidField );
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

void LogFieldExtractor::invalidateCache()
{
    cacheList_.clear();
    cacheMap_.clear();
}

ExtractedFields LogFieldExtractor::doExtract( const QString& line ) const
{
    ExtractedFields result;

    for ( const auto& re : compiledPatterns_ ) {
        auto match = re.match( line );
        if ( match.hasMatch() ) {
            result.setValid( true );

            // Extract all named capture groups
            const auto groupNames = re.namedCaptureGroups();
            for ( int i = 1; i < groupNames.size(); ++i ) {
                const auto& name = groupNames[ i ];
                if ( !name.isEmpty() ) {
                    result.setValue( name, match.captured( name ) );
                }
            }
            return result;
        }
    }

    // No pattern matched
    return result;
}
