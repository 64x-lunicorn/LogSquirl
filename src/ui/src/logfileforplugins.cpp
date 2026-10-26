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

#include "linelengthcap.h"
#include "logdata.h"
#include "logformatfieldreader.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

using logsquirl::plugins::PluginLogFormatFields;
using logsquirl::plugins::PluginLogLineFields;
using logsquirl::plugins::PluginLogLineFieldsUnread;
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

bool LogFormatForPlugins::set( const std::shared_ptr<const LogFormatDefinition>& format )
{
    // Built outside the lock: it compiles the Log Format's patterns.
    {
        const std::scoped_lock lock( mutex_ );
        if ( ( reader_ ? &reader_->format() : nullptr ) == format.get() ) {
            return false;
        }
    }
    auto reader = format ? std::make_shared<const LogFormatFieldReader>( format ) : nullptr;
    const std::scoped_lock lock( mutex_ );
    reader_ = std::move( reader );
    return true;
}

std::shared_ptr<const LogFormatFieldReader> LogFormatForPlugins::reader() const
{
    const std::scoped_lock lock( mutex_ );
    return reader_;
}

namespace {

// The Log Lines from logLine on, at most maxLines of them, of the count there are.
std::vector<LineNumber> logLinesFrom( std::uint64_t logLine, std::size_t maxLines,
                                      std::uint64_t count )
{
    const auto toRead = std::min<std::uint64_t>( maxLines, count - logLine );
    std::vector<LineNumber> logLines;
    logLines.reserve( static_cast<std::size_t>( toRead ) );
    for ( std::uint64_t i = 0; i < toRead; ++i ) {
        logLines.emplace_back( logLine + i );
    }
    return logLines;
}

// How many bytes of a Log Line its fields are read from: a character of its
// text takes no more than 4 of them, so one cut there has more characters
// than a Log Format parses.
constexpr qint64 MaxFieldLineBytes = 4 * static_cast<qint64>( MaxUiThreadLineLength );

} // namespace

LogFileForPlugins::LogFileForPlugins( std::shared_ptr<const LogData> logData,
                                      std::shared_ptr<const LogFormatForPlugins> logFormat )
    : logData_( std::move( logData ) )
    , logFormat_( std::move( logFormat ) )
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
    return readLogLineTextsForPlugin(
        *logData_, logLinesFrom( logLine, maxLines, count ),
        static_cast<qint64>( std::min<std::size_t>(
            maxBytes, static_cast<std::size_t>( std::numeric_limits<qint64>::max() ) ) ) );
}

std::optional<PluginLogFormatFields> LogFileForPlugins::logFormat() const
{
    const auto reader = logFormat_->reader();
    if ( !reader ) {
        return std::nullopt;
    }
    return PluginLogFormatFields{ .name = reader->format().name(),
                                  .fieldNames = reader->fieldNames() };
}

std::expected<PluginLogLineFields, PluginLogLineFieldsUnread>
LogFileForPlugins::readLogLineFields( std::uint64_t logLine, std::size_t maxLines,
                                      std::size_t maxBytes ) const
{
    // One Log Format for the whole read, though the tab may switch meanwhile.
    const auto reader = logFormat_->reader();
    if ( !reader ) {
        return std::unexpected( PluginLogLineFieldsUnread::NoLogFormat );
    }
    // As far as it is indexed now: a Log Line indexed later is read next time.
    const auto count = lineCount();
    if ( logLine >= count ) {
        return std::unexpected( PluginLogLineFieldsUnread::OutOfRange );
    }
    const auto logLines = logLinesFrom( logLine, maxLines, count );

    PluginLogLineFields read;
    const auto fieldCount = reader->fieldNames().size();
    read.fieldCount = static_cast<std::size_t>( fieldCount );
    if ( logLines.empty() ) {
        return read;
    }
    std::size_t bytes = 0;
    QStringList fields;
    // The Log File stays open while its lines are read.
    logData_->attachReader();
    logData_->readLinePrefixes( logLines, MaxFieldLineBytes, [ & ]( QString&& text, bool cut ) {
        // As a Field Term reads them, and so as the Table View's cells show
        // them: a Log Line not parsed has no fields.
        const auto parsed = !cut && reader->readFields( text, fields );
        read.parsed.push_back( parsed );
        for ( qsizetype field = 0; field < fieldCount; ++field ) {
            auto value = parsed ? fields[ field ].toUtf8() : QByteArray{};
            bytes += static_cast<std::size_t>( value.size() );
            read.values.push_back( std::move( value ) );
        }
        if ( bytes > maxBytes ) {
            read.more = read.parsed.size() < logLines.size();
            return false;
        }
        return true;
    } );
    logData_->detachReader();
    return read;
}
