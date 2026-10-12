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

#include "regexlab.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#include <QLatin1StringView>
#include <QRegularExpression>
#include <QStringList>

#include "regularexpression.h"

namespace regexlab {

namespace {

using Clock = std::chrono::steady_clock;

// The pattern as the Search compiles it for QRegularExpression, with its
// groups capturing: the Search leaves them out where it can (#336), the Lab
// shows them.
QRegularExpression capturing( const RegularExpressionPattern& pattern )
{
    auto regexp = static_cast<QRegularExpression>( pattern );
    regexp.setPatternOptions( regexp.patternOptions() & ~QRegularExpression::DontCaptureOption );
    return regexp;
}

// A sub-pattern of a logical combination, read with the combination's options
// the way the Search reads it.
RegularExpressionPattern subPattern( const RegularExpressionPattern& combination,
                                     const QString& text )
{
    return RegularExpressionPattern( text, combination.isCaseSensitive, false, false,
                                     combination.isPlainText );
}

// What marks the matches of one (sub-)pattern in a line: a Field Term
// (#798) marks within the text of its field, where the Log Format places
// it in the line; a sub-pattern matched on the whole line marks anywhere.
struct Marking {
    QRegularExpression regexp;
    // The field read, -1 for the whole line.
    int field = -1;
};

// What marks the matches in a line: the pattern, or each sub-pattern of a
// logical combination in the order written.
logsquirl::vector<Marking> markings( const RegularExpressionPattern& pattern )
{
    logsquirl::vector<Marking> marking;
    if ( !pattern.isBoolean ) {
        marking.push_back( { capturing( pattern ), -1 } );
        return marking;
    }
    const auto fields = logicalFieldTerms( pattern.pattern );
    qsizetype index = 0;
    for ( const auto& text : logicalSubPatterns( pattern.pattern ) ) {
        int field = -1;
        if ( index < fields.size() && !fields[ index ].isEmpty() && pattern.fieldReader ) {
            field = pattern.fieldReader->fieldIndex( fields[ index ] ).value_or( -1 );
        }
        marking.push_back( { capturing( subPattern( pattern, text ) ), field } );
        ++index;
    }
    return marking;
}

// Where a message of the regex engines names the index of the error: Vectorscan
// says "... at index 5", or "... started at index 2".
qsizetype indexNamedIn( const QString& message )
{
    static const QRegularExpression atIndex( QStringLiteral( R"(at index (\d+))" ) );
    const auto match = atIndex.match( message );
    if ( !match.hasMatch() ) {
        return -1;
    }
    bool isNumber = false;
    const auto index = match.captured( 1 ).toLongLong( &isNumber );
    return isNumber ? static_cast<qsizetype>( index ) : -1;
}

qsizetype regexpErrorPosition( const RegularExpressionPattern& pattern, const QString& message )
{
    const auto regexp = static_cast<QRegularExpression>( pattern );
    if ( !regexp.isValid() ) {
        return regexp.patternErrorOffset();
    }
    const auto index = indexNamedIn( message );
    return index <= pattern.pattern.size() ? index : -1;
}

// A quote after an odd run of backslashes is part of a sub-pattern, as the
// Search reads a logical combination.
bool isEscapedQuote( const QString& text, qsizetype quote )
{
    qsizetype backslashes = 0;
    while ( quote - backslashes > 0 && text[ quote - backslashes - 1 ] == QChar( '\\' ) ) {
        ++backslashes;
    }
    return backslashes % 2 == 1;
}

// The length of the field name and colon of a Field Term written at index
// (`Name:"...`, #798), 0 when none is written there.
qsizetype fieldTermAt( const QString& text, qsizetype index )
{
    auto end = index;
    while ( end < text.size() && isFieldNameChar( text[ end ] ) ) {
        ++end;
    }
    if ( end == index || end + 1 >= text.size() || text[ end ] != QChar( ':' )
         || text[ end + 1 ] != QChar( '"' ) ) {
        return 0;
    }
    return end + 1 - index;
}

// The length of the operator the combination has at index, 0 when it has none.
qsizetype operatorAt( const QString& text, qsizetype index )
{
    static constexpr std::array<QLatin1StringView, 7> Words{
        QLatin1StringView( "nand" ), QLatin1StringView( "xnor" ), QLatin1StringView( "and" ),
        QLatin1StringView( "nor" ),  QLatin1StringView( "xor" ),  QLatin1StringView( "not" ),
        QLatin1StringView( "or" ),
    };
    const auto character = text[ index ];
    if ( character == QChar( ' ' ) || character == QChar( '!' ) || character == QChar( '|' )
         || character == QChar( '&' ) ) {
        return 1;
    }
    for ( const auto word : Words ) {
        if ( QStringView( text ).mid( index ).startsWith( word, Qt::CaseInsensitive ) ) {
            return word.size();
        }
    }
    return 0;
}

// Where a logical combination goes wrong: a quote left open, something
// outside quotes that is no operator, a parenthesis without its partner, or
// the place in a sub-pattern that does not compile.
qsizetype combinationErrorPosition( const RegularExpressionPattern& combination )
{
    const auto& text = combination.pattern;

    struct Quoted {
        qsizetype open;
        qsizetype close;
    };
    logsquirl::vector<Quoted> quoted;
    logsquirl::vector<qsizetype> openParentheses;
    std::optional<qsizetype> openQuote;

    for ( qsizetype index = 0; index < text.size(); ) {
        const auto character = text[ index ];
        if ( character == QChar( '"' ) && !isEscapedQuote( text, index ) ) {
            if ( openQuote.has_value() ) {
                quoted.push_back( { *openQuote, index } );
                openQuote.reset();
            }
            else {
                openQuote = index;
            }
            ++index;
            continue;
        }
        if ( openQuote.has_value() ) {
            ++index;
            continue;
        }
        if ( character == QChar( '(' ) ) {
            openParentheses.push_back( index );
            ++index;
            continue;
        }
        if ( character == QChar( ')' ) ) {
            if ( openParentheses.empty() ) {
                return index;
            }
            openParentheses.pop_back();
            ++index;
            continue;
        }
        // A Field Term's field name is no operator: the quote after its
        // colon opens its sub-pattern.
        const auto length = std::max( fieldTermAt( text, index ), operatorAt( text, index ) );
        if ( length == 0 ) {
            return index;
        }
        index += length;
    }

    if ( openQuote.has_value() ) {
        return *openQuote;
    }
    if ( !openParentheses.empty() ) {
        return openParentheses.front();
    }
    if ( quoted.empty() ) {
        return 0;
    }

    const auto subPatterns = logicalSubPatterns( text );
    if ( static_cast<std::size_t>( subPatterns.size() ) != quoted.size() ) {
        return -1;
    }
    for ( std::size_t i = 0; i < quoted.size(); ++i ) {
        const auto regexp = static_cast<QRegularExpression>(
            subPattern( combination, subPatterns[ static_cast<qsizetype>( i ) ] ) );
        if ( !regexp.isValid() ) {
            return std::min( quoted[ i ].open + 1 + regexp.patternErrorOffset(),
                             quoted[ i ].close );
        }
    }
    return -1;
}

// Where the Field Term the message names begins, -1 when it names none: a
// Field Term's error names its field in quotes (#798).
qsizetype fieldTermErrorPosition( const RegularExpressionPattern& combination,
                                  const QString& message )
{
    for ( const auto& field : logicalFieldTerms( combination.pattern ) ) {
        if ( field.isEmpty() || !message.contains( QChar( '"' ) + field + QChar( '"' ) ) ) {
            continue;
        }
        const auto position = combination.pattern.indexOf( field + QStringLiteral( ":\"" ) );
        if ( position >= 0 ) {
            return position;
        }
    }
    return -1;
}

PatternError errorOf( const RegularExpressionPattern& pattern, const QString& message )
{
    if ( !pattern.isBoolean ) {
        return PatternError{ message, regexpErrorPosition( pattern, message ) };
    }
    auto position = combinationErrorPosition( pattern );
    if ( position < 0 ) {
        position = fieldTermErrorPosition( pattern, message );
    }
    return PatternError{ message, position };
}

// The capture groups of the first match of a (sub-)pattern; offset is where
// in the line the text matched starts (a Field Term's field).
void addGroups( const QRegularExpression& regexp, const QRegularExpressionMatch& match,
                int subPatternIndex, qsizetype offset, logsquirl::vector<CaptureGroup>& groups )
{
    const auto names = regexp.namedCaptureGroups();
    for ( int number = 0; number <= regexp.captureCount(); ++number ) {
        CaptureGroup group;
        group.subPattern = subPatternIndex;
        group.number = number;
        if ( number < names.size() ) {
            group.name = names[ number ];
        }
        group.start = match.capturedStart( number );
        if ( group.start >= 0 ) {
            group.start += offset;
            group.text = match.captured( number );
        }
        groups.push_back( std::move( group ) );
    }
}

// The matches that may still be marked in the sample.
struct MarkBudget {
    std::size_t left = 0;
    bool isCut = false;
};

// Marks a span of the line, unless the marks run out.
bool addMark( LineResult& result, MatchSpan span, const Bounds& bounds, MarkBudget& marks )
{
    if ( marks.left == 0 || result.matches.size() == bounds.maxMarksPerLine ) {
        marks.isCut = true;
        return false;
    }
    result.matches.push_back( span );
    --marks.left;
    return true;
}

LineResult evaluateLine( const PatternMatcher& matcher, const logsquirl::vector<Marking>& marking,
                         const FieldReader* fieldReader, const LineDecision& decision,
                         qsizetype subPatternCount, const QString& wholeLine, const Bounds& bounds,
                         MarkBudget& marks )
{
    const auto started = Clock::now();
    LineResult result;

    std::optional<logsquirl::vector<MatchSpan>> decided;
    if ( decision ) {
        decided = decision( wholeLine );
        result.isMatch = decided.has_value();
        result.isSlow = Clock::now() - started > bounds.slowLine;
    }
    else {
        // The Search matches the whole line's text as UTF-8 (see
        // filterLines()).
        const auto utf8 = wholeLine.toUtf8();
        const std::string_view text( utf8.constData(), static_cast<std::size_t>( utf8.size() ) );
        const auto verdict = matcher.decide( text );
        result.isMatch = verdict.isMatch;
        // What the Search spends on the line is what tells a slow one: the
        // scans below are the Lab's own. A line the engine gave up on is
        // slow however fast it gave up (#689).
        result.isSlow = Clock::now() - started > bounds.slowLine || verdict.isUndecided;
        // Which sub-patterns of a combination match, from the same engine the
        // verdict comes from.
        if ( subPatternCount > 0 ) {
            result.subPatternMatches = matcher.subPatternMatches( text );
            result.subPatternMatches.resize( static_cast<std::size_t>( subPatternCount ), false );
        }
    }

    // Only what is shown is marked.
    const auto line = cutLine( wholeLine, bounds.maxLineLength );
    result.isCut = line.size() < wholeLine.size();

    for ( std::size_t index = 0; index < marking.size(); ++index ) {
        const auto& regexp = marking[ index ].regexp;
        const auto subPatternIndex = static_cast<int>( index );
        // A Field Term is matched on its field's text, where the line shows
        // it; a field the Log Format places nowhere in the line (JSON,
        // logfmt), or a line it does not parse, marks nothing.
        qsizetype offset = 0;
        QString text = line;
        if ( marking[ index ].field >= 0 ) {
            const auto span = fieldReader != nullptr
                                  ? fieldReader->fieldSpan( wholeLine, marking[ index ].field )
                                  : std::nullopt;
            if ( !span.has_value() || span->first >= line.size() ) {
                continue;
            }
            offset = span->first;
            text = line.mid( span->first, span->second - span->first );
        }
        auto matches = regexp.globalMatch( text );
        bool isFirst = true;
        while ( matches.hasNext() ) {
            const auto match = matches.next();
            if ( isFirst ) {
                addGroups( regexp, match, subPatternIndex, offset, result.groups );
                isFirst = false;
            }
            if ( decision ) {
                // The decision marks; only the groups of the first match are
                // wanted.
                break;
            }
            if ( match.capturedLength() > 0
                 && !addMark(
                     result,
                     { match.capturedStart() + offset, match.capturedLength(), subPatternIndex },
                     bounds, marks ) ) {
                break;
            }
        }
    }

    // What the decision marks, as far as the line is shown.
    if ( decided.has_value() ) {
        for ( const auto& span : *decided ) {
            const auto length = std::min( span.start + span.length, line.size() ) - span.start;
            if ( span.start < 0 || length <= 0 ) {
                continue;
            }
            if ( !addMark( result, { span.start, length, span.subPattern }, bounds, marks ) ) {
                break;
            }
        }
    }

    std::ranges::sort( result.matches, []( const MatchSpan& left, const MatchSpan& right ) {
        return std::tie( left.start, left.subPattern ) < std::tie( right.start, right.subPattern );
    } );
    return result;
}

} // namespace

QString cutLine( const QString& line, qsizetype length )
{
    if ( line.size() <= length ) {
        return line;
    }
    if ( length > 0 && line[ length - 1 ].isHighSurrogate() ) {
        --length;
    }
    return line.left( std::max<qsizetype>( length, 0 ) );
}

Result evaluate( const RegularExpressionPattern& pattern, RegexpEngine engine,
                 const logsquirl::vector<QString>& sample, const Bounds& bounds,
                 const std::atomic<bool>& cancelled, const LineDecision& decision )
{
    const auto started = Clock::now();
    const auto elapsed = [ started ]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>( Clock::now() - started );
    };

    Result result;
    result.sampleLines = std::min( sample.size(), bounds.maxLines );

    // The Search's own matcher, compiled as a Search compiles it
    // (SearchSession::request()).
    const RegularExpression expression( pattern, engine );
    if ( !expression.isValid() ) {
        result.error = errorOf( pattern, expression.errorString() );
        result.elapsed = elapsed();
        return result;
    }
    const auto matcher = expression.createMatcher();
    const auto marking = markings( pattern );
    if ( pattern.isBoolean ) {
        result.subPatterns = logicalSubPatterns( pattern.pattern );
        result.subPatternFields = logicalFieldTerms( pattern.pattern );
    }
    MarkBudget marks{ bounds.maxMarks };

    result.lines.reserve( result.sampleLines );
    for ( std::size_t index = 0; index < result.sampleLines; ++index ) {
        if ( cancelled.load() ) {
            result.stop = Stop::Cancelled;
            break;
        }
        if ( index > 0 && elapsed() >= bounds.timeLimit ) {
            result.stop = Stop::TimeLimit;
            break;
        }
        result.lines.push_back( evaluateLine( *matcher, marking, pattern.fieldReader.get(),
                                              decision, result.subPatterns.size(), sample[ index ],
                                              bounds, marks ) );
        const auto& line = result.lines.back();
        result.matchingLines += line.isMatch ? 1 : 0;
        result.slowLines += line.isSlow ? 1 : 0;
    }

    result.elapsed = elapsed();
    result.isMarkingCut = marks.isCut;
    result.isSlow = result.elapsed >= bounds.slowThreshold || result.stop == Stop::TimeLimit
                    || result.slowLines > 0;
    return result;
}

} // namespace regexlab
