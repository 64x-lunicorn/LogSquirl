/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LOGSQUIRL_REGULAR_EXPRESSION_PATTERN_H
#define LOGSQUIRL_REGULAR_EXPRESSION_PATTERN_H

#include <QRegularExpression>
#include <QString>
#include <cstdint>
#include <qregularexpression.h>
#include <string>

#include "uuid.h"

// Whether a pattern refers back to one of its own groups: \1..\9 and \g...,
// the named forms \k... and (?P=name). Such a pattern only works when the
// groups actually capture, so it is the one case in which the don't-capture
// option has to go (#336).
//
// The scan follows the two contexts in which a backslash-digit means
// something else -- inside a character class it is an octal escape, and
// between \Q and \E everything is literal -- and it consumes an escaped
// character with its backslash, so that \\1 reads as a backslash followed by
// a digit. Where it is not exact it errs towards reporting a backreference:
// it ends a character class at the first ], although PCRE takes a leading ]
// literally, and it does not read (?#...) comments. That costs the pattern
// its capture-free fast path, whereas the opposite mistake would leave a real
// backreference invalid and matching nothing at all.
inline bool usesBackreference( const QString& pattern )
{
    bool isInCharacterClass = false;
    bool isInQuotedLiteral = false;

    for ( int index = 0; index < pattern.size(); ++index ) {
        const QChar character = pattern[ index ];

        if ( isInQuotedLiteral ) {
            if ( character == QChar( '\\' ) && index + 1 < pattern.size()
                 && pattern[ index + 1 ] == QChar( 'E' ) ) {
                isInQuotedLiteral = false;
                ++index;
            }
            continue;
        }

        if ( character == QChar( '\\' ) ) {
            if ( index + 1 >= pattern.size() ) {
                break;
            }

            const QChar escaped = pattern[ ++index ];
            if ( escaped == QChar( 'Q' ) ) {
                isInQuotedLiteral = true;
            }
            else if ( isInCharacterClass ) {
                continue;
            }
            else if ( ( escaped.isDigit() && escaped != QChar( '0' ) ) || escaped == QChar( 'g' )
                      || escaped == QChar( 'k' ) ) {
                return true;
            }
            continue;
        }

        if ( isInCharacterClass ) {
            if ( character == QChar( ']' ) ) {
                isInCharacterClass = false;
            }
            continue;
        }

        if ( character == QChar( '[' ) ) {
            isInCharacterClass = true;
        }
        else if ( character == QChar( '(' )
                  && pattern.mid( index, 4 ) == QLatin1String( "(?P=" ) ) {
            return true;
        }
    }

    return false;
}

struct RegularExpressionPattern {

    QString pattern;
    bool isCaseSensitive = true;
    bool isExclude = false;
    bool isBoolean = false;
    bool isPlainText = false;
    bool isPrefilter = false;

    RegularExpressionPattern() = default;

    explicit RegularExpressionPattern( const QString& expression )
        : RegularExpressionPattern( expression, true, false, false, false )
    {
    }

    RegularExpressionPattern( const QString& expression, bool caseSensitive, bool inverse,
                              bool boolean, bool plainText )
        : pattern( expression )
        , isCaseSensitive( caseSensitive )
        , isExclude( inverse )
        , isBoolean( boolean )
        , isPlainText( plainText )
        , patternId_( nextId() )
    {
    }

    std::string id() const
    {
        return patternId_;
    }

    explicit operator QRegularExpression() const
    {
        auto finalPattern = pattern;
        if ( isPlainText ) {
            finalPattern = QRegularExpression::escape( pattern );
        }

        QRegularExpression::PatternOptions patternOptions
            = QRegularExpression::UseUnicodePropertiesOption;

        if ( !isCaseSensitive ) {
            patternOptions |= QRegularExpression::CaseInsensitiveOption;
        }

        // Capturing costs time on every Log Line, so only a pattern that
        // needs its groups back pays for them (#336). The escaped form is
        // what gets compiled, so it is what decides.
        if ( !usesBackreference( finalPattern ) ) {
            patternOptions |= QRegularExpression::DontCaptureOption;
        }

        return QRegularExpression( finalPattern, patternOptions );
    }

    bool operator==( const RegularExpressionPattern& other ) const
    {
        return std::tie( pattern, isCaseSensitive, isExclude, isBoolean, isPlainText, isPrefilter )
               == std::tie( other.pattern, other.isCaseSensitive, other.isExclude, other.isBoolean,
                            other.isPlainText, other.isPrefilter );
    }

private:
    static std::string nextId()
    {
        static std::atomic<uint> counter_ = 0;
        return std::string{ "p_" } + std::to_string( counter_++ );
    }

    std::string patternId_;
};

#endif