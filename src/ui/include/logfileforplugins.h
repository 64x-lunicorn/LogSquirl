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

#include "linetypes.h"
#include "pluginlogfiles.h"

#include <QtGlobal>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>

class LogData;
class LogFormatDefinition;
class LogFormatFieldReader;

// The Log Format a tab applies to its Log File, for the plugins that read the
// fields of its Log Lines on any thread (#867). The tab sets it on the UI
// thread whenever it shows another Log Format, or none; a read takes the one
// set as it starts, and keeps it to its end.
class LogFormatForPlugins {
public:
    // Applies format from now on; none for no Log Format. Returns whether it
    // is another than the one applied before. On the UI thread.
    bool set( const std::shared_ptr<const LogFormatDefinition>& format );

    // What reads the fields under the Log Format applied now, as a Field Term
    // and the Table View read them; none when none is. On any thread.
    std::shared_ptr<const LogFormatFieldReader> reader() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const LogFormatFieldReader> reader_;
};

// The text of logLines as a plugin reads it (#663, #858): in the order given,
// each decoded as the views decode it and read from at most maxBytes of its
// bytes, and none read after one that was cut -- the last, then -- or after
// their UTF-8 text passed maxBytes; more says Log Lines were left unread.
// Safe on any thread, as reading the log data is.
logsquirl::plugins::PluginSelectedLogLines
readLogLineTextsForPlugin( const LogData& logData, std::span<const LineNumber> logLines,
                           qint64 maxBytes );

// A Log File's Log Lines as plugins read them, on any thread (#858): those of
// its log data, which it holds for as long as it lives, and their fields under
// the Log Format its tab applies (#867).
class LogFileForPlugins final : public logsquirl::plugins::PluginLogFileSource {
public:
    LogFileForPlugins( std::shared_ptr<const LogData> logData,
                       std::shared_ptr<const LogFormatForPlugins> logFormat );

    // Lets go of the log data on the thread it lives on: a plugin's read that
    // ends after the tab closed may hold it last, on a thread of its own.
    ~LogFileForPlugins() override;

    LogFileForPlugins( const LogFileForPlugins& ) = delete;
    LogFileForPlugins& operator=( const LogFileForPlugins& ) = delete;
    LogFileForPlugins( LogFileForPlugins&& ) = delete;
    LogFileForPlugins& operator=( LogFileForPlugins&& ) = delete;

    std::uint64_t lineCount() const override;
    std::optional<logsquirl::plugins::PluginSelectedLogLines>
    readLogLines( std::uint64_t logLine, std::size_t maxLines,
                  std::size_t maxBytes ) const override;

    std::optional<logsquirl::plugins::PluginLogFormatFields> logFormat() const override;
    // Each Log Line is read whole, up to MaxFieldLineBytes: one longer than
    // that has more characters than a Log Format parses (MaxUiThreadLineLength),
    // so it is not parsed, as the Table View does not parse it.
    std::expected<logsquirl::plugins::PluginLogLineFields,
                  logsquirl::plugins::PluginLogLineFieldsUnread>
    readLogLineFields( std::uint64_t logLine, std::size_t maxLines,
                       std::size_t maxBytes ) const override;

private:
    std::shared_ptr<const LogData> logData_;
    std::shared_ptr<const LogFormatForPlugins> logFormat_;
};
