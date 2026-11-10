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

#include <optional>

class TextEncoding;

/// What follows when a Log File is shown in an Encoding (decideEncoding()).
enum class EncodingChange {
    /// Shown in the Encoding it was settled in before: nothing to do.
    Unchanged,
    /// The Index stays: the new Encoding splits the Log File into the same
    /// Log Lines, which only decode differently.
    Redecode,
    /// Indexed again, in the Encoding shown: it splits the Log File into
    /// Log Lines differently than the one the Index was built in.
    ReindexForced,
    /// Indexed again, detecting the Encoding anew: the Encoding shown is the
    /// one detected, and the Index was built in another one forced before.
    ReindexDetecting,
};

/// The Encoding a Log File is shown in, and what follows from it.
struct EncodingDecision {
    /// The MIB of the Encoding shown: the one chosen, else the one detected,
    /// else the locale's.
    int shown{ 0 };
    EncodingChange change{ EncodingChange::Unchanged };
};

/// The one rule that decides the Encoding a Log File is shown in and whether
/// showing it re-indexes the Log File or only decodes its Log Lines again,
/// from Encodings by their MIBs: the one it was settled in before (none when
/// it never was, or when it is to be shown whatever it was before), the one
/// chosen (none is Auto), the one detected, and the Encoding the Index was
/// forced to and the one it guessed. Reads no file and needs no Qt
/// application.
EncodingDecision decideEncoding( std::optional<int> settled, std::optional<int> chosen,
                                 std::optional<int> detected, std::optional<int> indexForced,
                                 std::optional<int> indexGuess );

/// The MIB of an Encoding, none for no Encoding.
std::optional<int> encodingMib( const TextEncoding* encoding );
