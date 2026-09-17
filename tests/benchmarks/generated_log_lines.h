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

// The Log Lines the scrolling benchmarks scroll through: a million of them,
// generated from their number, so that reading one costs the same wherever it
// is and nothing proportional to the Log File is paid up front. Between 40 and
// about 600 characters long, words separated by spaces and now and then a tab,
// so that a Log Line wraps into anything from one Visual Line to several.
//
// Uses nothing newer than what origin/master had before #246, so that the text
// view benchmark can be built on both sides of an A/B comparison.

#include <QLatin1Char>
#include <QString>

#include <cstdint>

namespace scrollingbenchmark {

constexpr std::uint64_t LogLineCount = 1'000'000;
constexpr int LongestLogLine = 40 + 560;

inline QString generatedLogLine( std::uint64_t index )
{
    // A cheap scramble, so that neighbouring Log Lines differ in length.
    const auto scrambled = ( index * 2654435761u ) % 1000u;
    const int words = static_cast<int>( scrambled % 70u );

    QString line = QStringLiteral( "2026-09-17 12:34:56.%1 INFO  [worker-%2] " )
                       .arg( static_cast<int>( index % 1000u ), 3, 10, QLatin1Char( '0' ) )
                       .arg( static_cast<int>( index % 8u ) );
    for ( int word = 0; word < words; ++word ) {
        line += ( word % 11 == 10 ) ? QStringLiteral( "\tvalue" ) : QStringLiteral( " payload" );
    }
    return line;
}

} // namespace scrollingbenchmark
