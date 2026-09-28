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
#include "logfmtlogline.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QVector>

#include <algorithm>
#include <optional>

namespace {

// Minimum fraction of lines that must match a format for it to be accepted.
constexpr double MinMatchRatio = 0.5;

// A compiled regex pattern and the name of each of its capture groups.
struct CompiledPattern {
    QRegularExpression regex;
    QStringList groupNames;
};

// How well one Log Format fits the sample Log Lines. Each kind of Log Format
// scores only the Log Lines of its own kind.
struct FormatScore {
    std::shared_ptr<const LogFormatDefinition> format;
    int matchCount = 0;
    // The fields read from the matching Log Lines, summed over them: for a
    // regex format the named groups that took part in the match, for a JSON
    // or logfmt format the declared fields present in the Log Line.
    int capturedFields = 0;
    // A regex format's valid patterns; empty for the other kinds.
    QVector<CompiledPattern> patterns = {};
};

QVector<CompiledPattern> compilePatterns( const LogFormatDefinition& format )
{
    QVector<CompiledPattern> compiled;
    for ( const auto& patternStr : format.regexPatterns() ) {
        QRegularExpression re( patternStr );
        if ( re.isValid() ) {
            auto groupNames = re.namedCaptureGroups();
            compiled.append( { std::move( re ), std::move( groupNames ) } );
        }
    }
    return compiled;
}

// Whether the patterns of one scored regex format accept at least one of the
// sample lines another Log Format carries (its "sample" section).
bool acceptsSampleOf( const FormatScore& general, const FormatScore& other )
{
    return std::ranges::any_of( other.format->sampleLines(), [ & ]( const auto& sample ) {
        return std::ranges::any_of( general.patterns, [ & ]( const CompiledPattern& pattern ) {
            return pattern.regex.match( sample.line ).hasMatch();
        } );
    } );
}

// A Log Format is more specific than another when the other's patterns accept
// one of its sample lines, and its own patterns accept none of the other's:
// spdlog's lines are Apache error log lines too, but not the other way round.
bool moreSpecific( const FormatScore& candidate, const FormatScore& other )
{
    return acceptsSampleOf( other, candidate ) && !acceptsSampleOf( candidate, other );
}

// How many distinct named fields a match captured; a group that did not take
// part in the match (an optional one, or another alternative) does not count.
int capturedFieldCount( const QRegularExpressionMatch& match, const QStringList& groupNames )
{
    QStringList captured;
    for ( int group = 1; group < groupNames.size(); ++group ) {
        const auto& name = groupNames[ group ];
        if ( !name.isEmpty() && match.hasCaptured( group ) && !captured.contains( name ) ) {
            captured << name;
        }
    }
    return static_cast<int>( captured.size() );
}

// A regex format: how many of the Log Lines match at least one of its
// patterns, and how many fields the best matching pattern captures of each.
std::optional<FormatScore>
scoreRegexFormat( const std::shared_ptr<const LogFormatDefinition>& format,
                  const QStringList& lines )
{
    auto compiledPatterns = compilePatterns( *format );
    if ( compiledPatterns.isEmpty() ) {
        return std::nullopt;
    }

    int matchCount = 0;
    int capturedFields = 0;
    for ( const auto& line : lines ) {
        std::optional<int> bestFields;
        for ( const auto& pattern : compiledPatterns ) {
            const auto match = pattern.regex.match( line );
            if ( match.hasMatch() ) {
                bestFields = std::max( bestFields.value_or( 0 ),
                                       capturedFieldCount( match, pattern.groupNames ) );
            }
        }
        if ( bestFields ) {
            ++matchCount;
            capturedFields += *bestFields;
        }
    }

    if ( matchCount == 0 ) {
        return std::nullopt;
    }
    return FormatScore{ format, matchCount, capturedFields, std::move( compiledPatterns ) };
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

    const auto present = []( const QJsonObject& object, const QString& path ) {
        const auto value = JsonLogLine::valueAt( object, path );
        return !value.isUndefined() && !value.isNull();
    };

    int matchCount = 0;
    int capturedFields = 0;
    for ( const auto& object : objects ) {
        if ( present( object, timestampField ) ) {
            ++matchCount;
            capturedFields += static_cast<int>(
                std::ranges::count_if( format->valueFieldOrder(), [ & ]( const QString& path ) {
                    return present( object, path );
                } ) );
        }
    }

    if ( matchCount == 0 ) {
        return std::nullopt;
    }
    return FormatScore{ format, matchCount, capturedFields };
}

// A logfmt format: how many of the Log Lines read completely as key/value pairs
// and contain its timestamp field as a key. A JSON object is no logfmt line.
std::optional<FormatScore>
scoreLogfmtFormat( const std::shared_ptr<const LogFormatDefinition>& format,
                   const QStringList& lines )
{
    const auto& timestampField = format->timestampField();
    if ( timestampField.isEmpty() ) {
        return std::nullopt;
    }

    int matchCount = 0;
    int capturedFields = 0;
    for ( const auto& line : lines ) {
        const auto pairs = LogfmtLogLine::parse( line );
        if ( pairs && pairs->contains( timestampField ) ) {
            ++matchCount;
            capturedFields += static_cast<int>(
                std::ranges::count_if( format->valueFieldOrder(), [ & ]( const QString& key ) {
                    return pairs->contains( key );
                } ) );
        }
    }

    if ( matchCount == 0 ) {
        return std::nullopt;
    }
    return FormatScore{ format, matchCount, capturedFields };
}

// The best of the scored formats, when it matches enough of the sample lines.
//
// The formats matching the most Log Lines are ranked by (1) how many of their
// rivals are more specific than they are (fewest first), (2) the fields they
// capture from the Log Lines, and (3) their name, so that the answer never
// depends on the Catalog's order.
std::shared_ptr<const LogFormatDefinition> pickBest( const QVector<FormatScore>& scores,
                                                     qsizetype lineCount )
{
    if ( scores.isEmpty() ) {
        return nullptr;
    }

    const auto mostMatched = std::ranges::max( scores, {}, &FormatScore::matchCount ).matchCount;
    const double ratio = static_cast<double>( mostMatched ) / static_cast<double>( lineCount );
    if ( ratio < MinMatchRatio ) {
        return nullptr;
    }

    QVector<const FormatScore*> rivals;
    for ( const auto& score : scores ) {
        if ( score.matchCount == mostMatched ) {
            rivals.append( &score );
        }
    }

    struct Rank {
        int moreSpecificRivals = 0;
        int capturedFields = 0;
        const FormatScore* score = nullptr;
    };
    QVector<Rank> ranks;
    ranks.reserve( rivals.size() );
    for ( const auto* candidate : rivals ) {
        const auto moreSpecificRivals
            = std::ranges::count_if( rivals, [ & ]( const FormatScore* rival ) {
                  return rival != candidate && moreSpecific( *rival, *candidate );
              } );
        ranks.append(
            { static_cast<int>( moreSpecificRivals ), candidate->capturedFields, candidate } );
    }

    const auto best = std::ranges::min( ranks, []( const Rank& a, const Rank& b ) {
        if ( a.moreSpecificRivals != b.moreSpecificRivals ) {
            return a.moreSpecificRivals < b.moreSpecificRivals;
        }
        if ( a.capturedFields != b.capturedFields ) {
            return a.capturedFields > b.capturedFields;
        }
        return a.score->format->name() < b.score->format->name();
    } );
    return best.score->format;
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
    QVector<FormatScore> logfmtScores;
    scores.reserve( allFormats.size() );

    for ( auto it = allFormats.begin(); it != allFormats.end(); ++it ) {
        const auto& format = it.value();
        switch ( format->kind() ) {
        case LogFormatKind::Json:
            if ( const auto score = scoreJsonFormat( format, jsonObjects ) ) {
                scores.append( *score );
            }
            break;
        case LogFormatKind::Logfmt:
            if ( const auto score = scoreLogfmtFormat( format, lines ) ) {
                logfmtScores.append( *score );
            }
            break;
        case LogFormatKind::Regex:
            if ( const auto score = scoreRegexFormat( format, regexLines ) ) {
                scores.append( *score );
            }
            break;
        }
    }

    // A regex (or JSON) format keeps precedence: a logfmt format is only chosen
    // when none of them would have been.
    if ( auto best = pickBest( scores, lines.size() ) ) {
        return best;
    }
    return pickBest( logfmtScores, lines.size() );
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
