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

#pragma once

#include "filedigest.h"

#include <QtGlobal>

#include <optional>

/// A digest of one byte range of a Log File.
struct RangeDigest {
    qint64 offset = 0;
    qint64 size = 0;
    quint64 digest = 0;
};

/// The header and tail digests of an Index, taken from the bytes as they are
/// indexed, so that indexing what was appended to a Log File does not read
/// its header and tail again (#277).
///
/// The header is the first block of the Log File. The tail starts at a
/// multiple of half a block, the last but one before its end, so it is at
/// least half a block and less than a whole one long, and it can be carried
/// on from one append to the next: at every half block a new digest is
/// started, and the one before it is kept going until the tail moves past
/// its start. A Log File shorter than one block has its tail at offset 0,
/// the same bytes as its header.
///
/// Only bytes fed one after the other are digested: bytes fed anywhere else
/// than where the last ones ended start the digests over from there, and
/// what they cannot cover then is not known until it is read from the Log
/// File and fed again.
class HeaderAndTailDigests {
public:
    explicit HeaderAndTailDigests( qint64 blockSize );

    /// The range the tail of a Log File of the given size covers.
    static RangeDigest tailRange( qint64 blockSize, qint64 logFileSize );

    /// Forgets every byte fed: the next ones fed start at offset 0.
    void reset();

    /// The Log File is at least this long by the time its digests are asked
    /// for, so bytes before where its tail starts are not digested for the
    /// tail. Only saves work: bytes still needed are digested all the same.
    void expectLogFileSize( qint64 size );

    /// Digests bytes of the Log File starting at offset.
    void add( qint64 offset, const char* data, qint64 size );

    /// The header digest of a Log File of the given size, if the bytes fed
    /// cover it and end there.
    std::optional<RangeDigest> header( qint64 logFileSize ) const;

    /// The tail digest of a Log File of the given size, if the bytes fed
    /// cover it and end there.
    std::optional<RangeDigest> tail( qint64 logFileSize ) const;

private:
    // A digest of the bytes fed from offset up to end_.
    struct Running {
        qint64 offset = -1;
        FileDigest digest;
    };

    qint64 halfBlock() const
    {
        return blockSize_ / 2;
    }

    void startOver( qint64 offset );
    void startTailSegment( qint64 offset );

    qint64 blockSize_;
    qint64 end_ = 0;
    qint64 tailStartsFrom_ = 0;

    // From offset 0, when the bytes fed start there; fed the first block only.
    bool headerRunning_ = true;
    FileDigest header_;

    // Started at the last two multiples of half a block fed.
    Running previousSegment_;
    Running lastSegment_;
};
