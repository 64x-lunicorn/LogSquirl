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

// Parsing the ANSI color sequences of Log Lines into their text and colors
// (#573) next to removing them, as a Decoding Policy that hides them does, on
// the Log Lines one screen of a Text View shows: 60 of them. Parsing runs only
// for the Log Lines entering the Viewport under Show colors, so this is its
// whole cost per screen. Links logsquirl_logdata only.

#include "ansicolorsequences.h"

#include <QString>

#include <vector>

#include "instruction_count.h"
#include <catch2/catch_test_macros.hpp>

namespace {

constexpr int ScreenLines = 60;

// What a colored logger writes: a colored level, a colored source and a bold
// word, reset after each.
QString coloredLogLine( int index )
{
    return QStringLiteral( "2026-09-17 12:34:56.%1 \x1B[32mINFO \x1B[0m [\x1B[36mworker-%2\x1B[0m] "
                           "request %3 handled in %4 ms by the \x1B[1mfrontend\x1B[0m" )
        .arg( index % 1000, 3, 10, QLatin1Char( '0' ) )
        .arg( index % 8 )
        .arg( index * 7919 % 10'000'000 )
        .arg( index % 997 );
}

// A 256-color and a truecolor foreground and background on a longer line.
QString extendedColorLogLine( int index )
{
    QString line = QStringLiteral( "\x1B[38;5;%1m\x1B[48;2;30;30;%2m%3\x1B[0m" )
                       .arg( 16 + index % 216 )
                       .arg( index % 256 )
                       .arg( coloredLogLine( index ) );
    for ( int word = 0; word < 20; ++word ) {
        line += QStringLiteral( " \x1B[3%1mword\x1B[39m" ).arg( word % 8 );
    }
    return line;
}

// The same Log Line with no escape character: parsed without parsing.
QString plainLogLine( int index )
{
    auto line = coloredLogLine( index );
    removeAnsiColorSequences( line );
    return line;
}

template <typename MakeLine>
std::vector<QString> screenOf( MakeLine makeLine )
{
    std::vector<QString> lines;
    for ( int index = 0; index < ScreenLines; ++index ) {
        lines.push_back( makeLine( index ) );
    }
    return lines;
}

std::size_t parseAll( const std::vector<QString>& lines )
{
    std::size_t spans = 0;
    for ( const auto& line : lines ) {
        spans += parseAnsiColorSequences( line ).spans.size();
    }
    return spans;
}

qsizetype removeAll( const std::vector<QString>& lines )
{
    qsizetype size = 0;
    for ( auto line : lines ) {
        removeAnsiColorSequences( line );
        size += line.size();
    }
    return size;
}

} // namespace

TEST_CASE( "ANSI color sequence parser benchmarks", "[ansi-benchmark]" )
{
    const auto colored = screenOf( coloredLogLine );
    const auto extended = screenOf( extendedColorLogLine );
    const auto plain = screenOf( plainLogLine );

    BENCHMARK( "a screen of colored Log Lines: parsed" )
    {
        return parseAll( colored );
    };
    BENCHMARK( "a screen of colored Log Lines: sequences removed" )
    {
        return removeAll( colored );
    };

    BENCHMARK( "a screen of long 256-color and truecolor Log Lines: parsed" )
    {
        return parseAll( extended );
    };
    BENCHMARK( "a screen of long 256-color and truecolor Log Lines: sequences removed" )
    {
        return removeAll( extended );
    };

    BENCHMARK( "a screen of Log Lines without an escape character: parsed" )
    {
        return parseAll( plain );
    };
    BENCHMARK( "a screen of Log Lines without an escape character: sequences removed" )
    {
        return removeAll( plain );
    };
}
