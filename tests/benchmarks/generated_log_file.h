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

// The Log Files the log data benchmarks index and read (#275), written at run
// time and never checked in. Every Log Line is generated from its number, so
// the same size always gives the same Log File, and a benchmark that appends
// to a Log File (tailing) can go on where the file ended.

#include <QByteArray>
#include <QFile>
#include <QString>

#include <cstdint>
#include <string>

namespace logdatabenchmark {

enum class LogFileShape {
    // About 90 bytes per Log Line, no tabs: the common application log.
    ShortLines,
    // Tab-separated fields, Log Lines from about 60 bytes to about 2 KB, and
    // every thousandth one about 20 KB long.
    TabsAndLongLines,
};

inline const char* shapeName( LogFileShape shape )
{
    return shape == LogFileShape::ShortLines ? "short lines" : "tabs and long lines";
}

// The size of a generated Log File: about 1 GB, or the number of MiB in the
// environment variable LOGSQUIRL_BENCHMARK_LOG_FILE_MB, for a quick run.
inline std::uint64_t generatedLogFileBytes()
{
    constexpr std::uint64_t Mib = 1024 * 1024;
    const QByteArray mib = qgetenv( "LOGSQUIRL_BENCHMARK_LOG_FILE_MB" );
    bool isNumber = false;
    const auto requested = mib.toULongLong( &isNumber );
    return ( isNumber && requested > 0 ) ? requested * Mib : 1024 * Mib;
}

// Appends Log Line number `index`, with its line feed, to `out`.
inline void appendGeneratedLogLine( LogFileShape shape, std::uint64_t index, std::string& out )
{
    // A cheap scramble, so that neighbouring Log Lines differ.
    const auto scrambled = index * 2654435761u;
    const auto number = std::to_string( scrambled % 10'000'000u );
    const char* level = ( index % 97 == 0 ) ? "ERROR" : ( index % 13 == 0 ) ? "WARN " : "INFO ";

    out += "2026-09-17 12:34:56.";
    out += std::to_string( 100 + index % 900 );
    out += ' ';
    out += level;
    out += " [worker-";
    out += std::to_string( index % 8 );
    out += "] ";

    if ( shape == LogFileShape::ShortLines ) {
        out += "request ";
        out += number;
        out += " handled in ";
        out += std::to_string( scrambled % 997 );
        out += " ms by the frontend";
    }
    else {
        const auto fields = ( index % 1000 == 999 ) ? 2200u : ( scrambled >> 8 ) % 200u;
        out += "request=";
        out += number;
        for ( std::uint64_t field = 0; field < fields; ++field ) {
            out += ( field % 3 == 0 ) ? "\tstatus=ok" : "\tpayload";
        }
    }
    out += '\n';
}

// Writes Log Lines to fileName until it holds at least `bytes`, and returns
// how many Log Lines it holds. False in `written` when the file could not be
// written.
inline std::uint64_t writeGeneratedLogFile( const QString& fileName, LogFileShape shape,
                                            std::uint64_t bytes, bool& written )
{
    QFile file{ fileName };
    written = file.open( QIODevice::WriteOnly | QIODevice::Truncate );
    if ( !written ) {
        return 0;
    }

    constexpr std::size_t ChunkBytes = 4 * 1024 * 1024;
    std::string chunk;
    chunk.reserve( ChunkBytes + 64 * 1024 );

    std::uint64_t lines = 0;
    std::uint64_t total = 0;
    while ( total < bytes ) {
        chunk.clear();
        while ( chunk.size() < ChunkBytes && total + chunk.size() < bytes ) {
            appendGeneratedLogLine( shape, lines++, chunk );
        }
        const auto size = static_cast<qint64>( chunk.size() );
        if ( file.write( chunk.data(), size ) != size ) {
            written = false;
            return lines;
        }
        total += chunk.size();
    }
    return lines;
}

} // namespace logdatabenchmark
