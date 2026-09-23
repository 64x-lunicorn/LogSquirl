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

#include "textencoding.h"

#include <QStringEncoder>

#include <algorithm>
#include <cctype>
#include <deque>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

using Builtin = QStringConverter::Encoding;

struct EncodingSpec {
    int mib;
    const char* name;
    std::optional<Builtin> builtin;
    // Further names this Encoding is known by.
    std::vector<const char*> aliases;
    // Further MIB enums that stand for this same Encoding.
    std::vector<int> otherMibs;
};

// Names and MIB enums are the ones QTextCodec used, so a setting or an Index
// cache written before the switch still finds its Encoding. Every Encoding of
// the Encoding menu is here; the rest are what uchardet may report.
const std::vector<EncodingSpec>& specs()
{
    static const std::vector<EncodingSpec> all = {
        { 106, "UTF-8", Builtin::Utf8, { "utf8" }, {} },
        { 1013, "UTF-16BE", Builtin::Utf16BE, {}, {} },
        { 1014, "UTF-16LE", Builtin::Utf16LE, {}, {} },
        { 1015, "UTF-16", Builtin::Utf16, {}, {} },
        { 1017, "UTF-32", Builtin::Utf32, {}, {} },
        { 1018, "UTF-32BE", Builtin::Utf32BE, {}, {} },
        { 1019, "UTF-32LE", Builtin::Utf32LE, {}, {} },
        { 4, "ISO-8859-1", Builtin::Latin1, { "latin1", "iso-ir-100" }, {} },
        { 3, "US-ASCII", Builtin::Latin1, { "ASCII", "ANSI_X3.4-1968" }, {} },
        { 5, "ISO-8859-2", std::nullopt, {}, {} },
        { 6, "ISO-8859-3", std::nullopt, {}, {} },
        { 7, "ISO-8859-4", std::nullopt, {}, {} },
        { 8, "ISO-8859-5", std::nullopt, {}, {} },
        { 82, "ISO-8859-6", std::nullopt, {}, {} },
        { 10, "ISO-8859-7", std::nullopt, {}, {} },
        { 11, "ISO-8859-8", std::nullopt, { "ISO-8859-8-I" }, { 85 } },
        { 12, "ISO-8859-9", std::nullopt, {}, {} },
        { 13, "ISO-8859-10", std::nullopt, {}, {} },
        { 109, "ISO-8859-13", std::nullopt, {}, {} },
        { 110, "ISO-8859-14", std::nullopt, {}, {} },
        { 111, "ISO-8859-15", std::nullopt, {}, {} },
        { 112, "ISO-8859-16", std::nullopt, {}, {} },
        { 2250, "windows-1250", std::nullopt, {}, {} },
        { 2251, "windows-1251", std::nullopt, {}, {} },
        { 2252, "windows-1252", std::nullopt, {}, {} },
        { 2253, "windows-1253", std::nullopt, {}, {} },
        { 2254, "windows-1254", std::nullopt, {}, {} },
        { 2255, "windows-1255", std::nullopt, {}, {} },
        { 2256, "windows-1256", std::nullopt, {}, {} },
        { 2257, "windows-1257", std::nullopt, {}, {} },
        { 2258, "windows-1258", std::nullopt, {}, {} },
        { 2084, "KOI8-R", std::nullopt, {}, {} },
        { 2088, "KOI8-U", std::nullopt, {}, {} },
        { 2027, "macintosh", std::nullopt, {}, {} },
        { 0, "x-mac-cyrillic", std::nullopt, { "MAC-CYRILLIC" }, {} },
        { 0, "x-mac-centraleurroman", std::nullopt, { "MAC-CENTRALEUROPE" }, {} },
        { 2086, "IBM866", std::nullopt, {}, {} },
        { 2009, "IBM850", std::nullopt, {}, {} },
        { 2010, "IBM852", std::nullopt, {}, {} },
        { 2046, "IBM855", std::nullopt, {}, {} },
        { 2052, "IBM865", std::nullopt, {}, {} },
        { 2026, "Big5", std::nullopt, {}, {} },
        { 2025, "GBK", std::nullopt, { "GB2312", "CP936" }, {} },
        { 114, "GB18030", std::nullopt, {}, {} },
        { 2085, "HZ-GB-2312", std::nullopt, {}, {} },
        { 17, "Shift_JIS", std::nullopt, { "SJIS", "CP932" }, {} },
        { 18, "EUC-JP", std::nullopt, {}, {} },
        { 39, "ISO-2022-JP", std::nullopt, {}, {} },
        { 104, "ISO-2022-CN", std::nullopt, {}, {} },
        { 37, "ISO-2022-KR", std::nullopt, {}, {} },
        { 38, "windows-949", std::nullopt, { "EUC-KR", "CP949" }, {} },
        { 2259, "TIS-620", std::nullopt, {}, {} },
    };
    return all;
}

// "ISO-8859-1", "iso_8859_1" and "iso88591" are one name.
std::string normalized( QByteArrayView name )
{
    std::string result;
    result.reserve( static_cast<std::size_t>( name.size() ) );
    for ( const char c : name ) {
        if ( std::isalnum( static_cast<unsigned char>( c ) ) ) {
            result.push_back(
                static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ) );
        }
    }
    // "CP1252" is what Windows calls windows-1252.
    if ( result.size() == 6 && result.compare( 0, 2, "cp" ) == 0 && result[ 2 ] == '1'
         && result[ 3 ] == '2' ) {
        result = "windows" + result.substr( 2 );
    }
    return result;
}

bool canDecode( const char* name )
{
    return QStringDecoder( QAnyStringView( name ) ).isValid();
}

} // namespace

// Builds the interned objects once.
struct TextEncodingRegistry {
    std::deque<TextEncoding> encodings;
    std::map<std::string, const TextEncoding*> byName;
    std::map<int, const TextEncoding*> byMib;

    TextEncodingRegistry()
    {
        for ( const auto& spec : specs() ) {
            // Qt built without ICU or iconv only knows the Unicode Encodings
            // and Latin-1. An Encoding this Qt cannot decode is left out, the
            // way QTextCodec left out what it did not know.
            if ( !spec.builtin.has_value() && !canDecode( spec.name ) ) {
                continue;
            }
            encodings.push_back( TextEncoding( spec.mib, spec.name, spec.builtin ) );
            const TextEncoding* encoding = &encodings.back();

            byName.emplace( normalized( spec.name ), encoding );
            for ( const char* alias : spec.aliases ) {
                byName.emplace( normalized( alias ), encoding );
            }
            if ( spec.mib != 0 ) {
                byMib.emplace( spec.mib, encoding );
            }
            for ( const int mib : spec.otherMibs ) {
                byMib.emplace( mib, encoding );
            }
        }
    }

    static const TextEncodingRegistry& instance()
    {
        static const TextEncodingRegistry registry;
        return registry;
    }
};

const TextEncoding* TextEncoding::forName( QByteArrayView name )
{
    const auto& registry = TextEncodingRegistry::instance();
    const auto found = registry.byName.find( normalized( name ) );
    return found == registry.byName.end() ? nullptr : found->second;
}

const TextEncoding* TextEncoding::forMib( int mib )
{
    const auto& registry = TextEncodingRegistry::instance();
    const auto found = registry.byMib.find( mib );
    return found == registry.byMib.end() ? nullptr : found->second;
}

const TextEncoding* TextEncoding::forLocale()
{
#ifdef Q_OS_WIN
    switch ( ::GetACP() ) {
    case 932:
        return forName( "Shift_JIS" );
    case 936:
        return forName( "GBK" );
    case 949:
        return forName( "windows-949" );
    case 950:
        return forName( "Big5" );
    case 874:
        return forName( "TIS-620" );
    case 65001:
        break;
    default:
        if ( const auto* codePage = forName( "windows-" + QByteArray::number( ::GetACP() ) ) ) {
            return codePage;
        }
        break;
    }
#endif
    // Qt 6 takes the locale of a Unix system to be UTF-8.
    return forName( "UTF-8" );
}

const TextEncoding* TextEncoding::forUtfText( QByteArrayView data, const TextEncoding* fallback )
{
    const auto starts = [ &data ]( std::initializer_list<unsigned char> bom ) {
        return data.size() >= static_cast<qsizetype>( bom.size() )
               && std::equal( bom.begin(), bom.end(), data.begin(),
                              []( unsigned char expected, char byte ) {
                                  return expected == static_cast<unsigned char>( byte );
                              } );
    };

    // UTF-32LE first: its byte order mark starts with the UTF-16LE one.
    if ( starts( { 0xFF, 0xFE, 0x00, 0x00 } ) ) {
        return forName( "UTF-32LE" );
    }
    if ( starts( { 0x00, 0x00, 0xFE, 0xFF } ) ) {
        return forName( "UTF-32BE" );
    }
    if ( starts( { 0xFF, 0xFE } ) ) {
        return forName( "UTF-16LE" );
    }
    if ( starts( { 0xFE, 0xFF } ) ) {
        return forName( "UTF-16BE" );
    }
    if ( starts( { 0xEF, 0xBB, 0xBF } ) ) {
        return forName( "UTF-8" );
    }
    return fallback ? fallback : forLocale();
}

std::unique_ptr<QStringDecoder> TextEncoding::makeDecoder( QStringConverter::Flags flags ) const
{
    auto decoder
        = builtin_.has_value()
              ? std::make_unique<QStringDecoder>( *builtin_, flags )
              : std::make_unique<QStringDecoder>( QAnyStringView( name_.constData() ), flags );
    if ( !decoder->isValid() ) {
        throw std::runtime_error( "the Encoding cannot be used" );
    }
    return decoder;
}

QByteArray TextEncoding::fromUnicode( QStringView text ) const
{
    return makeEncoder().encode( text );
}

QString TextEncoding::toUnicode( QByteArrayView bytes ) const
{
    return makeDecoder()->decode( bytes );
}

bool TextEncoding::canEncode( QStringView text ) const
{
    // A converter substitutes what it cannot encode, and not every one says
    // so: it can encode the text if the text comes back unchanged.
    return toUnicode( fromUnicode( text ) ) == text;
}

QStringEncoder TextEncoding::makeEncoder() const
{
    QStringEncoder encoder = builtin_.has_value()
                                 ? QStringEncoder( *builtin_ )
                                 : QStringEncoder( QAnyStringView( name_.constData() ) );
    if ( !encoder.isValid() ) {
        throw std::runtime_error( "the Encoding cannot be used" );
    }
    return encoder;
}
