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

#include "timestampreader.h"

#include "jsonlogline.h"
#include "logfmtlogline.h"
#include "logfieldextractor.h"

#include <QDate>
#include <QTime>
#include <QTimeZone>

#include <array>
#include <cmath>
#include <vector>

namespace {

// The formats tried, in this order, for a Log Format that declares none.
// "%N" is an optional fraction of a second with its separator and "%_" the
// "T" or the whitespace between date and time; both are used only here.
constexpr std::array FallbackFormats = {
    "%Y-%m-%d%_%H:%M:%S%N%z", // ISO 8601: most application logs
    "%Y/%m/%d%_%H:%M:%S%N%z",
    "%d/%b/%Y:%H:%M:%S%N%z",  // Common Log Format
    "%d/%b/%Y %H:%M:%S%N%z",  // Python's HTTP servers
    "%a %b %e %H:%M:%S%N %Y", // uwsgi, Apache error log
    "%b %e %H:%M:%S%N",       // syslog
    "%Y%m%d %H:%M:%S%N",      // glog with the year
    "%m%d %H:%M:%S%N",        // glog
    "%y%m%d %H%M%S",          // HDFS
    "%y/%m/%d %H:%M:%S%N",    // Spark
    "%m-%d %H:%M:%S%N",       // logcat
    "%H:%M:%S%N",             // the time of day only
};

constexpr std::array<const char*, 12> MonthNames
    = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };

enum class Kind {
    Literal,
    Space,
    Year4,
    Year2,
    Month,
    Day,
    Hour,
    Minute,
    Second,
    Fraction,
    OptionalFraction,
    MonthName,
    DayName,
    Epoch,
    Zone,
    DateTimeSeparator,
};

struct Token {
    Kind kind;
    QChar literal;
};

using Pattern = std::vector<Token>;

bool isDigit( QChar c )
{
    return c >= QLatin1Char( '0' ) && c <= QLatin1Char( '9' );
}

bool isLetter( QChar c )
{
    const auto lower = c.toLower();
    return lower >= QLatin1Char( 'a' ) && lower <= QLatin1Char( 'z' );
}

// The tokens of a strftime-like format; none when it has a directive this
// reader does not know.
std::optional<Pattern> compile( const QString& format )
{
    Pattern pattern;
    for ( qsizetype i = 0; i < format.size(); ++i ) {
        const auto c = format[ i ];
        if ( c.isSpace() ) {
            if ( pattern.empty() || pattern.back().kind != Kind::Space ) {
                pattern.push_back( { Kind::Space, {} } );
            }
            continue;
        }
        if ( c != QLatin1Char( '%' ) ) {
            pattern.push_back( { Kind::Literal, c } );
            continue;
        }
        if ( ++i >= format.size() ) {
            return std::nullopt;
        }
        switch ( format[ i ].unicode() ) {
        case 'Y':
            pattern.push_back( { Kind::Year4, {} } );
            break;
        case 'y':
            pattern.push_back( { Kind::Year2, {} } );
            break;
        case 'm':
            pattern.push_back( { Kind::Month, {} } );
            break;
        case 'd':
        case 'e':
            pattern.push_back( { Kind::Day, {} } );
            break;
        case 'H':
            pattern.push_back( { Kind::Hour, {} } );
            break;
        case 'M':
            pattern.push_back( { Kind::Minute, {} } );
            break;
        case 'S':
            pattern.push_back( { Kind::Second, {} } );
            break;
        case 'L':
        case 'f':
            pattern.push_back( { Kind::Fraction, {} } );
            break;
        case 'N':
            pattern.push_back( { Kind::OptionalFraction, {} } );
            break;
        case 'b':
        case 'B':
        case 'h':
            pattern.push_back( { Kind::MonthName, {} } );
            break;
        case 'a':
        case 'A':
            pattern.push_back( { Kind::DayName, {} } );
            break;
        case 's':
            pattern.push_back( { Kind::Epoch, {} } );
            break;
        case 'z':
        case 'Z':
            pattern.push_back( { Kind::Zone, {} } );
            break;
        case '_':
            pattern.push_back( { Kind::DateTimeSeparator, {} } );
            break;
        case '%':
            pattern.push_back( { Kind::Literal, QLatin1Char( '%' ) } );
            break;
        default:
            return std::nullopt;
        }
    }
    return pattern;
}

// What a pattern read from a text.
struct Fields {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
    std::optional<qint64> epoch;
};

// Reads up to maxDigits digits at pos, at least one.
std::optional<int> readNumber( QStringView text, qsizetype& pos, int maxDigits )
{
    int value = 0;
    int digits = 0;
    while ( digits < maxDigits && pos < text.size() && isDigit( text[ pos ] ) ) {
        value = value * 10 + ( text[ pos ].unicode() - '0' );
        ++pos;
        ++digits;
    }
    if ( digits == 0 ) {
        return std::nullopt;
    }
    return value;
}

// The digits of a fraction of a second at pos, as milliseconds.
std::optional<int> readFraction( QStringView text, qsizetype& pos )
{
    int millis = 0;
    int digits = 0;
    while ( pos < text.size() && isDigit( text[ pos ] ) ) {
        if ( digits < 3 ) {
            millis = millis * 10 + ( text[ pos ].unicode() - '0' );
        }
        ++digits;
        ++pos;
    }
    if ( digits == 0 ) {
        return std::nullopt;
    }
    for ( int d = digits; d < 3; ++d ) {
        millis *= 10;
    }
    return millis;
}

// Skips a written time zone, if there is one: "Z", "+01", "-0400", "+01:00",
// "PDT", possibly after a space.
void skipZone( QStringView text, qsizetype& pos )
{
    auto p = pos;
    while ( p < text.size() && text[ p ].isSpace() ) {
        ++p;
    }
    if ( p >= text.size() ) {
        return;
    }
    if ( text[ p ] == QLatin1Char( '+' ) || text[ p ] == QLatin1Char( '-' ) ) {
        ++p;
        const auto digitsStart = p;
        while ( p < text.size() && ( isDigit( text[ p ] ) || text[ p ] == QLatin1Char( ':' ) ) ) {
            ++p;
        }
        if ( p - digitsStart >= 2 ) {
            pos = p;
        }
        return;
    }
    if ( isLetter( text[ p ] ) ) {
        while ( p < text.size() && isLetter( text[ p ] ) ) {
            ++p;
        }
        pos = p;
    }
}

bool readFields( const Pattern& pattern, QStringView text, Fields& fields )
{
    qsizetype pos = 0;
    for ( const auto& token : pattern ) {
        switch ( token.kind ) {
        case Kind::Literal:
            if ( pos >= text.size() || text[ pos ] != token.literal ) {
                return false;
            }
            ++pos;
            break;
        case Kind::Space:
            if ( pos >= text.size() || !text[ pos ].isSpace() ) {
                return false;
            }
            while ( pos < text.size() && text[ pos ].isSpace() ) {
                ++pos;
            }
            break;
        case Kind::DateTimeSeparator:
            if ( pos >= text.size() ) {
                return false;
            }
            if ( text[ pos ] == QLatin1Char( 'T' ) || text[ pos ] == QLatin1Char( 't' ) ) {
                ++pos;
            }
            else if ( text[ pos ].isSpace() ) {
                while ( pos < text.size() && text[ pos ].isSpace() ) {
                    ++pos;
                }
            }
            else {
                return false;
            }
            break;
        case Kind::Year4: {
            const auto start = pos;
            const auto year = readNumber( text, pos, 4 );
            if ( !year || pos - start != 4 ) {
                return false;
            }
            fields.year = *year;
            break;
        }
        case Kind::Year2: {
            const auto year = readNumber( text, pos, 2 );
            if ( !year ) {
                return false;
            }
            fields.year = *year < 69 ? 2000 + *year : 1900 + *year;
            break;
        }
        case Kind::Month: {
            const auto value = readNumber( text, pos, 2 );
            if ( !value ) {
                return false;
            }
            fields.month = *value;
            break;
        }
        case Kind::Day: {
            const auto value = readNumber( text, pos, 2 );
            if ( !value ) {
                return false;
            }
            fields.day = *value;
            break;
        }
        case Kind::Hour: {
            const auto value = readNumber( text, pos, 2 );
            if ( !value ) {
                return false;
            }
            fields.hour = *value;
            break;
        }
        case Kind::Minute: {
            const auto value = readNumber( text, pos, 2 );
            if ( !value ) {
                return false;
            }
            fields.minute = *value;
            break;
        }
        case Kind::Second: {
            const auto value = readNumber( text, pos, 2 );
            if ( !value ) {
                return false;
            }
            fields.second = *value;
            break;
        }
        case Kind::Fraction: {
            const auto millis = readFraction( text, pos );
            if ( !millis ) {
                return false;
            }
            fields.millisecond = *millis;
            break;
        }
        case Kind::OptionalFraction:
            if ( pos + 1 < text.size()
                 && ( text[ pos ] == QLatin1Char( '.' ) || text[ pos ] == QLatin1Char( ',' )
                      || text[ pos ] == QLatin1Char( ':' ) )
                 && isDigit( text[ pos + 1 ] ) ) {
                ++pos;
                fields.millisecond = readFraction( text, pos ).value_or( 0 );
            }
            break;
        case Kind::MonthName: {
            if ( pos + 3 > text.size() ) {
                return false;
            }
            const auto name = text.mid( pos, 3 ).toString().toLower();
            int month = 0;
            for ( size_t m = 0; m < MonthNames.size(); ++m ) {
                if ( name == QLatin1String( MonthNames[ m ] ) ) {
                    month = static_cast<int>( m ) + 1;
                }
            }
            if ( month == 0 ) {
                return false;
            }
            fields.month = month;
            pos += 3;
            while ( pos < text.size() && isLetter( text[ pos ] ) ) {
                ++pos;
            }
            break;
        }
        case Kind::DayName: {
            const auto start = pos;
            while ( pos < text.size() && isLetter( text[ pos ] ) ) {
                ++pos;
            }
            if ( pos - start < 3 ) {
                return false;
            }
            break;
        }
        case Kind::Epoch: {
            qint64 value = 0;
            int digits = 0;
            while ( pos < text.size() && isDigit( text[ pos ] ) && digits < 18 ) {
                value = value * 10 + ( text[ pos ].unicode() - '0' );
                ++pos;
                ++digits;
            }
            if ( digits == 0 ) {
                return false;
            }
            fields.epoch = value;
            break;
        }
        case Kind::Zone:
            skipZone( text, pos );
            break;
        }
    }

    while ( pos < text.size() && text[ pos ].isSpace() ) {
        ++pos;
    }
    return pos == text.size();
}

} // namespace

struct TimestampReader::Impl {
    std::shared_ptr<const LogFormatDefinition> format;
    std::unique_ptr<LogFieldExtractor> extractor;
    std::vector<Pattern> patterns;
    bool available = false;
    int referenceYear = 0;
    double divisor = 1.0;
};

TimestampReader::TimestampReader( const LogFormatDefinition& format, int referenceYear )
    : impl_( std::make_unique<Impl>() )
{
    // The extractor keeps a reference to the definition, so it gets a copy
    // that lives as long as it does.
    impl_->format = std::make_shared<const LogFormatDefinition>( format );
    impl_->extractor = std::make_unique<LogFieldExtractor>( *impl_->format );
    impl_->referenceYear = referenceYear > 0 ? referenceYear : QDate::currentDate().year();
    impl_->divisor = format.timestampDivisor() > 0.0 ? format.timestampDivisor() : 1.0;

    impl_->available = isAvailableFor( format );

    for ( const auto& declared : format.timestampFormats() ) {
        if ( auto pattern = compile( declared ) ) {
            impl_->patterns.push_back( std::move( *pattern ) );
        }
    }
    if ( format.timestampFormats().isEmpty() ) {
        for ( const auto* fallback : FallbackFormats ) {
            impl_->patterns.push_back( *compile( QLatin1String( fallback ) ) );
        }
    }
}

TimestampReader::~TimestampReader() = default;
TimestampReader::TimestampReader( TimestampReader&& ) noexcept = default;
TimestampReader& TimestampReader::operator=( TimestampReader&& ) noexcept = default;

bool TimestampReader::isAvailableFor( const LogFormatDefinition& format )
{
    const auto& field = format.timestampField();
    if ( field.isEmpty() ) {
        return false;
    }
    if ( format.kind() != LogFormatKind::Regex ) {
        return true;
    }
    const auto named = "(?<" + field + ">";
    const auto namedPython = "(?P<" + field + ">";
    for ( const auto& patternText : format.regexPatterns() ) {
        if ( patternText.contains( named ) || patternText.contains( namedPython ) ) {
            return true;
        }
    }
    return false;
}

bool TimestampReader::isAvailable() const
{
    return impl_->available;
}

std::optional<QDateTime> TimestampReader::timestampOf( const QString& line ) const
{
    if ( !impl_->available ) {
        return std::nullopt;
    }
    if ( impl_->format->kind() == LogFormatKind::Json ) {
        // The value of the timestamp field itself, not the text of its cell
        const auto object = JsonLogLine::parse( line );
        if ( !object ) {
            return std::nullopt;
        }
        const auto value = JsonLogLine::valueAt( *object, impl_->format->timestampField() );
        if ( value.isDouble() ) {
            return JsonLogLine::fromEpoch( value.toDouble(), impl_->divisor );
        }
        return value.isString() ? parseField( value.toString() ) : std::nullopt;
    }

    if ( impl_->format->kind() == LogFormatKind::Logfmt ) {
        const auto pairs = LogfmtLogLine::parse( line );
        if ( !pairs ) {
            return std::nullopt;
        }
        const auto text = pairs->value( impl_->format->timestampField() );
        return text.isEmpty() ? std::nullopt : parseField( text );
    }

    const auto fields = impl_->extractor->extractFields( line );
    if ( !fields.isValid() ) {
        return std::nullopt;
    }
    const auto text = fields.value( impl_->format->timestampField() );
    if ( text.isEmpty() ) {
        return std::nullopt;
    }
    return parseField( text );
}

std::optional<QDateTime> TimestampReader::parseField( QStringView text ) const
{
    for ( const auto& pattern : impl_->patterns ) {
        Fields fields;
        if ( !readFields( pattern, text, fields ) ) {
            continue;
        }

        if ( fields.epoch ) {
            const auto millis
                = std::llround( static_cast<double>( *fields.epoch ) * 1000.0 / impl_->divisor );
            return QDateTime::fromMSecsSinceEpoch(
                millis + ( impl_->divisor == 1.0 ? fields.millisecond : 0 ), QTimeZone::UTC );
        }

        QDate date( 1970, 1, 1 );
        if ( fields.month != 0 || fields.day != 0 || fields.year != 0 ) {
            date = QDate( fields.year != 0 ? fields.year : impl_->referenceYear,
                          fields.month != 0 ? fields.month : 1, fields.day != 0 ? fields.day : 1 );
        }
        const QTime time( fields.hour, fields.minute, fields.second, fields.millisecond );
        if ( !date.isValid() || !time.isValid() ) {
            continue;
        }
        return QDateTime( date, time, QTimeZone::UTC );
    }
    return std::nullopt;
}
