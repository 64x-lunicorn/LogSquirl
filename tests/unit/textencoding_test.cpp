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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QByteArray>
#include <QString>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "encodings.h"
#include "textencoding.h"

#ifdef Q_OS_MACOS
#include <QStringDecoder>
#include <QStringEncoder>

#include "iconvconverter.h"
#endif

namespace {

// What the Encoding menu offered while it was built on QTextCodec: the name
// QTextCodec::codecForMib() gave each MIB enum. 38 is windows-949 there, and
// -949 had no codec, so the menu never showed it.
const std::map<int, std::string>& menuEncodingsAsBefore()
{
    static const std::map<int, std::string> encodings = {
        { 106, "UTF-8" },         { 1013, "UTF-16BE" },     { 1014, "UTF-16LE" },
        { 1018, "UTF-32BE" },     { 1019, "UTF-32LE" },     { 82, "ISO-8859-6" },
        { 2256, "windows-1256" }, { 7, "ISO-8859-4" },      { 109, "ISO-8859-13" },
        { 2257, "windows-1257" }, { 110, "ISO-8859-14" },   { 8, "ISO-8859-5" },
        { 2084, "KOI8-R" },       { 2088, "KOI8-U" },       { 2027, "macintosh" },
        { 2086, "IBM866" },       { 2251, "windows-1251" }, { 2250, "windows-1250" },
        { 2026, "Big5" },         { 2025, "GBK" },          { 5, "ISO-8859-2" },
        { 10, "ISO-8859-7" },     { 2253, "windows-1253" }, { 85, "ISO-8859-8" },
        { 2255, "windows-1255" }, { 17, "Shift_JIS" },      { 18, "EUC-JP" },
        { 39, "ISO-2022-JP" },    { 38, "windows-949" },    { 2259, "TIS-620" },
        { 6, "ISO-8859-3" },      { 12, "ISO-8859-9" },     { 2254, "windows-1254" },
        { 3, "US-ASCII" },        { 4, "ISO-8859-1" },      { 111, "ISO-8859-15" },
        { 2009, "IBM850" },       { 2252, "windows-1252" }, { 2258, "windows-1258" },
    };
    return encodings;
}

std::string guessOf( const QByteArray& bytes )
{
    return TextEncoding::forUtfText( bytes, TextEncoding::forName( "ISO-8859-1" ) )
        ->name()
        .toStdString();
}

} // namespace

SCENARIO( "The Encoding menu offers the Encodings it always did", "[encoding][textencoding]" )
{
    GIVEN( "every MIB enum the menu is built from" )
    {
        const auto& expected = menuEncodingsAsBefore();

        for ( const auto& group : EncodingMenu::supportedEncodings() ) {
            for ( const int mib : group.second ) {
                CAPTURE( group.first.toStdString(), mib );
                const auto* encoding = TextEncoding::forMib( mib );
                const auto known = expected.find( mib );

                if ( known == expected.end() ) {
                    // No Encoding, so no menu entry, as before.
                    REQUIRE( mib == -949 );
                    REQUIRE( encoding == nullptr );
                    continue;
                }

                // An Encoding this Qt cannot read or write is a missing menu
                // entry: it shows on a platform whose Qt has no ICU or iconv.
                REQUIRE( encoding != nullptr );
                REQUIRE( encoding->name().toStdString() == known->second );

                const QString text = QStringLiteral( "abc 123" );
                REQUIRE( encoding->toUnicode( encoding->fromUnicode( text ) ) == text );
            }
        }
    }

    GIVEN( "the menu built for a default Encoding" )
    {
        QActionGroup group( nullptr );
        FileAccessPolicy fileAccess;
        fileAccess.defaultEncodingMib = 106;
        std::unique_ptr<QMenu> menu( EncodingMenu::generate( &group, fileAccess ) );

        THEN( "it has an entry for each Encoding of the list" )
        {
            std::size_t entries = 0;
            for ( const auto* submenu : menu->findChildren<QMenu*>() ) {
                entries += static_cast<std::size_t>( submenu->actions().size() );
            }
            REQUIRE( entries == menuEncodingsAsBefore().size() );
        }
    }
}

SCENARIO( "An Encoding is found by name however it is spelled", "[encoding][textencoding]" )
{
    THEN( "case, dashes and the CP prefix do not matter" )
    {
        REQUIRE( TextEncoding::forName( "utf-8" ) == TextEncoding::forName( "UTF-8" ) );
        REQUIRE( TextEncoding::forName( "UTF8" ) == TextEncoding::forName( "UTF-8" ) );
        REQUIRE( TextEncoding::forName( "latin1" ) == TextEncoding::forName( "ISO-8859-1" ) );
        REQUIRE( TextEncoding::forName( "ASCII" ) == TextEncoding::forName( "US-ASCII" ) );
        REQUIRE( TextEncoding::forName( "CP1252" ) == TextEncoding::forName( "windows-1252" ) );
        REQUIRE( TextEncoding::forName( "WINDOWS-1250" )
                 == TextEncoding::forName( "windows-1250" ) );
    }

    THEN( "what uchardet reports maps to the Encoding QTextCodec picked for it" )
    {
        const std::map<std::string, std::string> uchardet = {
            { "GB2312", "GBK" },
            { "SJIS", "Shift_JIS" },
            { "EUC-KR", "windows-949" },
            { "ISO-8859-8-I", "ISO-8859-8" },
            { "MAC-CYRILLIC", "x-mac-cyrillic" },
            { "BIG5", "Big5" },
        };
        for ( const auto& [ guess, name ] : uchardet ) {
            CAPTURE( guess );
            const auto* encoding = TextEncoding::forName( guess.c_str() );
            REQUIRE( encoding != nullptr );
            REQUIRE( encoding->name().toStdString() == name );
        }
    }

    THEN( "a name nothing knows is no Encoding" )
    {
        REQUIRE( TextEncoding::forName( "no-such-encoding" ) == nullptr );
        REQUIRE( TextEncoding::forName( "" ) == nullptr );
        REQUIRE( TextEncoding::forMib( -4242 ) == nullptr );
    }

    THEN( "the Encodings have the MIB enums the settings store" )
    {
        REQUIRE( TextEncoding::forName( "UTF-8" )->mibEnum() == 106 );
        REQUIRE( TextEncoding::forName( "UTF-16LE" )->mibEnum() == 1014 );
        REQUIRE( TextEncoding::forName( "UTF-16BE" )->mibEnum() == 1013 );
        REQUIRE( TextEncoding::forName( "ISO-8859-1" )->mibEnum() == 4 );
        REQUIRE( TextEncoding::forMib( 106 ) == TextEncoding::forName( "UTF-8" ) );
    }
}

SCENARIO( "A byte order mark decides the Encoding of a text", "[encoding][textencoding]" )
{
    THEN( "each Unicode byte order mark is recognized" )
    {
        REQUIRE( guessOf( QByteArray( "\xEF\xBB\xBF"
                                      "hi" ) )
                 == "UTF-8" );
        REQUIRE( guessOf( QByteArray( "\xFF\xFE"
                                      "h\0",
                                      4 ) )
                 == "UTF-16LE" );
        REQUIRE( guessOf( QByteArray( "\xFE\xFF\0h", 4 ) ) == "UTF-16BE" );
        REQUIRE( guessOf( QByteArray( "\xFF\xFE\0\0h\0\0\0", 8 ) ) == "UTF-32LE" );
        REQUIRE( guessOf( QByteArray( "\0\0\xFE\xFF\0\0\0h", 8 ) ) == "UTF-32BE" );
    }

    THEN( "without one, the fallback is the answer" )
    {
        REQUIRE( guessOf( "plain" ) == "ISO-8859-1" );
        REQUIRE( guessOf( "" ) == "ISO-8859-1" );
        REQUIRE( guessOf( "\xEF\xBB" ) == "ISO-8859-1" );
        REQUIRE( TextEncoding::forUtfText( "plain" ) == TextEncoding::forLocale() );
    }
}

SCENARIO( "A decoder keeps a character cut short between two chunks", "[encoding][textencoding]" )
{
    const auto* utf8 = TextEncoding::forName( "UTF-8" );
    const QString euro( QChar( 0x20AC ) );
    const QByteArray bytes = euro.toUtf8(); // three bytes

    auto decoder = utf8->makeDecoder();
    QString text = decoder->decode( QByteArrayView( bytes ).first( 2 ) );
    text += decoder->decode( QByteArrayView( bytes ).sliced( 2 ) );

    REQUIRE( text == euro );
}

SCENARIO( "An Encoding no converter exists for cannot be used", "[encoding][textencoding]" )
{
    const TextEncoding unusable( -1, "LogSquirl-Unusable-Encoding", std::nullopt );

    REQUIRE_THROWS_AS( unusable.makeDecoder(), std::runtime_error );
    REQUIRE_THROWS_AS( unusable.fromUnicode( u"x" ), std::runtime_error );
}

#ifdef Q_OS_MACOS

// The Qt packages for macOS know no Encoding but the Unicode ones and Latin-1
// (#442), so this compares the converters of iconv with the ones Qt has where
// it has them, and where it has none the tests above cover what iconv gives.
SCENARIO( "The system's iconv converts the Encodings Qt for macOS lacks",
          "[encoding][textencoding]" )
{
    const auto name
        = GENERATE( "windows-1252", "windows-1251", "ISO-8859-2", "KOI8-R", "IBM850", "Big5", "GBK",
                    "Shift_JIS", "EUC-JP", "ISO-2022-JP", "windows-949", "TIS-620", "macintosh" );

    GIVEN( std::string( "the Encoding " ) + name )
    {
        const int index = iconv_converter::indexForName( name );
        REQUIRE( index >= 0 );

        QStringDecoder qtDecoder{ QAnyStringView( name ) };
        WHEN( "every byte value is decoded" )
        {
            THEN( "it is what Qt makes of it, where Qt converts the Encoding" )
            {
                if ( !qtDecoder.isValid() ) {
                    SUCCEED( "Qt cannot convert this Encoding here" );
                }
                else if ( std::string( name ).find( "windows-12" ) == 0
                          || std::string( name ).find( "ISO-8859" ) == 0
                          || std::string( name ).find( "KOI8" ) == 0 ) {
                    for ( int byte = 0x20; byte < 0x100; ++byte ) {
                        const QByteArray one( 1, static_cast<char>( byte ) );
                        auto viaIconv = iconv_converter::makeDecoder( index, {} );
                        const QString got = viaIconv->decode( one );
                        // A byte the Encoding leaves undefined: ICU maps it to
                        // the control character of that number, iconv rejects it.
                        if ( got.contains( QChar::ReplacementCharacter ) ) {
                            continue;
                        }
                        INFO( "byte " << byte );
                        REQUIRE( got == QStringDecoder( QAnyStringView( name ) ).decode( one ) );
                    }
                }
            }
        }

        WHEN( "text is encoded and decoded again" )
        {
            const auto* encoding = TextEncoding::forName( name );
            REQUIRE( encoding != nullptr );
            const QString plain = QStringLiteral( "plain text 123" );

            THEN( "it comes back" )
            {
                REQUIRE( encoding->toUnicode( encoding->fromUnicode( plain ) ) == plain );
            }
        }
    }
}

SCENARIO( "The converters of iconv keep a character cut short and forget it on a reset",
          "[encoding][textencoding]" )
{
    const int index = iconv_converter::indexForName( "Shift_JIS" );
    REQUIRE( index >= 0 );

    // Two Japanese characters, two bytes each.
    const QByteArray bytes = QByteArray::fromHex( "82a082a2" );
    const QString expected = QStringLiteral( "\u3042\u3044" );

    auto decoder = iconv_converter::makeDecoder( index, {} );
    QString text;
    for ( const char byte : bytes ) {
        text += decoder->decode( QByteArrayView( &byte, 1 ) );
    }
    REQUIRE( text == expected );

    // A lead byte cut off, then a reset: the half character is not glued to
    // what comes after.
    decoder->decode( QByteArrayView( bytes ).first( 1 ) );
    decoder->resetState();
    REQUIRE( decoder->decode( QByteArrayView( bytes ) ) == expected );
    REQUIRE( !decoder->hasError() );
}

SCENARIO( "The converters of iconv say what they cannot convert", "[encoding][textencoding]" )
{
    const auto* latin2 = TextEncoding::forName( "ISO-8859-2" );
    REQUIRE( latin2 != nullptr );

    REQUIRE( latin2->canEncode( u"caf\u00E9" ) );
    REQUIRE( !latin2->canEncode( u"\u20AC" ) );
    REQUIRE( !latin2->canEncode( u"\U0001D11E" ) );
}

#endif // Q_OS_MACOS
