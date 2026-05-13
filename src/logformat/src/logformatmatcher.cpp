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

#include "logformatmatcher.h"

#include <QRegularExpression>

#include <algorithm>

// Minimum fraction of lines that must match a format for it to be accepted.
static constexpr double MinMatchRatio = 0.5;

LogFormatMatcher::LogFormatMatcher( const LogFormatRegistry& registry )
    : registry_( registry )
{
}

const LogFormatDefinition* LogFormatMatcher::detectFormat( const QStringList& lines ) const
{
    if ( lines.isEmpty() ) {
        return nullptr;
    }

    const auto& allFormats = registry_.allFormats();
    if ( allFormats.isEmpty() ) {
        return nullptr;
    }

    // For each format, compile all its regex patterns and count how many lines match.
    // Track: format name -> (match count, number of named capture groups in matching pattern)
    struct FormatScore {
        const LogFormatDefinition* format = nullptr;
        int matchCount = 0;
        int captureGroupCount = 0; // specificity: more groups = more specific
    };

    QVector<FormatScore> scores;
    scores.reserve( allFormats.size() );

    for ( auto it = allFormats.begin(); it != allFormats.end(); ++it ) {
        const auto& format = it.value();
        const auto& patterns = format.regexPatterns();

        // Compile all patterns for this format
        QVector<QRegularExpression> compiledPatterns;
        int maxGroups = 0;
        for ( const auto& patternStr : patterns ) {
            QRegularExpression re( patternStr );
            if ( re.isValid() ) {
                maxGroups = std::max( maxGroups, re.captureCount() );
                compiledPatterns.append( std::move( re ) );
            }
        }

        if ( compiledPatterns.isEmpty() ) {
            continue;
        }

        // Count how many lines match at least one pattern
        int matchCount = 0;
        for ( const auto& line : lines ) {
            for ( const auto& re : compiledPatterns ) {
                if ( re.match( line ).hasMatch() ) {
                    ++matchCount;
                    break; // one pattern matching is enough
                }
            }
        }

        if ( matchCount > 0 ) {
            scores.append( { &format, matchCount, maxGroups } );
        }
    }

    if ( scores.isEmpty() ) {
        return nullptr;
    }

    // Sort by: (1) match count descending, (2) capture group count descending (more specific)
    std::sort( scores.begin(), scores.end(), []( const FormatScore& a, const FormatScore& b ) {
        if ( a.matchCount != b.matchCount ) {
            return a.matchCount > b.matchCount;
        }
        return a.captureGroupCount > b.captureGroupCount;
    } );

    // Check if the best candidate passes the minimum threshold
    const auto& best = scores.first();
    const double ratio = static_cast<double>( best.matchCount ) / static_cast<double>( lines.size() );

    if ( ratio < MinMatchRatio ) {
        return nullptr;
    }

    return best.format;
}
