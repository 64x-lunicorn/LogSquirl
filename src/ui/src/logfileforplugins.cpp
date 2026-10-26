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

#include "logfileforplugins.h"

#include "logdata.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

using logsquirl::plugins::PluginSelectedLogLines;

PluginSelectedLogLines readLogLineTextsForPlugin( const LogData& logData,
                                                  std::span<const LineNumber> logLines,
                                                  qint64 maxBytes )
{
    PluginSelectedLogLines texts;
    if ( logLines.empty() ) {
        return texts;
    }

    // The Log File stays open while its lines are read.
    logData.attachReader();
    qint64 utf8Bytes = 0;
    std::size_t read = 0;
    logData.readLinePrefixes( logLines, maxBytes, [ & ]( QString&& text, bool cut ) {
        utf8Bytes += ( texts.lines.isEmpty() ? 0 : 1 ) + text.toUtf8().size();
        texts.lines.append( std::move( text ) );
        ++read;
        texts.lastCut = cut;
        if ( cut || utf8Bytes > maxBytes ) {
            texts.more = read < logLines.size();
            return false;
        }
        return true;
    } );
    logData.detachReader();
    return texts;
}

LogFileForPlugins::LogFileForPlugins( std::shared_ptr<const LogData> logData )
    : logData_( std::move( logData ) )
{
}

LogFileForPlugins::~LogFileForPlugins()
{
    // The log data is an object of the UI thread's, which hears its indexing
    // worker there: it goes there, from the event loop, however briefly a
    // plugin's thread held it last.
    auto* application = QCoreApplication::instance();
    if ( !logData_ || application == nullptr || QThread::currentThread() == logData_->thread() ) {
        return;
    }
    QMetaObject::invokeMethod(
        application, [ logData = std::move( logData_ ) ]() mutable { logData.reset(); },
        Qt::QueuedConnection );
}

std::uint64_t LogFileForPlugins::lineCount() const
{
    return logData_->getNbLine().get();
}

std::optional<PluginSelectedLogLines> LogFileForPlugins::readLogLines( std::uint64_t logLine,
                                                                       std::size_t maxLines,
                                                                       std::size_t maxBytes ) const
{
    // As far as it is indexed now: a Log Line indexed later is read next time.
    const auto count = lineCount();
    if ( logLine >= count ) {
        return std::nullopt;
    }
    const auto toRead = std::min<std::uint64_t>( maxLines, count - logLine );
    std::vector<LineNumber> logLines;
    logLines.reserve( static_cast<std::size_t>( toRead ) );
    for ( std::uint64_t i = 0; i < toRead; ++i ) {
        logLines.emplace_back( logLine + i );
    }
    return readLogLineTextsForPlugin(
        *logData_, logLines,
        static_cast<qint64>( std::min<std::size_t>(
            maxBytes, static_cast<std::size_t>( std::numeric_limits<qint64>::max() ) ) ) );
}
