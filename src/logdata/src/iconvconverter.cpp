/*
 * Copyright (C) 2026 LogSquirl contributors
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

#include "iconvconverter.h"

#include <QtGlobal>

#include <iconv.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace {

struct Entry {
    // The name TextEncoding knows the Encoding by.
    const char* encoding;
    const char* iconvName;
};

constexpr std::array entries = {
    Entry{ "ISO-8859-2", "ISO-8859-2" },
    Entry{ "ISO-8859-3", "ISO-8859-3" },
    Entry{ "ISO-8859-4", "ISO-8859-4" },
    Entry{ "ISO-8859-5", "ISO-8859-5" },
    Entry{ "ISO-8859-6", "ISO-8859-6" },
    Entry{ "ISO-8859-7", "ISO-8859-7" },
    Entry{ "ISO-8859-8", "ISO-8859-8" },
    Entry{ "ISO-8859-9", "ISO-8859-9" },
    Entry{ "ISO-8859-10", "ISO-8859-10" },
    Entry{ "ISO-8859-13", "ISO-8859-13" },
    Entry{ "ISO-8859-14", "ISO-8859-14" },
    Entry{ "ISO-8859-15", "ISO-8859-15" },
    Entry{ "ISO-8859-16", "ISO-8859-16" },
    Entry{ "windows-1250", "WINDOWS-1250" },
    Entry{ "windows-1251", "WINDOWS-1251" },
    Entry{ "windows-1252", "WINDOWS-1252" },
    Entry{ "windows-1253", "WINDOWS-1253" },
    Entry{ "windows-1254", "WINDOWS-1254" },
    Entry{ "windows-1255", "WINDOWS-1255" },
    Entry{ "windows-1256", "WINDOWS-1256" },
    Entry{ "windows-1257", "WINDOWS-1257" },
    Entry{ "windows-1258", "WINDOWS-1258" },
    Entry{ "KOI8-R", "KOI8-R" },
    Entry{ "KOI8-U", "KOI8-U" },
    Entry{ "macintosh", "MACINTOSH" },
    Entry{ "x-mac-cyrillic", "MACCYRILLIC" },
    Entry{ "x-mac-centraleurroman", "MACCENTRALEUROPE" },
    Entry{ "IBM866", "CP866" },
    Entry{ "IBM850", "CP850" },
    Entry{ "IBM852", "CP852" },
    Entry{ "IBM855", "CP855" },
    Entry{ "IBM865", "CP865" },
    Entry{ "Big5", "BIG5" },
    Entry{ "GBK", "GBK" },
    Entry{ "GB18030", "GB18030" },
    Entry{ "HZ-GB-2312", "HZ" },
    Entry{ "Shift_JIS", "SHIFT_JIS" },
    Entry{ "EUC-JP", "EUC-JP" },
    Entry{ "ISO-2022-JP", "ISO-2022-JP" },
    Entry{ "ISO-2022-CN", "ISO-2022-CN" },
    Entry{ "ISO-2022-KR", "ISO-2022-KR" },
    Entry{ "windows-949", "CP949" },
    Entry{ "TIS-620", "TIS-620" },
};

// What Qt's QChar is in memory.
constexpr const char* Utf16 = Q_BYTE_ORDER == Q_LITTLE_ENDIAN ? "UTF-16LE" : "UTF-16BE";

// A converter function may not throw, and no Encoding has more than a few
// bytes of a character or of an escape sequence left over between two calls.
constexpr std::size_t maxPendingBytes = 8;
constexpr std::size_t encodedBytesPerUnit = 4;
constexpr std::size_t encodedSlack = 16;

// iconv_open()'s failure value, (iconv_t)-1: a sentinel, never dereferenced.
// NOLINTNEXTLINE(performance-no-int-to-ptr)
const iconv_t noDescriptor = reinterpret_cast<iconv_t>( static_cast<std::intptr_t>( -1 ) );

// What a State keeps between two calls: the iconv conversion, which holds the
// shift state, and the bytes of a character cut off at the end of the input.
struct Context {
    iconv_t descriptor = noDescriptor;
    std::string pending;

    Context() = default;
    Context( const Context& ) = delete;
    Context& operator=( const Context& ) = delete;

    ~Context()
    {
        if ( descriptor != noDescriptor ) {
            ::iconv_close( descriptor );
        }
    }
};

// Made when the first bytes come, so a State that was reset finds its way
// again without being told what it converts.
Context& contextOf( QStringConverter::State* state, const char* to, const char* from )
{
    if ( state->d[ 0 ] == nullptr ) {
        auto context = std::make_unique<Context>();
        context->descriptor = ::iconv_open( to, from );
        state->d[ 0 ] = context.release();
        state->clearFn = []( QStringConverter::State* dying ) noexcept {
            delete static_cast<Context*>( dying->d[ 0 ] );
            dying->d[ 0 ] = nullptr;
        };
    }
    return *static_cast<Context*>( state->d[ 0 ] );
}

char16_t substitute( const QStringConverter::State* state )
{
    return state->flags.testFlag( QStringConverter::Flag::ConvertInvalidToNull ) ? u'\0' : u'�';
}

QChar* decode( const char* iconvName, QChar* out, QByteArrayView in,
               QStringConverter::State* state )
{
    auto& context = contextOf( state, Utf16, iconvName );
    auto* dst = reinterpret_cast<char*>( out );
    if ( context.descriptor == noDescriptor ) {
        state->invalidChars += 1;
        return out;
    }

    std::string input = std::move( context.pending );
    context.pending.clear();
    input.append( in.data(), static_cast<std::size_t>( in.size() ) );

    // Every byte gives at most one UTF-16 unit, a four byte character two.
    std::size_t room
        = ( static_cast<std::size_t>( in.size() ) + maxPendingBytes ) * sizeof( QChar );
    char* src = input.data();
    std::size_t left = input.size();
    while ( left > 0 ) {
        if ( ::iconv( context.descriptor, &src, &left, &dst, &room )
             != static_cast<std::size_t>( -1 ) ) {
            break;
        }
        if ( errno == EINVAL && !state->flags.testFlag( QStringConverter::Flag::Stateless )
             && left <= maxPendingBytes ) {
            // Cut off; the rest of the character comes with the next call.
            context.pending.assign( src, left );
            break;
        }
        if ( errno == E2BIG || room < sizeof( char16_t ) ) {
            break;
        }
        // Not a character of this Encoding (or cut off in a stateless read):
        // one replacement character for the byte, as Qt's own converters do.
        const char16_t replacement = substitute( state );
        std::memcpy( dst, &replacement, sizeof replacement );
        dst += sizeof replacement;
        room -= sizeof replacement;
        state->invalidChars += 1;
        ++src;
        --left;
    }
    return reinterpret_cast<QChar*>( dst );
}

char* encode( const char* iconvName, char* out, QStringView in, QStringConverter::State* state )
{
    auto& context = contextOf( state, iconvName, Utf16 );
    if ( context.descriptor == noDescriptor ) {
        state->invalidChars += 1;
        return out;
    }

    char* dst = out;
    std::size_t room = static_cast<std::size_t>( in.size() ) * encodedBytesPerUnit + encodedSlack;
    char* src = const_cast<char*>( reinterpret_cast<const char*>( in.utf16() ) );
    std::size_t left = static_cast<std::size_t>( in.size() ) * sizeof( QChar );
    while ( left > 0 ) {
        if ( ::iconv( context.descriptor, &src, &left, &dst, &room )
             != static_cast<std::size_t>( -1 ) ) {
            break;
        }
        if ( errno == E2BIG || room < 1 ) {
            break;
        }
        // Not encodable in this Encoding: a question mark, and the whole
        // character (a surrogate pair is one) is skipped.
        *dst = '?';
        ++dst;
        --room;
        state->invalidChars += 1;
        char16_t first = 0;
        std::memcpy( &first, src, sizeof first );
        const bool pair = QChar::isHighSurrogate( first ) && left >= 2 * sizeof( QChar );
        const std::size_t skipped = ( pair ? 2 : 1 ) * sizeof( QChar );
        src += skipped;
        left -= std::min( skipped, left );
    }
    // Back to the initial shift state (ISO-2022 writes an escape sequence for
    // it), so the next text starts clean.
    ::iconv( context.descriptor, nullptr, nullptr, &dst, &room );
    ::iconv( context.descriptor, nullptr, nullptr, nullptr, nullptr );
    return dst;
}

template <std::size_t Index>
QChar* decodeFn( QChar* out, QByteArrayView in, QStringConverter::State* state )
{
    return decode( entries[ Index ].iconvName, out, in, state );
}

template <std::size_t Index>
char* encodeFn( char* out, QStringView in, QStringConverter::State* state )
{
    return encode( entries[ Index ].iconvName, out, in, state );
}

qsizetype decodedLength( qsizetype inLength )
{
    return inLength + static_cast<qsizetype>( maxPendingBytes );
}

qsizetype encodedLength( qsizetype inLength )
{
    return inLength * static_cast<qsizetype>( encodedBytesPerUnit )
           + static_cast<qsizetype>( encodedSlack );
}

// Only a subclass of the converters can name Qt's Interface type.
struct Tables : QStringConverter {
    static const Interface* get( int index )
    {
        static const auto all = build( std::make_index_sequence<entries.size()>() );
        return &all[ static_cast<std::size_t>( index ) ];
    }

private:
    template <std::size_t Index>
    static Interface make()
    {
        Interface interface;
        interface.name = entries[ Index ].encoding;
        interface.toUtf16 = &decodeFn<Index>;
        interface.toUtf16Len = &decodedLength;
        interface.fromUtf16 = &encodeFn<Index>;
        interface.fromUtf16Len = &encodedLength;
        return interface;
    }

    template <std::size_t... Index>
    static std::array<Interface, sizeof...( Index )> build( std::index_sequence<Index...> )
    {
        return { make<Index>()... };
    }
};

struct Decoder : QStringDecoder {
    Decoder( int index, Flags flags )
        : QStringDecoder( Tables::get( index ) )
    {
        state.flags = flags;
    }
};

struct Encoder : QStringEncoder {
    explicit Encoder( int index )
        : QStringEncoder( Tables::get( index ) )
    {
    }
};

} // namespace

namespace iconv_converter {

int indexForName( const char* encodingName )
{
    for ( std::size_t index = 0; index < entries.size(); ++index ) {
        if ( std::strcmp( entries[ index ].encoding, encodingName ) != 0 ) {
            continue;
        }
        const iconv_t probe = ::iconv_open( Utf16, entries[ index ].iconvName );
        if ( probe == noDescriptor ) {
            return -1;
        }
        ::iconv_close( probe );
        return static_cast<int>( index );
    }
    return -1;
}

std::unique_ptr<QStringDecoder> makeDecoder( int index, QStringConverter::Flags flags )
{
    // Sliced on purpose: the subclass adds nothing to the object.
    return std::make_unique<QStringDecoder>( Decoder( index, flags ) );
}

QStringEncoder makeEncoder( int index )
{
    return QStringEncoder( Encoder( index ) );
}

} // namespace iconv_converter
