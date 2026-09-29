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

#include <cstdint>
#include <utility>

#include <QString>

#include "containers.h"

// Removes from text what a Decoding Policy that hides ANSI color sequences
// removes from every Log Line: the color (SGR) and erase-in-line sequences.
//
// The pattern is compiled once per process, and a text without an escape
// character is left as it is without running it. Safe to call from any
// number of threads at once, so every reader of Log Lines -- one line at a
// time or a block for a Search -- shares the one compiled pattern (#278).
void removeAnsiColorSequences( QString& text );

// A color an ANSI color sequence asks for: the Log Line's own color, one of
// the 256 indexed colors, or a truecolor. Indices 0-15 are the basic colors,
// which the Theme gives; the others are the xterm colors every terminal has.
class AnsiColor {
public:
    enum class Kind : std::uint8_t { LineColor, Indexed, Rgb };

    // The Log Line's own color.
    constexpr AnsiColor() = default;

    static constexpr AnsiColor indexed( std::uint8_t index )
    {
        AnsiColor color;
        color.kind_ = Kind::Indexed;
        color.value_ = index;
        return color;
    }

    static constexpr AnsiColor rgb( std::uint8_t red, std::uint8_t green, std::uint8_t blue )
    {
        AnsiColor color;
        color.kind_ = Kind::Rgb;
        color.value_ = ( std::uint32_t{ red } << 16 ) | ( std::uint32_t{ green } << 8 ) | blue;
        return color;
    }

    constexpr Kind kind() const
    {
        return kind_;
    }

    constexpr bool isLineColor() const
    {
        return kind_ == Kind::LineColor;
    }

    // The index of an Indexed color.
    constexpr std::uint8_t index() const
    {
        return static_cast<std::uint8_t>( value_ );
    }

    // The 0xRRGGBB of an Rgb color.
    constexpr std::uint32_t rgbValue() const
    {
        return value_;
    }

    constexpr bool operator==( const AnsiColor& ) const = default;

private:
    Kind kind_ = Kind::LineColor;
    std::uint32_t value_ = 0;
};

// A run of a Log Line's text that ANSI color sequences color, in the columns
// of the text without the sequences. At least one of the two colors is not
// the line's own.
struct AnsiColorSpan {
    int start = 0;
    int length = 0;
    AnsiColor foreground;
    AnsiColor background;

    constexpr bool operator==( const AnsiColorSpan& ) const = default;
};

// A Log Line's text without its ANSI color sequences, and the colors they
// asked for.
struct AnsiColoredText {
    AnsiColoredText() = default;
    explicit AnsiColoredText( QString lineText, logsquirl::vector<AnsiColorSpan> colorSpans = {} )
        : text( std::move( lineText ) )
        , spans( std::move( colorSpans ) )
    {
    }

    QString text;
    logsquirl::vector<AnsiColorSpan> spans;
};

// Splits text into the text removeAnsiColorSequences() leaves of it and the
// colors its ANSI color sequences ask for, in one linear pass. The text is
// always exactly what removeAnsiColorSequences() leaves; a text without an
// escape character is returned as it is, without parsing.
//
// Each text starts with the line's own colors: nothing carries over from the
// text before. Only foreground and background colors are kept (SGR 30-37,
// 39, 40-47, 49, 90-97, 100-107, 38/48 with 5;n or 2;r;g;b); every other
// code, and erase in line, is removed without effect. A sequence with a
// malformed color (e.g. 38;5 without an index) is removed without effect.
AnsiColoredText parseAnsiColorSequences( QString text );
