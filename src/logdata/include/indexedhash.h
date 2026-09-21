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

#include <QString>
#include <QtGlobal>

/// What an Index recorded about its Log File when it was built: the byte size
/// it was built at, and digests of those bytes. The header digest covers the
/// first indexing block, the tail digest the last one (taken at offset 0, and
/// equal to the header digest, for a Log File no longer than one block). The
/// full digest covers all of them, and is only recorded without fast
/// modification detection; otherwise it is 0.
struct IndexedHash {
    qint64 size = 0;
    quint64 fullDigest = 0;

    qint64 headerSize = 0;
    quint64 headerDigest = 0;

    qint64 tailSize = 0;
    qint64 tailOffset = 0;
    quint64 tailDigest = 0;
};

/// How an Index fits its Log File as the Log File is now.
enum class IndexFit {
    /// The bytes the Index was built from are there, and nothing more.
    Unchanged,
    /// The bytes the Index was built from are there, and more after them.
    Grown,
    /// The Log File is gone, shorter, empty, or the bytes the Index was built
    /// from are no longer the same.
    Changed,
    /// The Log File exists but cannot be read right now -- locked by another
    /// program, say. That says nothing about the Index either way.
    LogFileUnreadable,
};

/// Which recorded digests the bytes the Index was built from are checked
/// against.
enum class DigestCoverage {
    /// The header and tail digests: cheap however large the Log File, but a
    /// change in the middle of a Log File longer than two indexing blocks
    /// goes unnoticed. Every recorded hash has them, so this is what opening
    /// or following a Log File asks the Index Cache for.
    HeaderAndTail,
    /// The full digest: every byte the Index was built from is read again.
    /// Only a hash recorded without fast modification detection has one.
    Full,
    /// The header and tail digests when the Log File has grown, the full
    /// digest otherwise. What following a Log File indexed without fast
    /// modification detection asks, and what an explicit reload asks the
    /// Index Cache for (#337): an append is told from the header and tail
    /// alone, so a Log File growing by small appends is not read end to end
    /// on every change. A Log File that grew while bytes between its header
    /// and tail changed passes as Grown, the risk fast modification detection
    /// takes on every check.
    FullUnlessGrown,
};

/// The one rule that decides whether an Index still fits its Log File, which
/// both the Index Cache and the change detection of an Open Log File ask.
///
/// The Log File fits while it is no shorter than the recorded size and the
/// chosen digests, taken again over the same byte ranges, match. It is then
/// Unchanged or Grown by its size. An empty Log File has always changed,
/// whatever was recorded: there is nothing an Index of it could still show.
IndexFit indexFit( const IndexedHash& recorded, const QString& logFilePath,
                   DigestCoverage coverage );
