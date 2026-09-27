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
#include "encodingdetector.h"
#include "linetypes.h"

#include "textencoding.h"
#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <string_view>

// A block of raw Log Lines [startLine, startLine + endOfLines.size()): the
// bytes as they are in the Log File, where each Log Line ends, and how to
// decode them. endOfLines holds offsets into buffer, one past each line feed.
struct RawLines {
    LineNumber startLine;

    logsquirl::vector<char> buffer;
    logsquirl::vector<qint64> endOfLines;

    TextDecoder textDecoder;

    // Whether decoding removes ANSI color sequences, as the Decoding Policy
    // the block was read under says.
    bool hideAnsiColorSequences{};

public:
    // Every Log Line of the block as its text (loglinetext.h): as it is
    // displayed, before untabifying.
    logsquirl::vector<QString> decodeLines() const;

    // Every Log Line of the block as its text, the same as decodeLines(), in
    // UTF-8 as a Search matches it. A view stays valid while the block does
    // and until the next call: it points into buffer, or into UTF-8 converted
    // for this call.
    logsquirl::vector<std::string_view> buildUtf8View() const;

private:
    mutable QByteArray utf8Data_;
};

// Where a Search reads the Log Lines it matches from: it hands out blocks of
// raw Log Lines, one block per call, so a run pays for one call per block and
// none per Log Line. The log data is one adapter; a test can hand a Search
// Log Lines held in memory instead. Indexing does not read through this: it
// reads bytes, not Log Lines.
//
// getLinesRaw() is called from the Search's threads while the Search Session
// asks getNbLines() on its own, so an adapter must be safe to use from both.
class SearchBlockSource {
public:
    SearchBlockSource() = default;
    virtual ~SearchBlockSource() = default;

    SearchBlockSource( const SearchBlockSource& ) = delete;
    SearchBlockSource& operator=( const SearchBlockSource& ) = delete;
    SearchBlockSource( SearchBlockSource&& ) = delete;
    SearchBlockSource& operator=( SearchBlockSource&& ) = delete;

    // How many Log Lines there are to search right now.
    virtual LinesCount getNbLines() const = 0;

    // The raw Log Lines [first, first + number). A block that reaches past
    // the last Log Line comes back empty. May throw: a Search whose block
    // cannot be read fails.
    virtual RawLines getLinesRaw( LineNumber first, LinesCount number ) const = 0;

    // Bracket one run of a Search: whatever the source needs to keep open
    // while the run reads (the Log File, unless it is kept closed) is held
    // from attachReader() until the matching detachReader(). The Background
    // Run the Search runs through pairs them; nothing else calls them.
    virtual void attachReader() const = 0;
    virtual void detachReader() const = 0;
};
