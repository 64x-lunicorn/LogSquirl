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

#include "pluginuiport.h"

#include <QString>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace logsquirl::plugins {

/**
 * An open Log File as plugins read it (#858): its Log Lines as the views show
 * them. The UI layer implements it over the Log File's data, which it keeps
 * for as long as this object lives.
 *
 * Both functions may be called on any thread, several at the same time, also
 * while the Log File loads; neither waits for the UI thread.
 */
class PluginLogFileSource {
public:
    virtual ~PluginLogFileSource() = default;

    /** How many Log Lines are indexed so far. */
    virtual std::uint64_t lineCount() const = 0;

    /**
     * The text of at most maxLines Log Lines from logLine on, counted from 0,
     * in the order of the Log File: each read from at most maxBytes of its
     * bytes, and none read after one that was cut -- the last, then -- or
     * after their UTF-8 text passed maxBytes; more says Log Lines asked for
     * were left unread. None when logLine is past the last Log Line indexed.
     */
    virtual std::optional<PluginSelectedLogLines>
    readLogLines( std::uint64_t logLine, std::size_t maxLines, std::size_t maxBytes ) const = 0;
};

/** How the Log Lines of an open Log File changed, for the plugins (#858). */
enum class PluginLogLinesChange {
    Appended, ///< Log Lines were added at the end; those before are as they were.
    Replaced  ///< Loaded anew, or reading differently: every Log Line may differ.
};

/**
 * The Log Files open in the tabs of every window, by path, for the Plugin
 * Host to find on any thread (#858).
 *
 * The UI adds a Log File when its tab opens and removes it when the tab
 * closes, on the UI thread. A Log File found is held, its data with it, for
 * as long as the caller keeps what find() returned, so a read under way
 * finishes when its tab closes; it says it is no longer open then, and the
 * caller answers as though it had not been found. The lookup takes a lock
 * only to copy the list, and never waits for the UI thread.
 */
class PluginLogFiles {
public:
    /** Names a Log File added; 0 names none. */
    using Id = std::uint64_t;

    /** A Log File found by path. */
    class LogFile {
    public:
        LogFile( Id id, QString path, std::shared_ptr<const PluginLogFileSource> source )
            : id_( id )
            , path_( std::move( path ) )
            , source_( std::move( source ) )
        {
        }

        /** Its Log Lines; held for as long as this object lives. */
        const PluginLogFileSource& source() const
        {
            return *source_;
        }

        /** The path it was added under, as the views name it. */
        const QString& path() const
        {
            return path_;
        }

        /** False once its tab has closed, also while something still reads it. */
        bool isOpen() const
        {
            return open_.load();
        }

    private:
        friend class PluginLogFiles;

        Id id_;
        QString path_;
        std::shared_ptr<const PluginLogFileSource> source_;
        std::atomic<bool> open_{ true };
    };

    /**
     * A Log File opened in a tab, under the path the views name it by. Returns
     * the id that removes it again. On the UI thread.
     */
    Id add( const QString& path, std::shared_ptr<const PluginLogFileSource> source );

    /**
     * Its tab closed: it is found no more, and one found already says it is
     * no longer open. Nothing for an id not added, or removed already. On the
     * UI thread.
     */
    void remove( Id id );

    /**
     * The open Log File path names, as the file system names it -- another
     * spelling of the same path, or another case where case does not count,
     * finds it too -- the one added last when two tabs show it; none for an
     * empty path or a Log File no tab shows. On any thread.
     */
    std::shared_ptr<const LogFile> find( const QString& path ) const;

private:
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<LogFile>> logFiles_;
    Id idsSoFar_ = 0;
};

} // namespace logsquirl::plugins
