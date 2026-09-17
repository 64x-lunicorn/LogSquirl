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

#include <catch2/catch.hpp>

#include "encodingdetector.h"

#include <QTextCodec>

SCENARIO( "EncodingParameters from UTF-8 codec", "[encoding]" )
{
    GIVEN( "The UTF-8 text codec" )
    {
        auto* codec = QTextCodec::codecForName( "UTF-8" );
        REQUIRE( codec != nullptr );

        EncodingParameters params( codec );

        THEN( "It is marked as UTF-8 compatible" )
        {
            REQUIRE( params.isUtf8Compatible );
        }

        THEN( "It is not UTF-16 LE" )
        {
            REQUIRE_FALSE( params.isUtf16LE );
        }

        THEN( "Line feed width is 1 byte" )
        {
            REQUIRE( params.lineFeedWidth == 1 );
        }

        THEN( "Line feed index is 0" )
        {
            REQUIRE( params.lineFeedIndex == 0 );
        }
    }
}

SCENARIO( "EncodingParameters from UTF-16 LE codec", "[encoding]" )
{
    GIVEN( "The UTF-16 LE text codec" )
    {
        auto* codec = QTextCodec::codecForName( "UTF-16LE" );
        REQUIRE( codec != nullptr );

        EncodingParameters params( codec );

        THEN( "It is not UTF-8 compatible" )
        {
            REQUIRE_FALSE( params.isUtf8Compatible );
        }

        THEN( "It is marked as UTF-16 LE" )
        {
            REQUIRE( params.isUtf16LE );
        }

        THEN( "Line feed width is 2 bytes" )
        {
            REQUIRE( params.lineFeedWidth == 2 );
        }
    }
}

SCENARIO( "EncodingParameters from UTF-16 BE codec", "[encoding]" )
{
    GIVEN( "The UTF-16 BE text codec" )
    {
        auto* codec = QTextCodec::codecForName( "UTF-16BE" );
        REQUIRE( codec != nullptr );

        EncodingParameters params( codec );

        THEN( "It is not UTF-8 compatible" )
        {
            REQUIRE_FALSE( params.isUtf8Compatible );
        }

        THEN( "It is not UTF-16 LE" )
        {
            REQUIRE_FALSE( params.isUtf16LE );
        }

        THEN( "Line feed width is 2 bytes" )
        {
            REQUIRE( params.lineFeedWidth == 2 );
        }
    }
}

SCENARIO( "EncodingParameters from Latin-1 codec", "[encoding]" )
{
    GIVEN( "A Latin-1 (ISO-8859-1) text codec" )
    {
        auto* codec = QTextCodec::codecForName( "ISO-8859-1" );
        REQUIRE( codec != nullptr );

        EncodingParameters params( codec );

        THEN( "It is not UTF-8 compatible" )
        {
            REQUIRE_FALSE( params.isUtf8Compatible );
        }

        THEN( "It is not UTF-16 LE" )
        {
            REQUIRE_FALSE( params.isUtf16LE );
        }

        THEN( "Line feed width is 1 byte" )
        {
            REQUIRE( params.lineFeedWidth == 1 );
        }
    }
}

SCENARIO( "EncodingParameters equality operator", "[encoding]" )
{
    GIVEN( "Two parameters with the same widths" )
    {
        auto* utf8 = QTextCodec::codecForName( "UTF-8" );
        auto* latin1 = QTextCodec::codecForName( "ISO-8859-1" );
        REQUIRE( utf8 != nullptr );
        REQUIRE( latin1 != nullptr );

        EncodingParameters paramsUtf8( utf8 );
        EncodingParameters paramsLatin1( latin1 );

        THEN( "They compare as equal (equality is by width/index only)" )
        {
            REQUIRE( paramsUtf8 == paramsLatin1 );
        }
    }

    GIVEN( "Parameters with different feed widths" )
    {
        auto* utf8 = QTextCodec::codecForName( "UTF-8" );
        auto* utf16 = QTextCodec::codecForName( "UTF-16LE" );
        REQUIRE( utf8 != nullptr );
        REQUIRE( utf16 != nullptr );

        EncodingParameters paramsUtf8( utf8 );
        EncodingParameters paramsUtf16( utf16 );

        THEN( "They compare as not equal" )
        {
            REQUIRE( paramsUtf8 != paramsUtf16 );
        }
    }
}

SCENARIO( "EncodingParameters CR offset helpers", "[encoding]" )
{
    GIVEN( "UTF-8 encoding parameters" )
    {
        auto* codec = QTextCodec::codecForName( "UTF-8" );
        REQUIRE( codec != nullptr );
        EncodingParameters params( codec );

        THEN( "getBeforeCrOffset returns lineFeedIndex" )
        {
            REQUIRE( params.getBeforeCrOffset() == params.lineFeedIndex );
        }

        THEN( "getAfterCrOffset is lineFeedWidth - lineFeedIndex - 1" )
        {
            REQUIRE( params.getAfterCrOffset() == params.lineFeedWidth - params.lineFeedIndex - 1 );
        }
    }
}

SCENARIO( "EncodingDetector detects encoding from byte content", "[encoding]" )
{
    GIVEN( "A block of pure ASCII text" )
    {
        std::string text = "Hello world, this is plain ASCII text\nwith line breaks\n";
        logsquirl::vector<char> block( text.begin(), text.end() );

        auto* codec = EncodingDetector::getInstance().detectEncoding( block );

        THEN( "A valid codec is returned" )
        {
            REQUIRE( codec != nullptr );
        }

        THEN( "The codec can represent ASCII faithfully" )
        {
            // On some platforms uchardet may detect ASCII as a single-byte encoding
            // like Windows-1252 instead of UTF-8/US-ASCII. All of these represent
            // the ASCII range identically, so we check the codec name instead.
            QString name = QString::fromLatin1( codec->name() ).toLower();
            bool isAsciiCompatible = name.contains( "utf-8" ) || name.contains( "ascii" )
                                     || name.contains( "iso-8859" )
                                     || name.contains( "windows-1252" ) || name.contains( "latin" );
            REQUIRE( isAsciiCompatible );
        }
    }

    GIVEN( "A block with UTF-8 BOM" )
    {
        std::string text = "\xEF\xBB\xBF"
                           "UTF-8 with BOM\n";
        logsquirl::vector<char> block( text.begin(), text.end() );

        auto* codec = EncodingDetector::getInstance().detectEncoding( block );

        THEN( "The codec is UTF-8" )
        {
            REQUIRE( codec != nullptr );
            EncodingParameters params( codec );
            REQUIRE( params.isUtf8Compatible );
        }
    }
}

SCENARIO( "EncodingDetector looks at a sample of a large block", "[encoding]" )
{
    GIVEN( "A block smaller than the sample" )
    {
        std::string text = "short line\nanother line without end";
        logsquirl::vector<char> block( text.begin(), text.end() );

        THEN( "The whole block is the sample" )
        {
            REQUIRE( EncodingDetector::sampleSize( block.data(), block.size() ) == block.size() );
        }
    }

    GIVEN( "A 5 MB block of UTF-8 lines" )
    {
        std::string line = "2026-09-17 12:00:00 INFO gr\xC3\xBC\xC3\x9F Gott \xE2\x82\xAC\n";
        std::string text;
        while ( text.size() < 5 * 1024 * 1024 ) {
            text += line;
        }
        logsquirl::vector<char> block( text.begin(), text.end() );

        const auto sample = EncodingDetector::sampleSize( block.data(), block.size() );

        THEN( "The sample is no longer than the sample limit" )
        {
            REQUIRE( sample > 0 );
            REQUIRE( sample <= EncodingDetector::MaxSampleSize );
        }

        THEN( "The sample ends after a whole line" )
        {
            REQUIRE( block[ sample - 1 ] == '\n' );
        }

        THEN( "The block is still detected as UTF-8" )
        {
            auto* codec = EncodingDetector::getInstance().detectEncoding( block );
            REQUIRE( codec != nullptr );
            REQUIRE( EncodingParameters( codec ).isUtf8Compatible );
        }
    }

    GIVEN( "A large block of UTF-16LE lines" )
    {
        std::string text;
        while ( text.size() < 1024 * 1024 ) {
            for ( char c : std::string( "log line number one\n" ) ) {
                text += c;
                text += '\0';
            }
        }
        logsquirl::vector<char> block( text.begin(), text.end() );

        const auto sample = EncodingDetector::sampleSize( block.data(), block.size() );

        THEN( "The sample keeps whole UTF-16 code units" )
        {
            REQUIRE( sample % 2 == 0 );
            REQUIRE( block[ sample - 2 ] == '\n' );
            REQUIRE( block[ sample - 1 ] == '\0' );
        }
    }

    GIVEN( "A large block without any line feed" )
    {
        logsquirl::vector<char> block( 1024 * 1024, 'a' );

        THEN( "The sample is cut at the sample limit" )
        {
            REQUIRE( EncodingDetector::sampleSize( block.data(), block.size() )
                     == EncodingDetector::MaxSampleSize );
        }
    }
}

SCENARIO( "TextCodecHolder manages codec state", "[encoding]" )
{
    GIVEN( "A holder initialized with UTF-8" )
    {
        auto* utf8Codec = QTextCodec::codecForName( "UTF-8" );
        REQUIRE( utf8Codec != nullptr );

        TextCodecHolder holder( utf8Codec );

        THEN( "It returns the correct codec" )
        {
            REQUIRE( holder.codec() == utf8Codec );
        }

        THEN( "The encoding parameters match UTF-8" )
        {
            auto params = holder.encodingParameters();
            REQUIRE( params.isUtf8Compatible );
        }

        THEN( "The MIB enum matches" )
        {
            REQUIRE( holder.mibEnum() == utf8Codec->mibEnum() );
        }

        WHEN( "The codec is changed to Latin-1" )
        {
            auto* latin1 = QTextCodec::codecForName( "ISO-8859-1" );
            REQUIRE( latin1 != nullptr );

            holder.setCodec( latin1 );

            THEN( "It returns the new codec" )
            {
                REQUIRE( holder.codec() == latin1 );
            }

            THEN( "Parameters are updated" )
            {
                REQUIRE_FALSE( holder.encodingParameters().isUtf8Compatible );
            }
        }
    }
}
