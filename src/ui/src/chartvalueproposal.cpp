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

#include "chartvalueproposal.h"

#include <algorithm>

#include <QRegularExpression>

namespace logsquirl::chart {

using valuenames::LineRange;

namespace {

bool isWordCharacter( QChar character )
{
    return character.isLetterOrNumber() || character == u'_';
}

bool isDigit( QChar character )
{
    return character >= u'0' && character <= u'9';
}

// Where the word before position starts, with what separates it from
// position: the start of the line when there is none.
qsizetype wordBefore( QStringView line, qsizetype position )
{
    while ( position > 0 && !isWordCharacter( line[ position - 1 ] ) ) {
        --position;
    }
    while ( position > 0 && isWordCharacter( line[ position - 1 ] ) ) {
        --position;
    }
    return position;
}

// The last word of the text with a letter in it, empty when there is none.
QString lastWordWithLetter( QStringView text )
{
    QString last;
    qsizetype position = 0;
    while ( position < text.size() ) {
        if ( !isWordCharacter( text[ position ] ) ) {
            ++position;
            continue;
        }
        auto end = position;
        while ( end < text.size() && isWordCharacter( text[ end ] ) ) {
            ++end;
        }
        const auto word = text.mid( position, end - position );
        if ( std::any_of( word.begin(), word.end(), []( QChar c ) { return c.isLetter(); } ) ) {
            last = word.toString();
        }
        position = end;
    }
    return last;
}

} // namespace

std::optional<LineRange> numberSelected( QStringView line, LineRange selection )
{
    if ( selection.length <= 0 || selection.start < 0 || selection.start >= line.size() ) {
        return std::nullopt;
    }

    auto start = selection.start;
    auto end = std::min( selection.end(), line.size() );
    while ( start < end && line[ start ].isSpace() ) {
        ++start;
    }
    while ( end > start && line[ end - 1 ].isSpace() ) {
        --end;
    }
    if ( start == end ) {
        return std::nullopt;
    }

    static const QRegularExpression number(
        QRegularExpression::anchoredPattern( QStringLiteral( "-?\\d+(?:\\.\\d+)?" ) ) );
    if ( !number.matchView( line.mid( start, end - start ) ).hasMatch() ) {
        return std::nullopt;
    }
    // Not part of a longer number or word: no letter, digit or fraction right
    // before it, no digit or fraction right after; a unit may follow, 12ms.
    if ( start > 0 && ( isWordCharacter( line[ start - 1 ] ) || line[ start - 1 ] == u'.' ) ) {
        return std::nullopt;
    }
    if ( end < line.size()
         && ( isDigit( line[ end ] )
              || ( line[ end ] == u'.' && end + 1 < line.size()
                   && isDigit( line[ end + 1 ] ) ) ) ) {
        return std::nullopt;
    }
    // -5 with only the 5 selected, but not the 5 of id-5.
    if ( line[ start ] != u'-' && start > 0 && line[ start - 1 ] == u'-'
         && ( start == 1 || !isWordCharacter( line[ start - 2 ] ) ) ) {
        --start;
    }
    return LineRange{ start, end - start };
}

std::optional<ChartValueProposal> proposeChartValue( QStringView line, LineRange selection )
{
    const auto number = numberSelected( line, selection );
    if ( !number.has_value() ) {
        return std::nullopt;
    }
    const auto numberStart = number->start;
    const auto numberLength = number->length;

    // Any decimal number: the Log Lines like this one hold other numbers
    // there, with a fraction or without, negative or not.
    static const auto capture = QStringLiteral( "(-?\\d+(?:\\.\\d+)?)" );
    const auto patternFrom = [ & ]( qsizetype start, bool anchored ) {
        return ( anchored ? QStringLiteral( "^" ) : QString{} )
               + valuenames::escapedForRegex( line.mid( start, numberStart - start ) ) + capture;
    };
    // The chart reads the first match.
    const auto capturesNumber = [ & ]( const QString& pattern ) {
        const auto match = QRegularExpression( pattern ).matchView( line );
        return match.hasMatch() && match.capturedStart( 1 ) == numberStart
               && match.capturedLength( 1 ) == numberLength;
    };

    // The word before the number, then more of the Log Line until the
    // pattern captures the number selected, not an earlier one.
    std::optional<ChartValueProposal> proposal;
    auto start = wordBefore( line, numberStart );
    for ( ;; ) {
        auto pattern = patternFrom( start, start == numberStart );
        if ( capturesNumber( pattern ) ) {
            proposal = ChartValueProposal{ std::move( pattern ), QString{} };
            break;
        }
        if ( start == 0 ) {
            pattern = patternFrom( 0, true );
            if ( capturesNumber( pattern ) ) {
                proposal = ChartValueProposal{ std::move( pattern ), QString{} };
            }
            break;
        }
        start = wordBefore( line, start );
    }
    if ( proposal.has_value() ) {
        proposal->seriesName = lastWordWithLetter( line.mid( start, numberStart - start ) );
    }
    return proposal;
}

} // namespace logsquirl::chart
