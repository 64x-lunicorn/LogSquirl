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

#ifndef ENCODINGDETECTOR_H
#define ENCODINGDETECTOR_H

#include "containers.h"
#include "synchronization.h"
#include "textencoding.h"

#include <QByteArray>
#include <cstddef>
#include <memory>

struct EncodingParameters {
    EncodingParameters() = default;
    explicit EncodingParameters( const TextEncoding* codec );

    bool isUtf8Compatible{ false };
    bool isUtf16LE{ false };
    bool isUtf16BE{ false };
    bool isLatin1{ false };

    int lineFeedWidth{ 1 };
    int lineFeedIndex{ 0 };

    bool operator==( const EncodingParameters& other ) const
    {
        return lineFeedWidth == other.lineFeedWidth && lineFeedIndex == other.lineFeedIndex;
    }

    bool operator!=( const EncodingParameters& other ) const
    {
        return !operator==( other );
    }

    int getBeforeCrOffset() const
    {
        return lineFeedIndex;
    }

    int getAfterCrOffset() const
    {
        return lineFeedWidth - lineFeedIndex - 1;
    }
};

class EncodingDetector {
public:
    static EncodingDetector& getInstance()
    {
        static EncodingDetector instance;
        return instance;
    }

    EncodingDetector( const EncodingDetector& ) = delete;
    EncodingDetector& operator=( const EncodingDetector& ) = delete;
    EncodingDetector( const EncodingDetector&& ) = delete;
    EncodingDetector& operator=( const EncodingDetector&& ) = delete;

    // The detector looks at no more than this many leading bytes of a block:
    // a guess from a few hundred kilobytes is as good as one from the whole
    // indexing block, and costs a fraction of the time.
    static constexpr std::size_t MaxSampleSize = 256 * 1024;

    // How many leading bytes of the block the detector looks at: all of a
    // short block, else at most MaxSampleSize, ending after the last line
    // feed in there so no character is cut in half.
    static std::size_t sampleSize( const char* bytes, std::size_t size );

    const TextEncoding* detectEncoding( const logsquirl::vector<char>& block ) const;
    const TextEncoding* detectEncoding( const char* bytes, std::size_t size ) const;

private:
    EncodingDetector() = default;
    ~EncodingDetector() = default;

    mutable SharedMutex mutex_;
};

struct TextDecoder {
    std::unique_ptr<QStringDecoder> decoder;

    // Decodes the bytes, carrying a partial multi-byte sequence over to the
    // next call.
    QString decode( const char* bytes, qsizetype size ) const;
    EncodingParameters encodingParams;
};

class TextCodecHolder {
public:
    explicit TextCodecHolder( const TextEncoding* codec );

    void setCodec( const TextEncoding* codec );

    const TextEncoding* codec() const;
    EncodingParameters encodingParameters() const;
    int mibEnum() const;

    TextDecoder makeDecoder() const;

private:
    const TextEncoding* codec_;
    EncodingParameters encodingParams_;
    mutable SharedMutex mutex_;
};

#endif // ENCODINGDETECTOR_H
