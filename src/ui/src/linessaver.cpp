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

#include "linessaver.h"

#include <atomic>
#include <cmath>
#include <utility>

#include <QIODevice>
#include <QMetaObject>
#include <QTextCodec>
#include <QtConcurrent>

#include <tbb/flow_graph.h>

#include "log.h"

bool saveDisplayedLines( const DisplayedLinesReader& readLines, LineNumber begin, LineNumber end,
                         const QTextCodec* codec, QIODevice& output, const AtomicFlag& interrupt,
                         const std::function<void( int )>& progress )
{
    logsquirl::vector<std::pair<LineNumber, LinesCount>> offsets;
    auto lineOffset = begin;
    const auto chunkSize = 5000_lcount;
    offsets.reserve( ( end - ( lineOffset + chunkSize ) ).get() );

    for ( ; lineOffset + chunkSize < end; lineOffset += LinesCount( chunkSize.get() ) ) {
        offsets.emplace_back( lineOffset, chunkSize );
    }
    offsets.emplace_back( lineOffset, LinesCount( ( end - lineOffset ).get() % chunkSize.get() ) );

    if ( !codec ) {
        codec = QTextCodec::codecForName( "utf-8" );
    }

    // Write BOM (Byte Order Mark) for Unicode encodings so other applications
    // can detect the encoding when reopening the saved file.
    const int mib = codec->mibEnum();
    static constexpr int Utf8Mib = 106;
    static constexpr int Utf16Mib = 1015;
    static constexpr int Utf16BEMib = 1013;
    static constexpr int Utf16LEMib = 1014;
    if ( mib == Utf16LEMib || mib == Utf16Mib ) {
        // UTF-16 LE BOM: FF FE
        output.write( "\xFF\xFE", 2 );
    }
    else if ( mib == Utf16BEMib ) {
        // UTF-16 BE BOM: FE FF
        output.write( "\xFE\xFF", 2 );
    }
    else if ( mib == Utf8Mib ) {
        // UTF-8 BOM: EF BB BF
        output.write( "\xEF\xBB\xBF", 3 );
    }

    std::atomic<bool> writeFailed = false;
    const auto isStopped
        = [ &interrupt, &writeFailed ]() { return static_cast<bool>( interrupt ) || writeFailed; };

    tbb::flow::graph saveFileGraph;
    using LinesData = logsquirl::vector<QString>;
    auto lineReader = tbb::flow::input_node<LinesData>(
        saveFileGraph,
        [ &readLines, &offsets, &isStopped, &progress,
          offsetIndex = 0u ]( tbb::flow_control& fc ) mutable -> LinesData {
            if ( isStopped() || offsetIndex >= offsets.size() ) {
                fc.stop();
                return {};
            }

            const auto& offset = offsets.at( offsetIndex );
            auto lines = readLines( offset.first, offset.second );
            for ( auto& l : lines ) {
#if !defined( Q_OS_WIN )
                l.append( QChar::CarriageReturn );
#endif
                l.append( QChar::LineFeed );
            }

            offsetIndex++;
            progress( static_cast<int>( std::floor( static_cast<float>( offsetIndex )
                                                    / static_cast<float>( offsets.size() + 1 )
                                                    * 1000.f ) ) );
            return lines;
        } );

    auto lineWriter = tbb::flow::function_node<LinesData, tbb::flow::continue_msg>(
        saveFileGraph, 1, [ &isStopped, &writeFailed, codec, &output ]( const LinesData& lines ) {
            for ( const auto& l : lines ) {
                if ( isStopped() ) {
                    break;
                }

                // Use IgnoreHeader to prevent codec from inserting its own BOM
                // per line — we already wrote the BOM once at the start of the file.
                QTextCodec::ConverterState state( QTextCodec::IgnoreHeader );
                const auto encodedLine
                    = codec->fromUnicode( l.constData(), static_cast<int>( l.length() ), &state );
                const auto written = output.write( encodedLine );

                if ( written != encodedLine.size() ) {
                    LOG_ERROR << "Saving file write failed";
                    writeFailed = true;
                }
            }
            return tbb::flow::continue_msg{};
        } );

    tbb::flow::make_edge( lineReader, lineWriter );

    lineReader.activate();
    saveFileGraph.wait_for_all();

    return !isStopped();
}

LinesSaver::LinesSaver( QObject* parent )
    : QObject( parent )
{
    connect( &watcher_, &QFutureWatcher<bool>::finished, this,
             [ this ]() { Q_EMIT finished( watcher_.result() ); } );
}

LinesSaver::~LinesSaver()
{
    watcher_.disconnect();
    future_.waitForFinished();
}

void LinesSaver::save( DisplayedLinesReader readLines, LineNumber begin, LineNumber end,
                       const QTextCodec* codec, QIODevice* output, const AtomicFlag& interrupt )
{
    // Progress is posted to this object's thread and emitted there, so every
    // slot connected to progressed() runs on that thread. The destructor waits
    // for the save, and destroying this object discards what is still posted.
    future_ = QtConcurrent::run(
        [ this, readLines = std::move( readLines ), begin, end, codec, output, &interrupt ]() {
            const auto reportProgress = [ this ]( int value ) {
                QMetaObject::invokeMethod(
                    this, [ this, value ]() { Q_EMIT progressed( value ); }, Qt::QueuedConnection );
            };
            return saveDisplayedLines( readLines, begin, end, codec, *output, interrupt,
                                       reportProgress );
        } );
    watcher_.setFuture( future_ );
}

bool LinesSaver::waitForResult()
{
    return future_.result();
}
