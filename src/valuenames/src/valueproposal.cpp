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

#include "valueproposal.h"

#include <algorithm>

#include <QList>

namespace logsquirl::valuenames {

namespace {

bool isWordCharacter( QChar character )
{
    return character.isLetterOrNumber() || character == u'_';
}

bool isDigit( QChar character )
{
    return character >= u'0' && character <= u'9';
}

bool isHexDigit( QChar character )
{
    return isDigit( character ) || ( character >= u'a' && character <= u'f' )
           || ( character >= u'A' && character <= u'F' );
}

bool allOf( QStringView text, bool ( *predicate )( QChar ) )
{
    return !text.isEmpty() && std::all_of( text.begin(), text.end(), predicate );
}

bool hasLetter( QStringView text )
{
    return std::any_of( text.begin(), text.end(), []( QChar c ) { return c.isLetter(); } );
}

// The value a word of the line is, or starts with; none for a word that is
// no number.
std::optional<FoundValue> valueOfWord( QStringView line, LineRange word )
{
    const auto text = line.mid( word.start, word.length );
    if ( text.size() > 2 && text[ 0 ] == u'0' && ( text[ 1 ] == u'x' || text[ 1 ] == u'X' )
         && allOf( text.mid( 2 ), isHexDigit ) ) {
        return FoundValue{ word, FoundValue::Kind::PrefixedHex, false };
    }
    if ( allOf( text, isDigit ) ) {
        return FoundValue{ word, FoundValue::Kind::Decimal, false };
    }
    if ( allOf( text, isHexDigit ) && std::any_of( text.begin(), text.end(), isDigit )
         && hasLetter( text ) ) {
        return FoundValue{ word, FoundValue::Kind::BareHex, false };
    }
    // 12ms: the digits it starts with.
    qsizetype digits = 0;
    while ( digits < text.size() && isDigit( text[ digits ] ) ) {
        ++digits;
    }
    if ( digits > 0 ) {
        return FoundValue{ LineRange{ word.start, digits }, FoundValue::Kind::Decimal, false };
    }
    return std::nullopt;
}

// The words of the line overlapping the range, whole.
QList<LineRange> wordsOverlapping( QStringView line, LineRange range )
{
    QList<LineRange> words;
    qsizetype position = std::max<qsizetype>( range.start, 0 );
    // A word the range starts within starts before it.
    while ( position > 0 && position < line.size() && isWordCharacter( line[ position ] )
            && isWordCharacter( line[ position - 1 ] ) ) {
        --position;
    }
    const auto end = std::min( range.end(), line.size() );
    while ( position < end ) {
        if ( !isWordCharacter( line[ position ] ) ) {
            ++position;
            continue;
        }
        auto wordEnd = position;
        while ( wordEnd < line.size() && isWordCharacter( line[ wordEnd ] ) ) {
            ++wordEnd;
        }
        words.append( LineRange{ position, wordEnd - position } );
        position = wordEnd;
    }
    return words;
}

QString capturePattern( const FoundValue& value )
{
    QString pattern;
    switch ( value.kind ) {
    case FoundValue::Kind::Decimal:
        pattern = QStringLiteral( "\\d+" );
        break;
    case FoundValue::Kind::Fraction:
        pattern = QStringLiteral( "\\d+\\.\\d+" );
        break;
    case FoundValue::Kind::PrefixedHex:
        pattern = QStringLiteral( "0[xX][0-9A-Fa-f]+" );
        break;
    case FoundValue::Kind::BareHex:
        pattern = QStringLiteral( "[0-9A-Fa-f]+" );
        break;
    }
    if ( value.negative ) {
        pattern.prepend( QStringLiteral( "-?" ) );
    }
    return QStringLiteral( "(" ) + pattern + QStringLiteral( ")" );
}

} // namespace

std::optional<FoundValue> valueIn( QStringView line, LineRange range )
{
    if ( range.length <= 0 || range.start < 0 || range.start >= line.size() ) {
        return std::nullopt;
    }

    const auto words = wordsOverlapping( line, range );
    std::optional<FoundValue> last;
    for ( int index = 0; index < words.size(); ++index ) {
        auto value = valueOfWord( line, words[ index ] );
        if ( !value.has_value() ) {
            continue;
        }
        // 3.25: two words of digits around a dot are one value.
        if ( value->kind == FoundValue::Kind::Decimal && value->range == words[ index ]
             && index + 1 < words.size() && words[ index + 1 ].start == value->range.end() + 1
             && line[ value->range.end() ] == u'.'
             && allOf( line.mid( words[ index + 1 ].start, words[ index + 1 ].length ),
                       isDigit ) ) {
            value->kind = FoundValue::Kind::Fraction;
            value->range.length = words[ index + 1 ].end() - value->range.start;
            ++index;
        }
        last = value;
    }
    if ( !last.has_value() ) {
        return std::nullopt;
    }

    // -5, but not the 5 of id-5.
    const auto start = last->range.start;
    if ( ( last->kind == FoundValue::Kind::Decimal || last->kind == FoundValue::Kind::Fraction )
         && start > 0 && line[ start - 1 ] == u'-'
         && ( start == 1 || !isWordCharacter( line[ start - 2 ] ) ) ) {
        last->negative = true;
        last->range.start -= 1;
        last->range.length += 1;
    }
    return last;
}

std::optional<LineRange> valueTextAt( QStringView line, qsizetype column )
{
    if ( column < 0 || column >= line.size() ) {
        return std::nullopt;
    }

    const auto around = [ & ]( bool ( *belongs )( QChar ) ) -> std::optional<LineRange> {
        if ( !belongs( line[ column ] ) ) {
            return std::nullopt;
        }
        auto start = column;
        while ( start > 0 && belongs( line[ start - 1 ] ) ) {
            --start;
        }
        auto end = column + 1;
        while ( end < line.size() && belongs( line[ end ] ) ) {
            ++end;
        }
        const LineRange range{ start, end - start };
        return valueIn( line, range ).has_value() ? std::optional( range ) : std::nullopt;
    };

    if ( const auto word = around( isWordCharacter ) ) {
        return word;
    }
    return around( []( QChar c ) { return !c.isSpace(); } );
}

std::optional<NamingProposal> proposeNaming( QStringView line, LineRange range )
{
    const auto value = valueIn( line, range );
    if ( !value.has_value() ) {
        return std::nullopt;
    }
    const auto valueRange = value->range;

    // Before the value: the range's text, or, when that holds no letter, the
    // word before the value and what separates them.
    auto contextStart = std::min( range.start, valueRange.start );
    if ( !hasLetter( line.mid( contextStart, valueRange.start - contextStart ) ) ) {
        auto start = valueRange.start;
        while ( start > 0 && !isWordCharacter( line[ start - 1 ] ) ) {
            --start;
        }
        while ( start > 0 && isWordCharacter( line[ start - 1 ] ) ) {
            --start;
        }
        contextStart = std::min( contextStart, start );
    }
    const auto before = line.mid( contextStart, valueRange.start - contextStart );
    const auto afterEnd = std::max( std::min( range.end(), line.size() ), valueRange.end() );
    const auto after = line.mid( valueRange.end(), afterEnd - valueRange.end() );

    NamingProposal proposal;
    proposal.value = line.mid( valueRange.start, valueRange.length ).toString();
    proposal.valueRange = valueRange;
    proposal.pattern = ( before.isEmpty() ? QStringLiteral( "^" ) : escapedForRegex( before ) )
                       + capturePattern( *value ) + escapedForRegex( after );
    proposal.key = escapedForRegex( proposal.value );
    proposal.displayTemplate = proposedTemplate();

    // The last word with a letter before the value.
    for ( const auto& word : wordsOverlapping( before, LineRange{ 0, before.size() } ) ) {
        const auto text = before.mid( word.start, word.length );
        if ( hasLetter( text ) ) {
            proposal.tableName = text.toString();
        }
    }
    return proposal;
}

QString escapedForRegex( QStringView text )
{
    static const auto metacharacters = QStringLiteral( "\\^$.|?*+()[]{}" );
    QString escaped;
    escaped.reserve( text.size() );
    for ( const auto character : text ) {
        // A tab is written \t: in the regex field it could not be seen.
        if ( character == u'\t' ) {
            escaped += QStringLiteral( "\\t" );
            continue;
        }
        if ( metacharacters.contains( character ) ) {
            escaped += u'\\';
        }
        escaped += character;
    }
    return escaped;
}

} // namespace logsquirl::valuenames
