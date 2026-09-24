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

#include "encodingdetector.h"

#include "textencoding.h"

#include <string>
#include <string_view>

#include "containers.h"
#include "log.h"
#include <uchardet.h>

namespace {

class UchardetHolder {
public:
    UchardetHolder()
        : ud_{ uchardet_new() }
    {
    }
    ~UchardetHolder()
    {
        uchardet_delete( ud_ );
    }

    UchardetHolder( const UchardetHolder& ) = delete;
    UchardetHolder& operator=( const UchardetHolder& ) = delete;

    UchardetHolder( UchardetHolder&& other ) = delete;
    UchardetHolder& operator=( UchardetHolder&& other ) = delete;

    int handle_data( const char* data, size_t len )
    {
        return uchardet_handle_data( ud_, data, len );
    }

    void data_end()
    {
        uchardet_data_end( ud_ );
    }

    const char* get_charset()
    {
        return uchardet_get_charset( ud_ );
    }

private:
    uchardet_t ud_;
};

} // namespace

EncodingParameters::EncodingParameters( const TextEncoding* codec )
{
    static constexpr QChar LineFeed( QChar::LineFeed );
    static constexpr int Utf8Mib = TextEncoding::Utf8Mib;
    static constexpr int Utf16LEMib = TextEncoding::Utf16LEMib;
    static constexpr int Utf16BEMib = TextEncoding::Utf16BEMib;
    static constexpr int Latin1Mib = TextEncoding::Latin1Mib;
    static constexpr int UsAsciiMib = TextEncoding::UsAsciiMib;

    isUtf8Compatible = codec->mibEnum() == Utf8Mib || codec->mibEnum() == UsAsciiMib;
    isUtf16LE = codec->mibEnum() == Utf16LEMib;
    isUtf16BE = codec->mibEnum() == Utf16BEMib;
    isLatin1 = codec->mibEnum() == Latin1Mib;

    const QByteArray encodedLineFeed = codec->fromUnicode( QStringView( &LineFeed, 1 ) );

    lineFeedWidth = static_cast<int>( encodedLineFeed.size() );
    lineFeedIndex
        = encodedLineFeed[ 0 ] == '\n' ? 0 : ( static_cast<int>( encodedLineFeed.size() ) - 1 );
}

const TextEncoding* EncodingDetector::detectEncoding( const logsquirl::vector<char>& block ) const
{
    return detectEncoding( block.data(), block.size() );
}

std::size_t EncodingDetector::sampleSize( const char* bytes, std::size_t size )
{
    if ( size <= MaxSampleSize ) {
        return size;
    }

    const std::string_view sample{ bytes, MaxSampleSize };
    const auto lastLineFeed = sample.rfind( '\n' );
    if ( lastLineFeed == std::string_view::npos ) {
        return MaxSampleSize;
    }

    auto end = lastLineFeed + 1;
    // A UTF-16LE line feed is "\n\0": keep its second byte, so the sample
    // holds whole code units.
    if ( end % 2 == 1 && end < size && bytes[ end ] == '\0' ) {
        ++end;
    }
    return end;
}

const TextEncoding* EncodingDetector::detectEncoding( const char* bytes, std::size_t size ) const
{
    size = sampleSize( bytes, size );

    std::string uchardetGuess;
    int rc = 0;
    {
        // Only uchardet runs under the lock; the codec lookups below are
        // thread-safe on their own.
        UniqueLock lock( mutex_ );

        UchardetHolder ud;
        rc = ud.handle_data( bytes, size );
        if ( rc == 0 ) {
            ud.data_end();
            uchardetGuess = ud.get_charset();
        }
    }

    const TextEncoding* uchardetCodec = nullptr;
    if ( rc == 0 ) {
        LOG_DEBUG << "Uchardet encoding guess " << uchardetGuess;
        uchardetCodec = TextEncoding::forName( uchardetGuess.c_str() );
        if ( uchardetCodec ) {
            LOG_DEBUG << "Uchardet codec selected " << uchardetCodec->name().constData();
        }
        else {
            LOG_DEBUG << "Uchardet codec not found for guess " << uchardetGuess;
        }
    }

    QByteArray blockArray = QByteArray::fromRawData( bytes, static_cast<qsizetype>( size ) );

    const auto* encodingGuess = TextEncoding::forUtfText( blockArray, uchardetCodec );

    LOG_DEBUG << "Final encoding guess " << encodingGuess->name().constData();

    return encodingGuess;
}

QString TextDecoder::decode( const char* bytes, qsizetype size ) const
{
    return decoder->decode( QByteArrayView( bytes, size ) );
}

TextCodecHolder::TextCodecHolder( const TextEncoding* codec )
    : codec_{ codec }
    , encodingParams_{ codec }
{
    assert( codec != nullptr );
}

const TextEncoding* TextCodecHolder::codec() const
{
    SharedLock guard( mutex_ );
    return codec_;
}

EncodingParameters TextCodecHolder::encodingParameters() const
{
    SharedLock guard( mutex_ );
    return encodingParams_;
}

int TextCodecHolder::mibEnum() const
{
    SharedLock guard( mutex_ );
    return codec_->mibEnum();
}

void TextCodecHolder::setCodec( const TextEncoding* codec )
{
    UniqueLock guard( mutex_ );
    codec_ = codec;
    encodingParams_ = EncodingParameters{ codec_ };
}

TextDecoder TextCodecHolder::makeDecoder() const
{
    SharedLock guard( mutex_ );
    return { codec_->makeDecoder(), encodingParams_ };
}