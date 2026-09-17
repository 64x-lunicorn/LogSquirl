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

// Helpers shared by the tests of what an Index records about its Log File:
// the Index Cache's and the rule that decides whether an Index still fits
// its Log File (#235). The Log Files are real files, and their hash is
// computed from them the way the indexer computes it.

#include "filedigest.h"
#include "indexedhash.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <cstddef>

#include <catch2/catch.hpp>

inline constexpr qint64 DigestBlockSize = 5 * 1024 * 1024;

inline void writeFile( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( file.write( content ) == content.size() );
}

inline quint64 digestOf( const QByteArray& data )
{
    FileDigest digest;
    digest.addData( data.constData(), static_cast<std::size_t>( data.size() ) );
    return digest.digest();
}

// The hash the indexer records for a Log File: a digest of its first block;
// a digest of its tail, which starts at the last but one multiple of half a
// block before its end, or at offset 0 for a file shorter than one block; and
// a digest of all of it.
inline IndexedHash hashOfFile( const QString& path )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    const auto content = file.readAll();

    IndexedHash hash;
    hash.size = content.size();
    hash.fullDigest = digestOf( content );

    const auto header = content.left( DigestBlockSize );
    hash.headerSize = header.size();
    hash.headerDigest = digestOf( header );

    if ( content.size() < DigestBlockSize ) {
        hash.tailOffset = 0;
        hash.tailSize = header.size();
        hash.tailDigest = hash.headerDigest;
    }
    else {
        const auto halfBlock = DigestBlockSize / 2;
        hash.tailOffset = ( content.size() / halfBlock - 1 ) * halfBlock;
        const auto tail = content.mid( hash.tailOffset );
        hash.tailSize = tail.size();
        hash.tailDigest = digestOf( tail );
    }
    return hash;
}
