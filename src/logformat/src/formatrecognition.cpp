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
#include "jsonlogline.h"
#include "linetypes.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QVector>

#include <algorithm>
#include <optional>

namespace {

// Minimum fraction of lines that must match a format for it to be accepted.
constexpr double MinMatchRatio = 0.5;

// How well one Log Format fits the sample Log Lines. Each kind of Log Format
// scores only the Log Lines of its own kind.
struct FormatScore {
    std::shared_ptr<const LogFormatDefinition> format;
    int matchCount = 0;
    int specificity = 0; // more capture groups (regex) or fields (JSON) = more specific
};

// A regex format: how many of the Log Lines match at least one of its patterns.
std::optional<FormatScore>
scoreRegexFormat( const std::shared_ptr<const LogFormatDefinition>& format,
                  const QStringList& lines )
{
    // Compile all patterns for this format
    QVector<QRegularExpression> compiledPatterns;
    int maxGroups = 0;
    for ( const auto& patternStr : format->regexPatterns() ) {
        QRegularExpression re( patternStr );
        if ( re.isValid() ) {
            maxGroups = std::max( maxGroups, re.captureCount() );
            compiledPatterns.append( std::move( re ) );
        }
    }

    if ( compiledPatterns.isEmpty() ) {
        return std::nullopt;
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

    if ( matchCount == 0 ) {
        return std::nullopt;
    }
    return FormatScore{ format, matchCount, maxGroups };
}

// A JSON format: how many of the JSON objects contain its timestamp field.
std::optional<FormatScore>
scoreJsonFormat( const std::shared_ptr<const LogFormatDefinition>& format,
                 const QVector<QJsonObject>& objects )
{
    const auto& timestampField = format->timestampField();
    if ( timestampField.isEmpty() ) {
        return std::nullopt;
    }

    int matchCount = 0;
    for ( const auto& object : objects ) {
        const auto value = JsonLogLine::valueAt( object, timestampField );
        if ( !value.isUndefined() && !value.isNull() ) {
            ++matchCount;
        }
    }

    if ( matchCount == 0 ) {
        return std::nullopt;
    }
    return FormatScore{ format, matchCount, static_cast<int>( format->valueFieldOrder().size() ) };
}

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

    const bool hasJsonFormat
        = std::any_of( allFormats.begin(), allFormats.end(),
                       []( const auto& format ) { return format->kind() == LogFormatKind::Json; } );

    // A Log Line that is a JSON object is scored against the JSON formats only,
    // every other Log Line against the regex formats only. Without a JSON
    // format in the Catalog nothing is set apart, and no line is parsed.
    QVector<QJsonObject> jsonObjects;
    QStringList otherLines;
    if ( hasJsonFormat ) {
        for ( const auto& line : lines ) {
            if ( auto object = JsonLogLine::parse( line ) ) {
                jsonObjects.append( std::move( *object ) );
            }
            else {
                otherLines.append( line );
            }
        }
    }
    const auto& regexLines = hasJsonFormat ? otherLines : lines;

    QVector<FormatScore> scores;
    scores.reserve( allFormats.size() );

    for ( auto it = allFormats.begin(); it != allFormats.end(); ++it ) {
        const auto& format = it.value();
        const auto score = format->kind() == LogFormatKind::Json
                               ? scoreJsonFormat( format, jsonObjects )
                               : scoreRegexFormat( format, regexLines );
        if ( score ) {
            scores.append( *score );
        }
    }

    if ( scores.isEmpty() ) {
        return nullptr;
    }

    // Sort by: (1) match count descending, (2) specificity descending (more specific)
    std::sort( scores.begin(), scores.end(), []( const FormatScore& a, const FormatScore& b ) {
        if ( a.matchCount != b.matchCount ) {
            return a.matchCount > b.matchCount;
        }
        return a.specificity > b.specificity;
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
