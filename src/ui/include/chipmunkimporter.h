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
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "highlighterset.h"
#include "predefinedfilters.h"

namespace logsquirl {
namespace chipmunk {

// Represents a single filter entry extracted from a Chipmunk JSON export.
struct ChipmunkFilter {
    QString groupName;
    QString pattern;
    bool useRegex = true;
    bool ignoreCase = true;
    bool active = true;
    QColor foreColor{ Qt::white };
    QColor backColor{ Qt::black };
};

// Extract filters from a single group's content object (has "n", "e" keys).
inline void parseGroupContent( const QJsonObject& contentObj, QList<ChipmunkFilter>& result )
{
    const auto groupName = contentObj[ "n" ].toString();
    const auto entriesArray = contentObj[ "e" ].toArray();

    for ( const auto& entryVal : entriesArray ) {
        const auto entryObj = entryVal.toObject();

        // The "filters" field is a JSON-encoded string
        const auto filtersStr = entryObj[ "filters" ].toString();
        const auto filtersDoc = QJsonDocument::fromJson( filtersStr.toUtf8() );
        if ( !filtersDoc.isObject() ) {
            continue;
        }

        const auto filtersObj = filtersDoc.object();
        const auto filterObj = filtersObj[ "filter" ].toObject();
        const auto flagsObj = filterObj[ "flags" ].toObject();

        ChipmunkFilter filter;
        filter.groupName = groupName;
        filter.pattern = filterObj[ "filter" ].toString();
        filter.useRegex = flagsObj[ "reg" ].toBool( true );
        filter.ignoreCase = !flagsObj[ "cases" ].toBool( false );
        filter.active = filtersObj[ "active" ].toBool( true );

        // Parse colors from the "colors" object
        const auto colorsObj = filtersObj[ "colors" ].toObject();
        const auto fgStr = colorsObj[ "color" ].toString();
        const auto bgStr = colorsObj[ "background" ].toString();

        if ( !fgStr.isEmpty() ) {
            filter.foreColor = QColor( fgStr );
        }
        if ( !bgStr.isEmpty() ) {
            filter.backColor = QColor( bgStr );
        }

        if ( !filter.pattern.isEmpty() ) {
            result.append( filter );
        }
    }
}

// Parse groups from a "c" array where each element has a "content" JSON string.
inline void parseGroupsArray( const QJsonArray& groupsArray, QList<ChipmunkFilter>& result )
{
    for ( const auto& groupVal : groupsArray ) {
        const auto groupObj = groupVal.toObject();

        const auto contentStr = groupObj[ "content" ].toString();
        const auto contentDoc = QJsonDocument::fromJson( contentStr.toUtf8() );
        if ( !contentDoc.isObject() ) {
            continue;
        }

        parseGroupContent( contentDoc.object(), result );
    }
}

// Parse a Chipmunk JSON export file into a flat list of filter entries.
// Supports two Chipmunk export formats:
//   Format A (simple):  { "c": [ { "content": "..." }, ... ] }
//   Format B (wrapped): [ { "uuid": "...", "content": "{ \"c\": [...], ... }" } ]
// In both cases, the inner "content" strings are parsed recursively
// to reach the filter entries.
// Returns an empty list if the JSON is malformed or contains no filters.
inline QList<ChipmunkFilter> parseChipmunkJson( const QByteArray& jsonData )
{
    QList<ChipmunkFilter> result;

    const auto topDoc = QJsonDocument::fromJson( jsonData );

    if ( topDoc.isObject() ) {
        // Format A: top-level object with "c" array of groups
        parseGroupsArray( topDoc.object()[ "c" ].toArray(), result );
    }
    else if ( topDoc.isArray() ) {
        // Format B: top-level array of wrapper objects with "content" strings
        for ( const auto& wrapperVal : topDoc.array() ) {
            const auto wrapperObj = wrapperVal.toObject();
            const auto innerStr = wrapperObj[ "content" ].toString();
            const auto innerDoc = QJsonDocument::fromJson( innerStr.toUtf8() );
            if ( !innerDoc.isObject() ) {
                continue;
            }

            const auto innerObj = innerDoc.object();

            // The inner object may have "c" (filter groups) directly
            if ( innerObj.contains( "c" ) ) {
                const auto cVal = innerObj[ "c" ];
                if ( cVal.isArray() ) {
                    parseGroupsArray( cVal.toArray(), result );
                }
            }

            // Or the inner object may itself have "e" (entries) directly
            if ( innerObj.contains( "e" ) ) {
                parseGroupContent( innerObj, result );
            }
        }
    }

    return result;
}

// Convert parsed Chipmunk filters to PredefinedFilter list.
// Each filter gets its group name prefixed for readability.
inline QList<PredefinedFilter> toFilters( const QList<ChipmunkFilter>& chipmunkFilters )
{
    QList<PredefinedFilter> result;
    result.reserve( chipmunkFilters.size() );

    for ( const auto& cf : chipmunkFilters ) {
        PredefinedFilter pf;
        pf.name = cf.groupName.isEmpty() ? cf.pattern : cf.groupName + ": " + cf.pattern;
        pf.pattern = cf.pattern;
        pf.useRegex = cf.useRegex;
        result.append( pf );
    }

    return result;
}

// Convert parsed Chipmunk filters into a HighlighterSet with individual Highlighter entries.
// Each filter becomes a Highlighter with the corresponding colors.
inline HighlighterSet toHighlighterSet( const QList<ChipmunkFilter>& chipmunkFilters,
                                        const QString& setName )
{
    auto set = HighlighterSet::createNewSet( setName );

    for ( const auto& cf : chipmunkFilters ) {
        Highlighter h( cf.pattern, cf.ignoreCase, false, cf.foreColor, cf.backColor );
        h.setUseRegex( cf.useRegex );
        set.addHighlighter( h );
    }

    return set;
}

} // namespace chipmunk
} // namespace logsquirl
