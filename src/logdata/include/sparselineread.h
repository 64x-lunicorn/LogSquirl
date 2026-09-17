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

#include "containers.h"
#include "linetypes.h"

#include <QtGlobal>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

// How close Log Lines must be to share one read of a sparse read. Reading
// the bytes between two Log Lines costs a copy of them; reading the Log Lines
// separately costs a seek and a read of its own.
struct SparseReadLimits {
    // At most this many bytes not asked for lie between two Log Lines that
    // share a read.
    qint64 maxBytesBetween = 4 * 1024;
    // A read joins a Log Line across a gap only while it is shorter than
    // this. The Log Line right after the last one it holds always joins.
    qint64 maxReadBytes = 1024 * 1024;
};

// One read of a sparse read: the bytes [firstByte, firstByte + size) of the
// Log File, with where each of the Log Lines asked for lies in them.
struct SparseRead {
    struct Line {
        // Which of the Log Lines asked for this is: its index in the request.
        std::size_t request;
        // Where the Log Line starts and ends in the bytes read; end is one
        // past its line feed, as in the Index.
        qint64 begin;
        qint64 end;
    };

    OffsetInFile firstByte;
    qint64 size = 0;
    // In ascending order of Log Line.
    logsquirl::vector<Line> lines;
};

// Where the Log Lines [first, first + count) end, as the Index has it. May
// come back shorter when the Index reaches less far.
using EndOfLineOffsets
    = std::function<logsquirl::vector<OffsetInFile>( LineNumber first, LinesCount count )>;

// Plans reading lines from a Log File of nbLines Log Lines: nearby Log
// Lines are merged into one read, and each Log Line asked for is in exactly
// one read, in ascending order of Log Line. The Index is looked up once for
// Log Lines a few Log Lines apart, so no part of it is decoded twice and the
// Log Lines between far-apart ones are not decoded at all. lines may come in
// any order and repeat. A Log Line at or past nbLines, or one the Index has
// no offset for, is in no read.
logsquirl::vector<SparseRead> planSparseRead( std::span<const LineNumber> lines, LinesCount nbLines,
                                              const EndOfLineOffsets& endOfLineOffsets,
                                              const SparseReadLimits& limits = {} );
