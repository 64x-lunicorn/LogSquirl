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

#include "ansicolorsequences.h"

#include <optional>

#include <QChar>
#include <QRegularExpression>
#include <QString>
#include <QStringView>

namespace {

constexpr char16_t Escape = u'\x1B';

// Every ANSI color sequence starts with the escape character.
constexpr char AnsiColorSequencePattern[] = "\\x1B\\[([0-9]{1,4}((;|:)[0-9]{1,3})*)?[mK]";

// Built on first use; C++ guarantees the initialisation runs once even when
// several threads get here at the same time. optimize() compiles (and JITs)
// the pattern right away, so no match races to compile it later: Qt compiles
// under the expression's own mutex, and every match after that only reads the
// compiled pattern, with a JIT stack per thread.
const QRegularExpression& ansiColorSequences()
{
    static const QRegularExpression pattern = [] {
        QRegularExpression compiled( QLatin1StringView( AnsiColorSequencePattern ),
                                     QRegularExpression::CaseInsensitiveOption );
        compiled.optimize();
        return compiled;
    }();
    return pattern;
}

bool isDigit( QChar character )
{
    return character.unicode() >= u'0' && character.unicode() <= u'9';
}

bool isSeparator( QChar character )
{
    return character.unicode() == u';' || character.unicode() == u':';
}

// Where the sequence starting at the escape character at start ends -- one
// past its final byte -- if AnsiColorSequencePattern matches it there;
// nothing otherwise. The pattern reads the same greedy way this does: a run
// of digits longer than it allows is followed by another digit, which no
// shorter run can be followed by either.
std::optional<qsizetype> sequenceEnd( QStringView text, qsizetype start )
{
    const auto size = text.size();
    auto position = start + 1;
    if ( position >= size || text[ position ].unicode() != u'[' ) {
        return std::nullopt;
    }
    ++position;

    // The digits of one parameter, at most maxDigits of them.
    const auto skipDigits = [ &text, &position, size ]( qsizetype maxDigits ) {
        const auto first = position;
        while ( position < size && isDigit( text[ position ] ) ) {
            ++position;
        }
        const auto digits = position - first;
        return digits >= 1 && digits <= maxDigits;
    };

    if ( position < size && isDigit( text[ position ] ) ) {
        if ( !skipDigits( 4 ) ) {
            return std::nullopt;
        }
        while ( position < size && isSeparator( text[ position ] ) ) {
            ++position;
            if ( !skipDigits( 3 ) ) {
                return std::nullopt;
            }
        }
    }

    if ( position >= size ) {
        return std::nullopt;
    }
    // The pattern ignores case: m, M, K, k, and the Kelvin sign, which is a
    // capital K to a case-insensitive Unicode match.
    switch ( text[ position ].unicode() ) {
    case u'm':
    case u'M':
    case u'k':
    case u'K':
    case u'K':
        return position + 1;
    default:
        return std::nullopt;
    }
}

// The numeric parameters of a sequence, one after the other.
class SgrParameters {
public:
    explicit SgrParameters( QStringView parameters )
        : parameters_( parameters )
    {
    }

    bool atEnd() const
    {
        return position_ >= parameters_.size();
    }

    // The next parameter; nothing when there is none left.
    std::optional<int> next()
    {
        if ( atEnd() ) {
            return std::nullopt;
        }
        int value = 0;
        while ( position_ < parameters_.size() && isDigit( parameters_[ position_ ] ) ) {
            value = value * 10 + ( parameters_[ position_ ].unicode() - u'0' );
            ++position_;
        }
        // Past the separator, if any.
        ++position_;
        return value;
    }

private:
    QStringView parameters_;
    qsizetype position_ = 0;
};

struct SgrColors {
    AnsiColor foreground;
    AnsiColor background;
};

// The color 38 or 48 asks for with the parameters after it: 5;n or 2;r;g;b.
// Nothing when they are malformed.
std::optional<AnsiColor> extendedColor( SgrParameters& parameters )
{
    const auto byte = []( std::optional<int> value ) -> std::optional<std::uint8_t> {
        if ( !value.has_value() || *value > 255 ) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>( *value );
    };

    const auto mode = parameters.next();
    if ( mode == 5 ) {
        const auto index = byte( parameters.next() );
        if ( !index.has_value() ) {
            return std::nullopt;
        }
        return AnsiColor::indexed( *index );
    }
    if ( mode == 2 ) {
        const auto red = byte( parameters.next() );
        const auto green = byte( parameters.next() );
        const auto blue = byte( parameters.next() );
        if ( !red.has_value() || !green.has_value() || !blue.has_value() ) {
            return std::nullopt;
        }
        return AnsiColor::rgb( *red, *green, *blue );
    }
    return std::nullopt;
}

// The colors after an SGR sequence with these parameters, starting from
// colors; colors themselves when the sequence is malformed.
SgrColors applySgr( SgrColors colors, QStringView parameterText )
{
    if ( parameterText.isEmpty() ) {
        return {};
    }

    auto result = colors;
    SgrParameters parameters( parameterText );
    while ( !parameters.atEnd() ) {
        const auto code = *parameters.next();
        if ( code == 0 ) {
            result = {};
        }
        else if ( code >= 30 && code <= 37 ) {
            result.foreground = AnsiColor::indexed( static_cast<std::uint8_t>( code - 30 ) );
        }
        else if ( code >= 90 && code <= 97 ) {
            result.foreground = AnsiColor::indexed( static_cast<std::uint8_t>( code - 90 + 8 ) );
        }
        else if ( code >= 40 && code <= 47 ) {
            result.background = AnsiColor::indexed( static_cast<std::uint8_t>( code - 40 ) );
        }
        else if ( code >= 100 && code <= 107 ) {
            result.background = AnsiColor::indexed( static_cast<std::uint8_t>( code - 100 + 8 ) );
        }
        else if ( code == 39 ) {
            result.foreground = {};
        }
        else if ( code == 49 ) {
            result.background = {};
        }
        else if ( code == 38 || code == 48 ) {
            const auto color = extendedColor( parameters );
            if ( !color.has_value() ) {
                return colors;
            }
            ( code == 38 ? result.foreground : result.background ) = *color;
        }
        // Every other code is not drawn.
    }
    return result;
}

} // namespace

AnsiColoredText parseAnsiColorSequences( QString text )
{
    AnsiColoredText parsed;
    auto escape = text.indexOf( QChar( Escape ) );
    // The pattern matches nothing in a text that is not valid UTF-16, such
    // as one with a lone surrogate: it is left as it is too.
    if ( escape < 0 || !QStringView( text ).isValidUtf16() ) {
        parsed.text = std::move( text );
        return parsed;
    }

    const QStringView source( text );
    parsed.text.reserve( text.size() );

    SgrColors colors;
    // Where the text in the current colors starts, in the parsed text.
    int colorsStart = 0;
    const auto endColors = [ &parsed, &colors, &colorsStart ] {
        const auto end = static_cast<int>( parsed.text.size() );
        if ( end > colorsStart
             && ( !colors.foreground.isLineColor() || !colors.background.isLineColor() ) ) {
            auto& spans = parsed.spans;
            if ( !spans.empty() && spans.back().start + spans.back().length == colorsStart
                 && spans.back().foreground == colors.foreground
                 && spans.back().background == colors.background ) {
                // The same colors again after a change that colored no text.
                spans.back().length += end - colorsStart;
            }
            else {
                spans.push_back( AnsiColorSpan{ colorsStart, end - colorsStart, colors.foreground,
                                                colors.background } );
            }
        }
        colorsStart = end;
    };

    qsizetype copiedTo = 0;
    while ( escape >= 0 ) {
        const auto end = sequenceEnd( source, escape );
        if ( !end.has_value() ) {
            escape = text.indexOf( QChar( Escape ), escape + 1 );
            continue;
        }

        parsed.text.append( source.sliced( copiedTo, escape - copiedTo ) );
        copiedTo = *end;

        const auto finalByte = source[ *end - 1 ].unicode();
        if ( finalByte == u'm' || finalByte == u'M' ) {
            // Between "ESC [" and the final byte.
            const auto next
                = applySgr( colors, source.sliced( escape + 2, *end - 1 - ( escape + 2 ) ) );
            if ( next.foreground != colors.foreground || next.background != colors.background ) {
                endColors();
                colors = next;
            }
        }

        escape = text.indexOf( QChar( Escape ), *end );
    }
    parsed.text.append( source.sliced( copiedTo ) );
    endColors();

    return parsed;
}

void removeAnsiColorSequences( QString& text )
{
    if ( !text.contains( QChar( Escape ) ) ) {
        return;
    }
    text.remove( ansiColorSequences() );
}
