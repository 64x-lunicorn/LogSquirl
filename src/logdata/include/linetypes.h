/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <qglobal.h>
#include <string_view>

#include <type_safe/narrow_cast.hpp>
#include <type_safe/strong_typedef.hpp>

#include <QMetaType>
#include <QString>
#include <type_traits>

#include "containers.h"
#include "log.h"

template <typename StrongType>
constexpr StrongType maxValue()
{
    return StrongType( std::numeric_limits<typename StrongType::UnderlyingType>::max() );
}

// Qt file reading api has qint64 type offsets
struct OffsetInFile : type_safe::strong_typedef<OffsetInFile, int64_t>,
                      type_safe::strong_typedef_op::addition<OffsetInFile>,
                      type_safe::strong_typedef_op::subtraction<OffsetInFile>,
                      type_safe::strong_typedef_op::increment<OffsetInFile>,
                      type_safe::strong_typedef_op::relational_comparison<OffsetInFile>,
                      type_safe::strong_typedef_op::equality_comparison<OffsetInFile> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = int64_t;

    template <typename T = UnderlyingType>
    constexpr T get() const
    {
        const auto underlyingValue = type_safe::get( *this );

        if constexpr ( std::is_same_v<T, UnderlyingType> ) {
            return underlyingValue;
        }
        else if constexpr ( std::is_unsigned_v<T> ) {
            Q_ASSERT( underlyingValue >= 0 );
            return type_safe::narrow_cast<T>( static_cast<uint64_t>( underlyingValue ) );
        }
        else {
            return type_safe::narrow_cast<T>( underlyingValue );
        }
    }
};

struct LinesCount : type_safe::strong_typedef<LinesCount, uint64_t>,
                    type_safe::strong_typedef_op::addition<LinesCount>,
                    type_safe::strong_typedef_op::subtraction<LinesCount>,
                    type_safe::strong_typedef_op::increment<LinesCount>,
                    type_safe::strong_typedef_op::decrement<LinesCount>,
                    type_safe::strong_typedef_op::relational_comparison<LinesCount>,
                    type_safe::strong_typedef_op::equality_comparison<LinesCount> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = uint64_t;

    template <typename T = UnderlyingType>
    constexpr T get() const
    {
        static_assert( std::is_signed_v<T> == std::is_signed_v<UnderlyingType>,
                       "T should be the same sign as UnderlyingType" );

        if constexpr ( std::is_same_v<T, UnderlyingType> ) {
            return type_safe::get( *this );
        }
        else {
            return type_safe::narrow_cast<T>( type_safe::get( *this ) );
        }
    }
};
Q_DECLARE_METATYPE( LinesCount )

struct LineNumber : type_safe::strong_typedef<LineNumber, uint64_t>,
                    type_safe::strong_typedef_op::increment<LineNumber>,
                    type_safe::strong_typedef_op::decrement<LineNumber>,
                    type_safe::strong_typedef_op::relational_comparison<LineNumber>,
                    type_safe::strong_typedef_op::equality_comparison<LineNumber> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = uint64_t;

    template <typename T = UnderlyingType>
    constexpr T get() const
    {
        if constexpr ( std::is_same_v<T, UnderlyingType> ) {
            return type_safe::get( *this );
        }
        else {
            return type_safe::narrow_cast<T>( type_safe::get( *this ) );
        }
    }
};
Q_DECLARE_METATYPE( LineNumber )

struct LineLength : type_safe::strong_typedef<LineLength, decltype( QString{}.size() )>,
                    type_safe::strong_typedef_op::addition<LineLength>,
                    type_safe::strong_typedef_op::subtraction<LineLength>,
                    type_safe::strong_typedef_op::relational_comparison<LineLength>,
                    type_safe::strong_typedef_op::equality_comparison<LineLength> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = decltype( QString{}.size() );

    template <typename T = UnderlyingType>
    constexpr T get() const
    {
        static_assert( std::is_signed_v<T> == std::is_signed_v<UnderlyingType>,
                       "T should be the same sign as UnderlyingType" );

        if constexpr ( std::is_same_v<T, UnderlyingType> ) {
            return type_safe::get( *this );
        }
        else {
            return type_safe::narrow_cast<T>( type_safe::get( *this ) );
        }
    }
};
Q_DECLARE_METATYPE( LineLength )

struct LineColumn : type_safe::strong_typedef<LineColumn, decltype( QString{}.size() )>,
                    type_safe::strong_typedef_op::increment<LineColumn>,
                    type_safe::strong_typedef_op::relational_comparison<LineColumn>,
                    type_safe::strong_typedef_op::equality_comparison<LineColumn> {
    using strong_typedef::strong_typedef;

    using UnderlyingType = decltype( QString{}.size() );

    template <typename T = UnderlyingType>
    constexpr T get() const
    {
        if constexpr ( std::is_same_v<T, UnderlyingType> ) {
            return type_safe::get( *this );
        }
        else if constexpr ( std::is_same_v<T, size_t> ) {
            Q_ASSERT( type_safe::get( *this ) >= 0 );
            return static_cast<T>( type_safe::get( *this ) );
        }
        else {
            static_assert( std::is_signed_v<T> == std::is_signed_v<UnderlyingType>,
                           "T should be the same sign as UnderlyingType" );
            return type_safe::narrow_cast<T>( type_safe::get( *this ) );
        }
    }
};
Q_DECLARE_METATYPE( LineColumn )

inline constexpr OffsetInFile operator"" _offset( unsigned long long int value )
{
    return OffsetInFile( static_cast<OffsetInFile::UnderlyingType>( value ) );
}
inline constexpr LineNumber operator"" _lnum( unsigned long long int value )
{
    return LineNumber( static_cast<LineNumber::UnderlyingType>( value ) );
}
inline constexpr LinesCount operator"" _lcount( unsigned long long int value )
{
    return LinesCount( static_cast<LinesCount::UnderlyingType>( value ) );
}
inline constexpr LineLength operator"" _length( unsigned long long int value )
{
    return LineLength( static_cast<LineLength::UnderlyingType>( value ) );
}
inline constexpr LineColumn operator"" _lcol( unsigned long long int value )
{
    return LineColumn( static_cast<LineColumn::UnderlyingType>( value ) );
}

template <typename T, typename U, typename R = T>
inline R boundedAdd( const T& value, const U& inc )
{
    return ( value.get() <= maxValue<R>().get() - inc.get() ) ? R( value.get() + inc.get() )
                                                              : maxValue<R>();
}

template <typename T, typename U, typename R = T>
inline R boundedSubtract( const T& value, const U& dec )
{
    return value.get() >= dec.get() ? R( value.get() - dec.get() ) : R{};
}

inline LineNumber& operator+=( LineNumber& number, const LinesCount& count )
{
    number = boundedAdd( number, count );
    return number;
}
inline LineNumber operator+( const LineNumber& number, const LinesCount& count )
{
    return boundedAdd( number, count );
}

inline LineColumn& operator+=( LineColumn& column, const LineLength& length )
{
    column = boundedAdd( column, length );
    return column;
}
inline LineColumn operator+( const LineColumn& column, const LineLength& length )
{
    return boundedAdd( column, length );
}

inline LineNumber operator-( const LineNumber& number, const LinesCount& count )
{
    return boundedSubtract( number, count );
}

inline LineColumn operator-( const LineColumn& column, const LineLength& length )
{
    return boundedSubtract( column, length );
}

inline LinesCount operator-( const LineNumber& n1, const LineNumber& n2 )
{
    return boundedSubtract<LineNumber, LineNumber, LinesCount>( n1, n2 );
}

inline LineLength operator-( const LineColumn& n1, const LineColumn& n2 )
{
    return boundedSubtract<LineColumn, LineColumn, LineLength>( n1, n2 );
}

inline bool operator<( const LineNumber& number, const LinesCount& count )
{
    return number.get() < count.get();
}
inline bool operator>=( const LineNumber& number, const LinesCount& count )
{
    return !( number < count );
}

using OptionalLineNumber = std::optional<LineNumber>;

template <typename Tag, typename T>
QDebug operator<<( QDebug dbg, type_safe::strong_typedef<Tag, T> const& object )
{
    QDebugStateSaver saver( dbg );
    dbg << type_safe::get( object );

    return dbg;
}

// Represents a position in a file (line, column)
class FilePosition {
public:
    FilePosition()
        : column_{ -1 }
    {
    }
    FilePosition( LineNumber line, LineColumn column )
        : line_{ line }
        , column_{ column }
    {
    }

    LineNumber line() const
    {
        return line_;
    }
    LineColumn column() const
    {
        return column_;
    }

    bool operator==( const FilePosition& other ) const
    {
        return line_ == other.line_ && column_ == other.column_;
    }

    bool operator!=( const FilePosition& other ) const
    {
        return !this->operator==( other );
    }

private:
    LineNumber line_;
    LineColumn column_;
};

// Length of a tab stop
constexpr int TabStop = 8;

// Maximum expanded line length for display (prevents OOM on malicious/corrupt files).
// Lines exceeding this after tab expansion are truncated.
constexpr int MaxExpandedLineLength = 1'000'000;

// Replaces tab characters with the appropriate number of spaces using O(n) single-pass
// algorithm. Also replaces null characters with spaces.
inline QString untabify( QString&& line, LineColumn initialPosition = 0_lcol )
{
    const auto* src = line.constData();
    const auto srcLen = static_cast<int>( line.size() );

    // Fast path: no tabs and no nulls — return as-is
    bool hasTabs = false;
    bool hasNulls = false;
    for ( int i = 0; i < srcLen; ++i ) {
        if ( src[ i ] == QChar::Tabulation ) {
            hasTabs = true;
            break;
        }
        if ( src[ i ] == QChar::Null ) {
            hasNulls = true;
        }
    }

    if ( !hasTabs ) {
        if ( hasNulls ) {
            line.replace( QChar::Null, QChar::Space );
        }
        return std::move( line );
    }

    // First pass: compute expanded length
    int expandedLen = 0;
    int column = initialPosition.get<int>();
    for ( int i = 0; i < srcLen; ++i ) {
        if ( src[ i ] == QChar::Tabulation ) {
            const int spaces = TabStop - ( column % TabStop );
            expandedLen += spaces;
            column += spaces;
        }
        else {
            ++expandedLen;
            ++column;
        }
    }

    // Cap at maximum display length to prevent OOM
    const bool truncated = expandedLen > MaxExpandedLineLength;
    if ( truncated ) {
        expandedLen = MaxExpandedLineLength;
    }

    // Second pass: build result string in one allocation
    QString result;
    result.reserve( expandedLen );

    column = initialPosition.get<int>();
    int outPos = 0;
    for ( int i = 0; i < srcLen && outPos < expandedLen; ++i ) {
        if ( src[ i ] == QChar::Tabulation ) {
            const int spaces = TabStop - ( column % TabStop );
            const int toWrite = std::min( spaces, expandedLen - outPos );
            result.append( QString( toWrite, QChar::Space ) );
            column += spaces;
            outPos += toWrite;
        }
        else {
            result.append( src[ i ] == QChar::Null ? QChar::Space : src[ i ] );
            ++column;
            ++outPos;
        }
    }

    return result;
}

template <typename LineType>
LineLength getUntabifiedLength( const LineType& utf8Line )
{
    size_t totalSpaces = 0;

    auto tabPosition = utf8Line.find( '\t' );
    while ( tabPosition != std::string_view::npos ) {
        const auto spaces = TabStop - ( ( tabPosition + totalSpaces ) % TabStop ) - 1;
        totalSpaces += spaces;

        tabPosition = utf8Line.find( '\t', tabPosition + 1 );
    }

    const auto expandedLen = static_cast<int64_t>( utf8Line.size() + totalSpaces );
    const auto cappedLen = std::min( expandedLen, static_cast<int64_t>( MaxExpandedLineLength ) );

    return LineLength( type_safe::narrow_cast<LineLength::UnderlyingType>( cappedLen ) );
}

// Calculates the expanded length of a QString after tab expansion, without allocating
// a new string. O(n) in the length of the input.
inline LineLength getUntabifiedLength( const QString& line )
{
    const auto* src = line.constData();
    const auto srcLen = static_cast<int>( line.size() );
    int expandedLen = 0;
    int column = 0;

    for ( int i = 0; i < srcLen; ++i ) {
        if ( src[ i ] == QChar::Tabulation ) {
            const int spaces = TabStop - ( column % TabStop );
            expandedLen += spaces;
            column += spaces;
        }
        else {
            ++expandedLen;
            ++column;
        }
    }

    return LineLength( std::min( expandedLen, MaxExpandedLineLength ) );
}
