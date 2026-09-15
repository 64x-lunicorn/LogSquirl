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

#include "formatrecognition.h"

#include "abstractlogdata.h"
#include "linetypes.h"

#include <QRegularExpression>
#include <QVector>

#include <algorithm>

namespace {

// Minimum fraction of lines that must match a format for it to be accepted.
constexpr double MinMatchRatio = 0.5;

std::shared_ptr<const LogFormatDefinition> bestMatch( const QStringList& lines,
                                                      const LogFormatCatalog& catalog )
{
    if ( lines.isEmpty() ) {
        return nullptr;
    }

    const auto& allFormats = catalog.allFormats();
    if ( allFormats.isEmpty() ) {
        return nullptr;
    }

    // For each format, compile all its regex patterns and count how many lines match.
    struct FormatScore {
        std::shared_ptr<const LogFormatDefinition> format;
        int matchCount = 0;
        int captureGroupCount = 0; // specificity: more groups = more specific
    };

    QVector<FormatScore> scores;
    scores.reserve( allFormats.size() );

    for ( auto it = allFormats.begin(); it != allFormats.end(); ++it ) {
        const auto& format = it.value();
        const auto& patterns = format->regexPatterns();

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
            scores.append( { format, matchCount, maxGroups } );
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
    const double ratio
        = static_cast<double>( best.matchCount ) / static_cast<double>( lines.size() );

    if ( ratio < MinMatchRatio ) {
        return nullptr;
    }

    return best.format;
}

} // namespace

namespace FormatRecognition {

std::shared_ptr<const LogFormatDefinition> recognize( const AbstractLogData& logFile,
                                                      const RecognitionPolicy& policy,
                                                      const LogFormatCatalog& catalog )
{
    if ( !policy.enabled ) {
        return nullptr;
    }

    const auto sampleCount = std::min( logFile.getNbLine().get(),
                                       static_cast<LinesCount::UnderlyingType>( SampleDepth ) );
    if ( sampleCount == 0 ) {
        return nullptr;
    }

    QStringList sampleLines;
    sampleLines.reserve( static_cast<qsizetype>( sampleCount ) );
    for ( const auto& line : logFile.getLines( 0_lnum, LinesCount( sampleCount ) ) ) {
        sampleLines << line;
    }

    return bestMatch( sampleLines, catalog );
}

std::shared_ptr<const LogFormatDefinition> recognize( const QStringList& firstLogLines,
                                                      const RecognitionPolicy& policy,
                                                      const LogFormatCatalog& catalog )
{
    if ( !policy.enabled ) {
        return nullptr;
    }

    return bestMatch( firstLogLines.mid( 0, SampleDepth ), catalog );
}

} // namespace FormatRecognition
