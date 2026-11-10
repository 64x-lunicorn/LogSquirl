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

// This file implements the Log Line Reader: every read of Log Lines, from the
// Index and the bytes of the Log File.

#include "loglinereader.h"

#include <algorithm>
#include <exception>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "fileholder.h"
#include "indexoperation.h"
#include "log.h"
#include "loglinetext.h"
#include "sparselineread.h"

namespace {

// A Log Line as readLines() returns it: its text, as it is.
QString unchanged( QString&& lineText )
{
    return std::move( lineText );
}

// A Log Line as readExpandedLines() returns it, from its text.
QString untabified( QString&& lineText )
{
    return untabify( std::move( lineText ) );
}

// Holds the bytes of the Log File for one read.
class ScopedRead {
public:
    explicit ScopedRead( LogFileBytes& file )
        : file_( file )
    {
        file_.beginRead();
    }

    ~ScopedRead()
    {
        file_.endRead();
    }

    ScopedRead( const ScopedRead& ) = delete;
    ScopedRead& operator=( const ScopedRead& ) = delete;
    ScopedRead( ScopedRead&& ) = delete;
    ScopedRead& operator=( ScopedRead&& ) = delete;

    qint64 readAt( qint64 offset, char* out, qint64 size )
    {
        return file_.readAt( offset, out, size );
    }

private:
    LogFileBytes& file_;
};

} // namespace

LogLineReader::LogLineReader( ReadHandles handles )
    : handles_( std::move( handles ) )
{
}

RawLines LogLineReader::readRawLines( LineNumber firstLine, LinesCount number, qint64 maxBytes ) &&
{
    RawLines rawLines;
    rawLines.startLine = firstLine;

    try {
        OffsetInFile::UnderlyingType firstByte = 0;
        logsquirl::vector<OffsetInFile> endOfLines;
        {
            // Only the offsets are taken under the index lock: the file is
            // read without it, so indexing can publish a block meanwhile.
            IndexingData::ConstAccessor scopedAccessor{ handles_.index.get() };
            if ( ( firstLine + number ).get() > scopedAccessor.getNbLines().get() ) {
                LOG_WARNING << "Lines out of bound asked for";
                return {}; /* exception? */
            }

            rawLines.hideAnsiColorSequences = handles_.decodingPolicy.hideAnsiColorSequences;

            firstByte = ( firstLine == 0_lnum )
                            ? 0
                            : scopedAccessor.getEndOfLineOffset( firstLine - 1_lcount ).get();

            endOfLines = scopedAccessor.getEndOfLineOffsets( firstLine, number );
        }

        // Guard against the (rare) case where the index has been concurrently truncated
        // between the firstByte lookup above and this call: back() on an empty vector is
        // undefined behavior.
        if ( endOfLines.empty() ) {
            LOG_DEBUG << "no end-of-line offsets returned for firstLine=" << firstLine
                      << " number=" << number;
            return rawLines;
        }
        // Before a Log File is attached, its Index is empty and nothing is
        // read; nor is anything read without one.
        if ( !handles_.file ) {
            return rawLines;
        }

        // Where each Log Line ends in the buffer. A Log Line too long to read
        // is left out of it but for a line feed, and the block ends before
        // the Log Line that would take the buffer past maxBytes (#825).
        const auto& encodingParams = handles_.decoder.encodingParams;
        const auto lineFeedWidth = encodingParams.lineFeedWidth;
        rawLines.endOfLines.reserve( endOfLines.size() );
        qint64 lineStart = firstByte;
        qint64 skipped = 0;
        for ( const auto& offset : endOfLines ) {
            const auto end = offset.get();
            if ( end < lineStart ) {
                // Out of a tampered Index: the block reads as warnings (#824).
                throw std::runtime_error( "Index offsets out of order" );
            }
            const bool tooLong = end - lineStart - lineFeedWidth >= MaxLogLineLength;
            if ( tooLong ) {
                skipped += end - lineStart - lineFeedWidth;
            }
            const auto bufferEnd = end - firstByte - skipped;
            if ( bufferEnd > maxBytes && !rawLines.endOfLines.empty() ) {
                break;
            }
            if ( tooLong ) {
                rawLines.tooLongLines.push_back( rawLines.endOfLines.size() );
            }
            rawLines.endOfLines.push_back( bufferEnd );
            lineStart = end;
        }

        const auto bytesToRead = rawLines.endOfLines.back();
        LOG_DEBUG << "will try to read:" << bytesToRead << " bytes";
        rawLines.buffer.resize( static_cast<std::size_t>( bytesToRead ) );

        {
            ScopedRead file( *handles_.file );
            // Reads the bytes [fileOffset, fileOffset + size) of the Log File
            // to bufferOffset in the buffer. When the Log File is shorter than
            // its Index says, as when it was cut short on disk and is not
            // indexed again yet, only the bytes read are kept: the Log Lines
            // past them read as a warning.
            const auto readRun
                = [ &rawLines, &file ]( qint64 fileOffset, qint64 bufferOffset, qint64 size ) {
                      if ( size <= 0 ) {
                          return size == 0;
                      }
                      const auto bytesRead
                          = file.readAt( fileOffset, rawLines.buffer.data() + bufferOffset, size );
                      if ( bytesRead != size ) {
                          LOG_DEBUG << "failed to read " << size << " bytes, got " << bytesRead;
                          rawLines.buffer.resize( static_cast<std::size_t>(
                              bufferOffset + std::max( bytesRead, qint64{ 0 } ) ) );
                          return false;
                      }
                      return true;
                  };

            if ( rawLines.tooLongLines.empty() ) {
                // One read for a block of ordinary Log Lines.
                readRun( firstByte, 0, bytesToRead );
            }
            else {
                // A run of bytes before each Log Line too long to read, then
                // a line feed in the Encoding standing for it, then the run
                // after the last one.
                qint64 runFile = firstByte;
                qint64 runBuffer = 0;
                bool complete = true;
                for ( const auto lineIndex : rawLines.tooLongLines ) {
                    const auto bufferEnd = rawLines.endOfLines[ lineIndex ];
                    complete = readRun( runFile, runBuffer, bufferEnd - lineFeedWidth - runBuffer );
                    if ( !complete ) {
                        break;
                    }
                    rawLines.buffer[ static_cast<std::size_t>(
                        bufferEnd - lineFeedWidth + encodingParams.lineFeedIndex ) ] = '\n';
                    runFile = endOfLines[ lineIndex ].get();
                    runBuffer = bufferEnd;
                }
                if ( complete ) {
                    readRun( runFile, runBuffer, bytesToRead - runBuffer );
                }
            }
        }

        LOG_DEBUG << "done reading lines:" << rawLines.buffer.size();
        rawLines.textDecoder = std::move( handles_.decoder );
        return rawLines;

    } catch ( const std::bad_alloc& ) {
        LOG_ERROR << "not enough memory";
        rawLines.endOfLines.clear();
        rawLines.tooLongLines.clear();
        rawLines.buffer.clear();
        return rawLines;
    } catch ( const std::exception& error ) {
        // Offsets out of a tampered Index make the buffer's size absurd;
        // the block reads as warnings rather than throwing into the paint
        // path (#824).
        LOG_ERROR << "cannot read Log Lines: " << error.what();
        rawLines.endOfLines.clear();
        rawLines.tooLongLines.clear();
        rawLines.buffer.clear();
        return rawLines;
    }
}

logsquirl::vector<QString> LogLineReader::readLines( LineNumber first, LinesCount number ) &&
{
    return std::move( *this ).readRange( first, number, unchanged );
}

logsquirl::vector<QString> LogLineReader::readExpandedLines( LineNumber first,
                                                             LinesCount number ) &&
{
    return std::move( *this ).readRange( first, number, untabified );
}

logsquirl::vector<QString> LogLineReader::readRange( LineNumber firstLine, LinesCount number,
                                                     QString ( *processLine )( QString&& ) ) &&
{
    LOG_DEBUG << "firstLine:" << firstLine << " nb:" << number;

    if ( number.get() == 0 ) {
        return logsquirl::vector<QString>();
    }

    logsquirl::vector<QString> processedLines;
    try {
        const auto rawLines = std::move( *this ).readRawLines( firstLine, number );
        auto decodedLines = rawLines.decodeLines();

        processedLines.reserve( decodedLines.size() );

        for ( auto&& line : decodedLines ) {
            processedLines.push_back( processLine( std::move( line ) ) );
        }

    } catch ( const std::bad_alloc& e ) {
        LOG_ERROR << "not enough memory " << e.what();
        processedLines.push_back( warningText( NotEnoughMemoryWarning ) );
    } catch ( const std::exception& error ) {
        // The Log Lines not made yet read as warnings below (#824).
        LOG_ERROR << "cannot read Log Lines: " << error.what();
    }

    processedLines.reserve( number.get() - processedLines.size() );
    while ( processedLines.size() < number.get() ) {
        processedLines.push_back( warningText( LinesNotReadWarning ) );
    }

    return processedLines;
}

logsquirl::vector<AnsiColoredText> LogLineReader::readAnsiColoredLines( LineNumber firstLine,
                                                                        LinesCount number ) &&
{
    if ( number.get() == 0 ) {
        return {};
    }

    logsquirl::vector<AnsiColoredText> lines;
    try {
        lines = std::move( *this ).readRawLines( firstLine, number ).decodeAnsiColoredLines();
    } catch ( const std::bad_alloc& e ) {
        LOG_ERROR << "not enough memory " << e.what();
        lines.push_back( AnsiColoredText{ warningText( NotEnoughMemoryWarning ) } );
    } catch ( const std::exception& error ) {
        // As in readRange() (#824).
        LOG_ERROR << "cannot read Log Lines: " << error.what();
    }

    while ( lines.size() < number.get() ) {
        lines.push_back( AnsiColoredText{ warningText( LinesNotReadWarning ) } );
    }
    return lines;
}

template <typename OnLine>
void LogLineReader::readSparseLines( std::span<const LineNumber> lines, OnLine&& onLine ) const
{
    // Everything the reads need from the Index is copied under the index
    // lock; the file is read without it, so indexing can publish a block
    // meanwhile (#289). A read cut short by a Log File shorter than its Index
    // reads as a warning below.
    logsquirl::vector<SparseRead> reads;
    const auto hideAnsiColorSequences = handles_.decodingPolicy.hideAnsiColorSequences;
    const auto lineFeedWidth = handles_.decoder.encodingParams.lineFeedWidth;
    {
        IndexingData::ConstAccessor scopedAccessor{ handles_.index.get() };
        SparseReadLimits limits;
        limits.lineFeedWidth = lineFeedWidth;
        reads = planSparseRead(
            lines, scopedAccessor.getNbLines(),
            [ &scopedAccessor ]( LineNumber first, LinesCount count ) {
                return scopedAccessor.getEndOfLineOffsets( first, count );
            },
            limits );
    }

    if ( reads.empty() || !handles_.file ) {
        return;
    }

    ScopedRead file( *handles_.file );

    logsquirl::vector<char> buffer;
    for ( const auto& read : reads ) {
        // A read with no bytes holds a Log Line too long to read, which reads
        // as the warning below without touching the Log File (#825).
        qint64 bytesRead = 0;
        if ( read.size > 0 ) {
            buffer.resize( static_cast<std::size_t>( read.size ) );
            bytesRead = file.readAt( read.firstByte.get(), buffer.data(), read.size );
            if ( bytesRead != read.size ) {
                LOG_DEBUG << "failed to read " << read.size << " bytes, got " << bytesRead;
            }
        }
        // A read that could not seek read nothing: every Log Line of it reads
        // as the warning, an empty one included (#845).
        if ( bytesRead < 0 ) {
            for ( const auto& line : read.lines ) {
                onLine( ReadLogLine{ .request = line.request,
                                     .bytes = {},
                                     .warning = FileReadFailedWarning,
                                     .hideAnsiColorSequences = hideAnsiColorSequences } );
            }
            continue;
        }

        const std::string_view bytes
            = bytesRead > 0
                  ? std::string_view( buffer.data(), static_cast<std::size_t>( bytesRead ) )
                  : std::string_view();
        for ( const auto& line : read.lines ) {
            onLine( readLogLine( line.request, bytes, line.begin,
                                 line.end - line.begin - lineFeedWidth, hideAnsiColorSequences ) );
        }
    }
}

logsquirl::vector<QString> LogLineReader::readLinesSparse( std::span<const LineNumber> lines ) &&
{
    return decodeReadLogLines<QString>(
        handles_.decoder, lines.size(),
        [ this, lines ]( const auto& onLine ) { readSparseLines( lines, onLine ); }, logLineText );
}

logsquirl::vector<QString>
LogLineReader::readExpandedLinesSparse( std::span<const LineNumber> lines ) &&
{
    return decodeReadLogLines<QString>(
        handles_.decoder, lines.size(),
        [ this, lines ]( const auto& onLine ) { readSparseLines( lines, onLine ); },
        []( QString&& decodedLine, bool hideAnsiColorSequences ) {
            return untabified( logLineText( std::move( decodedLine ), hideAnsiColorSequences ) );
        } );
}

logsquirl::vector<AnsiColoredText>
LogLineReader::readAnsiColoredLinesSparse( std::span<const LineNumber> lines ) &&
{
    return decodeReadLogLines<AnsiColoredText>(
        handles_.decoder, lines.size(),
        [ this, lines ]( const auto& onLine ) { readSparseLines( lines, onLine ); },
        ansiColoredLogLineText );
}

std::string LogLineReader::readUtf8LinesSparse( std::span<const LineNumber> lines ) &&
{
    return decodeReadLogLinesToUtf8(
        handles_.decoder, lines.size(),
        [ this, lines ]( const auto& onLine ) { readSparseLines( lines, onLine ); } );
}

void LogLineReader::readLinePrefixes(
    std::span<const LineNumber> lines, qint64 maxBytes,
    const std::function<bool( QString&& text, bool cut )>& onLine ) &&
{
    // Where each Log Line lies, taken under the index lock; the file is read
    // without it, as by readSparseLines().
    struct Place {
        OffsetInFile begin;
        qint64 length = 0; // without its line feed
    };
    logsquirl::vector<Place> places;
    const auto hideAnsiColorSequences = handles_.decodingPolicy.hideAnsiColorSequences;
    const auto& textDecoder = handles_.decoder;
    const auto lineFeedWidth = textDecoder.encodingParams.lineFeedWidth;
    {
        IndexingData::ConstAccessor scopedAccessor{ handles_.index.get() };
        const auto nbLines = scopedAccessor.getNbLines();
        places.reserve( lines.size() );
        for ( const auto line : lines ) {
            if ( line.get() >= nbLines.get() ) {
                break;
            }
            const auto first = line.get() == 0 ? line : line - 1_lcount;
            const auto count = line.get() == 0 ? 1_lcount : 2_lcount;
            const auto ends = scopedAccessor.getEndOfLineOffsets( first, count );
            if ( ends.size() != count.get() ) {
                break;
            }
            const auto begin = line.get() == 0 ? OffsetInFile( 0 ) : ends.front();
            places.push_back( Place{ .begin = begin,
                                     .length = std::max( ( ends.back() - begin ).get()
                                                             - static_cast<qint64>( lineFeedWidth ),
                                                         qint64{ 0 } ) } );
        }
    }
    if ( places.empty() || !handles_.file ) {
        return;
    }

    ScopedRead file( *handles_.file );
    logsquirl::vector<char> buffer;
    for ( const auto& place : places ) {
        const auto toRead = std::min( place.length, std::max( maxBytes, qint64{ 0 } ) );
        buffer.resize( static_cast<std::size_t>( toRead ) );
        const auto bytesRead
            = toRead > 0 ? file.readAt( place.begin.get(), buffer.data(), toRead ) : qint64{ 0 };
        // A Log Line the file could not seek to reads as the warning (#845).
        if ( bytesRead < 0 ) {
            if ( !onLine( QString::fromUtf8( FileReadFailedWarning ), false ) ) {
                return;
            }
            continue;
        }
        const bool cut = place.length > toRead;

        textDecoder.decoder->resetState();
        auto text
            = logLineText( textDecoder.decode( buffer.data(), static_cast<qsizetype>( bytesRead ) ),
                           hideAnsiColorSequences );
        // A character the bound cut in two decodes as a replacement character.
        while ( cut && text.endsWith( QChar::ReplacementCharacter ) ) {
            text.chop( 1 );
        }
        if ( !onLine( std::move( text ), cut ) ) {
            return;
        }
    }
}
