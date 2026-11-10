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

#include "ansicolorsequences.h"
#include "containers.h"
#include "encodingdetector.h"
#include "linetypes.h"
#include "searchblocksource.h"
#include "settingspolicies.h"

#include <QString>
#include <QtGlobal>

#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>

class IndexingData;
class LogFileBytes;

// What a read of Log Lines holds of the log data, so that the read goes on
// without it: the Index and the Log File through shared handles, the rest by
// value, all taken at once (#832). So a read sees one Encoding and one
// Decoding Policy from its start to its end.
struct ReadHandles {
    std::shared_ptr<const IndexingData> index;
    std::shared_ptr<LogFileBytes> file;
    // Made for this read alone, and carrying the Encoding's parameters.
    TextDecoder decoder;
    DecodingPolicy decodingPolicy;
};

// Every read of Log Lines, the views', the Search's, the plugins' and the grep
// CLI's: a block of them, a range, a sparse set, the first bytes of each, or
// their text in UTF-8. It applies the rules every read keeps, here and nowhere
// else: a Log Line too long to read costs its line feed alone and reads as the
// too-long warning (#825), a carriage return ending a Log Line and the byte
// order marks starting it are no part of its text, and a UTF-8 Log Line in a
// UTF-8 Log File is passed through as it was read (loglinetext.h).
//
// A reader reads once, on whichever thread holds it, and is then gone: its
// decoder goes into what it reads. Whoever builds it decides the thread. The
// Index is looked at under its lock, only for the offsets; the Log File is
// read without it, so indexing can publish a block meanwhile (#289). No read
// throws (#824): what cannot be read reads as a warning.
class LogLineReader {
public:
    explicit LogLineReader( ReadHandles handles );

    // The raw Log Lines [first, first + number), as a Search reads them and as
    // the reads of a range decode them. The block ends before the Log Line
    // that would take its bytes past maxBytes, its first Log Line always in
    // it: a Search's block is bounded so (SearchBlockMaxBytes), the view's is
    // not. A Log Line too long to read costs its line feed alone
    // (RawLines::tooLongLines, #825). A range reaching past the last Log Line
    // reads as no Log Lines.
    RawLines readRawLines( LineNumber first, LinesCount number,
                           qint64 maxBytes = std::numeric_limits<qint64>::max() ) &&;

    // The text of the Log Lines [first, first + number), one entry for each:
    // a Log Line that could not be read reads as a warning.
    logsquirl::vector<QString> readLines( LineNumber first, LinesCount number ) &&;
    // The same, with tabs expanded.
    logsquirl::vector<QString> readExpandedLines( LineNumber first, LinesCount number ) &&;
    // The same, as their text under a Decoding Policy that hides ANSI color
    // sequences, with the colors the sequences ask for.
    logsquirl::vector<AnsiColoredText> readAnsiColoredLines( LineNumber first,
                                                             LinesCount number ) &&;

    // The text of a sparse set of Log Lines, one entry per Log Line asked for
    // and in the order asked: lines may come in any order and repeat. Nearby
    // Log Lines are merged into one read of the Log File (planSparseRead());
    // each Log Line is decoded on its own. A Log Line past the last one reads
    // as LinesNotReadWarning.
    logsquirl::vector<QString> readLinesSparse( std::span<const LineNumber> lines ) &&;
    // The same, with tabs expanded.
    logsquirl::vector<QString> readExpandedLinesSparse( std::span<const LineNumber> lines ) &&;
    // The same, as readAnsiColoredLines() reads them.
    logsquirl::vector<AnsiColoredText>
    readAnsiColoredLinesSparse( std::span<const LineNumber> lines ) &&;
    // The same, in UTF-8: the text of each Log Line, followed by a line feed.
    // A Log Line that is UTF-8 in a UTF-8 Log File, or ASCII in an ASCII
    // compatible one, is copied as it was read and never decoded.
    std::string readUtf8LinesSparse( std::span<const LineNumber> lines ) &&;

    // Reads Log Lines one after another, in the order asked, each from at
    // most maxBytes of its bytes, decoded as readLinesSparse() decodes them,
    // and hands each to onLine( text, cut ) -- cut when the Log Line has more
    // bytes than were read -- until onLine returns false. A Log Line past the
    // last one ends the read. Never reads more than maxBytes of a Log Line,
    // nor a Log Line after the one onLine refused (#663).
    void readLinePrefixes( std::span<const LineNumber> lines, qint64 maxBytes,
                           const std::function<bool( QString&& text, bool cut )>& onLine ) &&;

private:
    template <typename OnLine>
    void readSparseLines( std::span<const LineNumber> lines, OnLine&& onLine ) const;

    // The Log Lines [first, first + number), each made a Line by
    // processLine( QString&& text ).
    logsquirl::vector<QString> readRange( LineNumber first, LinesCount number,
                                          QString ( *processLine )( QString&& ) ) &&;

    ReadHandles handles_;
};
